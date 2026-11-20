// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/net_fields.h"

#include <array>
#include <span>
#include <cstring>

namespace reluva::idtech3::jk2 {

namespace {

// A field: its byte offset and width in bits (0: a float; negative:
// signed, sign-extended when read).
struct Field {
    int32_t offset;
    int bits;
};

// jk2mp.exe's tables (entity state 0x4f5ab0, player state 0x4f5e20), in
// their order: each entry there is the field's name, offset and width.
constexpr std::array<Field, 73> kEntityFields{{
    {16, 32},  // pos.trTime
    {24, 0},   // pos.trBase[0]
    {28, 0},   // pos.trBase[1]
    {36, 0},   // pos.trDelta[0]
    {40, 0},   // pos.trDelta[1]
    {32, 0},   // pos.trBase[2]
    {64, 0},   // apos.trBase[1]
    {44, 0},   // pos.trDelta[2]
    {60, 0},   // apos.trBase[0]
    {252, 10}, // event
    {132, 0},  // angles2[1]
    {4, 8},    // eType
    {284, 16}, // torsoAnim
    {288, 16}, // forceFrame
    {256, 8},  // eventParm
    {280, 16}, // legsAnim
    {192, 10}, // groundEntityNum
    {12, 8},   // pos.trType
    {8, 32},   // eFlags
    {140, 8},  // bolt1
    {144, 10}, // bolt2
    {148, 16}, // trickedentindex
    {152, 16}, // trickedentindex2
    {156, 16}, // trickedentindex3
    {160, 16}, // trickedentindex4
    {164, 0},  // speed
    {168, 2},  // fireflag
    {172, 32}, // genericenemyindex
    {176, 6},  // activeForcePass
    {180, 32}, // emplacedOwner
    {184, 10}, // otherEntityNum
    {276, 8},  // weapon
    {220, 8},  // clientNum
    {120, 0},  // angles[1]
    {20, 32},  // pos.trDuration
    {48, 8},   // apos.trType
    {92, 0},   // origin[0]
    {96, 0},   // origin[1]
    {100, 0},  // origin[2]
    {248, 24}, // solid
    {260, 10}, // owner
    {264, 8},  // teamowner
    {268, 1},  // shouldtarget
    {272, 16}, // powerups
    {204, 4},  // modelGhoul2
    {208, 8},  // g2radius
    {212, -8}, // modelindex
    {188, 10}, // otherEntityNum2
    {200, 8},  // loopSound
    {292, 8},  // generic1
    {112, 0},  // origin2[2]
    {104, 0},  // origin2[0]
    {108, 0},  // origin2[1]
    {216, 8},  // modelindex2
    {116, 0},  // angles[0]
    {84, 32},  // time
    {52, 32},  // apos.trTime
    {56, 32},  // apos.trDuration
    {68, 0},   // apos.trBase[2]
    {72, 0},   // apos.trDelta[0]
    {76, 0},   // apos.trDelta[1]
    {80, 0},   // apos.trDelta[2]
    {88, 32},  // time2
    {124, 0},  // angles[2]
    {128, 0},  // angles2[0]
    {136, 0},  // angles2[2]
    {196, 32}, // constantLight
    {224, 16}, // frame
    {228, 1},  // saberInFlight
    {232, 10}, // saberEntityNum
    {236, 8},  // saberMove
    {240, 32}, // forcePowersActive
    {244, 1},  // isJediMaster
}};
constexpr std::array<Field, 105> kPlayerFields{{
    {0, 32},    // commandTime
    {20, 0},    // origin[0]
    {24, 0},    // origin[1]
    {8, 8},     // bobCycle
    {32, 0},    // velocity[0]
    {36, 0},    // velocity[1]
    {160, 0},   // viewangles[1]
    {156, 0},   // viewangles[0]
    {44, -16},  // weaponTime
    {48, 32},   // weaponChargeTime
    {52, 32},   // weaponChargeSubtractTime
    {28, 0},    // origin[2]
    {40, 0},    // velocity[2]
    {16, -16},  // pm_time
    {112, 16},  // eventSequence
    {100, 16},  // torsoAnim
    {96, 16},   // torsoTimer
    {92, 16},   // legsAnim
    {88, 16},   // legsTimer
    {104, 4},   // movementDir
    {116, 10},  // events[0]
    {120, 10},  // events[1]
    {12, 16},   // pm_flags
    {84, 10},   // groundEntityNum
    {152, 4},   // weaponstate
    {108, 32},  // eFlags
    {132, 10},  // externalEvent
    {56, 16},   // gravity
    {60, 16},   // speed
    {64, 16},   // basespeed
    {72, 16},   // delta_angles[1]
    {136, 8},   // externalEventParm
    {168, -8},  // viewheight
    {172, 8},   // damageEvent
    {176, 8},   // damageYaw
    {180, 8},   // damagePitch
    {184, 8},   // damageCount
    {188, 2},   // damageType
    {472, 8},   // generic1
    {4, 8},     // pm_type
    {68, 16},   // delta_angles[0]
    {76, 16},   // delta_angles[2]
    {124, -16}, // eventParms[0]
    {128, 8},   // eventParms[1]
    {144, 8},   // clientNum
    {148, 5},   // weapon
    {164, 0},   // viewangles[2]
    {480, 10},  // jumppad_ent
    {476, 16},  // loopSound
    {1320, 2},  // zoomMode
    {1324, 32}, // zoomTime
    {1328, 1},  // zoomLocked
    {1332, 8},  // zoomFov
    {836, 32},  // fd.forcePowersActive
    {1144, 16}, // fd.forceMindtrickTargetIndex
    {1148, 16}, // fd.forceMindtrickTargetIndex2
    {1152, 16}, // fd.forceMindtrickTargetIndex3
    {1156, 16}, // fd.forceMindtrickTargetIndex4
    {1080, 0},  // fd.forceJumpZStart
    {840, 8},   // fd.forcePowerSelected
    {832, 32},  // fd.forcePowersKnown
    {920, 8},   // fd.forcePower
    {1176, 2},  // fd.forceSide
    {1212, 1},  // fd.sentryDeployed
    {936, 2},   // fd.forcePowerLevel[FP_LEVITATION]
    {988, 2},   // fd.forcePowerLevel[FP_SEE]
    {612, 32},  // genericEnemyIndex
    {624, 6},   // activeForcePass
    {628, 1},   // hasDetPackPlanted
    {596, 10},  // emplacedIndex
    {1160, 32}, // fd.forceRageRecoveryTime
    {580, 8},   // rocketLockIndex
    {588, 32},  // rocketLockTime
    {592, 32},  // rocketTargetTime
    {712, 32},  // holocronBits
    {604, 1},   // isJediMaster
    {1340, 32}, // fallingToDeath
    {728, 32},  // electrifyTime
    {764, 32},  // fd.forcePowerDebounce[FP_LEVITATION]
    {512, 32},  // saberMove
    {508, 1},   // saberActive
    {504, 1},   // saberInFlight
    {520, 8},   // saberBlocked
    {544, 10},  // saberEntityNum
    {560, 1},   // saberCanThrow
    {1236, 8},  // forceHandExtend
    {1248, 16}, // forceDodgeAnim
    {1216, 2},  // fd.saberAnimLevel
    {1220, 2},  // fd.saberDrawAnimLevel
    {1296, 4},  // saberAttackChainCount
    {1300, 1},  // saberHolstered
    {1304, 1},  // usingATST
    {1308, 1},  // atstAltFire
    {1284, 10}, // duelIndex
    {1288, 32}, // duelTime
    {1292, 1},  // duelInProgress
    {524, 32},  // saberLockTime
    {528, 10},  // saberLockEnemy
    {532, 16},  // saberLockFrame
    {540, 1},   // saberLockAdvance
    {1348, 1},  // inAirAnim
    {1352, 1},  // dualBlade
    {1364, 0},  // lastHitLoc[2]
    {1356, 0},  // lastHitLoc[0]
    {1360, 0},  // lastHitLoc[1]
}};

// 1.03's and 1.04's (their tables at 0x4fce48 and 0x4fd1b8): modelGhoul2
// a bit wider; three new player fields after isJediMaster
// (forceRestricted, trueJedi, trueNonJedi), the player state 12 bytes
// longer from offset 608, every field from there on moved with it.
constexpr size_t kModelGhoul2Field = 44;
constexpr size_t kIsJediMasterField = 75;
constexpr int32_t kInsertedAt = 608;
constexpr int32_t kInserted = 12;
constexpr std::array<Field, 73> kEntityFieldsPatched = [] {
    auto t = kEntityFields;
    t[kModelGhoul2Field].bits = 5;
    return t;
}();
constexpr std::array<Field, 108> kPlayerFieldsPatched = [] {
    std::array<Field, 108> t{};
    size_t n = 0;
    for (size_t i = 0; i < kPlayerFields.size(); ++i) {
        Field f = kPlayerFields[i];
        if (f.offset >= kInsertedAt)
            f.offset += kInserted;
        t[n++] = f;
        if (i == kIsJediMasterField) {
            t[n++] = {kInsertedAt, 1};
            t[n++] = {kInsertedAt + 4, 1};
            t[n++] = {kInsertedAt + 8, 1};
        }
    }
    return t;
}();

// The player state's arrays, in the order they're sent: stats, persistant,
// ammo (16-bit elements), powerups (32-bit); 16 elements each.
struct ArrayGroup {
    int32_t offset;
    int bits;
};
constexpr std::array<ArrayGroup, 4> kPlayerArrays{{{216, 16}, {280, 16}, {408, 16}, {344, 32}}};
constexpr int kArrayLength = 16;

constexpr int kEntityNumberBits = 10;
constexpr int kSmallFloatBits = 13;
constexpr int32_t kSmallFloatBias = 1 << (kSmallFloatBits - 1);

// jk2mp.exe's byte counts for the message code (0x4f6310), as Team
// Arena's.
constexpr std::array<uint32_t, 256> kByteCounts{{
    250315, 41193, 6292, 7106,  3730, 3750, 6110, 23283, 33317, 6950, 7838, 9714, 9257, 17259, 3949,  1778,
    8288,   1604,  1590, 1663,  1100, 1213, 1238, 1134,  1749,  1059, 1246, 1149, 1273, 4486,  2805,  3472,
    21819,  1159,  1670, 1066,  1043, 1012, 1053, 1070,  1726,  888,  1180, 850,  960,  780,   1752,  3296,
    10630,  4514,  5881, 2685,  4650, 3837, 2093, 1867,  2584,  1949, 1972, 940,  1134, 1788,  1670,  1206,
    5719,   6128,  7222, 6654,  3710, 3795, 1492, 1524,  2215,  1140, 1355, 971,  2180, 1248,  1328,  1195,
    1770,   1078,  1264, 1266,  1168, 965,  1155, 1186,  1347,  1228, 1529, 1600, 2617, 2048,  2546,  3275,
    2410,   3585,  2504, 2800,  2675, 6146, 3663, 2840,  14253, 3164, 2221, 1687, 3208, 2739,  3512,  4796,
    4091,   3515,  5288, 4016,  7937, 6031, 5360, 3924,  4892,  3743, 4566, 4807, 5852, 6400,  6225,  8291,
    23243,  7838,  7073, 8935,  5437, 4483, 3641, 5256,  5312,  5328, 5370, 3492, 2458, 1694,  1821,  2121,
    1916,   1149,  1516, 1367,  1236, 1029, 1258, 1104,  1245,  1006, 1149, 1025, 1241, 952,   1287,  997,
    1713,   1009,  1187, 879,   1099, 929,  1078, 951,   1656,  930,  1153, 1030, 1262, 1062,  1214,  1060,
    1621,   930,   1106, 912,   1034, 892,  1158, 990,   1175,  850,  1121, 903,  1087, 920,   1144,  1056,
    3462,   2240,  4397, 12136, 7758, 1345, 1307, 3278,  1950,  886,  1023, 1112, 1077, 1042,  1061,  1071,
    1484,   1001,  1096, 915,   1052, 995,  1070, 876,   1111,  851,  1059, 805,  1112, 923,   1103,  817,
    1899,   1872,  976,  841,   1127, 956,  1159, 950,   7791,  954,  1289, 933,  1127, 3207,  1020,  927,
    1355,   768,   1040, 745,   952,  805,  1073, 740,   1013,  805,  1008, 796,  996,  1057,  11457, 13504,
}};

int32_t wordAt(const std::vector<uint8_t> &s, int32_t offset) {
    if (s.size() < size_t(offset) + 4)
        return 0;
    int32_t v;
    std::memcpy(&v, s.data() + offset, 4);
    return v;
}

void setWord(std::vector<uint8_t> &s, int32_t offset, int32_t v) {
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

int width(int bits) {
    return bits < 0 ? -bits : bits;
}

int32_t readField(wire::MessageReader &r, int bits) {
    return bits < 0 ? r.signedBits(-bits) : int32_t(r.bits(bits));
}

// A float that's a whole number from -4096 to 4095 in 13 bits after a 0,
// anything else in 32 after a 1.
void writeFloat(wire::MessageWriter &w, int32_t word) {
    const float f = floatOf(word);
    const auto whole = int32_t(f);
    if (float(whole) == f && whole + kSmallFloatBias >= 0 && whole + kSmallFloatBias < (1 << kSmallFloatBits)) {
        w.bits(0, 1);
        w.bits(uint32_t(whole + kSmallFloatBias), kSmallFloatBits);
    } else {
        w.bits(1, 1);
        w.bits(uint32_t(word), 32);
    }
}

int32_t readFloat(wire::MessageReader &r) {
    if (r.bits(1) == 0)
        return bitsOf(float(int32_t(r.bits(kSmallFloatBits)) - kSmallFloatBias));
    return int32_t(r.bits(32));
}

// How many fields to send: up to the last that changed.
int changedCount(std::span<const Field> fields, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to) {
    int count = 0;
    for (size_t i = 0; i < fields.size(); ++i)
        if (wordAt(from, fields[i].offset) != wordAt(to, fields[i].offset))
            count = int(i) + 1;
    return count;
}

void writeDeltaKey(wire::MessageWriter &w, int32_t key, int32_t from, int32_t to, int bits) {
    if (from == to) {
        w.bits(0, 1);
        return;
    }
    w.bits(1, 1);
    w.bits(uint32_t(to ^ key) & ((1u << bits) - 1), bits);
}

// The key is masked a bit wider than the field, as the original's table of
// masks is off by one: a 16-bit field can take on bit 16.
int32_t readDeltaKey(wire::MessageReader &r, int32_t key, int32_t from, int bits) {
    if (!r.bits(1))
        return from;
    const uint32_t mask = bits >= 31 ? 0xffffffffu : (2u << bits) - 1;
    return int32_t(r.bits(bits) ^ (uint32_t(key) & mask));
}

} // namespace

const wire::Huffman &messageHuffman() {
    static const wire::Huffman kCode = wire::Huffman::fromCounts(kByteCounts);
    return kCode;
}

void writeEntityRemoved(wire::MessageWriter &w, int32_t number) {
    w.bits(uint32_t(number), kEntityNumberBits);
    w.bits(1, 1);
}

namespace {

void entityDelta(std::span<const Field> fields, wire::MessageWriter &w, const std::vector<uint8_t> &from,
                 const std::vector<uint8_t> &to, bool force) {
    const int count = changedCount(fields, from, to);
    if (count == 0 && !force)
        return;
    w.bits(uint32_t(wordAt(to, 0)), kEntityNumberBits);
    w.bits(0, 1); // not removed
    if (count == 0) {
        w.bits(0, 1);
        return;
    }
    w.bits(1, 1);
    w.byte(uint32_t(count));
    for (int i = 0; i < count; ++i) {
        const Field &f = fields[size_t(i)];
        const int32_t v = wordAt(to, f.offset);
        if (wordAt(from, f.offset) == v) {
            w.bits(0, 1);
            continue;
        }
        w.bits(1, 1);
        // Zero is one bit, a float or not.
        if (f.bits == 0 && floatOf(v) == 0.0f) {
            w.bits(0, 1);
        } else if (f.bits == 0) {
            w.bits(1, 1);
            writeFloat(w, v);
        } else if (v == 0) {
            w.bits(0, 1);
        } else {
            w.bits(1, 1);
            w.bits(uint32_t(v), width(f.bits));
        }
    }
}

bool readEntity(std::span<const Field> fields, wire::MessageReader &r, int32_t number, const std::vector<uint8_t> &from,
                std::vector<uint8_t> &to) {
    to.assign(kNetEntityStateSize, 0);
    if (r.bits(1)) {
        setWord(to, 0, 1023);
        return true;
    }
    to = from;
    to.resize(kNetEntityStateSize, 0);
    setWord(to, 0, number);
    if (!r.bits(1))
        return false;
    const auto count = int(r.byte());
    for (int i = 0; i < count && i < int(fields.size()); ++i) {
        const Field &f = fields[size_t(i)];
        if (!r.bits(1))
            continue;
        if (f.bits == 0)
            setWord(to, f.offset, r.bits(1) ? readFloat(r) : 0);
        else
            setWord(to, f.offset, r.bits(1) ? readField(r, f.bits) : 0);
    }
    return false;
}

void playerDelta(std::span<const Field> fields, wire::MessageWriter &w, const std::vector<uint8_t> &from,
                 const std::vector<uint8_t> &to) {
    const int count = changedCount(fields, from, to);
    w.byte(uint32_t(count));
    for (int i = 0; i < count; ++i) {
        const Field &f = fields[size_t(i)];
        const int32_t v = wordAt(to, f.offset);
        if (wordAt(from, f.offset) == v) {
            w.bits(0, 1);
            continue;
        }
        w.bits(1, 1);
        if (f.bits == 0)
            writeFloat(w, v);
        else
            w.bits(uint32_t(v), width(f.bits));
    }
    std::array<uint32_t, kPlayerArrays.size()> masks{};
    for (size_t g = 0; g < kPlayerArrays.size(); ++g)
        for (int i = 0; i < kArrayLength; ++i)
            if (wordAt(from, kPlayerArrays[g].offset + 4 * i) != wordAt(to, kPlayerArrays[g].offset + 4 * i))
                masks[g] |= 1u << i;
    if (!(masks[0] | masks[1] | masks[2] | masks[3])) {
        w.bits(0, 1);
        return;
    }
    w.bits(1, 1);
    for (size_t g = 0; g < kPlayerArrays.size(); ++g) {
        if (!masks[g]) {
            w.bits(0, 1);
            continue;
        }
        w.bits(1, 1);
        w.shortValue(int32_t(masks[g]));
        for (int i = 0; i < kArrayLength; ++i)
            if (masks[g] & (1u << i))
                w.bits(uint32_t(wordAt(to, kPlayerArrays[g].offset + 4 * i)), kPlayerArrays[g].bits);
    }
}

void readPlayer(std::span<const Field> fields, size_t size, wire::MessageReader &r, const std::vector<uint8_t> &from,
                std::vector<uint8_t> &to) {
    to = from;
    to.resize(size, 0);
    const auto count = int(r.byte());
    for (int i = 0; i < count && i < int(fields.size()); ++i) {
        const Field &f = fields[size_t(i)];
        if (!r.bits(1))
            continue;
        setWord(to, f.offset, f.bits == 0 ? readFloat(r) : readField(r, f.bits));
    }
    if (!r.bits(1))
        return;
    for (const ArrayGroup &g : kPlayerArrays) {
        if (!r.bits(1))
            continue;
        const auto mask = uint32_t(r.bits(16));
        for (int i = 0; i < kArrayLength; ++i)
            if (mask & (1u << i))
                setWord(to, g.offset + 4 * i, g.bits == 16 ? r.signedBits(16) : int32_t(r.bits(32)));
    }
}

} // namespace

void writeEntityDelta(wire::MessageWriter &w, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to,
                      bool force) {
    entityDelta(kEntityFields, w, from, to, force);
}

bool readEntityDelta(wire::MessageReader &r, int32_t number, const std::vector<uint8_t> &from,
                     std::vector<uint8_t> &to) {
    return readEntity(kEntityFields, r, number, from, to);
}

void writePlayerDelta(wire::MessageWriter &w, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to) {
    playerDelta(kPlayerFields, w, from, to);
}

void readPlayerDelta(wire::MessageReader &r, const std::vector<uint8_t> &from, std::vector<uint8_t> &to) {
    readPlayer(kPlayerFields, kNetPlayerStateSize, r, from, to);
}

void writeEntityDeltaPatched(wire::MessageWriter &w, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to,
                             bool force) {
    entityDelta(kEntityFieldsPatched, w, from, to, force);
}

bool readEntityDeltaPatched(wire::MessageReader &r, int32_t number, const std::vector<uint8_t> &from,
                            std::vector<uint8_t> &to) {
    return readEntity(kEntityFieldsPatched, r, number, from, to);
}

void writePlayerDeltaPatched(wire::MessageWriter &w, const std::vector<uint8_t> &from, const std::vector<uint8_t> &to) {
    playerDelta(kPlayerFieldsPatched, w, from, to);
}

void readPlayerDeltaPatched(wire::MessageReader &r, const std::vector<uint8_t> &from, std::vector<uint8_t> &to) {
    readPlayer(kPlayerFieldsPatched, kNetPlayerStateSizePatched, r, from, to);
}

void writeUserCommand(wire::MessageWriter &w, int32_t key, const eliteforce::GameHost::UserCommand &from,
                      const eliteforce::GameHost::UserCommand &to) {
    // The time as a step of under 256 ms, or whole.
    if (to.serverTime - from.serverTime < 256) {
        w.bits(1, 1);
        w.bits(uint32_t(to.serverTime - from.serverTime), 8);
    } else {
        w.bits(0, 1);
        w.bits(uint32_t(to.serverTime), 32);
    }
    const bool same = from.angles == to.angles && from.forward == to.forward && from.right == to.right &&
                      from.up == to.up && from.buttons == to.buttons && from.weapon == to.weapon &&
                      from.forceSelect == to.forceSelect && from.inventorySelect == to.inventorySelect &&
                      from.genericCommand == to.genericCommand;
    if (same) {
        w.bits(0, 1);
        return;
    }
    w.bits(1, 1);
    key ^= to.serverTime;
    for (size_t i = 0; i < 3; ++i)
        writeDeltaKey(w, key, from.angles[i], to.angles[i], 16);
    writeDeltaKey(w, key, from.forward, to.forward, 8);
    writeDeltaKey(w, key, from.right, to.right, 8);
    writeDeltaKey(w, key, from.up, to.up, 8);
    writeDeltaKey(w, key, from.buttons, to.buttons, 16);
    writeDeltaKey(w, key, from.weapon, to.weapon, 8);
    writeDeltaKey(w, key, from.forceSelect, to.forceSelect, 8);
    writeDeltaKey(w, key, from.inventorySelect, to.inventorySelect, 8);
    writeDeltaKey(w, key, from.genericCommand, to.genericCommand, 8);
}

eliteforce::GameHost::UserCommand readUserCommand(wire::MessageReader &r, int32_t key,
                                                  const eliteforce::GameHost::UserCommand &from) {
    eliteforce::GameHost::UserCommand to = from;
    if (r.bits(1))
        to.serverTime = from.serverTime + int32_t(r.bits(8));
    else
        to.serverTime = int32_t(r.bits(32));
    if (!r.bits(1))
        return to;
    key ^= to.serverTime;
    for (size_t i = 0; i < 3; ++i)
        to.angles[i] = readDeltaKey(r, key, from.angles[i], 16);
    to.forward = int8_t(readDeltaKey(r, key, from.forward, 8));
    to.right = int8_t(readDeltaKey(r, key, from.right, 8));
    to.up = int8_t(readDeltaKey(r, key, from.up, 8));
    to.buttons = readDeltaKey(r, key, from.buttons, 16);
    to.weapon = uint8_t(readDeltaKey(r, key, from.weapon, 8));
    to.forceSelect = uint8_t(readDeltaKey(r, key, from.forceSelect, 8));
    to.inventorySelect = uint8_t(readDeltaKey(r, key, from.inventorySelect, 8));
    to.genericCommand = uint8_t(readDeltaKey(r, key, from.genericCommand, 8));
    return to;
}

int32_t hashKey(const std::string &text, size_t maxLength) {
    // Summed with wrapping, then shifted keeping the sign, as the
    // original's ints.
    uint32_t sum = 0;
    for (size_t i = 0; i < maxLength && i < text.size() && text[i]; ++i)
        sum += uint32_t(int32_t(static_cast<signed char>(text[i])) * int32_t(119 + i));
    const auto hash = int32_t(sum);
    return hash ^ (hash >> 10) ^ (hash >> 20);
}

} // namespace reluva::idtech3::jk2
