#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

// std::shuffle and std::uniform_int_distribution are implementation-defined: the
// native CLI links libstdc++ while the Emscripten build links libc++, so they
// consume/map the mt19937 stream differently and the same seed would yield
// different ROMs on each. mt19937 itself is standardized, so these portable
// helpers (which only use raw rng() outputs) guarantee CLI/web parity for a seed.
// Changing either of these changes the output for every seed, so keep them stable.
inline uint32_t bounded_rng(std::mt19937& rng, uint32_t bound) {
    return static_cast<uint32_t>(rng()) % bound;  // bound must be > 0
}

template<typename T>
inline void portable_shuffle(std::vector<T>& v, std::mt19937& rng) {
    // Fisher-Yates, drawing one bounded value per step from the front.
    for(size_t i = v.size(); i > 1; --i) {
        size_t j = bounded_rng(rng, static_cast<uint32_t>(i));  // j in [0, i)
        std::swap(v[i - 1], v[j]);
    }
}

using PC_addr = uint32_t;

// this works for LoROM files only
struct SNES_addr {
    uint8_t bank;
    uint16_t addr;

    [[nodiscard]] constexpr PC_addr toPc() const {
    return (static_cast<PC_addr>(bank & 0x7F) * 0x8000u) + (addr & 0x7FFFu);
    }

    constexpr void fromPc(PC_addr pc) {
        bank = static_cast<uint8_t>(((pc / 0x8000u) & 0x7F) | 0x80);
        addr = static_cast<uint16_t>((pc % 0x8000u) | 0x8000u);
    }
};


struct RandomizerOptions {
    // When use_seed is set the RNG is seeded deterministically, so the same
    // input + options + seed always produces the same ROM (shareable seeds).
    // Otherwise a random_device seed is used, matching the original behaviour.
    bool use_seed{};
    uint64_t seed{};

    // Town walk speed in pixels per frame. Replaces the player's tier-2 velocity
    // table entries (see reverse-engineering-docs/town-movement.md).
    // 3 = vanilla; 4/5/6 are ~33%/66%/100% faster. Clamped to [1, 15].
    uint8_t walk_speed{3};

    // Melee (sword) reach in pixels per facing direction: how far the attack
    // hitbox extends from the player toward where they're facing. These rewrite
    // the far-edge offset of the per-direction box table at $01:B6F0 (rows 0-3,
    // the only rows the player's $050A pose index ever selects). Vanilla reach is
    // right/left = 28, down/up = 17. Each must be in [1, 128]; randomize_rom
    // rejects out-of-range values. See reverse-engineering-docs/sword-attack.md.
    uint8_t sword_reach_right{28};
    uint8_t sword_reach_down{17};
    uint8_t sword_reach_left{28};
    uint8_t sword_reach_up{17};

    // Speed up the menu/transition fade-to-black. The fade ramps PPU master
    // brightness down one step at a time, waiting 3 frames per step (~0.75s).
    // When set, drops the per-step wait to 1 frame in both the fade-out routine
    // and its fade-in sibling, making it ~3x faster (still a smooth per-frame
    // ramp). Not configurable -- it's vanilla (off) or fast (on).
    // See reverse-engineering-docs/screen-fade.md.
    bool fade{};

    // Experimental: convert the LoROM image to LoROM+FastROM. Flags the header
    // map-mode byte Fast, installs a reset boot stub that enables $420D and runs
    // the boot/NMI/game-mode dispatch from the fast $80-$BF mirror, and redirects
    // a list of logged long-jump sites into that mirror. Deterministic; off by
    // default. Implemented by apply_fast_rom / apply_fast_rom_banks below.
    bool fast_rom{};
};

struct RandomizerResult {
    bool success{};
    std::string error;
    std::vector<char> data;
};

