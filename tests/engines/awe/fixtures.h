// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the AWE engine's unit tests share: bytes built as the game's files
// are, and a script host that records what it's asked. No game data is
// ever committed; every file a test reads is made here.

#pragma once

#include "engines/awe/ac2.h"
#include "engines/awe/bink_texture.h"
#include "engines/awe/camera.h"
#include "engines/awe/conditions.h"
#include "engines/awe/curves.h"
#include "engines/awe/dialogue.h"
#include "engines/awe/game_text.h"
#include "engines/awe/gr2.h"
#include "engines/awe/model.h"
#include "engines/awe/oodle1.h"
#include "engines/awe/particles.h"
#include "engines/awe/picking.h"
#include "engines/awe/script.h"
#include "engines/awe/story.h"
#include "engines/awe/savegame.h"
#include "engines/awe/shadows.h"
#include "engines/awe/lighting.h"
#include "image/image.h"
#include "engines/awe/actor.h"
#include "engines/awe/animator.h"
#include "engines/awe/sprite.h"
#include "engines/awe/tables.h"
#include "engines/awe/walk_grid.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "tests/support/test.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::awe::testing {

struct Bytes {
    std::vector<uint8_t> d;
    void u16(uint16_t v) { d.push_back(uint8_t(v)), d.push_back(uint8_t(v >> 8)); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++)
            d.push_back(uint8_t(v >> (8 * i)));
    }
    void f32(float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        u32(v);
    }
    void tag(const char* t) { u32(uint32_t(t[0]) << 24 | uint32_t(t[1]) << 16 | uint32_t(t[2]) << 8 | uint32_t(t[3])); }
    // UTF-16 in a fixed buffer, the rest filled as the game's files are.
    void text(const std::string& s, size_t chars) {
        for (char c : s)
            u16(uint8_t(c));
        u16(0);
        for (size_t i = s.size() + 1; i < chars; i++)
            d.push_back(0xcd), d.push_back(0xcd);
    }
    void put32(size_t at, uint32_t v) {
        for (int i = 0; i < 4; i++)
            d[at + size_t(i)] = uint8_t(v >> (8 * i));
    }
};

inline std::vector<uint8_t> utf16File(const std::string& s) {
    Bytes b;
    b.d = {0xff, 0xfe};
    for (char c : s)
        b.u16(uint8_t(c));
    return b.d;
}
// A world for scripts: walks and fades take until told; the rest is
// instant. It records what it was asked.
struct FakeHost : ScriptHost {
    std::vector<std::string> started;
    bool start(ScriptRunner&, const ScriptEvent& e, bool wait) override {
        std::string s = e.verb();
        for (size_t i = 1; i < e.words.size(); i++)
            s += " " + e.words[i];
        started.push_back(s + (wait ? "" : " (no wait)"));
        return e.verb() == "walk" || e.verb() == "fade" || e.verb() == "talk";
    }
};

} // namespace reluva::awe::testing
