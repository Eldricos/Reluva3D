// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/setup_screen.h"

#include "engines/botf/ui/timers.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace reluva::botf {

namespace {

// gmesetup.wdf's widgets, matched to the manual's controls by their order
// on the screen and checked against a recording of the game.
constexpr int32_t kAccept = 9201;
constexpr int32_t kCancel = 9202;
constexpr std::array<int32_t, 5> kCivButtons = {9223, 9225, 9227, 9229, 9231};
constexpr std::array<int32_t, 5> kCivTexts = {9224, 9226, 9228, 9230, 9232};
constexpr int32_t kMinorRaces = 9207;
constexpr int32_t kMinorRacesText = 9208;
constexpr int32_t kDifficulty = 9209;
constexpr int32_t kDifficultyText = 9210;
constexpr int32_t kStrategicLess = 9211;
constexpr int32_t kStrategicMore = 9212;
constexpr int32_t kStrategicText = 9213;
constexpr int32_t kTacticalLess = 9214;
constexpr int32_t kTacticalMore = 9215;
constexpr int32_t kTacticalText = 9216;
constexpr int32_t kRandomEvents = 9217;
constexpr int32_t kRandomEventsText = 9218;
constexpr int32_t kCombat = 9219;
constexpr int32_t kCombatText = 9220;
constexpr int32_t kVictory = 9221;
constexpr int32_t kVictoryText = 9222;
constexpr int32_t kShape = 9234;
constexpr int32_t kShapeText = 9206;
constexpr int32_t kShapePicture = 9204;
constexpr int32_t kSize = 9233;
constexpr int32_t kSizeText = 9205;
constexpr int32_t kSizePicture = 9203;

// The texts' colours, measured from a screenshot of the game; the files
// don't hold them (most of these fonts say "the game's default", and the
// timers' own colour isn't theirs). The values and timers are in the
// options screen's colours.
constexpr Color kValueColor{24, 101, 189};  // the preferences and the galaxy
constexpr Color kLevelColor{90, 207, 140};  // the civilization levels
constexpr Color kTimerColor{173, 227, 115}; // the two timers

const char *const kShapePictures[] = {"cc_gsir.tga", "cc_gsel.tga", "cc_gsrg.tga", "cc_gssp.tga"};
const char *const kSizePictures[] = {"cc_gzsm.tga", "cc_gzmd.tga", "cc_gzlg.tga"};

} // namespace

Error SetupScreen::load() {
    return loadLayouts("gmesetup.wdf",
                       {kAccept, kCancel, kMinorRaces, kDifficulty, kStrategicLess, kStrategicMore, kTacticalLess,
                        kTacticalMore, kRandomEvents, kCombat, kVictory, kShape, kSize, kCivButtons[0], kCivButtons[1],
                        kCivButtons[2], kCivButtons[3], kCivButtons[4]});
}

GameSetup setupFromIni(const std::map<std::string, std::string> &ini) {
    GameSetup s;
    // Each value as a saved game's gameInfo holds it
    // (docs/engines/botf/formats/sav.md), so the difficulty is one more than
    // the screen's order.
    auto number = [&](const char *key, int &out, int offset, int count) {
        auto it = ini.find(key);
        if (it == ini.end() || it->second.empty())
            return;
        const int v = std::atoi(it->second.c_str()) - offset;
        if (v >= 0 && v < count)
            out = v;
    };
    number("SHAPE", s.galaxyShape, 0, 4);
    number("SIZE", s.galaxySize, 0, 3);
    number("RACENUM", s.minorRaces, 0, 4);
    number("DIFFICULTY", s.difficulty, 1, 5);
    for (size_t i = 0; i < s.civilization.size(); ++i)
        number(("CIVLEVEL" + std::to_string(i + 1)).c_str(), s.civilization[i], 0, 5);
    return s;
}

std::map<std::string, std::string> setupIniValues(const GameSetup &s) {
    std::map<std::string, std::string> v = {
        {"SHAPE", std::to_string(s.galaxyShape)},
        {"SIZE", std::to_string(s.galaxySize)},
        {"RACENUM", std::to_string(s.minorRaces)},
        {"DIFFICULTY", std::to_string(s.difficulty + 1)}, // as setupFromIni reads it
    };
    for (size_t i = 0; i < s.civilization.size(); ++i)
        v["CIVLEVEL" + std::to_string(i + 1)] = std::to_string(s.civilization[i]);
    return v;
}

std::string galaxyShapeName(const Lexicon &lexicon, int shape) {
    static const std::pair<int, const char *> kShapes[] = {{lex::kIrregular, "Irregular"},
                                                           {lex::kElliptical, "Elliptical"},
                                                           {lex::kRing, "Ring"},
                                                           {lex::kSpiral, "Spiral"}};
    const auto &[entry, english] = kShapes[std::clamp(shape, 0, 3)];
    const std::string &w = lexicon[size_t(entry)];
    return w.empty() ? english : w;
}

std::string civilizationLevel(const Lexicon &lexicon, int level) {
    static const std::pair<int, const char *> kLevels[] = {{lex::kBeginning, "BEGINNING"},
                                                           {lex::kEarly, "EARLY"},
                                                           {lex::kDeveloped, "DEVELOPED"},
                                                           {lex::kExpanded, "EXPANDED"},
                                                           {lex::kAdvanced, "ADVANCED"}};
    level = std::clamp(level, 0, 4);
    const std::string &w = lexicon[size_t(kLevels[level].first)];
    // The screens number the levels; lexicon.dic holds only the word.
    return std::to_string(level + 1) + " " + (w.empty() ? kLevels[level].second : w);
}

