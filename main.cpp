#include <fstream>
#include <iterator>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>
#include <vector>

#include "randomizer.hpp"

using namespace std;

int main(int argc, const char** argv) {
    string input_file;
    string output_file;

    bool help{};
    RandomizerOptions options;
    uint64_t seed{numeric_limits<uint64_t>::max()};
    // Parsed as int (Lyra treats uint8_t as a character), then narrowed below.
    int walk_speed{options.walk_speed};

    auto cli = lyra::opt(input_file, "input")
                ["-i"]["--input"]("Which rom to take as input").required()
               | lyra::opt(output_file, "output")
                ["-o"]["--output"]("What filename to output to").required()
               | lyra::opt(seed, "seed")
                ["-s"]["--seed"]("Seed the RNG with this value (same seed + options reproduces the same ROM, identical to the web version)")
               | lyra::opt(walk_speed, "walk-speed")
                ["-w"]["--walk-speed"]("Town walk speed in px/frame (3 = default, 4/5/6 = ~33/66/100% faster)");

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
