// Emscripten/embind entry point for the browser build.
//
// Exposes a single function `Module.randomizeRom(romBytes, options)` to
// JavaScript. It takes the uploaded ROM as a Uint8Array plus a plain options
// object and returns { success, error, data }, where `data` is a Uint8Array
// holding the patched ROM. The actual work happens in the shared randomize_rom
// in randomizer.hpp, so the browser and the native CLI behave identically.

#include <cstdint>
#include <vector>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "randomizer.hpp"

using namespace emscripten;

namespace {

bool get_bool(const val& opts, const char* key, bool fallback) {
    if(opts.isUndefined() || opts.isNull()) {
        return fallback;
    }
    val v = opts[key];
    if(v.isUndefined() || v.isNull()) {
        return fallback;
    }
    return v.as<bool>();
}

void set_optional_uint32(uint32_t& target, const val& opts, const char* key) {
    if(opts.isUndefined() || opts.isNull()) {
        return;
    }
    val v = opts[key];
    if(v.isUndefined() || v.isNull()) {
        return;
    }
    target = static_cast<uint32_t>(v.as<double>());
}

val randomize(val input, val opts) {
    RandomizerOptions options;

    if(!opts.isUndefined() && !opts.isNull()) {
        val seed = opts["seed"];
        if(!seed.isUndefined() && !seed.isNull()) {
            options.use_seed = true;
            options.seed = static_cast<uint64_t>(seed.as<double>());
        }
    }

    uint32_t walk_speed = options.walk_speed;
    set_optional_uint32(walk_speed, opts, "walkSpeed");
    options.walk_speed = static_cast<uint8_t>(walk_speed);

    // Sword reach per facing direction (right, down, left, up). Read as uint32 and
    // range-check before narrowing to uint8_t so an out-of-range value can't wrap
    // into the valid band; randomize_rom enforces the same [1,128] range too.
    uint32_t sword[4] = {options.sword_reach_right, options.sword_reach_down,
                         options.sword_reach_left, options.sword_reach_up};
    set_optional_uint32(sword[0], opts, "swordReachRight");
    set_optional_uint32(sword[1], opts, "swordReachDown");
    set_optional_uint32(sword[2], opts, "swordReachLeft");
    set_optional_uint32(sword[3], opts, "swordReachUp");
    for(uint32_t r : sword) {
        if(r < 1 || r > 128) {
            val ret = val::object();
            ret.set("success", false);
            ret.set("error", "Sword reach values must each be in the range 1..128.");
            return ret;
        }
    }
    options.sword_reach_right = static_cast<uint8_t>(sword[0]);
    options.sword_reach_down  = static_cast<uint8_t>(sword[1]);
    options.sword_reach_left  = static_cast<uint8_t>(sword[2]);
    options.sword_reach_up    = static_cast<uint8_t>(sword[3]);

    std::vector<uint8_t> in = convertJSArrayToNumberVector<uint8_t>(input);
    std::vector<char> bytes(in.begin(), in.end());

    RandomizerResult res = randomize_rom(std::move(bytes), options);

    val ret = val::object();
    ret.set("success", res.success);
    ret.set("error", res.error);
    if(res.success) {
        // Allocate a JS-owned Uint8Array and copy into it before res.data dies.
        val out = val::global("Uint8Array").new_(res.data.size());
        out.call<void>("set", val(typed_memory_view(
            res.data.size(), reinterpret_cast<const uint8_t*>(res.data.data()))));
        ret.set("data", out);
    }
    return ret;
}

} // namespace

EMSCRIPTEN_BINDINGS(lagoon_randomizer) {
    function("randomizeRom", &randomize);
}
