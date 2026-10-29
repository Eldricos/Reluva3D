// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The game's saved games (motoe.exe 0x411230 writes, 0x411c60 reads):
// Saved.Games\slot<N>.ac2, quick.ac2, and the developers' starting points
// Text\Scripts\slot<N>.ac2. Tagged records like the scenes' (DESC, TIME,
// DATE, GLBT...), but back to back with no lengths, to the end of the
// file; text in fixed UTF-16 buffers. See docs/formats/awe/saves.md.

namespace reluva::awe {

struct SaveGame {
    std::string description, time, date; // "VGFDGFDG", "10:41", "11-02-06"
    int timeOfDay = 0, weather = 0, moving = 0; // GLBT, GLBW, GLBM: the globals when saved
    // INFO: the location, the saved scene's path
    // ("Locations\Calais.Hall\Calais.Hall_03_06b.ac2"), the act.
    std::string location, scene;
    int actMajor = 1, actMinor = 1;
    // ACTI: the act's row as the game held it (acts.txt isn't read again).
    struct Act {
        std::string preLoad, location, postLoad, completion, event, music;
        int weather = 0, timeOfDay = 0, moving = 0;
    } act;
    struct Placement {
        float position[3] = {0, 0, 0};
        float rotation[3] = {0, 0, 0}; // radians; y is the heading
    };
    std::optional<Placement> player, follower; // CHAR, FLLW
    std::vector<std::pair<std::string, int>> tags, timers; // TAGS, TIMS
    std::vector<std::pair<std::string, std::string>> texts; // STAT: text tags
    // POSI: where characters were, by scene file and model file
    // ("Sirkeci.Market_01.ac2", "klaus_model.gr2").
    struct Position {
        std::string scene, character;
        Placement at;
    };
    std::vector<Position> positions;
    // TALK: conversations' progress (part a = 0; thread "1" = 96).
    struct Talk {
        std::string name, act;
        int part = 0, thread = 0, status = 0, nest = -1;
    };
    std::vector<Talk> talks;
    // ITEM: the inventory page shown, then each item: its state (2 in the
    // bag, 4 taken away but kept, 5 a passport, 6 a fingerprint card), its
    // slot and page (16 slots a page).
    int itemPage = 0;
    struct Item {
        std::string name;
        int state = 2, slot = -1, page = -1;
    };
    std::vector<Item> items;
};

// Reads a saved game; fails on a record it doesn't know or a short file.
Error readSaveGame(const std::vector<uint8_t>& data, SaveGame& out);
std::vector<uint8_t> writeSaveGame(const SaveGame& save);
// The first three records alone (0x412440): for the load screen's list.
Error readSaveHeader(const std::vector<uint8_t>& data, std::string& description, std::string& date, std::string& time);

// A 24-bit BMP of an RGBA picture (the slots' 256x192 thumbnails), as
// the game writes them (alpha dropped).
std::vector<uint8_t> encodeBmp24(int width, int height, const std::vector<uint8_t>& rgba);

} // namespace reluva::awe
