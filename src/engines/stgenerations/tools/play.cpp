// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's command for running the game without a window: keys and
// clicks at given times, frames saved as pictures, a checksum of every
// frame shown. For seeing what the port does on a machine without a
// display, and for the game-files test.

#include "common/image.h"
#include "common/md5.h"
#include "common/stream.h"
#include "common/text.h"
#include "common/log.h"
#include "engines/stgenerations/debug.h"
#include "engines/stgenerations/game/generations.h"
#include "engines/stgenerations/game/random.h"
#include "engines/stgenerations/platform.h"
#include "engines/stgenerations/tools/scripted_system.h"
#include "tools/tool.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

namespace {

using namespace reluva;
using namespace reluva::stgenerations;

uint32_t keyNamed(const std::string& name) {
    static const std::map<std::string, uint32_t> names = {
        {"esc", key::Escape}, {"escape", key::Escape}, {"space", key::Space}, {"return", key::Return},
        {"enter", key::Return}, {"tab", key::Tab}, {"backspace", key::Backspace}, {"up", key::Up},
        {"down", key::Down}, {"left", key::Left}, {"right", key::Right}, {"home", key::Home}, {"end", key::End},
        {"pageup", key::PageUp}, {"pagedown", key::PageDown}, {"lctrl", key::LCtrl}, {"lshift", key::LShift},
        {"kpplus", key::KpPlus}, {"kpminus", key::KpMinus}, {"kpenter", key::KpEnter},
    };
    const std::string n = text::toLower(name);
    if (auto it = names.find(n); it != names.end())
        return it->second;
    if (n.size() >= 2 && n[0] == 'f' && std::atoi(n.c_str() + 1) >= 1)
        return key::F1 + uint32_t(std::atoi(n.c_str() + 1) - 1);
    if (n.size() == 1)
        return uint32_t(uint8_t(n[0]));
    return key::Unknown;
}

// "1500:x,y" -> 1500, x, y.
bool timeAndRest(const char* arg, uint64_t& at, std::string& rest) {
    const char* colon = std::strchr(arg, ':');
    if (!colon)
        return false;
    at = uint64_t(std::strtoull(arg, nullptr, 10));
    rest = colon + 1;
    return true;
}

int cmdPlay(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: reluva-tool stgenerations play <game folder> [--cd1 <dir>] [--cd2 <dir>] [--viewer]\n"
                     "       [--story <section>] [--combat <n>] [--seed <n>] [--saves <dir>] [--until <ms>]\n"
                     "       [--key <ms>:<name>] [--click <ms>:<x>,<y>] [--rclick <ms>:<x>,<y>] [--type <ms>:<text>]\n"
                     "       [--shot <ms>:<file.png>] [--frames] [--debug <level>] [--channels resources,scripts,sound]\n");
        return 2;
    }
    Options options;
    options.gameDir = argv[2];
    options.description = "Star Trek: Generations";
    // Saves dated the same on every run, as the menu shows them.
    options.fixedSaveDate = "01/01 00:00";
    std::filesystem::path saves;
    uint64_t until = 60000;
    uint32_t seed = 1;
    bool listFrames = false;
    ScriptedSystem sys;
    std::map<uint64_t, std::string> shots;
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : "";
        uint64_t at = 0;
        std::string rest;
        if (a == "--cd1") options.cd1 = v, ++i;
        else if (a == "--cd2") options.cd2 = v, ++i;
        else if (a == "--viewer") options.viewer = true;
        else if (a == "--story") options.story = v, ++i;
        else if (a == "--combat") options.combat = std::atoi(v), ++i;
        else if (a == "--seed") seed = uint32_t(std::strtoul(v, nullptr, 10)), ++i;
        else if (a == "--saves") saves = v, ++i;
        else if (a == "--until") until = std::strtoull(v, nullptr, 10), ++i;
        else if (a == "--frames") listFrames = true;
        else if (a == "--debug") log::setDebugLevel(std::atoi(v)), ++i;
        else if (a == "--channels") {
            for (const std::string& c : text::split(v, ','))
                enableDebugChannel(c);
            ++i;
        }
        else if (a == "--key" && timeAndRest(v, at, rest)) {
            const uint32_t k = keyNamed(rest);
            if (k == key::Unknown) {
                std::fprintf(stderr, "unknown key %s\n", rest.c_str());
                return 2;
            }
            sys.key(at, k);
            ++i;
        } else if ((a == "--click" || a == "--rclick") && timeAndRest(v, at, rest)) {
            int x = 0, y = 0;
            if (std::sscanf(rest.c_str(), "%d,%d", &x, &y) != 2)
                return 2;
            sys.click(at, x, y, a == "--click" ? MouseButton::Left : MouseButton::Right);
            ++i;
        } else if (a == "--type" && timeAndRest(v, at, rest)) {
            sys.type(at, rest);
            ++i;
        } else if (a == "--shot" && timeAndRest(v, at, rest)) {
            shots[at] = rest;
            ++i;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    sys.quit(until);
    RandomSource::setFixedSeed(seed);

    // Each frame's checksum, and the pictures asked for, taken from the
    // frame on screen at their time.
    int frames = 0;
    std::string last;
    uint64_t lastAt = 0;
    sys.onFrame = [&](const Surface& rgba, uint64_t ms) {
        ++frames;
        Md5 md5;
        md5.update(rgba.pixels.data(), rgba.pixels.size());
        const std::string sum = md5.hexDigest();
        if (listFrames && sum != last)
            std::printf("%8llu ms  %s\n", static_cast<unsigned long long>(ms), sum.c_str());
        last = sum;
        lastAt = ms;
        while (!shots.empty() && shots.begin()->first <= ms) {
            image::Image img;
            img.w = rgba.w, img.h = rgba.h;
            img.rgba = rgba.pixels;
            if (!writeFile(shots.begin()->second, image::encodePng(img)))
                std::fprintf(stderr, "can't write %s\n", shots.begin()->second.c_str());
            shots.erase(shots.begin());
        }
    };

    audio::Mixer mixer(sys.audioRate());
    sys.attachMixer(&mixer);
    Platform platform(sys, &mixer, saves);
    Error err;
    {
        Generations game(&platform, options);
        err = game.run();
    }
    std::printf("%d frames shown, until %llu ms; the last %s\n", frames, static_cast<unsigned long long>(lastAt),
                last.c_str());
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    return 0;
}

const reluva::tool::Commands commands("stgenerations", {
    {"play", "<game folder> [options]",
     "run the game without a window: keys and clicks at given times (--key 2000:esc, --click 3000:120,40), "
     "pictures of the screen (--shot 5000:title.png), each frame's checksum (--frames); --until <ms> ends it",
     cmdPlay},
});

} // namespace
