// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the Clue engine's tests share: its files, made up here (packed
// archives, images, animations, objects, levels, dialogs) and a small board.

#pragma once

#include "engines/clue/board.h"
#include "engines/clue/level.h"
#include "tests/support/test.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace reluva::clue::testing {

using Bytes = std::vector<uint8_t>;

inline void put16(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

inline void put32(Bytes& b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

inline void putText(Bytes& b, const char* s) {
    b.push_back(uint8_t(std::strlen(s)));
    b.insert(b.end(), s, s + std::strlen(s));
}

// A zlib stream holding the data in one stored (uncompressed) block.
inline Bytes zlibStored(const Bytes& data) {
    Bytes z = {0x78, 0x01, 0x01};
    put16(z, uint32_t(data.size()));
    put16(z, uint32_t(~data.size() & 0xffff));
    z.insert(z.end(), data.begin(), data.end());
    uint32_t a = 1, b = 0;
    for (uint8_t v : data) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    const uint32_t adler = b << 16 | a;
    for (int s = 24; s >= 0; s -= 8)
        z.push_back(uint8_t(adler >> s));
    return z;
}

inline Bytes pack(const Bytes& data) {
    const Bytes z = zlibStored(data);
    Bytes p = {'P', 'A', 'C', 'K'};
    put32(p, uint32_t(z.size()));
    put32(p, uint32_t(data.size()));
    p.insert(p.end(), z.begin(), z.end());
    return p;
}

// A DDRV archive of named records, its directory in reverse name order as
// the game's aren't in file order either.
inline Bytes drive(const std::vector<std::pair<std::string, Bytes>>& files) {
    Bytes d = {'D', 'D', 'R', 'V', 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> offsets;
    for (const auto& f : files) {
        offsets.push_back(uint32_t(d.size()));
        d.insert(d.end(), f.second.begin(), f.second.end());
    }
    const uint32_t directory = uint32_t(d.size());
    for (size_t i = files.size(); i-- > 0;) {
        put32(d, 0);
        put32(d, 0x01bddc75); // a FILETIME
        put32(d, 0xad5a42e0);
        put32(d, offsets[i]);
        put32(d, uint32_t(files[i].second.size()));
        Bytes name(128, 0);
        std::memcpy(name.data(), files[i].first.data(), files[i].first.size());
        d.insert(d.end(), name.begin(), name.end());
    }
    put32(d, uint32_t(files.size()));
    const uint32_t count = uint32_t(files.size());
    std::memcpy(d.data() + 4, &directory, 4);
    std::memcpy(d.data() + 8, &count, 4);
    return d;
}

// A version 0x200 EIMG: 3x2, row 0 a skip of 1 then red and green, row 1
// blue then nothing.
inline Bytes image200() {
    const Bytes pixels = {0x81, 0x02, 0x00, 0xf8, 0xe0, 0x07, 0x80, 0x01, 0x1f, 0x00, 0x80};
    Bytes r = {'E', 'I', 'M', 'G'};
    put32(r, 0x200);
    put32(r, uint32_t(45 + pixels.size()));
    put32(r, 2);
    put32(r, 3);
    put32(r, 2);
    r.push_back(3);
    put32(r, 8);
    r.insert(r.end(), 12, 0);
    put32(r, 3);
    put32(r, 2);
    put32(r, 0xff00);
    put32(r, uint32_t(pixels.size()));
    r.insert(r.end(), pixels.begin(), pixels.end());
    return r;
}

// A version 0x100 EIMG: a 2x1 white glyph at (1, 3) in a 5x8 cell; its
// rows end with 0.
inline Bytes image100() {
    const Bytes pixels = {0x02, 0xff, 0xff, 0xff, 0xff, 0x00};
    Bytes r = {'E', 'I', 'M', 'G'};
    put32(r, 0x100);
    put32(r, uint32_t(40 + pixels.size()));
    put16(r, 2);
    put16(r, 1);
    put32(r, 16);
    put32(r, 2);
    r.insert(r.end(), 12, 0);
    put16(r, 1);
    put16(r, 3);
    put16(r, 5);
    put16(r, 8);
    put32(r, 0xff00);
    put32(r, uint32_t(pixels.size()));
    r.insert(r.end(), pixels.begin(), pixels.end());
    return r;
}

// An ANIM record of 2 frames, 2 layers and 2 images, with an ISOA block in
// the long form (one extra on frame 1) or the short one.
inline Bytes animation(bool shortForm, int solid = 1, int sizeX = 16, int sizeY = 16) {
    Bytes r = {'A', 'N', 'I', 'M'};
    put16(r, 0x200);
    r.push_back(0x80);
    r.insert(r.end(), 128, 0xcd); // unnamed
    r.insert(r.end(), {6, 2, 2});  // kind, kind, layers
    put16(r, 2);                   // frames
    put16(r, 2);                   // images
    put16(r, 75);
    put16(r, 120);
    put32(r, 10);
    put32(r, 136);
    put32(r, 4 + 2 * 2 * 10);
    putText(r, "test:Ia");
    putText(r, "test:Ib");
    for (int layer = 0; layer < 2; ++layer) {
        put32(r, uint32_t(layer));
        for (int frame = 0; frame < 2; ++frame) {
            put16(r, uint32_t(layer));                  // image
            put16(r, uint32_t(layer * 10 + frame));     // x
            put16(r, uint32_t(-5 & 0xffff));            // y
            put16(r, 0);
            put16(r, 0);
        }
    }
    r.insert(r.end(), {'I', 'S', 'O', 'A'});
    Bytes isoa;
    for (int frame = 0; frame < 2; ++frame) {
        put16(isoa, uint32_t(-44 & 0xffff));
        put16(isoa, uint32_t(-106 & 0xffff));
        if (shortForm) {
            isoa.insert(isoa.end(), 6, 0);
            continue;
        }
        for (int i = 0; i < 5; ++i)
            put16(isoa, uint32_t(i));
        put16(isoa, uint32_t(solid & 0xffff));
        put16(isoa, uint32_t(sizeX)); // the box on the floor
        put16(isoa, uint32_t(sizeY));
        put16(isoa, 100 + uint32_t(frame));
        put16(isoa, uint32_t(frame)); // extras
        if (frame == 1)
            isoa.insert(isoa.end(), {0x88, 0x13, 0, 0, 0, 0, 0});
    }
    put32(r, uint32_t(isoa.size()));
    r.insert(r.end(), isoa.begin(), isoa.end());
    return r;
}

// An object record: its states and timings.
inline Bytes objectRecord(const char* name, uint8_t type, const std::vector<std::pair<int, const char*>>& states,
                          const std::vector<std::pair<int, int>>& timings = {}) {
    Bytes r;
    put16(r, 0); // the length, filled in below
    r.push_back(1);
    Bytes field(32, 0);
    std::memcpy(field.data(), name, std::strlen(name));
    field[std::strlen(name) + 1] = 0x28; // what memory held after the name
    r.insert(r.end(), field.begin(), field.end());
    r.push_back(type);
    put32(r, 1);
    put16(r, uint32_t(states.size()));
    for (const auto& [id, anim] : states) {
        put16(r, uint32_t(id));
        Bytes a(32, 0);
        std::memcpy(a.data(), anim, std::strlen(anim));
        r.insert(r.end(), a.begin(), a.end());
    }
    put16(r, uint32_t(timings.size()));
    for (const auto& [time, state] : timings) {
        put16(r, uint32_t(time));
        r.push_back(0);
        put16(r, uint32_t(state));
        put16(r, 0);
    }
    r[0] = uint8_t(r.size());
    r[1] = uint8_t(r.size() >> 8);
    r.insert(r.end(), {0x4e, 0x00}); // the archive's size takes in the next record's first 2 bytes
    return r;
}

struct TestPlacement {
    uint8_t flags;
    int tag, object, x, y, id, layer;
    std::vector<std::pair<int, int>> timings;
};

// clue.lvl: World.dat's names and Objects.dat's placements.
inline Bytes levelFile(const std::vector<const char*>& names, const std::vector<TestPlacement>& placements) {
    Bytes world(40, 0);
    std::memcpy(world.data(), "Unnamed", 7);
    put16(world, uint32_t(names.size()));
    for (const char* n : names)
        putText(world, n);
    Bytes objects;
    put16(objects, uint32_t(placements.size()));
    for (const TestPlacement& p : placements) {
        Bytes r;
        r.push_back(p.flags);
        if (p.flags & 0x02)
            put16(r, uint32_t(p.tag));
        put16(r, uint32_t(p.object));
        put16(r, uint32_t(p.x & 0xffff));
        put16(r, uint32_t(p.y & 0xffff));
        put16(r, uint32_t(p.id & 0xffff));
        r.push_back(uint8_t(p.layer));
        if (p.flags & 0x20) {
            put16(r, uint32_t(p.timings.size()));
            for (const auto& [time, state] : p.timings) {
                put16(r, uint32_t(time));
                r.push_back(0);
                put16(r, uint32_t(state));
                put16(r, 0);
            }
        }
        put16(objects, uint32_t(r.size() + 2));
        objects.insert(objects.end(), r.begin(), r.end());
    }
    return drive({{"/World.dat", world}, {"/Info.dat", {0, 0}}, {"/Objects.dat", objects},
                  {"/Workspace.dat", Bytes(32, 0)}});
}

// A RESI string: its length, the characters and a NUL (nothing for "").
inline void putResiText(Bytes& b, const std::string& s) {
    b.push_back(uint8_t(s.size()));
    if (s.empty())
        return;
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

// A control: its 112-byte block, its two strings and its list's items.
inline void putControl(Bytes& b, int type, int id, int l, int t, int r, int bot, int centreX, int font, int group,
                       const std::string& text, const std::string& anim, const std::vector<std::string>& items = {}) {
    std::array<int32_t, 28> v{};
    v[0] = type;
    v[1] = id;
    v[2] = l;
    v[3] = t;
    v[4] = r;
    v[5] = bot;
    v[6] = 0x01bbaa28; // pointers the file holds as they were
    v[9] = centreX;
    v[10] = 1;
    v[14] = font;
    v[15] = font;
    v[16] = group;
    v[19] = 55;
    v[22] = -1;
    for (int32_t x : v)
        put32(b, uint32_t(x));
    putResiText(b, text);
    putResiText(b, anim);
    put16(b, uint32_t(items.size()));
    for (const auto& item : items)
        putResiText(b, item);
}

inline Bytes resiFile() {
    Bytes b = {'R', 'E', 'S', 'I', 1, 1}; // a dialog, a font
    put32(b, 6);
    putResiText(b, "largeu:Alargeu");
    put32(b, 0);
    put32(b, 6);
    for (int i = 0; i < 9; ++i)
        put32(b, i == 0 ? 7 : i == 6 ? 6 : 0);
    putResiText(b, "menu:AexitBgnd");
    b.push_back(4);
    putControl(b, 1, 9999, 180, 120, 458, 183, 1, 6, 0, "Are you sure you wish|to exit Clue?", "");
    putControl(b, 3, 10001, 260, 185, 380, 220, 1, 6, 0, "Yes", "menu:AexitYesBtn");
    putControl(b, 3, 10000, 260, 228, 380, 260, 1, 6, 1, "No", "menu:AexitNoBtn");
    putControl(b, 5, 10, 210, 258, 380, 0, 0, 6, 0, "", "", {"one", "two"});
    return b;
}

// A small mansion: two rows of corridor (y 2 and 3, x 0-26) and, above
// them, the nine rooms, 2 x 2 each, three columns apart, each with a door
// down to the corridor at its left column. The suspects start on row 3,
// five apart; the kitchen and the study have the secret passages.
inline Level boardLevel() {
    Level level;
    const auto object = [&](const std::string& name) {
        for (size_t i = 0; i < level.objects.size(); ++i)
            if (level.objects[i] == name)
                return uint16_t(i);
        level.objects.push_back(name);
        return uint16_t(level.objects.size() - 1);
    };
    const auto place = [&](const std::string& name, int x, int y, int layer) {
        Placement p;
        p.object = object(name);
        p.x = int16_t(x);
        p.y = int16_t(y);
        p.layer = uint8_t(layer);
        level.placements.push_back(p);
    };
    const auto at = [](int square) { return 16 + 64 * square; };
    for (int x = 0; x <= 26; ++x)
        for (int y : {2, 3})
            place("r_hwfloor:Ohwfloor", at(x), at(y), 0);
    static const char* const kFolders[kRoomCount] = {"hallroom", "lounge", "diningroom", "kitchen", "ballroom",
                                                     "conservatory", "billiardroom", "library", "study"};
    for (int r = 0; r < kRoomCount; ++r) {
        for (int x = 3 * r; x < 3 * r + 2; ++x)
            for (int y : {0, 1})
                place("r_woodfloor:Owoodfloor", at(x), at(y), 1);
        place(std::string(kFolders[r]) + ":Ochair", at(3 * r + 1) + 8, at(0) + 8, 5);
        place("wall:Ohwdrtop_na", at(3 * r), at(1) + 48, 3);
    }
    place("secretpass:Oktsp", at(9) + 8, at(0) + 8, 5);
    place("secretpass:Osp_w", at(24) + 8, at(0) + 8, 5);
    static const char* const kNames[kSuspectCount] = {"scarlet", "mustard", "white", "green", "peacock", "plum"};
    for (int s = 0; s < kSuspectCount; ++s)
        place(std::string("character:O") + kNames[s], at(5 * s) + 24, at(3) + 24, 7);
    return level;
}

} // namespace reluva::clue::testing
