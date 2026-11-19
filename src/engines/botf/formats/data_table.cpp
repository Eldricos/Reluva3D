// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/data_table.h"

#include "common/text.h"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace reluva::botf {

namespace {

using F = FieldType;
using C = TableLayout::Count;

uint32_t fieldBytes(const Field &f) {
    switch (f.type) {
    case F::kText:
        return f.size;
    case F::kU8:
        return f.size;
    case F::kU16:
    case F::kS16:
        return 2 * f.size;
    case F::kU32:
    case F::kS32:
        return 4 * f.size;
    case F::kF32:
        return 4;
    case F::kF64:
        return 8;
    }
    return 0;
}

uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24;
}

// Tables that come several to a layout: the starting buildings of each era
// (bldset1-5.bin) and each major race's colonisation weights (?colval.wtf).
std::vector<TableLayout> withRepeats(std::vector<TableLayout> layouts) {
    static const char *const kBldsets[] = {"bldset1.bin", "bldset2.bin", "bldset3.bin", "bldset4.bin", "bldset5.bin"};
    for (const char *file : kBldsets)
        layouts.push_back(
            {file,
             nullptr,
             C::kFileSize,
             0,
             8,
             {{"race", 0, F::kS16}, {"building (edifice.bst's number)", 2, F::kS16}, {"how many", 4, F::kS32}},
             0,
             -1});
    static const char *const kColvals[] = {"ccolval.wtf", "hcolval.wtf", "fcolval.wtf", "kcolval.wtf", "rcolval.wtf"};
    static const char *const kColumns[] = {"Worthless",        "Dilithium",      "Food",     "Energy", "Poor_Industry",
                                           "Average_Industry", "Prime_Industry", "Distance", "Trade",  "Near_Minor",
                                           "Gal_Density",      "Military",       "Rnd"};
    for (const char *file : kColvals) {
        TableLayout l{file, nullptr, C::kFileSize, 474, 104, {}, -1, -1, {}, 441, 33};
        for (uint32_t i = 0; i < 13; ++i)
            l.fields.push_back({kColumns[i], 8 * i, F::kF64});
        layouts.push_back(std::move(l));
    }
    return layouts;
}

} // namespace

