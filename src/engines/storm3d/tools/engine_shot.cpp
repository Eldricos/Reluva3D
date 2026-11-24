// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool storm3d engine-shot <game folder> <out.png> <ms> [event...]:
// runs the game itself (its front end) without a window, by a clock of
// its own, the player's moves given as events, and writes the screen at
// `ms` as PNG, to set beside the original's frames from `host`.

#include "common/headless_system.h"
#include "common/text.h"
#include "devtools/tool.h"
#include "engines/registry.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace reluva;

// Gives the events at their times, keeps the screen at `shotAt`, then quits.
class ScriptSystem : public HeadlessSystem {
public:
    struct Timed {
        uint64_t at;
        Event ev;
    };
    std::vector<Timed> events; // sorted by time
    uint64_t shotAt = 0;
    Surface shot;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        if (shot.pixels.empty() && getMillis() >= shotAt)
            shot = s;
    }
    bool pollEvent(Event &ev) override {
        if (_next < events.size() && events[_next].at <= getMillis()) {
            ev = events[_next++].ev;
            return true;
        }
        if (!shot.pixels.empty() && !_quit) {
            _quit = true;
            ev = Event{};
            ev.type = EventType::kQuit;
            return true;
        }
        return false;
    }

private:
    size_t _next = 0;
    bool _quit = false;
};

// "1500:move:90:18", "2000:click:90:18" (down, up a tenth of a second
// later), "300:key:escape".
bool parseEvent(const std::string &text, std::vector<ScriptSystem::Timed> &out) {
    const std::vector<std::string> parts = text::split(text, ':');
    if (parts.size() < 3)
        return false;
    const uint64_t at = std::strtoull(parts[0].c_str(), nullptr, 10);
    Event ev;
    if ((parts[1] == "move" || parts[1] == "click") && parts.size() == 4) {
        ev.mouseX = std::atoi(parts[2].c_str());
        ev.mouseY = std::atoi(parts[3].c_str());
        ev.button = MouseButton::kLeft;
        ev.type = EventType::kMouseMove;
        out.push_back({at, ev});
        if (parts[1] == "click") {
            ev.type = EventType::kMouseDown;
            out.push_back({at, ev});
            ev.type = EventType::kMouseUp;
            out.push_back({at + 100, ev});
        }
        return true;
    }
    if (parts[1] == "key" && parts.size() == 3) {
        ev.type = EventType::kKeyDown;
        if (parts[2] == "escape")
            ev.key = key::kEscape;
        else if (parts[2] == "space")
            ev.key = key::kSpace;
        else if (parts[2].size() == 1)
            ev.key = uint32_t(parts[2][0]);
        else
            return false;
        out.push_back({at, ev});
        return true;
    }
    return false;
}

int cmdEngineShot(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool storm3d engine-shot <game folder> <out.png> <ms> [event...]\n");
        return 2;
    }
    ScriptSystem sys;
    sys.shotAt = std::strtoull(argv[4], nullptr, 10);
    for (int i = 5; i < argc; ++i)
        if (!parseEvent(argv[i], sys.events)) {
            std::fprintf(stderr, "not an event: %s\n", argv[i]);
            return 2;
        }
    std::stable_sort(sys.events.begin(), sys.events.end(),
                     [](const ScriptSystem::Timed &a, const ScriptSystem::Timed &b) { return a.at < b.at; });
    const MetaEngine *engine = findEngine("storm3d");
    if (!engine)
        return 1;
    GameTarget target{"armada", "storm3d", "armada", "Star Trek: Armada", argv[2], {}, {}};
    if (Error err = engine->create(sys, target)->run()) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    if (sys.shot.pixels.empty()) {
        std::fprintf(stderr, "nothing shown by %s ms\n", argv[4]);
        return 1;
    }
    image::Image img;
    img.w = sys.shot.w;
    img.h = sys.shot.h;
    img.rgba = sys.shot.pixels;
    const std::vector<uint8_t> png = image::encodePng(img);
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
    return out ? 0 : 1;
}

const tool::Commands kCommands("storm3d", {
                                              {"engine-shot", "<game folder> <out.png> <ms> [event...]",
                                               "the game run without a window, events at their times (1500:move:x:y, "
                                               "2000:click:x:y, 300:key:escape), its screen at ms written as PNG",
                                               cmdEngineShot},
                                          });

} // namespace
