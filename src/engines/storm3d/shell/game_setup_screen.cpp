// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/game_setup_screen.h"

#include <functional>

#include <algorithm>

#include "engines/storm3d/formats/odf.h"

#include <cstdio>
#include <cstdlib>

namespace reluva::storm3d {

namespace {

constexpr const char *kSection = "multiplayer_setup";
// Where the game's frame has them (the pictures matched against it).
constexpr int kRowY = 54;
constexpr int kRowStep = 19;
constexpr int kWhoX = 5;
constexpr int kSideX = 150;
constexpr int kTeamX = 261;
constexpr int kSwatchX = 16;
constexpr int kSwatchY = 209;
// The bottom bar, a pixel right and down of where other screens have it.
constexpr int kBarDx = 1;
constexpr int kBarDy = 1;
constexpr int kMainDx = 2; // its right plate two
constexpr int kSwatchStep = 29;
constexpr int kSwatchW = 27;
constexpr int kSwatchH = 16;
constexpr int kScrollX = 325;
constexpr int kBulletX = 15;
constexpr size_t kShroudOption = 18; // into options()
constexpr size_t kPlacementOption = 16;
constexpr size_t kTechOption = 8;
constexpr size_t kAlliancesOption = 9;
constexpr float kBullet = 20;
constexpr std::array<uint8_t, 3> kTaken = {25, 24, 25};
// Arial Bold as the game asks GDI for it, fitted to the frame's text: 12
// pixels on the controls, 16 for the headings, 20 for the title.
constexpr float kText = 12;
constexpr float kHeading = 16;
constexpr float kTitle = 20;
constexpr Color kBlack{0, 0, 0};
constexpr Color kYellow{255, 190, 0};
constexpr Color kWhite{255, 255, 255};
constexpr Color kPlayerColour{255, 0, 0}; // the player's own name
// An open list's items: 20 high, grey with a light left edge, as the
// original's frame has them.
constexpr int kItemHeight = 20;
constexpr Color kItemFill{132, 134, 132};
constexpr Color kItemLight{181, 178, 181};

void fillRect(Surface &screen, int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0)
        return;
    std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = c.r, rgba[i + 1] = c.g, rgba[i + 2] = c.b;
        rgba[i + 3] = 255;
    }
    drawPixels(screen, rgba.data(), w, h, x, y, false);
}
// teamcolor.odf's colours a player can take, in the swatches' order.
constexpr const char *kColourNames[] = {"empty", "white", "red", "blue", "green", "yellow", "purple", "cyan", "brown"};

bool inside(int x, int y, int rx, int ry, int w, int h) {
    return x >= rx && y >= ry && x < rx + w && y < ry + h;
}

// The controls' sizes, by Option::Look.
constexpr int kLookSize[5][2] = {{111, 20}, {111, 20}, {128, 20}, {128, 20}, {58, 19}};
constexpr int kSlotSize[3][2] = {{145, 19}, {111, 19}, {58, 19}};
constexpr int kSlotX[3] = {kWhoX, kSideX, kTeamX};

} // namespace