// Fields are listed only where the evidence is in tables.md (the numbers:
// matched against the game's own screens in a recording); the rest of each
// record shows as bytes.
const std::vector<TableLayout> &tableLayouts() {
    static const std::vector<TableLayout> kLayouts = withRepeats({
        {"race.rst",
         "racedesc.rst",
         C::kU16AtStart,
         2,
         108,
         {{"name", 0, F::kText, 40},
          {"picture", 40, F::kText, 16},
          {"description", 60, F::kU32},
          {"home planet (planet.pst's number)", 64, F::kS16},
          {"starting population", 66, F::kS16},
          {"growth rate", 68, F::kF32},
          {"starting morale under each major race (5), then ?", 72, F::kU8, 6},
          {"dilithium at home (0, 1)", 78, F::kS16},
          {"unrest threshold", 84, F::kS32},
          {"ground combat multiplier", 104, F::kF32}},
         0,
         2},
        {"shiplist.sst",
         "shipdesc.sst",
         C::kFileSize,
         0,
         288,
         {{"name", 0, F::kText, 40},
          {"class", 40, F::kText, 44},
          {"description", 84, F::kU32},
          {"model", 88, F::kText, 14},
          {"number", 102, F::kU16},
          {"role (0 scout, 1 destroyer, 2 cruiser, 3 strike cruiser, 4 command ship, 5 colony ship, 6 outpost, "
           "7 starbase, 8 monster, 9 troop transport)",
           104, F::kU16},
          {"name group (shipname.bin)", 106, F::kU16},
          {"beam accuracy", 108, F::kU32},
          {"beam weapons", 112, F::kU32},
          {"beam damage multiplier", 116, F::kU32},
          {"beam damage, each", 120, F::kU32},
          {"beam shield penetration", 124, F::kU32},
          {"torpedo accuracy", 128, F::kU32},
          {"torpedoes", 132, F::kU32},
          {"torpedo damage multiplier", 136, F::kU32},
          {"torpedo damage, each", 140, F::kU32},
          {"shield level", 144, F::kU32},
          {"shields", 148, F::kU32},
          {"shield recharge", 152, F::kU32},
          {"offensive strength, for the AI", 156, F::kU32},
          {"defensive strength, for the AI", 160, F::kU32},
          {"hit points", 168, F::kU32},
          {"defense", 172, F::kU32},
          {"stealth", 176, F::kU32},
          {"extra beam weapons", 180, F::kU32},
          {"extra beam accuracy", 184, F::kU32},
          {"extra beam damage multiplier", 188, F::kU32},
          {"extra beam damage, each", 192, F::kU32},
          {"extra beam shield penetration", 196, F::kU32},
          {"agility", 212, F::kF64},
          {"firing arc, degrees", 220, F::kF64},
          {"beam range", 228, F::kF64},
          {"torpedo minimum range", 236, F::kF64},
          {"torpedo range", 244, F::kF64},
          {"extra beam range", 252, F::kF64},
          {"range (0 short, 1 medium, 2 long)", 260, F::kF64},
          {"speed", 268, F::kF64},
          {"scan range", 276, F::kU16},
          {"production (colony ships: terraforming; transports: ground combat)", 278, F::kU16},
          {"race (race.rst)", 280, F::kU16},
          {"build cost", 284, F::kU16},
          {"maintenance cost", 286, F::kU16}},
         0,
         2},
        {"techtree.tec",
         "techdesc.tec",
         C::kFileSize,
         0,
         72,
         {{"name", 0, F::kText, 30},
          {"number", 40, F::kU16},
          {"field's short name", 42, F::kText, 6},
          {"field", 55, F::kU8},
          {"level", 56, F::kU8},
          {"cost", 60, F::kU32},
          {"description", 68, F::kU32}},
         0,
         6},
        {"tecfield.tec", nullptr, C::kU16AtStart, 2, 41, {{"name", 0, F::kText, 40}, {"number", 40, F::kU8}}, 0, -1},
        {"edifice.bst",
         "edifdesc.bst",
         C::kU32AtStart,
         740,
         176,
         {{"name", 0, F::kText, 40},
          {"plural", 40, F::kText, 40},
          {"number", 80, F::kU16},
          {"upgrade of (32767: none)", 82, F::kU16},
          {"picture", 88, F::kText, 8},
          {"research field needed (tecfield.tec)", 101, F::kU8},
          {"technology level needed", 102, F::kU8},
          {"races that can't build it, a bit each", 103, F::kU8},
          {"resident races needed, a bit each (low, high)", 104, F::kU32, 2},
          {"system status needed (0 home system, 1 native member, 2 non-native member, 3 subjugated, ...)", 112,
           F::kU8},
          {"planet or star needed in the system", 113, F::kU8},
          {"output type (its wording in edifbnft.bst)", 114, F::kU8},
          {"build limit", 115, F::kU8},
          {"group", 116, F::kU8},
          {"category (0 research, 1 industry, 2 food, 3 energy, 4 defense)", 117, F::kU8},
          {"build cost", 120, F::kU16},
          {"energy maintenance", 124, F::kU16},
          {"population maintenance", 128, F::kU16},
          {"morale", 130, F::kS16},
          {"output, per race (5 major races, then ?)", 134, F::kS16, 6},
          {"unrest bonus, per race", 146, F::kS16, 6},
          {"description", 172, F::kU32}},
         0,
         21},
        {"planet.pst",
         nullptr,
         C::kU16AtStart,
         260,
         76,
         {{"name", 0, F::kText, 40},
          {"animation", 40, F::kText, 14},
          {"number", 54, F::kS16},
          {"size (0 small, 1 medium, 2 large)", 56, F::kS16},
          {"terraforming points", 58, F::kU16},
          {"terraforming done", 60, F::kU16},
          {"food bonus (0, 310, 620: levels 0-2)", 62, F::kS16},
          {"energy bonus (0, 310, 620)", 64, F::kS16},
          {"growth bonus (0, 310, 620)", 66, F::kS16},
          {"type (environ.est)", 72, F::kU8},
          {"atmosphere (environ.est)", 73, F::kU8}},
         0,
         -1},
        {"environ.est", nullptr, C::kU16AtStart, 130, 50, {{"name", 0, F::kText, 40}, {"number", 40, F::kU16}}, 0, -1},
        {"starname.bin",
         nullptr,
         C::kU32AtStart,
         4,
         24,
         {{"name", 0, F::kText, 20}, {"name's length + 1", 20, F::kU32}},
         0,
         -1},
        {"shipname.bin", nullptr, C::kFileSize, 252, 44, {{"group", 0, F::kU16}, {"name", 4, F::kText, 40}}, 1, -1},
        {"shiptech.sst",
         nullptr,
         C::kFileSize,
         0,
         16,
         {{"race", 0, F::kU16},
          {"ship (shiplist.sst)", 4, F::kU16},
          {"upgrades from (65535: none)", 6, F::kU16},
          {"technology levels needed", 8, F::kU8, 6}},
         1,
         -1},
        {"shiprace.sst", nullptr, C::kFileSize, 0, 4, {{"first ship (4294967295: none)", 0, F::kU32}}, 0, -1},
        {"planspac.bin",
         nullptr,
         C::kFileSize,
         0,
         40,
         {{"planet type (environ.est)", 0, F::kU8},
          {"size", 1, F::kU8},
          {"maximum population, by growth bonus level (3) x energy bonus level (3)", 4, F::kS32, 9}},
         -1,
         -1},
        {"planboni.bin",
         nullptr,
         C::kFileSize,
         0,
         40,
         {{"planet type (environ.est)", 0, F::kU8},
          {"size", 1, F::kU8},
          {"bonus in 5% steps, by food bonus level (3) x energy bonus level (3)", 4, F::kS32, 9}},
         -1,
         -1},
        {"racetech.tec",
         nullptr,
         C::kFileSize,
         1300,
         108,
         {{"race", 0, F::kS16},
          {"biotech level", 4, F::kS16},
          {"biotech research done", 8, F::kS32},
          {"computer level", 12, F::kS16},
          {"computer research done", 16, F::kS32},
          {"construction level", 20, F::kS16},
          {"construction research done", 24, F::kS32},
          {"energy level", 28, F::kS16},
          {"energy research done", 32, F::kS32},
          {"propulsion level", 36, F::kS16},
          {"propulsion research done", 40, F::kS32},
          {"weapons level", 44, F::kS16},
          {"weapons research done", 48, F::kS32},
          {"sociology level", 52, F::kS16},
          {"sociology research done", 56, F::kS32},
          {"buildings available at the start, a bit each by number", 60, F::kU32, 12}},
         0,
         -1},
        {"morale.bin",
         nullptr,
         C::kFileSize,
         0,
         20,
         {{"change, for each major race", 0, F::kS32, 5}},
         -1,
         -1,
         {"Eliminate an empire",      "Win a major battle",
          "Win a significant battle", "Win a minor battle",
          "Lose a major battle",      "Lose a significant battle",
          "Lose a minor battle",      "Sign a membership",
          "Subjugate a system",       "Colonize a system",
          "Liberate a minor race",    "Liberate natives",
          "Lose the home system",     "Lose a member system",
          "Lose a subjugated system", "Lose a system to rebellion",
          "Bombard a system",         "Declare war",
          "War declared on you",      "Reject peace",
          "Break a treaty",           "Sign peace",
          "Sign an alliance"}},
        {"intel.bin",
         nullptr,
         C::kFileSize,
         0,
         96,
         {{"espionage base", 0, F::kS32},
          {"sabotage base", 4, F::kS32},
          {"by target race (5)", 8, F::kS32, 5},
          {"by difficulty (simple, easy, normal, hard, impossible)", 28, F::kS32, 5},
          {"by morale level (rebellious ... fanatic)", 48, F::kS32, 8},
          {"by treaty (neutral, war, alliance, peace)", 80, F::kS32, 4}},
         -1,
         -1,
         {"Cardassians", "Federation", "Ferengi", "Klingons", "Romulans"}},
        {"meplanet.bin",
         nullptr,
         C::kFileSize,
         0,
         88,
         {{"planet (planet.pst)", 0, F::kText, 40},
          {"race", 40, F::kText, 40},
          {"? (10 for the gas giants, 1 for Cardassia 5, otherwise 0)", 80, F::kS32},
          {"records for this race", 84, F::kS32}},
         0,
         -1},
        {"aibldreq.bin",
         nullptr,
         C::kFileSize,
         0,
         24,
         {{"building (edifice.bst's number)", 0, F::kS32},
          {"population needed", 4, F::kS32},
          {"maximum population needed", 8, F::kS32},
          {"energy needed", 12, F::kS32}},
         0,
         -1},
        {"habit.bin",
         nullptr,
         C::kFileSize,
         0,
         8,
         {{"planet type (environ.est)", 0, F::kU8},
          {"atmosphere (environ.est)", 1, F::kU8},
          {"habitability for each major race, then for minor races and empty systems", 2, F::kU8, 6}},
         -1,
         -1},
        {"meprvnum.bin",
         nullptr,
         C::kFileSize,
         0,
         20,
         {{"extra starting systems at each civilization level (Beginning ... Advanced)", 0, F::kS32, 5}},
         -1,
         -1,
         {"Cardassians", "Federation", "Ferengi", "Klingons", "Romulans", "?"}},
        {"ppgrowth.bin",
         nullptr,
         C::kFileSize,
         0,
         12,
         {{"from", 0, F::kU16}, {"to", 2, F::kU16}, {"value", 4, F::kU32}, {"flag", 8, F::kU8}},
         -1,
         -1},
        {"combat.bin",
         nullptr,
         C::kFileSize,
         0,
         500,
         {{"first value against each ship (the even bytes)", 0, F::kU8, 250},
          {"second value against each ship (the odd bytes)", 250, F::kU8, 250}},
         -1,
         -1},
        // The .wtf tables name their own columns and rows: a u32 (1), u16
        // columns, u16 rows, u32 values, 33-byte column names, 33-byte row
        // names, then f64 values row by row.
        {"aiempire.wtf",
         nullptr,
         C::kFileSize,
         474,
         72,
         {{"Default_Espionage_Tolerance", 0, F::kF64},
          {"Espionage_Tolerance_Turn_Delta", 8, F::kF64},
          {"Espionage_Tolerance_Incident_Delta", 16, F::kF64},
          {"Default_Raiding_Tolerance", 24, F::kF64},
          {"Raiding_Tolerance_Turn_Delta", 32, F::kF64},
          {"Raiding_Tolerance_Incident_Delta", 40, F::kF64},
          {"Default_Audience_Tolerance", 48, F::kF64},
          {"Audience_Tolerance_Turn_Delta", 56, F::kF64},
          {"Audience_Tolerance_Incident_Delta", 64, F::kF64}},
         -1,
         -1,
         {},
         309,
         33},
        {"aiminor.wtf",
         nullptr,
         C::kFileSize,
         1233,
         56,
         {{"Personality (0 warlike, 1 peaceful, 2 pacifist, 3 deceptive, 4 spiritual)", 0, F::kF64},
          {"Explorer (0 no, 1 yes)", 8, F::kF64},
          {"Cardassian_Hostility (0 enraged ... 4 neutral ... 8 worshipful)", 16, F::kF64},
          {"Federation_Hostility", 24, F::kF64},
          {"Ferengi_Hostility", 32, F::kF64},
          {"Klingon_Hostility", 40, F::kF64},
          {"Romulan_Hostility", 48, F::kF64}},
         -1,
         -1,
         {},
         243,
         33},
    });
    return kLayouts;
}

