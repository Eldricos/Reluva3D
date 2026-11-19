// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/motoe/motoe.h"

namespace reluva::awe::motoe {

const GameInfo &game() {
    static const GameInfo kGame{
        .id = "motoe",
        // interface.txt's "title" row, read by 0x4017c0 for WinMain's
        // CreateWindowExW (0x401a13).
        .title = "Agatha Christie - Murder on the Orient Express",
        .titleFromInterface = true,
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
        // Setup's typical install, as And Then There Were None's: the game,
        // and motoe.exe, readme.txt and ac.ico beside it (the CDs' data1.hdr).
        .setupGroups = {"Program Executable Files", "Txt Files"},
        // The fog's full-screen pictures are 1024x768 (docs/engines/awe/games/motoe/PLAN.md).
        .width = 1024,
        .height = 768,
        .dialogue = {},    // the defaults are its tables' (motoe.exe 0x438f00)
        .talk = {},        // and its dialogue manager's (0x43a630 on)
        .lighting = {},    // and its lights' (0x420cb0 on)
        .fonts = {},       // from text/fonts.txt
        .dialogueBox = {}, // the defaults are its box (0x43cf90)
        .saves = {},       // and its saves
        .flow = {},        // and its acts and scenes
        .options = {},     // and its options screen
        .yesNo = {},       // and its yes/no popup
        .inventory = {},   // and its items
        .safe = {},        // its safe
        .exe = "motoe.exe",
        // English 1.0's (docs/engines/awe/games/motoe/formats/ac2.md, the
        // walk grid).
        .pathFinder = {.statics = {0x42e820, {0xdd, 0x05, 0x18, 0x2d, 0x48, 0x00, 0xdc, 0x25}},
                       .constructor = {0x42e840, {0x8b, 0xc1, 0xb1, 0xff, 0xc7, 0x00, 0x20, 0x2d}},
                       .buildTables = {0x42e900, {0x56, 0x8b, 0xf1, 0x57, 0xb9, 0x00, 0x04, 0x00}},
                       .search = {0x42ef40, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec}},
                       .operatorNew = {0x472551, {0x6a, 0x01, 0xff, 0x74, 0x24, 0x08, 0xe8, 0xdb}},
                       .operatorDelete = {0x4722d2, {0xff, 0x74, 0x24, 0x04, 0xe8, 0xe1, 0x11, 0x00}},
                       .objectSize = 0x4d4600},
    };
    return kGame;
}

} // namespace reluva::awe::motoe
