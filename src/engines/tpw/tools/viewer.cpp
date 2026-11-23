// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World command for running the data viewer
// headless, as the game-files test does.

#include "common/events.h"
#include "common/headless_system.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/registry.h"
#include "engines/viewer_registry.h"
#include "image/image.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace reluva;

// The headless System with a player: key presses at given times. It keeps
// a checksum of every screen shown and the window's titles, and with a
// folder, writes the first screen after each key press as a PNG.
class PlayingSystem : public HeadlessSystem {
public:
    struct Input {
        uint64_t at;
        Event event;
    };
    std::vector<Input> inputs;
    Md5 screens;
    int shown = 0;
    std::vector<std::string> titles;
    std::filesystem::path shots;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        screens.update(s.pixels.data(), s.pixels.size());
        ++shown;
        if (!shots.empty() && _shotInput != _next) {
            _shotInput = _next;
            image::Image img;
            img.w = s.w;
            img.h = s.h;
            img.rgba = s.pixels;
            const std::vector<uint8_t> png = image::encodePng(img);
            char name[32];
            std::snprintf(name, sizeof name, "%03zu.png", _next);
            std::ofstream(shots / name, std::ios::binary)
                .write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
        }
    }
    void setWindowTitle(const std::string &t) override {
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
    }
    bool pollEvent(Event &ev) override {
        if (_next < inputs.size() && inputs[_next].at <= getMillis()) {
            ev = inputs[_next++].event;
            return true;
        }
        return false;
    }

private:
    size_t _next = 0;
    size_t _shotInput = ~size_t(0);
};

// Every page, a few keys on each, as reluva-viewer shows them: the next
// picture, zoomed to its size and back; the next font; the next table, a
// string down, in the game's font; the next settings file, scrolled; the
// last model, turned, tilted, nearer, untextured; the next world, turned,
// without its ground, its objects, then its sky; the next bank, a sound down, played (nothing heard:
// there's no device); the movies' list; a file down. Tab round to the pictures, then Esc.
int cmdViewerRun(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw viewer-run <game folder> [-shots <folder>]\n");
        return 2;
    }
    PlayingSystem sys;
    for (int i = 3; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "-shots") == 0) {
            sys.shots = argv[i + 1];
            std::filesystem::create_directories(sys.shots);
        }
    const MetaEngine *engine = findEngine("tpw");
    const MetaViewer *viewer = findViewer("tpw");
    const std::vector<DetectedGame> found = engine ? engine->detect(argv[2]) : std::vector<DetectedGame>{};
    if (!viewer || found.empty()) {
        std::fprintf(stderr, "%s: no game of the tpw engine found\n", argv[2]);
        return 1;
    }
    uint64_t t = 500;
    const auto press = [&](uint32_t key, uint16_t mods = 0) {
        Event e;
        e.type = EventType::kKeyDown;
        e.key = key;
        e.mods = mods;
        sys.inputs.push_back({t, e});
        t += 200;
    };
    for (uint32_t k : {uint32_t(key::kRight), uint32_t('z'), uint32_t('z'), uint32_t(key::kTab)})
        press(k);
    for (uint32_t k : {uint32_t(key::kRight), uint32_t(key::kTab)})
        press(k);
    for (uint32_t k : {uint32_t(key::kRight), uint32_t(key::kDown), uint32_t('f'), uint32_t(key::kTab)})
        press(k);
    for (uint32_t k : {uint32_t(key::kRight), uint32_t(key::kDown), uint32_t(key::kTab)})
        press(k);
    for (uint32_t k :
         {uint32_t(key::kEnd), uint32_t('d'), uint32_t('w'), uint32_t('z'), uint32_t('t'), uint32_t(key::kTab)})
        press(k);
    for (uint32_t k :
         {uint32_t(key::kRight), uint32_t('a'), uint32_t('g'), uint32_t('o'), uint32_t('k'), uint32_t(key::kTab)})
        press(k);
    for (uint32_t k : {uint32_t(key::kRight), uint32_t(key::kDown), uint32_t(key::kSpace), uint32_t(key::kTab)})
        press(k);
    press(key::kTab);
    press(key::kDown);
    press(key::kTab);
    press(key::kTab, keymod::kShift);
    press(key::kTab);
    press(key::kEscape);
    // Esc should have ended it: this only stops a run that didn't.
    sys.inputs.push_back({t + 5000, Event{EventType::kReturnToLauncher}});

    GameTarget target{"tpw", "tpw", found[0].gameId, found[0].description, argv[2], {}, {}};
    std::unique_ptr<Engine> run = viewer->create(sys, target);
    const Error err = run->run();
    std::printf("result: %s\n", err ? ("error: " + err.message).c_str() : "ok");
    std::printf("screens: %d %s\n", sys.shown, sys.screens.hexDigest().c_str());
    for (const std::string &title : sys.titles)
        std::printf("title: %s\n", title.c_str());
    std::printf("left by: %s\n", sys.getMillis() < t + 5000 ? "Esc" : "nothing: still running");
    return err ? 1 : 0;
}

const tool::Commands kCommands("tpw", {
                                          {"viewer-run", "<game folder> [-shots <folder>]",
                                           "run the data viewer headless: every page, a few keys on each, then Esc; a "
                                           "checksum of every screen shown and the window's titles (-shots writes the "
                                           "screen after each key)",
                                           cmdViewerRun},
                                      });

} // namespace