void SetupScreen::click(int32_t id, bool shift) {
    GameSetup &s = _setup;
    for (size_t i = 0; i < kCivButtons.size(); ++i)
        if (id == kCivButtons[i]) {
            // The manual: with Shift, every empire takes this one's level;
            // if they already have it, the next level.
            const bool same = std::all_of(s.civilization.begin(), s.civilization.end(),
                                          [&](int c) { return c == s.civilization[i]; });
            const int level = shift && !same ? s.civilization[i] : (s.civilization[i] + 1) % 5;
            if (shift)
                s.civilization.fill(level);
            else
                s.civilization[i] = level;
            return;
        }
    switch (id) {
    case kMinorRaces:
        s.minorRaces = (s.minorRaces + 1) % 4;
        break;
    case kDifficulty:
        s.difficulty = (s.difficulty + 1) % 5;
        break;
    // The arrows step through the timer's choices, stopping at either end.
    case kStrategicLess:
        s.strategicTimer = std::max(s.strategicTimer - 1, 0);
        break;
    case kStrategicMore:
        s.strategicTimer = std::min(s.strategicTimer + 1, int(strategicTimerChoices().size()) - 1);
        break;
    case kTacticalLess:
        s.tacticalTimer = std::max(s.tacticalTimer - 1, 0);
        break;
    case kTacticalMore:
        s.tacticalTimer = std::min(s.tacticalTimer + 1, int(tacticalTimerChoices().size()) - 1);
        break;
    case kRandomEvents:
        s.randomEvents = !s.randomEvents;
        break;
    case kCombat:
        s.manualCombat = !s.manualCombat;
        break;
    case kVictory:
        s.victory = (s.victory + 1) % 2;
        break;
    case kShape:
        s.galaxyShape = (s.galaxyShape + 1) % 4;
        break;
    case kSize:
        s.galaxySize = (s.galaxySize + 1) % 3;
        break;
    default:
        break;
    }
}

SetupScreen::Result SetupScreen::handle(const Event &e) {
    if (e.type == EventType::kKeyDown && (e.key == key::kReturn || e.key == key::kKpEnter))
        return Result::kAccepted;
    if (e.type == EventType::kKeyDown && e.key == key::kEscape)
        return Result::kCancelled;
    if (e.button != MouseButton::kLeft)
        return Result::kOpen;
    if (e.type == EventType::kMouseDown) {
        _pressed = buttonAt(e.mouseX, e.mouseY);
    } else if (e.type == EventType::kMouseUp) {
        // A button acts when the mouse comes up on it, as Windows buttons do.
        const int32_t id = _pressed && buttonAt(e.mouseX, e.mouseY) == _pressed ? _pressed : 0;
        _pressed = 0;
        if (id == kAccept)
            return Result::kAccepted;
        if (id == kCancel)
            return Result::kCancelled;
        if (id) {
            sound(UiSound::kSetupClick);
            click(id, (e.mods & keymod::kShift) != 0);
        }
    }
    return Result::kOpen;
}

void SetupScreen::draw(Surface &screen) {
    drawLayouts(screen);
    const GameSetup &s = _setup;
    drawPicture(screen, kShapePicture, kShapePictures[s.galaxyShape]);
    drawPicture(screen, kSizePicture, kSizePictures[s.galaxySize]);

    for (size_t i = 0; i < kCivTexts.size(); ++i)
        drawText(screen, kCivTexts[i], civilizationLevel(_lexicon, s.civilization[i]), kLevelColor);
    const std::pair<int, const char *> minors[] = {
        {lex::kNone, "None"}, {lex::kFew, "FEW"}, {lex::kSome, "SOME"}, {lex::kMany, "MANY"}};
    const std::pair<int, const char *> difficulties[] = {{lex::kSimple, "SIMPLE"},
                                                         {lex::kEasy, "EASY"},
                                                         {lex::kNormal, "NORMAL"},
                                                         {lex::kHard, "HARD"},
                                                         {lex::kImpossible, "IMPOSSIBLE"}};
    const std::pair<int, const char *> victories[] = {{lex::kDomination, "DOMINATION"}, {lex::kVendetta, "VENDETTA"}};
    const std::pair<int, const char *> sizes[] = {
        {lex::kSmall, "Small"}, {lex::kMedium, "Medium"}, {lex::kLarge, "Large"}};
    auto value = [&](int32_t box, const std::pair<int, const char *> &w) {
        drawText(screen, box, word(w.first, w.second), kValueColor);
    };
    value(kMinorRacesText, minors[s.minorRaces]);
    value(kDifficultyText, difficulties[s.difficulty]);
    value(kRandomEventsText, s.randomEvents ? std::pair{lex::kOn, "ON"} : std::pair{lex::kOff, "OFF"});
    value(kCombatText, s.manualCombat ? std::pair{lex::kManual, "MANUAL"} : std::pair{lex::kAutomatic, "AUTOMATIC"});
    value(kVictoryText, victories[s.victory]);
    drawText(screen, kShapeText, galaxyShapeName(_lexicon, s.galaxyShape), kValueColor);
    value(kSizeText, sizes[s.galaxySize]);
    // The timers show "NONE" though the entry is "None".
    // None, or the time ("01:00"); here "NONE", where the entry is "None".
    auto timer = [&](int32_t box, const std::vector<int> &choices, int choice) {
        const int seconds = choices[size_t(std::clamp(choice, 0, int(choices.size()) - 1))];
        drawText(screen, box, seconds ? timerText(seconds) : upperAscii(word(lex::kNone, "None")), kTimerColor);
    };
    timer(kStrategicText, strategicTimerChoices(), s.strategicTimer);
    timer(kTacticalText, tacticalTimerChoices(), s.tacticalTimer);
}

} // namespace reluva::botf
