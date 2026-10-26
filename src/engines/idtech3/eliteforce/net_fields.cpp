// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/net_fields.h"

#include <array>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// A field: its byte offset and width in bits (0: a float; negative:
// signed, sign-extended when read).
struct Field {
    int32_t offset;
    int bits;
};

// Entity state fields in the order the protocol numbers them.
constexpr std::array<Field, 50> kEntityFields{{
    {4, 8},    {8, 24},   {12, 8},   {16, 32},  {20, 32},  {24, 0},   {28, 0},   {32, 0},   {36, 0},   {40, 0},
    {44, 0},   {48, 8},   {52, 32},  {56, 32},  {60, 0},   {64, 0},   {68, 0},   {72, 0},   {76, 0},   {80, 0},
    {84, 32},  {88, 32},  {92, 0},   {96, 0},   {100, 0},  {104, 0},  {108, 0},  {112, 0},  {116, 0},  {120, 0},
    {124, 0},  {128, 0},  {132, 0},  {136, 0},  {140, 10}, {144, 10}, {148, 10}, {156, 16}, {152, 32}, {160, 8},
    {164, 8},  {172, 16}, {168, 8},  {176, 24}, {180, 10}, {184, 8},  {188, 16}, {192, 8},  {196, 8},  {200, 8},
}};

// The change vectors the protocol names by a 5-bit number (31: the
// vector itself follows), as bytes: field i is bit i % 8 of byte i / 8.
constexpr uint8_t kChangeVectors[31][8] = {
    {0x60, 0x80, 0, 0, 0, 0, 0, 0},       {0x60, 0xc0, 0, 0, 0, 0, 0, 0},       {0x60, 0, 0, 0, 0, 0, 0, 0},
    {0x40, 0x80, 0, 0, 0, 0, 0, 0},       {0x20, 0x80, 0, 0, 0, 0, 0, 0},       {0x02, 0, 0, 0, 0, 0, 0, 0},
    {0x20, 0xc0, 0, 0, 0, 0, 0, 0},       {0x40, 0xc0, 0, 0, 0, 0, 0, 0},       {0x60, 0x40, 0, 0, 0, 0, 0, 0},
    {0x20, 0, 0, 0, 0, 0, 0, 0},          {0x40, 0, 0, 0, 0, 0, 0, 0},          {0x60, 0x80, 0, 0, 0, 0x10, 0, 0},
    {0x60, 0x80, 0, 0, 0x01, 0, 0, 0},    {0, 0, 0, 0, 0, 0x10, 0, 0},          {0, 0xc0, 0, 0, 0, 0, 0, 0},
    {0xe0, 0xc0, 0, 0, 0, 0, 0, 0},       {0x60, 0xc0, 0, 0, 0x01, 0, 0, 0},    {0x60, 0xc0, 0, 0, 0, 0x10, 0, 0},
    {0, 0x80, 0, 0, 0, 0, 0, 0},          {0x60, 0xc0, 0, 0, 0x01, 0, 0x01, 0}, {0x60, 0xc0, 0, 0, 0, 0x10, 0x02, 0},
    {0x60, 0x80, 0, 0, 0x01, 0, 0x01, 0}, {0x60, 0, 0, 0, 0, 0x10, 0, 0},       {0xe1, 0, 0xc0, 0x01, 0x90, 0, 0, 0},
    {0xed, 0x07, 0, 0, 0, 0x30, 0, 0},    {0x60, 0xc0, 0, 0, 0, 0, 0x02, 0},    {0x62, 0xc0, 0, 0, 0, 0, 0x02, 0},
    {0x02, 0, 0, 0, 0, 0x10, 0, 0},       {0xe0, 0x80, 0, 0, 0, 0, 0, 0},       {0xe0, 0, 0, 0, 0, 0, 0, 0},
    {0x60, 0x80, 0, 0, 0, 0, 0x02, 0},
};
constexpr int kVectorBits = 5;
constexpr uint32_t kExplicitVector = 31;