const std::vector<GameSetupScreen::Option> &GameSetupScreen::options() {
    using O = Option;
    static const std::vector<Option> kOptions = {
        {O::kList111, 524, 60, {"normal_game_type", "head_to_head_type", "ladder_game_type"}, 0},
        {O::kList111,
         524,
         84,
         {"maximum_players_2", "maximum_players_3", "maximum_players_4", "maximum_players_5", "maximum_players_6",
          "maximum_players_7", "maximum_players_8"},
         2},
        {O::kList111, 524, 108, {"ping_filter_off", "ping_filter_200", "ping_filter_400", "ping_filter_600"}, 0},
        {O::kToggle111, 524, 132, {"lock_game_off", "lock_game_on"}, 0},
        {O::kList111,
         524,
         156,
         {"starting_dilithium|sd_value_1", "starting_dilithium|sd_value_2", "starting_dilithium|sd_value_3",
          "starting_dilithium|sd_value_4", "starting_dilithium|sd_value_5"},
         0},
        {O::kList111,
         524,
         180,
         {"starting_crew|sc_value_1", "starting_crew|sc_value_2", "starting_crew|sc_value_3",
          "starting_crew|sc_value_4", "starting_crew|sc_value_5"},
         0},
        {O::kList111,
         524,
         204,
         {"max_officers|mo_value_1", "max_officers|mo_value_2", "max_officers|mo_value_3", "max_officers|mo_value_4",
          "max_officers|mo_value_5"},
         0},
        {O::kList111,
         524,
         228,
         {"default_units_standard", "default_units_map", "default_units_minimal", "default_units_super"},
         0},
        {O::kList111,
         524,
         252,
         {"tech_level_one", "tech_level_two", "tech_level_three", "tech_level_four", "tech_level_five",
          "tech_level_six", "tech_level_seven"},
         0},
        // label.map names some toggles by the other state: "ferengi_off"
        // says "Ferengi On".
        {O::kToggle111, 524, 276, {"alliances_teams_off", "alliances_teams_on"}, 0},
        {O::kToggle111, 524, 300, {"give_units_on", "give_units_off"}, 0},
        {O::kToggle111, 524, 324, {"infinite_moons_on", "infinite_moons_off"}, 0},
        {O::kToggle111, 524, 348, {"ferengi_off", "ferengi_on"}, 0},
        {O::kToggle111, 524, 372, {"directors_on", "directors_off"}, 0},
        {O::kToggle128, 385, 272, {"select_map"}, 0},
        {O::kToggle128, 385, 295, {"hide_minimap_off", "hide_minimap_on"}, 0},
        {O::kToggle128, 385, 318, {"fixed_placement_on", "fixed_placement_off"}, 0},
        {O::kList128, 385, 341, {"show_all", "show_available", "show_allies", "show_none"}, 0},
        {O::kList128,
         385,
         364,
         {"shroud_fog_on_on", "shroud_fog_off_on", "shroud_fog_on_off", "shroud_fog_off_off"},
         0},
        // Who the messages typed go to.
        {O::kList58, 563, 421, {"all", "allies", "enemies", "private"}, 0},
    };
    return kOptions;
}

const std::vector<const char *> &GameSetupScreen::sides() {
    static const std::vector<const char *> kSides = {"random", "federation", "klingon", "romulan", "borg"};
    return kSides;
}

int GameSetupScreen::targetAt(int x, int y) {
    for (int slot = 0; slot < 8; ++slot)
        for (int part = 0; part < 3; ++part)
            if (inside(x, y, kSlotX[part], kRowY + slot * kRowStep, kSlotSize[part][0], kSlotSize[part][1]))
                return 100 * slot + part;
    for (size_t i = 0; i < options().size(); ++i) {
        const Option &o = options()[i];
        if (inside(x, y, o.x, o.y, kLookSize[o.look][0], kLookSize[o.look][1]))
            return 1000 + int(i);
    }
    if (NavigationBar::onPrevious(x, y, kBarDx, kBarDy))
        return 2000;
    if (NavigationBar::onStart(x, y, kBarDx, kBarDy))
        return 2001;
    if (NavigationBar::onMain(x, y, kMainDx, kBarDy))
        return 2002;
    return -1;
}

const char *GameSetupScreen::colourName(int colour) {
    return colour >= 0 && size_t(colour) < std::size(kColourNames) ? kColourNames[colour] : "empty";
}

std::string GameSetupScreen::raceOf(const Slot &slot, size_t index) {
    static const char *const kRaces[] = {"fed", "kling", "rom", "borg"};
    return kRaces[slot.side > 0 ? size_t(slot.side - 1) % 4 : index % 4];
}

int GameSetupScreen::amount(size_t option) const {
    const std::string value = options()[option].values[size_t(_values[option])];
    const size_t bar = value.find('|');
    return std::atoi(_text.text(kSection, bar == std::string::npos ? value : value.substr(bar + 1)).c_str());
}

bool GameSetupScreen::shroud() const {
    // Its values: on and on, off and on, on and off, off and off.
    return _values[kShroudOption] % 2 == 0;
}

bool GameSetupScreen::fog() const {
    return _values[kShroudOption] < 2;
}

std::vector<int> GameSetupScreen::startsOf(const BznMap &map) {
    std::vector<int> starts;
    for (int n = 1; n <= 8; ++n)
        for (const BznMap::Path &p : map.paths)
            if (p.name == "Camera_" + std::to_string(n)) {
                starts.push_back(n);
                break;
            }
    return starts;
}

bool GameSetupScreen::randomPlacement() const {
    return _values[kPlacementOption] == 0;
}

int GameSetupScreen::techLevel() const {
    return _values[kTechOption];
}