Error DataTable::load(const TableLayout &layout, std::vector<uint8_t> data, std::vector<uint8_t> descriptions) {
    _layout = &layout;
    _data = std::move(data);
    _descriptions = std::move(descriptions);
    _count = 0;
    if (_data.size() < layout.headerSize)
        return Error::fail("too small for its header");
    const size_t room = (_data.size() - layout.headerSize) / layout.recordSize;
    switch (layout.count) {
    case C::kU16AtStart:
        _count = _data.size() >= 2 ? size_t(_data[0] | _data[1] << 8) : 0;
        break;
    case C::kU32AtStart:
        _count = _data.size() >= 4 ? u32(_data.data()) : 0;
        break;
    case C::kFileSize:
        _count = room;
        if ((_data.size() - layout.headerSize) % layout.recordSize)
            return Error::fail(std::to_string(_data.size()) + " bytes isn't a whole number of " +
                               std::to_string(layout.recordSize) + "-byte records");
        break;
    }
    if (_count > room) {
        const size_t claimed = _count;
        _count = 0;
        return Error::fail(std::to_string(claimed) + " records don't fit in " + std::to_string(_data.size()) +
                           " bytes");
    }
    return Error::success();
}

std::string DataTable::value(size_t r, const Field &f) const {
    const uint8_t *p = record(r) + f.offset;
    char buf[64];
    switch (f.type) {
    case F::kText: {
        size_t n = 0;
        while (n < f.size && p[n])
            ++n;
        return text::windows1252ToUtf8(std::string_view(reinterpret_cast<const char *>(p), n));
    }
    case F::kU8: {
        std::string s;
        for (uint32_t i = 0; i < f.size; ++i)
            s += (i ? " " : "") + std::to_string(p[i]);
        return s;
    }
    case F::kU16: {
        std::string s;
        for (uint32_t i = 0; i < f.size; ++i)
            s += (i ? " " : "") + std::to_string(p[2 * i] | p[2 * i + 1] << 8);
        return s;
    }
    case F::kS16: {
        std::string s;
        for (uint32_t i = 0; i < f.size; ++i)
            s += (i ? " " : "") + std::to_string(int16_t(p[2 * i] | p[2 * i + 1] << 8));
        return s;
    }
    case F::kU32: {
        std::string s;
        for (uint32_t i = 0; i < f.size; ++i)
            s += (i ? " " : "") + std::to_string(u32(p + 4 * i));
        return s;
    }
    case F::kS32: {
        std::string s;
        for (uint32_t i = 0; i < f.size; ++i)
            s += (i ? " " : "") + std::to_string(int32_t(u32(p + 4 * i)));
        return s;
    }
    case F::kF32: {
        float v;
        std::memcpy(&v, p, 4);
        std::snprintf(buf, sizeof buf, "%g", double(v));
        return buf;
    }
    case F::kF64: {
        double v;
        std::memcpy(&v, p, 8);
        std::snprintf(buf, sizeof buf, "%g", v);
        return buf;
    }
    }
    return {};
}

