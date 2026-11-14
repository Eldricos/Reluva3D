// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/attwn.h"

#include "engines/awe/attwn/files.h"
#include "engines/awe/attwn/script.h"

namespace reluva::awe::attwn {

const GameInfo &game() {
    static const GameInfo kGame{
        .id = "attwn",
        // ac.exe's own, given to CreateWindowExA at 0x401980 (through
        // [0x47c0c0]).
        .title = "Agatha Christie - And Then There Were None",
        // Built with `reluva --hash`, as Orient Express's: the executable and
        // the item list.
        .variants = {{"attwn",
                      "Agatha Christie: And Then There Were None",
                      "en",
                      "windows",
                      {{"ac.exe", "24bc962a1f2acfb788132d0ec198063d", 593920},
                       {"Text/itemlist.act", "b9f27db08f64b7340ccbaaef131bf9b8", 12306}}}},
        .fallback = {{"attwn",
                      "Agatha Christie: And Then There Were None (unknown release)",
                      "",
                      "windows",
                      {{"ac.exe", "", -1}, {"Text/itemlist.act", "", -1}, {"granny2.dll", "", -1}}}},
        .playable = false,
        .openFiles = openFiles,
        // Its window's client area (CreateWindowExA at 0x401980: 0x320 by
        // 0x258); the backgrounds and cameras are drawn at it.
        .width = 800,
        .height = 600,
        .condition = condition,
        .verb = verb,
        .exe = "ac.exe",
        // Orient Express's path-finder, the same code (24 functions) but
        // for a call of its debug log, an empty function here (0x407af0):
        // docs/engines/awe/games/attwn/formats/ac1.md.
        .pathFinder = {.statics = {0x426600, {0xdd, 0x05, 0x18, 0x4b, 0x47, 0x00, 0xdc, 0x25}},
                       .constructor = {0x426620, {0x8b, 0xc1, 0xb1, 0xff, 0xc7, 0x00, 0x20, 0x4b}},
                       .buildTables = {0x4266e0, {0x56, 0x8b, 0xf1, 0x57, 0xb9, 0x00, 0x04, 0x00}},
                       .search = {0x426d20, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec}},
                       .operatorNew = {0x46622c, {0x6a, 0x01, 0xff, 0x74, 0x24, 0x08, 0xe8, 0xec}},
                       .operatorDelete = {0x4661d2, {0xff, 0x74, 0x24, 0x04, 0xe8, 0xcd, 0x14, 0x00}},
                       .objectSize = 0x4d4600},
    };
    return kGame;
}

} // namespace reluva::awe::attwn
