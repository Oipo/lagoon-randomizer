#include <cctype>
#include <fstream>
#include <iterator>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

#include "randomizer.hpp"

using namespace std;

int main(int argc, const char** argv) {
    string input_file;
    string output_file;

    bool help{};
    bool fast_rom{};
    bool fade{};
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
               | lyra::opt(fade)
                ["--fade"]("Speed up the menu/transition fade-to-black (3 frames/step -> 1, ~3x faster)")
               | lyra::opt(fast_rom)
                ["-f"]["--fast-rom"]("Experimental: convert to LoROM+FastROM (flag header, enable $420D, run boot, NMI and logged long-jumps from bank $80)");

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
    options.fade = fade;
    options.fast_rom = fast_rom;

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

    copy(result.data.begin(), result.data.end(), ostreambuf_iterator<char>(output));

    return 0;
}