std::string DataTable::name(size_t r) const {
    if (r < _layout->recordNames.size())
        return _layout->recordNames[r];
    if (_layout->headerNameSize) {
        const size_t at = _layout->headerNamesAt + r * _layout->headerNameSize;
        if (at + _layout->headerNameSize > _layout->headerSize)
            return std::to_string(r);
        const char *p = reinterpret_cast<const char *>(_data.data() + at);
        return text::windows1252ToUtf8(std::string_view(p, strnlen(p, _layout->headerNameSize)));
    }
    if (_layout->nameField < 0)
        return std::to_string(r);
    return value(r, _layout->fields[size_t(_layout->nameField)]);
}

std::string DataTable::description(size_t r) const {
    if (_layout->descriptionField < 0 || _descriptions.empty())
        return {};
    const uint32_t off = u32(record(r) + _layout->fields[size_t(_layout->descriptionField)].offset);
    if (off >= _descriptions.size())
        return {};
    const char *s = reinterpret_cast<const char *>(_descriptions.data() + off);
    const size_t n = strnlen(s, _descriptions.size() - off);
    return text::windows1252ToUtf8(std::string_view(s, n));
}

std::vector<std::pair<uint32_t, uint32_t>> DataTable::unknownRanges() const {
    std::vector<bool> known(_layout->recordSize, false);
    for (const Field &f : _layout->fields)
        for (uint32_t i = 0; i < fieldBytes(f) && f.offset + i < known.size(); ++i)
            known[f.offset + i] = true;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    for (uint32_t i = 0; i < known.size();) {
        if (known[i]) {
            ++i;
            continue;
        }
        uint32_t j = i;
        while (j < known.size() && !known[j])
            ++j;
        ranges.push_back({i, j});
        i = j;
    }
    return ranges;
}

} // namespace reluva::botf