// Player state fields in protocol order (patch 1.2's layout).
constexpr std::array<Field, 48> kPlayerFields{{
    {0, 32},   {4, 8},    {8, 8},    {12, 16},  {16, -16}, {20, 0},   {24, 0},   {28, 0},   {32, 0},   {36, 0},
    {40, 0},   {44, -16}, {60, 16},  {64, 16},  {68, 16},  {72, 16},  {76, 16},  {80, 10},  {84, 8},   {92, 12},
    {88, 8},   {96, 8},   {100, 4},  {104, 16}, {108, 16}, {112, 8},  {116, 8},  {120, 8},  {124, 8},  {128, 8},
    {132, 8},  {136, 8},  {140, 8},  {144, 10}, {148, 8},  {156, 8},  {160, 5},  {164, 4},  {168, 0},  {172, 0},
    {176, 0},  {180, -8}, {184, 8},  {188, 8},  {192, 8},  {196, 8},  {200, 8},  {56, 32},
}};
// The arrays, in the order they're sent: stats, persistant, ammo (16-bit
// elements), powerups (32-bit); 16 elements each.
struct ArrayGroup {
    int32_t offset;
    int bits;
};
constexpr std::array<ArrayGroup, 4> kPlayerArrays{{{204, 16}, {268, 16}, {396, 16}, {332, 32}}};
constexpr int kArrayLength = 16;

