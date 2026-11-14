// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/attwn.h"

#include "engines/awe/attwn/files.h"
#include "engines/awe/attwn/script.h"

#include <string>

namespace reluva::awe::attwn {

namespace {

// Narracott's "nothing to say" (0x4325a0): narracottgen's part 5 for the
// women, 6 for the men, "I have nothing more to say to her" or "him". The
// list's test is a "contains", so before anyone's been talked to it's "her".
std::string noTalk(const std::string &generic, int &part) {
    const std::string women = "brentgen claythornegen erogersgen mothergen";
    part = women.find(generic) != std::string::npos ? 0x68 : 0x69;
    return "narracottgen";
}

} // namespace

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
        // The table loader (0x431160) reads 14 columns: the 7th, Tone (notes
        // for the voices: "sleeping noises"), it drops; Duration and Location
        // it doesn't read; Line Translation it keeps apart (none in the
        // English release). Lines past 750 pixels wrap (0x431cba).
        .dialogue = {.target = false,
                     .duration = false,
                     .optional = false,
                     .translation = false,
                     .wrap = 750,
                     // Tried in turn (0x43357a); only Acts/Events is in the install.
                     .events = {"dialogue/acts/events/", "dialogue/location/events/", "dialogue/general/events/"}},
        // Its dialogue manager, against Orient Express's:
        // docs/engines/awe/games/attwn/systems/dialogue.md.
        .talk = {.player = "narracott",
                 .generic = "%sgen",   // 0x4323d0
                 .genericExact = true, // 0x4324f4
                 .goodbyes = "%sexit", // 0x432ad0
                 .goodbyesElse = nullptr,
                 .finale = nullptr,
                 .regreet = false, // 0x4328fa
                 .noTalk = noTalk,
                 .noTalkAlways = true, // 0x40d74f
                 .talkOptions = 1,     // 0x40d6af
                 .greetAndPlus = false,
                 .loneChoicePlays = false, // 0x432ec1
                 .repeatsStayOffered = false,
                 .alternativeKept = false, // 0x431f48
                 .offerLooksOn = false,    // 0x433040
                 .advance = false,
                 .keepBox = "talk ", // 0x4339ff
                 .dropBox = "fade ",
                 .leaveBoxWins = true,
                 .goodbyeLeft = true, // 0x432dc0
                 .textRate = 10,      // 0x4333f7
                 .textMin = 2,
                 .playerResetsColour = true, // 0x433254
                 .box = 90,
                 .finaleBox = 90,
                 .boxByRows = {90, 115, 141}, // 0x4328b6
                 .closesInventory = false,
                 .speakerElse = {{"brent", "brentknit"}, {"brent", "brentwig"}}}, // 0x433364
        // The camera's constructor (0x408510), the light's (0x41d5a0), and
        // the light's night test (0x41d760).
        .lighting = {.cameraAmbient = 0.5f, .lightDiffuse = 1, .att0 = 0, .att1 = 0.0001f, .nightSparesLights = true},
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
