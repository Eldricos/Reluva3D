// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/object_names.h"

#include "engines/tpw/ui/text.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace reluva::tpw {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string joined(const std::array<std::string, 2> &pair) {
    return pair[1].empty() ? pair[0] : pair[0].empty() ? pair[1] : pair[0] + " " + pair[1];
}

std::array<std::string, 2> split(const std::string &name) {
    const size_t space = name.find(' ');
    if (space == std::string::npos)
        return {name, {}};
    return {name.substr(0, space), name.substr(space + 1)};
}

bool has(const std::string &text, const char *word) {
    return text.find(word) != std::string::npos;
}

// The second old strings of pairs whose first is a ride's or a game's
// own name, "Log Mill" "Special Ride", or empty, "TV Sim" ""; with
// others the first stands alone, "Gem Grabber".
bool kindOfRide(const std::string &old) {
    constexpr const char *kKinds[] = {"ride", "coaster", "game", "strength", "twister", "tours", "bouncer"};
    return std::any_of(std::begin(kKinds), std::end(kKinds), [&](const char *kind) { return has(old, kind); });
}

// OBJECT_NAMES' runs of strings, world by world; the shared rides' (ids
// 4xxx) between Polar Zone's and Arabian Nights'.
struct Section {
    const char *world;
    size_t begin;
    size_t end;
};
constexpr Section kSections[] = {
    {"science", 0, 158}, {"water", 158, 307}, {"generic", 307, 319}, {"arabian", 319, 534}};

// Rides' descriptions, by id, matched by hand to what OBJECT_DESC says:
// the ride a string's words fit, of its world's run. Most rides have none
// that fits.
constexpr std::pair<int, int> kRideDescriptions[] = {
    {1150, 5},  {1102, 4},  {1108, 18}, {1105, 21}, {1112, 9},  {2103, 3},  {2140, 23}, {2113, 33}, {2116, 34},
    {4185, 35}, {4201, 38}, {2170, 31}, {3170, 45}, {4190, 56}, {4110, 58}, {3106, 20}, {3107, 51}};

// A sideshow's by its game (Info.RideTypeStringIndex).
int sideshowDescription(int rideType) {
    switch (rideType) {
    case 19: // a shooting gallery
        return 68;
    case 20: // a strength test
        return 69;
    case 21: // an arcade gambler
        return 64;
    case 22: // a grab-a-prize
        return 66;
    case 23: // a race
        return 67;
    default:
        return 65;
    }
}

// A shop's by what its settings' name says it sells.
int shopDescription(const std::string &name) {
    constexpr std::pair<const char *, int> kWords[] = {
        {"costume", 70}, {"drink", 71}, {"hot dog", 72}, {"burger", 72},   {"balloon", 73},  {"restaurant", 74},
        {"coffee", 74},  {"info", 75},  {"photo", 76},   {"gift", 77},     {"umbrella", 78}, {"candy", 79},
        {"floss", 79},   {"chip", 80},  {"fries", 80},   {"ice cream", 81}};
    for (const auto &[word, text] : kWords)
        if (has(name, word))
            return text;
    return 0;
}

int facilityDescription(const Attractions::Attraction &a, const std::string &name) {
    if (a.lab)
        return 89;
    if (a.trainingRoom)
        return 91;
    if (a.security)
        return 84;
    if (a.holdsLitter)
        return 85;
    if (a.relief)
        return a.gender == 2 ? 88 : 87;
    if (a.requiresWatering)
        return 82;
    if (has(name, "staff room"))
        return 90;
    if (has(name, "speaker"))
        return 86;
    if (has(name, "sign"))
        return 92;
    return a.kind == Attractions::Kind::kFeature && !a.obstacle ? 83 : 0;
}

} // namespace

Error ObjectNames::load(const SearchSet &files, const std::string &folder, const CharacterSet &characters) {
    StringTable names;
    StringTable oldNames;
    StringTable descriptions;
    if (Error err = readStringTable(files, folder, "OBJECT_NAMES", characters, names))
        return err;
    if (Error err = readStringTable(files, folder, "OLD_OBJECT_NAMES", characters, oldNames))
        return err;
    if (Error err = readStringTable(files, folder, "OBJECT_DESC", characters, descriptions))
        return err;
    auto texts = [](const StringTable &t) {
        std::vector<std::string> out;
        for (size_t i = 0; i < t.size(); ++i)
            out.push_back(t.text(i));
        return out;
    };
    set(texts(names), texts(oldNames), texts(descriptions));
    return {};
}

void ObjectNames::set(std::vector<std::string> names, std::vector<std::string> oldNames,
                      std::vector<std::string> descriptions) {
    _names = std::move(names);
    _oldNames = std::move(oldNames);
    _descriptions = std::move(descriptions);
}