int32_t wordAt(const std::vector<uint8_t>& s, int32_t offset) {
    if (s.size() < size_t(offset) + 4)
        return 0;
    int32_t v;
    std::memcpy(&v, s.data() + offset, 4);
    return v;
}
void setWord(std::vector<uint8_t>& s, int32_t offset, int32_t v) {
    if (s.size() >= size_t(offset) + 4)
        std::memcpy(s.data() + offset, &v, 4);
}
float floatOf(int32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
int32_t bitsOf(float f) {
    int32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

void writeField(wire::MessageWriter& w, const Field& f, int32_t value) {
    if (f.bits == 0)
        w.smallFloat(floatOf(value));
    else
        w.bits(uint32_t(value), f.bits < 0 ? -f.bits : f.bits);
}
int32_t readField(wire::MessageReader& r, const Field& f) {
    if (f.bits == 0)
        return bitsOf(r.smallFloat());
    if (f.bits < 0)
        return r.signedBits(-f.bits);
    return int32_t(r.bits(f.bits));
}

std::vector<uint8_t> sized(const std::vector<uint8_t>& from, int32_t size) {
    std::vector<uint8_t> s = from;
    s.resize(size_t(size), 0);
    return s;
}

} // namespace

void writeEntityRemoved(wire::MessageWriter& w, int32_t number) {
    w.bits(uint32_t(number), kEntityNumberBits);
    w.bits(1, 1);
}

void writeEntityDelta(wire::MessageWriter& w, const std::vector<uint8_t>& from, const std::vector<uint8_t>& to,
                      bool force) {
    uint8_t vector[8] = {};
    bool any = false;
    for (size_t i = 0; i < kEntityFields.size(); ++i)
        if (wordAt(from, kEntityFields[i].offset) != wordAt(to, kEntityFields[i].offset)) {
            vector[i >> 3] = uint8_t(vector[i >> 3] | (1 << (i & 7)));
            any = true;
        }
    if (!any && !force)
        return;
    w.bits(uint32_t(wordAt(to, 0)), kEntityNumberBits);
    w.bits(0, 1); // not removed
    if (!any) {
        w.bits(0, 1); // nothing changed
        return;
    }
    w.bits(1, 1);
    uint32_t index = kExplicitVector;
    for (uint32_t v = 0; v < kExplicitVector; ++v)
        if (std::memcmp(vector, kChangeVectors[v], 8) == 0) {
            index = v;
            break;
        }
    w.bits(index, kVectorBits);
    if (index == kExplicitVector) {
        for (int b = 0; b < 6; ++b)
            w.byte(vector[b]);
        w.bits(vector[6], int(kEntityFields.size()) - 48);
    }
    for (size_t i = 0; i < kEntityFields.size(); ++i)
        if (vector[i >> 3] & (1 << (i & 7)))
            writeField(w, kEntityFields[i], wordAt(to, kEntityFields[i].offset));
}

bool readEntityDelta(wire::MessageReader& r, int32_t number, const std::vector<uint8_t>& from,
                     std::vector<uint8_t>& to) {
    if (r.bits(1)) {
        to.clear();
        return true;
    }
    to = sized(from, kNetEntityStateSize);
    setWord(to, 0, number);
    if (!r.bits(1))
        return false; // unchanged
    uint8_t vector[8] = {};
    const uint32_t index = r.bits(kVectorBits);
    if (index == kExplicitVector) {
        for (int b = 0; b < 6; ++b)
            vector[b] = uint8_t(r.bits(8));
        vector[6] = uint8_t(r.bits(int(kEntityFields.size()) - 48));
    } else {
        std::memcpy(vector, kChangeVectors[index], 8);
    }
    for (size_t i = 0; i < kEntityFields.size(); ++i)
        if (vector[i >> 3] & (1 << (i & 7)))
            setWord(to, kEntityFields[i].offset, readField(r, kEntityFields[i]));
    return false;
}

void writePlayerDelta(wire::MessageWriter& w, const std::vector<uint8_t>& from, const std::vector<uint8_t>& to) {
    for (const Field& f : kPlayerFields) {
        const int32_t value = wordAt(to, f.offset);
        if (value == wordAt(from, f.offset)) {
            w.bits(0, 1);
            continue;
        }
        w.bits(1, 1);
        writeField(w, f, value);
    }
    for (const ArrayGroup& g : kPlayerArrays) {
        uint32_t mask = 0;
        for (int i = 0; i < kArrayLength; ++i)
            if (wordAt(to, g.offset + 4 * i) != wordAt(from, g.offset + 4 * i))
                mask |= 1u << i;
        w.bits(mask ? 1 : 0, 1);
        if (!mask)
            continue;
        w.bits(mask, kArrayLength);
        for (int i = 0; i < kArrayLength; ++i)
            if (mask & (1u << i))
                w.bits(uint32_t(wordAt(to, g.offset + 4 * i)), g.bits);
    }
}

void readPlayerDelta(wire::MessageReader& r, const std::vector<uint8_t>& from, std::vector<uint8_t>& to) {
    to = sized(from, kNetPlayerStateSize);
    for (const Field& f : kPlayerFields)
        if (r.bits(1))
            setWord(to, f.offset, readField(r, f));
    for (const ArrayGroup& g : kPlayerArrays) {
        if (!r.bits(1))
            continue;
        const uint32_t mask = r.bits(kArrayLength);
        for (int i = 0; i < kArrayLength; ++i)
            if (mask & (1u << i))
                setWord(to, g.offset + 4 * i, g.bits == 32 ? int32_t(r.bits(32)) : r.signedBits(g.bits));
    }
}

void writeUserCommand(wire::MessageWriter& w, const GameHost::UserCommand& from, const GameHost::UserCommand& to) {
    const int32_t step = to.serverTime - from.serverTime;
    if (step >= 0 && step < 256) {
        w.bits(1, 1);
        w.bits(uint32_t(step), 8);
    } else {
        w.bits(0, 1);
        w.longValue(to.serverTime);
    }
    auto field = [&](int32_t old, int32_t now, int bits) {
        if (old == now) {
            w.bits(0, 1);
            return;
        }
        w.bits(1, 1);
        w.bits(uint32_t(now), bits);
    };
    for (size_t i = 0; i < 3; ++i)
        field(from.angles[i] & 0xffff, to.angles[i] & 0xffff, 16);
    field(from.forward, to.forward, 8);
    field(from.right, to.right, 8);
    field(from.up, to.up, 8);
    field(from.buttons & 0xff, to.buttons & 0xff, 8);
    field(from.weapon, to.weapon, 8);
}

GameHost::UserCommand readUserCommand(wire::MessageReader& r, const GameHost::UserCommand& from) {
    GameHost::UserCommand to = from;
    if (r.bits(1))
        to.serverTime = from.serverTime + int32_t(r.bits(8));
    else
        to.serverTime = r.longValue();
    auto field = [&](int32_t old, int bits) { return r.bits(1) ? int32_t(r.bits(bits)) : old; };
    for (size_t i = 0; i < 3; ++i)
        to.angles[i] = field(from.angles[i] & 0xffff, 16);
    // A move of -128 is taken as -127, as the original's readers do.
    auto move = [](int32_t v) { return int8_t(int8_t(v) == -128 ? -127 : int8_t(v)); };
    to.forward = move(field(uint8_t(from.forward), 8));
    to.right = move(field(uint8_t(from.right), 8));
    to.up = move(field(uint8_t(from.up), 8));
    to.buttons = field(from.buttons & 0xff, 8);
    to.weapon = uint8_t(field(from.weapon, 8));
    return to;
}

} // namespace reluva::idtech3::eliteforce