bool GameSetupScreen::teams() const {
    return _values[kAlliancesOption] == 0;
}

int GameSetupScreen::randomSide(int roll) {
    // The game's races in its order, as sides() has them.
    static constexpr int kSides[4] = {1, 4, 3, 2};
    return kSides[std::clamp(roll, 0, 3)];
}

std::vector<int> GameSetupScreen::placeAtRandom(std::vector<int> starts, size_t players,
                                                const std::function<int(int)> &below) {
    std::vector<int> placed;
    for (size_t i = 0; i < players && !starts.empty(); ++i) {
        const int at = std::clamp(below(int(starts.size())), 0, int(starts.size()) - 1);
        placed.push_back(starts[size_t(at)]);
        starts.erase(starts.begin() + at);
    }
    return placed;
}

Error GameSetupScreen::load(const GameFiles &files, const std::string &name) {
    if (Error err = loadPicture(files, "bitmaps/multiset/mpsBG.bmp", _background))
        return err;
    const char *slotNames[3] = {"mpsplapd", "mpsracpd", "mpsteapd"};
    for (size_t part = 0; part < 3; ++part)
        for (size_t i = 0; i < 3; ++i)
            if (Error err = loadPicture(
                    files, "bitmaps/multiset/" + std::string(slotNames[part]) + std::to_string(i + 1) + ".bmp",
                    _slotLooks[part][i]))
                return err;
    const char *lookNames[5] = {"mps111pd", "mps111tg", "mps128pd", "mps128tg", "mpsteapd"};
    for (size_t look = 0; look < 5; ++look)
        for (size_t i = 0; i < 3; ++i)
            if (Error err = loadPicture(
                    files, "bitmaps/multiset/" + std::string(lookNames[look]) + std::to_string(i + 1) + ".bmp",
                    _optionLooks[look][i]))
                return err;
    if (Error err = _up.load(files, "bitmaps/multiset", "scrollup", kScrollX, 250))
        return err;
    if (Error err = _down.load(files, "bitmaps/multiset", "scrolldn", kScrollX, 377))
        return err;
    if (Error err = loadPicture(files, "bitmaps/multiset/scrolltb1.bmp", _thumb))
        return err;
    if (Error err = _bar.load(files))
        return err;
    _bar.setOffset(kBarDx, kBarDy, kMainDx, true);
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    _colours.clear();
    Odf colours;
    const Odf::Loader loader = [&files](std::string_view file, std::string &text) {
        std::vector<uint8_t> bytes;
        if (Error err = files.read(file, bytes))
            return err;
        text.assign(bytes.begin(), bytes.end());
        return Error::success();
    };
    const bool haveColours = !colours.load("teamcolor.odf", loader);
    for (const char *colourName : kColourNames) {
        float c[3] = {0.5f, 0.5f, 0.5f};
        if (const Odf::Entry *e = haveColours ? colours.find(colourName) : nullptr)
            std::sscanf(e->value.c_str(), "%f %f %f", &c[0], &c[1], &c[2]);
        _colours.push_back({uint8_t(c[0] * 255 + 0.5f), uint8_t(c[1] * 255 + 0.5f), uint8_t(c[2] * 255 + 0.5f)});
    }
    // The game opens it on its first map, "The Very First Map", for the
    // player and a computer.
    _map = "4first.bzn";
    BznMap map;
    _starts = !files.read("bzn/" + _map, data) && !map.parse(data) ? startsOf(map) : std::vector<int>{};
    _players = int(_starts.size());
    _slots = {};
    _slots[0] = {Who::kPlayer, 0, 0, 1};
    _slots[1] = {Who::kComputerMedium, 0, 1, 2};
    _values.clear();
    for (const Option &o : options())
        _values.push_back(o.initial);
    _name = name;
    _font.load();
    _hover = _pressed = -1;
    return Error::success();
}

bool GameSetupScreen::pointerAt(int x, int y) {
    const int now = targetAt(x, y);
    if (now == _hover)
        return false;
    _hover = now;
    return now != -1;
}

void GameSetupScreen::press(int x, int y) {
    _pressed = targetAt(x, y);
    // A list opens as it's pressed, and stays open while the button's held.
    if (_pressed >= 1000 && _pressed < 2000 && isList(options()[size_t(_pressed - 1000)]))
        _open = _pressed - 1000;
}

int GameSetupScreen::widthOf(Option::Look look) {
    return look == Option::kList58 ? 58 : look == Option::kList128 || look == Option::kToggle128 ? 128 : 111;
}

