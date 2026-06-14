#pragma once

#include <algorithm>
#include <cstdint>
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

    [[nodiscard]] PC_addr toPc() const {
    return (static_cast<PC_addr>(bank & 0x7F) * 0x8000u) + (addr & 0x7FFFu);
    }

    void fromPc(PC_addr pc) {
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
    auto put_velocity = [&](uint32_t offset, int16_t value) {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    };
    put_velocity(0x010149, +v);  // tier 2, right  (+X)
    put_velocity(0x01014F, +v);  // tier 2, down   (+Y)
    put_velocity(0x010151, -v);  // tier 2, left   (-X)
    put_velocity(0x010157, -v);  // tier 2, up     (-Y)

    std::mt19937 rng;
    if(options.use_seed) {
        rng.seed(static_cast<std::mt19937::result_type>(options.seed));
    } else {
        std::random_device dev;
        rng.seed(dev());
    }

    // Keep the SNES header checksum valid after the byte patches above.
    fix_header_checksum(bytes);

    result.success = true;
    result.data = std::move(bytes);
    return result;
}