// CRC32 (IEEE 802.3, the same variant used by zlib and the No-Intro database)
// of the headerless ROM image. Used to confirm the input is the exact ROM the
// randomizer's hardcoded offsets were reverse-engineered against.
inline uint32_t crc32_ieee(const std::vector<char>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for(char c : data) {
        crc ^= static_cast<unsigned char>(c);
        for(int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// CRC32 of "Lagoon (USA)" with any copier header removed.
inline constexpr uint32_t LAGOON_USA_CRC32 = 0xD2554270u;

// Recomputes the SNES internal checksum/complement (LoROM: $7FDC-$7FDF) so the
// header stays self-consistent after we patch ROM bytes. The complement and
// checksum words always sum to $01FE (each byte + its complement = $FF), so we
// neutralize those four bytes to that fixed contribution, sum every byte, then
// store checksum = sum & $FFFF and complement = checksum ^ $FFFF. Assumes a
// power-of-two image summed whole (true for Lagoon's 1 MiB).
inline void fix_header_checksum(std::vector<char>& bytes) {
    constexpr size_t complement_lo = 0x7FDC;
    constexpr size_t checksum_lo = 0x7FDE;
    if(bytes.size() < checksum_lo + 2) {
        return;
    }
    uint32_t sum = 0;
    for(char c : bytes) {
        sum += static_cast<unsigned char>(c);
    }
    for(size_t off = complement_lo; off < complement_lo + 4; ++off) {
        sum -= static_cast<unsigned char>(bytes[off]);
    }
    sum += 0x01FEu;  // canonical contribution of complement=$FFFF + checksum=$0000
    const uint16_t checksum = static_cast<uint16_t>(sum & 0xFFFFu);
    const uint16_t complement = static_cast<uint16_t>(checksum ^ 0xFFFFu);
    bytes[complement_lo] = static_cast<char>(complement & 0xFF);
    bytes[complement_lo + 1] = static_cast<char>((complement >> 8) & 0xFF);
    bytes[checksum_lo] = static_cast<char>(checksum & 0xFF);
    bytes[checksum_lo + 1] = static_cast<char>((checksum >> 8) & 0xFF);
}

// Experimental LoROM -> LoROM+FastROM conversion (enabled by options.fast_rom).
//
// LoROM mirrors bank $00:8000-$FFFF onto $80:8000-$FFFF (identical bytes), and
// FastROM only accelerates banks $80-$FF *and* only once MEMSEL ($420D) bit 0 is
// set. The CPU always resets into bank $00, so we repoint the reset vector at a
// small boot stub that (a) enables fast timing via $420D and (b) `JML`s into the
// fast mirror at $80, re-entering the original boot code. From there bank-
// relative (absolute) JSR/JMP keep running in $80.
//
// The stub lives in the 9 bytes of free padding right before the header
// ($00:FFB7-$FFBF, all $00; $FFDA != $33 so there is no extended header). At
// reset the CPU is in emulation mode (8-bit A) with DBR=$00, so the 8-bit
// LDA/STA reach $00:420D correctly.
//
// Offsets are headerless PC; randomize_rom has already de-headered the image and
// CRC-verified it as Lagoon (USA), so these bytes are known-good.
inline void apply_fast_rom(std::vector<char>& data) {
    // (1) Header map-mode byte $FFD5 ($7FD5 headerless): $20 (Slow) -> $30 (Fast).
    data[0x7FD5] = static_cast<char>(0x30);

    // (2) Boot stub at $00:FFB7. Read the boot entry from the live reset vector
    //     first so the JML re-enters the real address in bank $80.
    constexpr uint32_t stub_off = 0x7FB7;  // file off of $00:FFB7
    const uint16_t reset_entry = static_cast<uint16_t>(
        (static_cast<uint8_t>(data[0x7FFD]) << 8) | static_cast<uint8_t>(data[0x7FFC]));
    const unsigned char stub[] = {
        0xA9, 0x01,              // LDA #$01
        0x8D, 0x0D, 0x42,        // STA $420D      ; enable FastROM (MEMSEL bit 0)
        0x5C,                    // JML ...
        static_cast<unsigned char>(reset_entry & 0xFF),
        static_cast<unsigned char>((reset_entry >> 8) & 0xFF),
        0x80,                    // ... $80:<reset entry>
    };
    for(size_t i = 0; i < sizeof(stub); ++i) {
        data[stub_off + i] = static_cast<char>(stub[i]);
    }

    // (3) Point the reset vector $FFFC at the stub ($00:FFB7).
    const uint16_t stub_addr = static_cast<uint16_t>((stub_off & 0x7FFF) | 0x8000);
    data[0x7FFC] = static_cast<char>(stub_addr & 0xFF);
    data[0x7FFD] = static_cast<char>((stub_addr >> 8) & 0xFF);

    // (4) The NMI handler runs slow because interrupts always vector through bank
    //     $00 (the CPU forces PBR=$00 on an interrupt). Put a `JML $80:<nmi>`
    //     trampoline in the free emulation-vector slot $00:FFF0-$FFF3 and repoint
    //     both NMI vectors ($FFEA native, $FFFA emulation) at it, so the handler
    //     runs from the fast mirror. RTI still returns to the interrupted bank
    //     (it restores PBR from the stack), so this is transparent. NMI is vblank
    //     work, so running it faster only frees up time. (IRQ is left slow on
    //     purpose -- IRQ handlers are often raster-timed and would glitch faster.)
    constexpr uint32_t nmi_tramp_off = 0x7FF0;  // file off of $00:FFF0
    const uint16_t nmi_entry = static_cast<uint16_t>(
        (static_cast<uint8_t>(data[0x7FEB]) << 8) | static_cast<uint8_t>(data[0x7FEA]));
    data[nmi_tramp_off + 0] = static_cast<char>(0x5C);                       // JML
    data[nmi_tramp_off + 1] = static_cast<char>(nmi_entry & 0xFF);
    data[nmi_tramp_off + 2] = static_cast<char>((nmi_entry >> 8) & 0xFF);
    data[nmi_tramp_off + 3] = static_cast<char>(0x80);                       // bank $80
    const uint16_t nmi_tramp_addr = static_cast<uint16_t>((nmi_tramp_off & 0x7FFF) | 0x8000);
    data[0x7FEA] = static_cast<char>(nmi_tramp_addr & 0xFF);   // $FFEA native NMI
    data[0x7FEB] = static_cast<char>((nmi_tramp_addr >> 8) & 0xFF);
    data[0x7FFA] = static_cast<char>(nmi_tramp_addr & 0xFF);   // $FFFA emulation NMI
    data[0x7FFB] = static_cast<char>((nmi_tramp_addr >> 8) & 0xFF);

    // (5) Game-mode dispatcher. $00:80C9 builds a 24-bit pointer in $10/$11/$12
    //     -- where $12 is the *caller-supplied* target bank -- and reaches the
    //     handler via `JML [$0010]` at $00:80E6. The bank comes from the caller
    //     and varies per call, so no static immediate fixes it. Instead redirect
    //     that indirect jump through a stub that forces bit 7 of the bank, so
    //     every dispatched handler runs from the fast $80-$BF mirror:
    //
    //       $00:80E6  DC 10 00 (JML [$0010])  ->  4C 7E 9C (JMP $9C7E)  [in $80]
    //       stub @ $00:9C7E (free $FF padding):  LDA $12 / ORA #$80 / STA $12 /
    //                                            JML [$0010]
    //
    //     The JMP stays in the current bank (the call site is already bumped to
    //     $80), and M is 8-bit at this point, so the stub's ops are byte-wide.
    constexpr uint32_t disp_stub_off = 0x1C7E;  // file off of $00:9C7E
    const unsigned char disp_stub[] = {
        0xA5, 0x12,        // LDA $12
        0x09, 0x80,        // ORA #$80       ; force the fast-mirror bank
        0x85, 0x12,        // STA $12
        0xDC, 0x10, 0x00,  // JML [$0010]    ; the original indirect dispatch
    };
    for(size_t i = 0; i < sizeof(disp_stub); ++i) {
        data[disp_stub_off + i] = static_cast<char>(disp_stub[i]);
    }
    const uint16_t disp_stub_addr = static_cast<uint16_t>((disp_stub_off & 0x7FFF) | 0x8000);
    data[0x000E6] = static_cast<char>(0x4C);                           // JMP abs
    data[0x000E7] = static_cast<char>(disp_stub_addr & 0xFF);
    data[0x000E8] = static_cast<char>((disp_stub_addr >> 8) & 0xFF);
}

// Long-jump (JSL/JML) sites whose target bank is a ROM bank ($00-$3F), gathered
// from a Mesen Code/Data Logger playthrough and decoded using the CDL's M/X
// flags so only true instruction operands are listed (no data false-positives).
// Each value is the headerless file offset of the bank byte; apply_fast_rom_banks
// ORs $80 into it to send the call/jump to the fast $80-$BF mirror. To extend
// coverage, re-log with more of the game exercised and regenerate this list, or
// append individual real JSL/JML sites found later by hand (keep them sorted).
inline constexpr uint32_t kFastRomBankBumps[] = {
    0x00068, 0x0009D, 0x000A1, 0x000AE, 0x000B2, 0x000E3, 0x00123, 0x00127,
    0x0012B, 0x0012F, 0x00133, 0x00137, 0x0013B, 0x0013F, 0x00143, 0x00147,
    0x001A0, 0x001A4, 0x001B3, 0x001B8, 0x001BC, 0x001C1, 0x001C5, 0x00202,
    0x002A6, 0x002C6, 0x002E4, 0x00301, 0x00318, 0x00356, 0x00370, 0x0038A,
    0x003A3, 0x003D8, 0x00411, 0x0042A, 0x00436, 0x00463, 0x00471, 0x0048C,
    0x004A7, 0x004C0, 0x004CC, 0x004F7, 0x005FE, 0x008D2, 0x00A9C, 0x00E3E,
    0x00E6F, 0x010A1, 0x010D8, 0x010F2, 0x01113, 0x01359, 0x0135F, 0x01365, 0x013D0,
    0x013D7,  // JSL $009370 (save routine) at $00:93D4 -- 2nd call site, not logged
    0x014D6, 0x014E3, 0x017EB, 0x018F1, 0x01930, 0x08087, 0x08093, 0x08099,
    0x080D0, 0x080E6, 0x080EA, 0x080F0, 0x080F9, 0x08108, 0x081AF, 0x081B3,
    0x081B9, 0x08708, 0x08717, 0x0871B, 0x0871F, 0x08723, 0x08727, 0x0872B,
    0x0872F, 0x08736, 0x0874A, 0x0875F, 0x08765, 0x08792, 0x087C3, 0x087C7,
    0x087CE, 0x087DC, 0x087E5, 0x08874, 0x08889, 0x08898, 0x088B3, 0x088CB, 0x088DF, 0x08920,
    0x08932, 0x08A2B, 0x08A80, 0x08A90, 0x08ABB, 0x08ACB, 0x08AD0, 0x08AF5, 0x08B0D, 0x08B20,
    0x08B26, 0x08B2C, 0x08B32, 0x08C3D, 0x08EE5, 0x0903A, 0x090F1, 0x09123, 0x09224, 0x0929E, 0x09447, 0x094EC,
    0x094F0, 0x09B94, 0x09B9D, 0x0A1CD, 0x0A6F0, 0x0A750, 0x0A7A9, 0x0BA8B, 0x0BAF2, 0x0C154, 0x0C179,
    0x0C19E, 0x0C1C4, 0x0C1F2, 0x0C254, 0x0C282, 0x0C479, 0x0C5B2, 0x0C5D8,
    0x0C67F, 0x0C6C6, 0x0C6CA, 0x0C6D0, 0x0C6D9, 0x0C70E, 0x0C720, 0x0C795,
    0x0C799, 0x0C8EA, 0x0C938, 0x0C941, 0x0C9C0, 0x0CA4C, 0x0CA7E, 0x0CA93, 0x0CC62,
    0x0CC6B, 0x0CC74, 0x0CC78, 0x0CC80, 0x0CCDE, 0x0CD35, 0x0CD5B, 0x0CD61, 0x0CDDB, 0x0CDDF,
    0x0CE81, 0x0CE87, 0x0CE8D, 0x0CE96, 0x0CE9A, 0x0CF23, 0x0CF3B, 0x0CF3F,
    0x0CF46, 0x0CF4A, 0x0CF50, 0x0CF56, 0x0CF75, 0x0CF79, 0x0CF7D, 0x0CFC5, 0x0D00A, 0x0D00E,
    0x0D023, 0x0D02B, 0x0D046, 0x0D04F, 0x0D0B9, 0x0D0FD, 0x0D101, 0x0D124, 0x0D15F,
    0x0D168, 0x0D16E, 0x0D1A9, 0x0D1B3, 0x0D1B7, 0x0D1E1, 0x0D1F4, 0x0D1FB, 0x0D204, 0x0D20A, 0x0D2E7, 0x0D9BD, 0x0DA1D, 0x10045, 0x1006E, 0x1024D,
    0x1041A, 0x1041E, 0x10447, 0x10457, 0x10470, 0x10499, 0x1049D, 0x104BA,
    0x104DF, 0x1050E, 0x10522, 0x1064E, 0x10652, 0x1066C, 0x109B9, 0x10BE4,
    0x10BEE, 0x10BF2, 0x10C20, 0x10C27, 0x120BD, 0x120D5, 0x120E7, 0x12364,
    0x123C7, 0x123DF, 0x132DD, 0x132F4, 0x13307, 0x13319, 0x1333E, 0x13356, 0x1335C, 0x13360, 0x13368,
    0x13379, 0x13382, 0x1339D, 0x133FC, 0x13460, 0x13588, 0x13593, 0x135BB,
    0x1368E, 0x136B4, 0x136B9, 0x136CF, 0x136E2, 0x136E6, 0x136EA, 0x13707,
    0x13713, 0x13725, 0x1373A, 0x1379A, 0x137E3, 0x13813, 0x13817, 0x1381B, 0x13844, 0x1387D,
    0x1398F, 0x13C44, 0x13C65, 0x13C69, 0x13C9B, 0x13CDE, 0x13DCE, 0x13DDE,
    0x13E01, 0x13E07, 0x13E17, 0x13E3A, 0x13E63, 0x13E6C, 0x13E74, 0x13E8A,
    0x13E96, 0x13EB4, 0x13EB8, 0x13EC1, 0x13EE3, 0x13EEA, 0x13F15, 0x13F24,
    0x13F2A, 0x13F30, 0x13F34, 0x13F42, 0x13F48, 0x13F4E, 0x13F52, 0x13F57,
    0x13F5B, 0x13F5F, 0x13F63, 0x13F67, 0x13F6B, 0x13F6F, 0x13F76, 0x13F92,
    0x13F98, 0x13FB0, 0x13FB4, 0x13FD5, 0x13FF9, 0x140AC, 0x140DD, 0x141FA,
    0x145C4, 0x14656, 0x1465E, 0x14663, 0x1466D, 0x14675, 0x1467A, 0x146D2, 0x1479D, 0x1484C, 0x1489A, 0x148B1, 0x148BA, 0x14A44, 0x14A48,
    0x1551A, 0x1554C, 0x1564F, 0x15655, 0x15663, 0x15669, 0x1566D, 0x15671,
    0x15677, 0x1567D, 0x15683, 0x15689, 0x1568F, 0x15695, 0x156D4, 0x156DB,
    0x156DF, 0x15722, 0x15726, 0x1572D, 0x15743, 0x157B0, 0x157B4, 0x157B8,
    0x15833, 0x1586A, 0x1590A, 0x15916, 0x1591D, 0x1594E, 0x159A1, 0x159A5, 0x159AF, 0x15DDF, 0x15DEE, 0x15DF8,
    0x15E01, 0x15E0C, 0x15E42, 0x15E46, 0x15E4A, 0x15E63, 0x15F56, 0x15F94,
    0x161A9, 0x161AD, 0x16217, 0x16238, 0x1624D, 0x16FC6, 0x16FCC, 0x16FD0,
    0x16FD4, 0x16FDA, 0x16FE0, 0x16FE6, 0x16FEC, 0x16FF2, 0x16FFD, 0x17012,
    0x17016, 0x1701A, 0x17020, 0x1702E, 0x171F6, 0x67832, 0x6784D, 0x678E0,
    0x678E9, 0x678F2, 0x6799C, 0x679C5, 0x679CE, 0x67ECB, 0x67ECF, 0x67ED8,
    0x67EDC, 0x67F1C, 0x67F20, 0x67F29, 0x67F2D,
};

// Redirects every hardcoded long-jump site into the fast $80-$BF mirror.
inline void apply_fast_rom_banks(std::vector<char>& data) {
    for(uint32_t off : kFastRomBankBumps) {
        if(off < data.size()) {
            data[off] = static_cast<char>(static_cast<unsigned char>(data[off]) | 0x80);
        }
    }
}

// Applies the randomizer to a raw ROM image and returns the patched bytes.
// This is the single source of truth shared by the native CLI and the web
// (Emscripten) build, so both produce identical output for identical input.
inline RandomizerResult randomize_rom(std::vector<char> bytes, const RandomizerOptions& options) {
    using std::numeric_limits;
    using std::vector;

    RandomizerResult result;

    bool has_copier_header = (bytes.size() % 1024) == 512;
    if(has_copier_header) {
        bytes.erase(bytes.begin(), bytes.begin() + 512);
    }

    // The SNES internal header sits at 0x7FC0 for LoROM and 0xFFC0 for HiROM.
    // We can't assume which (an uploaded ROM might be a valid HiROM that isn't
    // Lagoon), so probe both: in a real header the checksum (+0x1E) and its
    // complement (+0x1C) XOR to 0xFFFF. The map-mode byte (+0x15) then gives the
    // memory layout in bit 0 (LoROM/HiROM) and the bus speed in bit 4 (Fast/Slow).
    auto header_checksum_valid = [&](size_t base) {
        if(bytes.size() < base + 0x40) return false;  // full 0x40-byte header must fit
        auto u16 = [&](size_t off) {
            return static_cast<uint16_t>(static_cast<uint8_t>(bytes[off]) |
                                         (static_cast<uint8_t>(bytes[off + 1]) << 8));
        };
        return static_cast<uint16_t>(u16(base + 0x1C) ^ u16(base + 0x1E)) == 0xFFFF;
    };

    size_t header_base = 0;
    bool header_found = true;
    if(header_checksum_valid(0x7FC0)) {
        header_base = 0x7FC0;  // LoROM
    } else if(header_checksum_valid(0xFFC0)) {
        header_base = 0xFFC0;  // HiROM
    } else {
        header_found = false;
    }

    if(header_found) {
        auto byte = [&](size_t off) { return static_cast<uint8_t>(bytes[header_base + off]); };
        auto u16 = [&](size_t off) {
            return static_cast<uint16_t>(byte(off) | (byte(off + 1) << 8));
        };
        // ROM/SRAM size fields are exponents: size = 0x400 << n bytes (0 = none).
        auto kib = [](uint8_t n) -> uint32_t { return n == 0 ? 0u : (n < 24 ? (1u << n) : 0u); };

        std::string title;
        for(size_t i = 0; i < 0x15; ++i) {
            char c = static_cast<char>(byte(i));
            title.push_back((c >= 0x20 && c < 0x7F) ? c : '.');
        }
        while(!title.empty() && title.back() == ' ') title.pop_back();

        const char* region;
        switch(byte(0x19)) {
            case 0x00: region = "Japan (NTSC)"; break;
            case 0x01: region = "North America (NTSC)"; break;
            case 0x02: region = "Europe/Oceania (PAL)"; break;
            default:   region = "other"; break;
        }

        const uint8_t map_mode = byte(0x15);
        const uint16_t complement = u16(0x1C);
        const uint16_t checksum = u16(0x1E);

        std::string vectors;
        for(size_t i = 0; i < 0x20; ++i) {
            vectors += fmt::format("{}{:02X}", i == 0 ? "" : " ", byte(0x20 + i));
        }

        fmt::println("SNES header @ 0x{:04X}:", header_base);
        fmt::println("  Title:           \"{}\"", title);
        fmt::println("  Map mode:        0x{:02X} ({} / {})", map_mode,
            (map_mode & 0x01) ? "HiROM" : "LoROM",
            (map_mode & 0x10) ? "FastROM" : "SlowROM");
        fmt::println("  Cartridge type:  0x{:02X}", byte(0x16));
        fmt::println("  ROM size:        0x{:02X} ({} KiB)", byte(0x17), kib(byte(0x17)));
        fmt::println("  SRAM size:       0x{:02X} ({} KiB)", byte(0x18), kib(byte(0x18)));
        fmt::println("  Region:          0x{:02X} ({})", byte(0x19), region);
        fmt::println("  Maker code:      0x{:02X}", byte(0x1A));
        fmt::println("  ROM version:     0x{:02X}", byte(0x1B));
        fmt::println("  Checksum:        0x{:04X} (complement 0x{:04X}, {})", checksum, complement,
            static_cast<uint16_t>(checksum ^ complement) == 0xFFFF ? "ok" : "MISMATCH");
        fmt::println("  Vectors:         {}", vectors);
        fmt::println("  Reset vector:    0x{:04X}", u16(0x3C));  // emulation-mode RESET
    } else {
        fmt::println("SNES header: no valid header found at 0x7FC0 or 0xFFC0");
    }

    if(bytes.size() < 0x100000) {
        result.error = "Given input ROM is too small to be a Lagoon ROM.";
        return result;
    }

    // Verify this is exactly the ROM the randomizer was built for. All the patch
    // offsets below are hardcoded against "Lagoon (USA)", so a different
    // region/revision/dump would be silently corrupted.
    const uint32_t crc = crc32_ieee(bytes);
    if(crc != LAGOON_USA_CRC32) {
        result.error = fmt::format(
            "Given input ROM is not \"Lagoon (USA)\" (CRC32 {:08X}, expected {:08X}).",
            crc, LAGOON_USA_CRC32);
        return result;
    }

    // Camera fix, hardcoded from "Lagoon camera fix [001].ips". The original IPS
    // rewrites four immediate operands that act as camera scroll-boundary
    // thresholds. Offsets are headerless PC addresses, so they apply directly to
    // this post-header `bytes` image (verified to be Lagoon by the CRC above).
    //   $01:8408  SEC; SBC #$00C0 -> #$0084
    //   $01:8432  LDA #$0040      -> #$007C
    //   $01:845F  SEC; SBC #$00B0 -> #$0084
    //   $01:8489  LDA #$0050      -> #$007C
    static constexpr std::pair<uint32_t, uint8_t> camera_fix[] = {
        {0x008408, 0x84}, {0x008432, 0x7C}, {0x00845F, 0x84}, {0x008489, 0x7C},
    };
    for(const auto& [offset, value] : camera_fix) {
        bytes[offset] = static_cast<char>(value);
    }

    // Configurable town walk speed. The player (object 0) walks via the generic
    // object velocity setter $02:80EA, which looks up a per-frame velocity from a
    // speed-tier table at $02:8129 (16 bytes/tier: 4 directions x {Xvel.w,Yvel.w})
    // and stores it to $0506/$0508. The player walks at tier 2 = +/-3 px/frame; we
    // rewrite that tier's four nonzero 16-bit entries to +/- walk_speed.
    // (Confirmed by runtime: $0506 reads -3 = FD FF when holding left.)
    // See reverse-engineering-docs/town-movement.md.
    const int16_t v = std::clamp<int>(options.walk_speed, 1, 15);
    auto put_i16le = [&](uint32_t offset, int16_t value) {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    };
    put_i16le(0x010149, +v);  // tier 2, right  (+X)
    put_i16le(0x01014F, +v);  // tier 2, down   (+Y)
    put_i16le(0x010151, -v);  // tier 2, left   (-X)
    put_i16le(0x010157, -v);  // tier 2, up     (-Y)

    // Configurable sword (melee) reach. The player's attack hitbox builder
    // $01:B6BD reads the facing index $050A (only ever 0-3, confirmed via the
    // pose table $02:80DA) and adds four signed 16-bit offsets from the table at
    // $01:B6F0 to the player position to form the hitbox [$40,$42]x[$44,$46].
    // The near edge of each box sits exactly on the player (offset 0), so the
    // far-edge offset == the reach. We rewrite only that far edge per direction:
    // right/down use +reach, left/up use -reach. The default values reproduce the
    // vanilla bytes exactly. See reverse-engineering-docs/sword-attack.md.
    auto reach_in_range = [](uint8_t r) { return r >= 1 && r <= 128; };
    if(!reach_in_range(options.sword_reach_right) ||
       !reach_in_range(options.sword_reach_down) ||
       !reach_in_range(options.sword_reach_left) ||
       !reach_in_range(options.sword_reach_up)) {
        result.error = "Sword reach values must each be in the range 1..128.";
        return result;
    }
    put_i16le(0x00B6F2, +static_cast<int16_t>(options.sword_reach_right)); // row 0 right: dx1
    put_i16le(0x00B6FE, +static_cast<int16_t>(options.sword_reach_down));  // row 1 down:  dy1
    put_i16le(0x00B700, -static_cast<int16_t>(options.sword_reach_left));  // row 2 left:  dx0
    put_i16le(0x00B70C, -static_cast<int16_t>(options.sword_reach_up));    // row 3 up:    dy0

    // Optional faster screen fade. The fade-to-black on menu open ramps PPU master
    // brightness (INIDISP $2100) down one step at a time, waiting N frames per step
    // via the generic frame-wait $00:84E0. Vanilla waits 3 frames/step (~0.75s);
    // dropping the call-site constant to 1 makes it ~3x faster while staying a
    // smooth per-frame ramp. We patch the per-step delay immediate of both the
    // fade-out routine ($00:83D3 LDA #$03, operand at 0x0003D4) and its fade-in
    // sibling ($00:840C LDA #$03, operand at 0x00040D). $00:84E0 itself is the
    // shared frame-wait used game-wide, so only these call-site constants change.
    // See reverse-engineering-docs/screen-fade.md.
    if(options.fade) {
        bytes[0x0003D4] = static_cast<char>(0x01);  // fade-out: 3 -> 1 frame/step
        bytes[0x00040D] = static_cast<char>(0x01);  // fade-in:  3 -> 1 frame/step
    }

    std::mt19937 rng;
    if(options.use_seed) {
        rng.seed(static_cast<std::mt19937::result_type>(options.seed));
    } else {
        std::random_device dev;
        rng.seed(dev());
    }

    // Experimental LoROM -> LoROM+FastROM conversion. Patches header map-mode,
    // the reset/NMI vectors + boot stub, the game-mode dispatch, and the logged
    // long-jump sites so the game runs from the fast $80-$BF mirror. Applied last
    // (right before the checksum fix) so it sees the de-headered, CRC-verified
    // image; deterministic, so it doesn't touch the RNG stream.
    if(options.fast_rom) {
        apply_fast_rom(bytes);
        apply_fast_rom_banks(bytes);
        fmt::println("FastROM: header flagged, $420D enabled, boot + NMI + {} "
                     "long-jumps redirected to bank $80.",
                     std::size(kFastRomBankBumps));
    }

    // Keep the SNES header checksum valid after the byte patches above.
    fix_header_checksum(bytes);

    result.success = true;
    result.data = std::move(bytes);
    return result;
}
