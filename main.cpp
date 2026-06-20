#include <cctype>
#include <fstream>
#include <iterator>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

#include "randomizer.hpp"

using namespace std;

// Experimental LoROM -> LoROM+FastROM conversion (CLI-only, --fast-rom).
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
void apply_fast_rom(vector<char>& data) {
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
static const uint32_t kFastRomBankBumps[] = {
    0x00068, 0x0009D, 0x000A1, 0x000AE, 0x000B2, 0x000E3, 0x00123, 0x00127,
    0x0012B, 0x0012F, 0x00133, 0x00137, 0x0013B, 0x0013F, 0x00143, 0x00147,
    0x001A0, 0x001A4, 0x001B3, 0x001B8, 0x001BC, 0x001C1, 0x001C5, 0x00202,
    0x002A6, 0x002C6, 0x002E4, 0x00301, 0x00318, 0x00356, 0x00370, 0x0038A,
    0x003A3, 0x003D8, 0x00411, 0x0042A, 0x00436, 0x00463, 0x00471, 0x0048C,
    0x004A7, 0x004C0, 0x004CC, 0x004F7, 0x005FE, 0x008D2, 0x00A9C, 0x00E3E,
    0x00E6F, 0x010A1, 0x010D8, 0x010F2, 0x01359, 0x0135F, 0x01365, 0x013D0,
    0x013D7,  // JSL $009370 (save routine) at $00:93D4 -- 2nd call site, not logged
    0x014D6, 0x014E3, 0x017EB, 0x018F1, 0x01930, 0x08087, 0x08093, 0x08099,
    0x080D0, 0x080E6, 0x080EA, 0x080F0, 0x080F9, 0x08108, 0x081AF, 0x081B3,
    0x081B9, 0x08708, 0x08717, 0x0871B, 0x0871F, 0x08723, 0x08727, 0x0872B,
    0x0872F, 0x08736, 0x0874A, 0x0875F, 0x08765, 0x08792, 0x087C3, 0x087C7,
    0x087CE, 0x087DC, 0x087E5, 0x08874, 0x08889, 0x08898, 0x088DF, 0x08920,
    0x08932, 0x08A2B, 0x08A80, 0x08A90, 0x08AD0, 0x08AF5, 0x08B0D, 0x08B20,
    0x08B26, 0x08B2C, 0x08B32, 0x08C3D, 0x09224, 0x0929E, 0x09447, 0x094EC,
    0x094F0, 0x09B94, 0x09B9D, 0x0A1CD, 0x0A6F0, 0x0BA8B, 0x0C154, 0x0C179,
    0x0C19E, 0x0C1C4, 0x0C1F2, 0x0C254, 0x0C282, 0x0C479, 0x0C5B2, 0x0C5D8,
    0x0C67F, 0x0C6C6, 0x0C6CA, 0x0C6D0, 0x0C6D9, 0x0C70E, 0x0C720, 0x0C795,
    0x0C799, 0x0C8EA, 0x0C938, 0x0C941, 0x0CA4C, 0x0CA7E, 0x0CA93, 0x0CC62,
    0x0CC6B, 0x0CC74, 0x0CC78, 0x0CC80, 0x0CCDE, 0x0CD35, 0x0CD5B, 0x0CD61,
    0x0CE81, 0x0CE87, 0x0CE8D, 0x0CE96, 0x0CE9A, 0x0CFC5, 0x0D00A, 0x0D00E,
    0x0D023, 0x0D02B, 0x0D046, 0x0D04F, 0x0D0B9, 0x0D0FD, 0x0D101, 0x0D124, 0x0D15F,
    0x0D168, 0x0D16E, 0x0D2E7, 0x0D9BD, 0x0DA1D, 0x10045, 0x1006E, 0x1024D,
    0x1041A, 0x1041E, 0x10447, 0x10457, 0x10470, 0x10499, 0x1049D, 0x104BA,
    0x104DF, 0x1050E, 0x10522, 0x1064E, 0x10652, 0x1066C, 0x109B9, 0x10BE4,
    0x10BEE, 0x10BF2, 0x10C20, 0x10C27, 0x120BD, 0x120D5, 0x120E7, 0x12364,
    0x123C7, 0x13307, 0x13319, 0x1333E, 0x13356, 0x1335C, 0x13360, 0x13368,
    0x13379, 0x13382, 0x1339D, 0x133FC, 0x13460, 0x13588, 0x13593, 0x135BB,
    0x1368E, 0x136B4, 0x136B9, 0x136CF, 0x136E2, 0x136E6, 0x136EA, 0x13707,
    0x13713, 0x1373A, 0x137E3, 0x13813, 0x13817, 0x1381B, 0x13844, 0x1387D,
    0x1398F, 0x13C44, 0x13C65, 0x13C69, 0x13C9B, 0x13CDE, 0x13DCE, 0x13DDE,
    0x13E01, 0x13E07, 0x13E17, 0x13E3A, 0x13E63, 0x13E6C, 0x13E74, 0x13E8A,
    0x13E96, 0x13EB4, 0x13EB8, 0x13EC1, 0x13EE3, 0x13EEA, 0x13F15, 0x13F24,
    0x13F2A, 0x13F30, 0x13F34, 0x13F42, 0x13F48, 0x13F4E, 0x13F52, 0x13F57,
    0x13F5B, 0x13F5F, 0x13F63, 0x13F67, 0x13F6B, 0x13F6F, 0x13F76, 0x13F92,
    0x13F98, 0x13FB0, 0x13FB4, 0x13FD5, 0x13FF9, 0x140AC, 0x140DD, 0x141FA,
    0x145C4, 0x14656, 0x1465E, 0x14663, 0x1466D, 0x14675, 0x1467A, 0x1479D,
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
void apply_fast_rom_banks(vector<char>& data) {
    for(uint32_t off : kFastRomBankBumps) {
        if(off < data.size()) {
            data[off] = static_cast<char>(static_cast<unsigned char>(data[off]) | 0x80);
        }
    }
}

int main(int argc, const char** argv) {
    string input_file;
    string output_file;

    bool help{};
    bool fast_rom{};
    RandomizerOptions options;
    uint64_t seed{numeric_limits<uint64_t>::max()};
    // Parsed as int (Lyra treats uint8_t as a character), then narrowed below.
    int walk_speed{options.walk_speed};
    // Four comma-separated reach values "right,down,left,up"; parsed below.
    string sword_reach;

    auto cli = lyra::opt(input_file, "input")
                ["-i"]["--input"]("Which rom to take as input").required()
               | lyra::opt(output_file, "output")
                ["-o"]["--output"]("What filename to output to").required()
               | lyra::opt(seed, "seed")
                ["-s"]["--seed"]("Seed the RNG with this value (same seed + options reproduces the same ROM, identical to the web version)")
               | lyra::opt(walk_speed, "walk-speed")
                ["-w"]["--walk-speed"]("Town walk speed in px/frame (3 = default, 4/5/6 = ~33/66/100% faster)")
               | lyra::opt(sword_reach, "right,down,left,up")
                ["--sword-reach"]("Melee sword reach in px per facing direction as right,down,left,up (each 1-128; vanilla 28,17,28,17)")
               | lyra::opt(fast_rom)
                ["-f"]["--fast-rom"]("Experimental: convert to LoROM+FastROM (flag header, enable $420D, run boot, NMI and logged long-jumps from bank $80; CLI-only)");

    cli.add_argument(lyra::help(help));

    auto parse = cli.parse({argc, argv});

    if(help) {
        cout << cli;
        return 0;
    }

    if(!parse) {
        spdlog::error("Error in command line: {}", parse.message());
        return 1;
    }

    if(seed != numeric_limits<uint64_t>::max()) {
        options.use_seed = true;
        options.seed = seed;
    }

    options.walk_speed = static_cast<uint8_t>(std::clamp(walk_speed, 1, 15));

    // --sword-reach "right,down,left,up": exactly four integers, each in [1,128].
    // We range-check here (before the uint8_t narrowing) so out-of-range values
    // can't silently wrap; randomize_rom enforces the same range as a backstop.
    if(!sword_reach.empty()) {
        int reach[4];
        int count = 0;
        bool ok = true;
        std::stringstream ss(sword_reach);
        std::string token;
        while(std::getline(ss, token, ',')) {
            if(count >= 4) { ok = false; break; }
            try {
                size_t pos = 0;
                int value = std::stoi(token, &pos);
                while(pos < token.size() && std::isspace(static_cast<unsigned char>(token[pos]))) ++pos;
                if(pos != token.size()) { ok = false; break; }  // trailing garbage
                reach[count++] = value;
            } catch(...) {
                ok = false;
                break;
            }
        }
        if(!ok || count != 4) {
            spdlog::error("--sword-reach expects four comma-separated integers: right,down,left,up");
            return 1;
        }
        for(int value : reach) {
            if(value < 1 || value > 128) {
                spdlog::error("--sword-reach values must each be in the range 1..128");
                return 1;
            }
        }
        options.sword_reach_right = static_cast<uint8_t>(reach[0]);
        options.sword_reach_down  = static_cast<uint8_t>(reach[1]);
        options.sword_reach_left  = static_cast<uint8_t>(reach[2]);
        options.sword_reach_up    = static_cast<uint8_t>(reach[3]);
    }

    ifstream input(input_file, ios::binary);
    ofstream output(output_file, ios::binary | ios::trunc);

    vector<char> bytes{istreambuf_iterator<char>(input), istreambuf_iterator<char>()};

    auto result = randomize_rom(std::move(bytes), options);
    if(!result.success) {
        spdlog::error("Input ROM \"{}\": {}", input_file, result.error);
        return 1;
    }

    if(fast_rom) {
        apply_fast_rom(result.data);
        apply_fast_rom_banks(result.data);
        // Fast-rom edited header + code bytes; refresh the checksum once more.
        fix_header_checksum(result.data);
        spdlog::info("FastROM: header flagged, $420D enabled, boot + NMI + {} "
                     "long-jumps redirected to bank $80.",
                     std::size(kFastRomBankBumps));
    }

    copy(result.data.begin(), result.data.end(), ostreambuf_iterator<char>(output));

    return 0;
}
