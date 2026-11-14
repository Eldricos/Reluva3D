// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/attwn.h"

#include "engines/awe/attwn/files.h"

namespace reluva::awe::attwn {

const GameInfo &game() {
    static const GameInfo kGame{
        "attwn",
        // ac.exe's own, given to CreateWindowExA at 0x401980 (through
        // [0x47c0c0]).
        "Agatha Christie - And Then There Were None",
        // Built with `reluva --hash`, as Orient Express's: the executable and
        // the item list.
        {{"attwn",
          "Agatha Christie: And Then There Were None",
          "en",
          "windows",
          {{"ac.exe", "24bc962a1f2acfb788132d0ec198063d", 593920},
           {"Text/itemlist.act", "b9f27db08f64b7340ccbaaef131bf9b8", 12306}}}},
        {{"attwn",
          "Agatha Christie: And Then There Were None (unknown release)",
          "",
          "windows",
          {{"ac.exe", "", -1}, {"Text/itemlist.act", "", -1}, {"granny2.dll", "", -1}}}},
        false,
        openFiles,
    };
    return kGame;
}

} // namespace reluva::awe::attwn