bool GameSetupScreen::isList(const Option &option) {
    return option.look == Option::kList111 || option.look == Option::kList128 || option.look == Option::kList58;
}

int GameSetupScreen::itemAt(int x, int y) const {
    if (_open < 0)
        return -1;
    const Option &o = options()[size_t(_open)];
    const int w = widthOf(o.look) - 2;
    // Upwards from the control, the first value nearest it, 20 each.
    for (size_t v = 0; v < o.values.size(); ++v) {
        const int bottom = o.y - int(v) * kItemHeight;
        if (x >= o.x && x < o.x + w && y > bottom - kItemHeight && y <= bottom)
            return int(v);
    }
    return -1;
}

SetupChoice GameSetupScreen::release(int x, int y) {
    const int pressed = _pressed;
    _pressed = -1;
    // An open list takes the item it's let go on, or stays as it was.
    if (_open >= 0) {
        const int item = itemAt(x, y);
        if (item >= 0)
            _values[size_t(_open)] = item;
        _open = -1;
        return SetupChoice::kNone;
    }
    const int at = targetAt(x, y);
    if (at != pressed || at < 0)
        return SetupChoice::kNone;
    if (at == 2000)
        return SetupChoice::kPreviousMenu;
    if (at == 2001)
        return SetupChoice::kLaunch;
    if (at == 2002)
        return SetupChoice::kMainMenu;
    // A toggle turns to its next value.
    if (at >= 1000) {
        const Option &o = options()[size_t(at - 1000)];
        _values[size_t(at - 1000)] = (_values[size_t(at - 1000)] + 1) % int(o.values.size());
        return SetupChoice::kNone;
    }
    Slot &slot = _slots[size_t(at / 100)];
    switch (at % 100) {
    case 0:
        // The player's own slot stays; the others turn through empty and
        // the computer's three levels.
        if (slot.who != Who::kPlayer)
            slot.who = slot.who == Who::kComputerHard ? Who::kEmpty : Who(int(slot.who) == 0 ? 2 : int(slot.who) + 1);
        break;
    case 1:
        slot.side = (slot.side + 1) % int(sides().size());
        break;
    default:
        slot.team = (slot.team + 1) % 8;
        break;
    }
    return SetupChoice::kNone;
}

std::string GameSetupScreen::whoText(const Slot &slot) const {
    switch (slot.who) {
    case Who::kPlayer:
        return _name;
    case Who::kComputerEasy:
        return _text.text(kSection, "computer_easy");
    case Who::kComputerMedium:
        return _text.text(kSection, "computer_medium");
    case Who::kComputerHard:
        return _text.text(kSection, "computer_hard");
    default:
        return _text.text(kSection, "empty");
    }
}

std::string GameSetupScreen::sideText(const Slot &slot, size_t index) const {
    const size_t side = slot.side > 0 ? size_t(slot.side) : 1 + index % 4;
    return _text.text(kSection, sides()[side < sides().size() ? side : 1]);
}

