// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/motoe/motoe.h"

namespace reluva::awe::motoe {

const GameInfo &game() {
    static const GameInfo kGame{
        .id = "motoe",
        // The original's title is its command line (WinMain 0x4018f0,
        // CreateWindowExW at 0x401a13), given by the installer's shortcut,
        // which the install doesn't hold: the name is the one its readme and
        // shortcut go by.
        .title = "Agatha Christie: Murder on the Orient Express",
        // Built with `reluva --hash`: each entry needs the size and the MD5
        // of the first kDetectionHashBytes bytes. Detection keys on the item
        // list: a patch is likelier to replace the executable than the text.
        .variants = {{"motoe",
                      "Agatha Christie: Murder on the Orient Express",
                      "en",
                      "windows",
                      {{"motoe.exe", "cdfcba4e6c8c7cee637959bc78bd46e7", 684032},
                       {"Text/itemlist.txt", "a2a0d2cd331a6fb36ffa79dced219aa5", 37328}}}},
        .fallback = {{"motoe",
                      "Agatha Christie: Murder on the Orient Express (unknown release)",
                      "",
                      "windows",
                      {{"motoe.exe", "", -1}, {"Text/itemlist.txt", "", -1}, {"granny2.dll", "", -1}}}},
        .playable = true,
        // The fog's full-screen pictures are 1024x768 (docs/engines/awe/games/motoe/PLAN.md).
        .width = 1024,
        .height = 768,
    };
    return kGame;
}

} // namespace reluva::awe::motoe
