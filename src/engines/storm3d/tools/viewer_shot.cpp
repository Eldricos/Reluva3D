// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool storm3d viewer-shot <game folder> <out.png> [key...]: runs the
// data viewer on a game without a window, presses the keys in turn (each
// a tenth of a second after the last), and writes the last screen as PNG.
// Keys are a letter or digit, or tab, left, right, up, down, pageup,
// pagedown, home, end, space.

#include "common/headless_system.h"
#include "engines/viewer_registry.h"
#include "devtools/tool.h"
#include "image/image.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva;

// Presses the keys at set times, then Esc; keeps the last screen.
class ShotSystem : public HeadlessSystem {
public:
    std::vector<uint32_t> keys;
    Surface last;
    std::string title;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        last = s;
    }
    void setWindowTitle(const std::string &t) override { title = t; }
    bool pollEvent(Event &ev) override {
        if (getMillis() < _nextAt)
            return false;
        _nextAt = getMillis() + 100;
        ev.type = EventType::kKeyDown;
        ev.key = _next < keys.size() ? keys[_next] : key::kEscape;
        ++_next;
        return true;
    }

private:
    size_t _next = 0;
    uint64_t _nextAt = 100;
};

int cmdViewerShot(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d viewer-shot <game folder> <out.png> [key...]\n");
        return 2;
    }
    const std::map<std::string, uint32_t> named = {
        {"tab", key::kTab},   {"left", key::kLeft},     {"right", key::kRight},       {"up", key::kUp},
        {"down", key::kDown}, {"pageup", key::kPageUp}, {"pagedown", key::kPageDown}, {"home", key::kHome},
        {"end", key::kEnd},   {"space", key::kSpace}};
    ShotSystem sys;
    for (int i = 4; i < argc; ++i) {
        if (const auto it = named.find(argv[i]); it != named.end())
            sys.keys.push_back(it->second);
        else if (std::strlen(argv[i]) == 1)
            sys.keys.push_back(uint32_t(argv[i][0]));
        else {
            std::fprintf(stderr, "no key %s\n", argv[i]);
            return 2;
        }
    }
    const MetaViewer *viewer = findViewer("storm3d");
    if (!viewer)
        return 1;
    GameTarget target{"armada", "storm3d", "armada", "Star Trek: Armada", argv[2], {}, {}};
    if (Error err = viewer->create(sys, target)->run()) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    image::Image img;
    img.w = sys.last.w;
    img.h = sys.last.h;
    img.rgba = sys.last.pixels;
    const std::vector<uint8_t> png = image::encodePng(img);
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
    std::printf("%s\n", sys.title.c_str());
    return out ? 0 : 1;
}

const tool::Commands kCommands("storm3d", {
                                              {"viewer-shot", "<game folder> <out.png> [key...]",
                                               "the data viewer run without a window, keys pressed, its last screen "
                                               "written as PNG",
                                               cmdViewerShot},
                                          });

} // namespace