// OLD_OBJECT_NAMES, string by string at the same places as OBJECT_NAMES',
// holds older names, mostly those the objects' settings have. Rides',
// sideshows' and shops' names come in pairs of strings, a theme and a
// kind: old "Log Mill" "Special Ride", "Rocket Man" "Costume Shop"; new
// "The" "Thrill Mill", "Costume" "Shop". The settings' name can be the
// pair, its first string when the second is a kind of ride or game, or
// its second when it names a kind of shop or game itself. Some names are one string, the same old and new ("Info
// Kiosk"), and features' and facilities' are one string each ("Small
// Bush", "Bush (small)").
std::optional<std::array<std::string, 2>> ObjectNames::find(const Attractions::Attraction &a, const std::string &own,
                                                            size_t begin, size_t end) const {
    end = std::min({end, _names.size(), _oldNames.size()});
    auto name = [&](size_t i) { return _names[i]; };
    auto old = [&](size_t i) { return _oldNames[i]; };
    for (size_t i = begin; i + 1 < end; ++i)
        if (!old(i).empty() && !old(i + 1).empty() && lower(old(i) + " " + old(i + 1)) == own) {
            const std::array<std::string, 2> shown{name(i), name(i + 1)};
            if (!joined(shown).empty())
                return shown[0].empty() || shown[1].empty() ? split(joined(shown)) : shown;
        }
    const bool single = a.kind == Attractions::Kind::kFacility || a.kind == Attractions::Kind::kFeature;
    const bool second = has(own, "shop") || has(own, "kiosk") || has(own, "restaurant") || has(own, "test");
    for (size_t i = begin; i < end; ++i) {
        if (lower(old(i)) != own || name(i).empty())
            continue;
        if (single)
            return split(name(i));
        if (second && name(i) != old(i) && i > begin && !name(i - 1).empty() && name(i - 1) != old(i - 1))
            return std::array<std::string, 2>{name(i - 1), name(i)};
        if (!second && i + 1 < end && !name(i + 1).empty() && (old(i + 1).empty() || kindOfRide(lower(old(i + 1)))))
            return std::array<std::string, 2>{name(i), name(i + 1)};
        return split(name(i));
    }
    return std::nullopt;
}

// The loudspeakers of a world all have the settings' name "Loudspeaker",
// and which of OBJECT_NAMES' tunes is whose isn't known: they keep it.
// Settings' names that aren't their old names: the recording shows Land
// of Invention's "Chips Shop" as "Fries Shop", and its "TV sim" as
// "InvenTiVe World", whose old name is "Mad Worlds Of Invention"; its log
// flume's settings add "Flume" to the old "Hammer Fluge Log Ride"
// (inferred). By world ("" any) and the settings' name, lower case.
constexpr std::array<std::array<const char *, 3>, 3> kOldNames{{
    {"", "chips shop", "fries shop"},
    {"science", "tv sim", "mad worlds of invention"},
    {"", "hammer fluge log flume ride", "hammer fluge log ride"},
}};

// The photo kiosks' settings name them "Bellow Photo" and "Photo Booth",
// as other objects; the game calls them "Photo Kiosk" (UITEXT 44),
// OBJECT_NAMES' string 2.
std::array<std::string, 2> ObjectNames::lines(const Attractions::Attraction &a) const {
    std::string own = a.photo ? "photo kiosk" : lower(a.name);
    if (has(own, "speaker"))
        return split(a.name);
    const size_t at = a.folder.find('/');
    const std::string world =
        at == std::string::npos ? std::string() : a.folder.substr(at + 1, a.folder.find('/', at + 1) - at - 1);
    for (const auto &[in, settings, old] : kOldNames)
        if ((*in == 0 || world == in) && own == settings)
            own = old;
    for (const Section &s : kSections)
        if (world == s.world)
            if (auto found = find(a, own, s.begin, s.end))
                return *found;
    if (auto found = find(a, own, kSections[2].begin, kSections[2].end))
        return *found;
    if (auto found = find(a, own, 0, _names.size()))
        return *found;
    return split(a.name);
}

std::string ObjectNames::shown(const Attractions::Attraction &a) const {
    return joined(lines(a));
}

std::string ObjectNames::description(const Attractions::Attraction &a) const {
    const std::string name = lower(a.name);
    int text = 0;
    switch (a.kind) {
    case Attractions::Kind::kRide:
        for (const auto &[id, ride] : kRideDescriptions)
            if (id == a.id)
                text = ride;
        break;
    case Attractions::Kind::kSideshow:
        text = sideshowDescription(a.rideType);
        break;
    case Attractions::Kind::kShop:
        text = shopDescription(name);
        break;
    case Attractions::Kind::kFacility:
    case Attractions::Kind::kFeature:
        text = facilityDescription(a, name);
        break;
    }
    return text > 0 && size_t(text) < _descriptions.size() ? _descriptions[size_t(text)] : std::string();
}

} // namespace reluva::tpw