void GameSetupScreen::draw(Surface &screen) {
    drawPicture(screen, _background, 0, 0, false);
    _font.draw(screen, 64, 6, _text.text(kSection, "title"), kTitle, kWhite);
    _font.draw(screen, 32, 31, _text.text(kSection, "player_name"), kHeading, kYellow);
    _font.draw(screen, 189, 31, _text.text(kSection, "side"), kHeading, kYellow);
    _font.draw(screen, 268, 31, _text.text(kSection, "teams"), kHeading, kYellow);
    _font.draw(screen, 393, 6, _text.text(kSection, "game_name"), kHeading, kYellow);
    _font.draw(screen, 9, 233, _text.text(kSection, "messages"), kHeading, kYellow);
    for (size_t i = 0; i < _slots.size(); ++i) {
        const Slot &slot = _slots[i];
        const int y = kRowY + int(i) * kRowStep;
        const std::string words[3] = {whoText(slot), _text.text(kSection, sides()[size_t(slot.side)]),
                                      _text.text(kSection, "team_" + std::to_string(slot.team + 1))};
        for (int part = 0; part < 3; ++part) {
            const int target = 100 * int(i) + part;
            const auto &looks = _slotLooks[size_t(part)];
            const image::Image &look = _pressed == target ? looks[2] : _hover == target ? looks[1] : looks[0];
            drawPicture(screen, look, kSlotX[part], y, true);
            _font.drawCentred(screen, kSlotX[part], y, look.w, look.h, words[part], kText,
                              part == 0 && slot.who == Who::kPlayer ? kPlayerColour : kBlack);
        }
        // A player's colour, as a bullet before the name.
        if (slot.who != Who::kEmpty && size_t(slot.colour) < _colours.size()) {
            const auto &c = _colours[size_t(slot.colour)];
            _font.draw(screen, kBulletX, y + (kSlotSize[0][1] - int(kBullet)) / 2, _text.text(kSection, "bullet"),
                       kBullet, {c[0], c[1], c[2]});
        }
    }
    for (size_t i = 0; i < _colours.size(); ++i) {
        const int x = kSwatchX + int(i) * kSwatchStep;
        bool taken = false;
        for (const Slot &slot : _slots)
            taken = taken || (slot.who != Who::kEmpty && size_t(slot.colour) == i);
        // A colour a player has is dark; the first, none, is framed in black.
        std::array<uint8_t, 3> c = taken ? kTaken : _colours[i];
        for (int yy = kSwatchY; yy < kSwatchY + kSwatchH && yy < screen.h; ++yy)
            for (int xx = x; xx < x + kSwatchW && xx < screen.w; ++xx) {
                const bool edge =
                    i == 0 && (yy == kSwatchY || xx == x || yy == kSwatchY + kSwatchH - 1 || xx == x + kSwatchW - 1);
                uint8_t *p = screen.row(yy) + size_t(xx) * 4;
                p[0] = edge ? 0 : c[0], p[1] = edge ? 0 : c[1], p[2] = edge ? 0 : c[2], p[3] = 255;
            }
    }
    _up.draw(screen, false, false, true);
    drawPicture(screen, _thumb, kScrollX, 271, true);
    _down.draw(screen, false, false, true);
    // The map: its file's name and how many it takes, over its picture's place.
    _font.drawCentred(screen, 386, 103, 128, int(kHeading), _map, kHeading, kYellow);
    _font.drawCentred(screen, 386, 117, 128, int(kHeading),
                      std::to_string(_players) + " " + _text.text(kSection, "players"), kHeading, kYellow);
    for (size_t i = 0; i < options().size(); ++i) {
        const Option &o = options()[i];
        const int target = 1000 + int(i);
        const auto &looks = _optionLooks[size_t(o.look)];
        const image::Image &look = _pressed == target ? looks[2] : _hover == target ? looks[1] : looks[0];
        drawPicture(screen, look, o.x, o.y, true);
        std::string value = o.values[size_t(_values[i])];
        const size_t bar = value.find('|');
        const std::string words = bar == std::string::npos ? _text.text(kSection, value)
                                                           : _text.text(kSection, value.substr(0, bar)) + ":   " +
                                                                 _text.text(kSection, value.substr(bar + 1));
        _font.drawCentred(screen, o.x, o.y, look.w, look.h, words, kText, kBlack);
    }
    _bar.draw(screen, _font, _text.text(kSection, "previous_menu"), _hover == 2000, _pressed == 2000,
              _text.text(kSection, "launch"), _text.text(kSection, "main_menu"));
    // An open list over everything: its items upwards from the control,
    // grey with a light left edge and black right and bottom ones, their
    // words black, as the original's shroud list held open.
    if (_open >= 0) {
        const Option &o = options()[size_t(_open)];
        const int w = widthOf(o.look) - 2;
        for (size_t v = 0; v < o.values.size(); ++v) {
            const int bottom = o.y - int(v) * kItemHeight;
            const int top = bottom - kItemHeight + 1;
            fillRect(screen, o.x, top, w, kItemHeight, kItemFill);
            fillRect(screen, o.x, top, 1, kItemHeight, kItemLight);
            fillRect(screen, o.x + w - 1, top, 1, kItemHeight, kBlack);
            fillRect(screen, o.x, bottom, w, 1, kBlack);
            std::string value = o.values[v];
            const size_t bar = value.find('|');
            const std::string words = bar == std::string::npos ? _text.text(kSection, value)
                                                               : _text.text(kSection, value.substr(0, bar)) + ":   " +
                                                                     _text.text(kSection, value.substr(bar + 1));
            _font.drawCentred(screen, o.x, top, w, kItemHeight - 1, words, kText, kBlack);
        }
    }
}

} // namespace reluva::storm3d
