// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/options_screen.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::botf {

namespace {

// options.wdf's widgets, matched to the manual's controls by their order on
// the screen and checked against a screenshot of the game.
constexpr int32_t kAccept = 8100;
constexpr int32_t kCancel = 8101;
constexpr int32_t kTitle = 8122;
// Voice, video, music, sound effects, as the manual lists them, top down.
constexpr std::array<int32_t, 4> kVolumes = {8102, 8103, 8104, 8105};
constexpr std::array<int32_t, 4> kVolumeLabels = {8123, 8124, 8125, 8126};
// The four settings' bars (their names are in their pictures) and the
// boxes for their values, beside them.
constexpr int32_t kToolTips = 8107; // its pictures are named 3dacc, but show "TOOLTIPS"
constexpr int32_t kToolTipsText = 8160;
constexpr int32_t kCombat = 8106;
constexpr int32_t kCombatText = 8161;
constexpr int32_t kRandomEvents = 8108;
constexpr int32_t kRandomEventsText = 8162;
constexpr int32_t kAutosave = 8109;
constexpr int32_t kAutosaveText = 8163;
constexpr int32_t kStrategicLess = 8115;
constexpr int32_t kStrategicMore = 8114;
constexpr int32_t kStrategicLabel = 8116;
constexpr int32_t kStrategicText = 8117;
constexpr int32_t kTacticalLess = 8119;
constexpr int32_t kTacticalMore = 8118;
constexpr int32_t kTacticalLabel = 8120;
constexpr int32_t kTacticalText = 8121;

// A volume bar's cells unlit: stbof.res has it beside the lit vol-bar.tga
// the layout names, the same size, though no layout names it.
constexpr const char *kUnlitBar = "volbarof.tga";

// The settings' and timers' values aren't in their fonts' colours, as the
// names are: measured from a screenshot of the game.
constexpr Color kValueColor{24, 101, 189};
constexpr Color kTimerColor{173, 227, 115};

} // namespace

GameOptions optionsFromIni(const std::map<std::string, std::string> &ini) {
    GameOptions o;
    auto value = [&](const char *key) -> const std::string * {
        auto it = ini.find(key);
        return it == ini.end() || it->second.empty() ? nullptr : &it->second;
    };
    auto onOff = [&](const char *key, bool &out) {
        if (const std::string *v = value(key))
            out = *v != "OFF";
    };
    const char *const volumes[] = {"VOICEVOLUME", "VIDEOVOLUME", "MUSICVOLUME", "FXVOLUME"};
    for (size_t i = 0; i < o.volume.size(); ++i)
        if (const std::string *v = value(volumes[i]))
            o.volume[i] = std::clamp(std::atoi(v->c_str()), 0, GameOptions::kMaxVolume);
    onOff("TOOLTIPS", o.toolTips);
    onOff("AUTOSAVE", o.autosave);
    onOff("RANDOMEVENTS", o.randomEvents);
    // ABSTRACT is automatic combat; what manual is called there isn't known,
    // so anything else is taken as it.
    if (const std::string *v = value("TACTICAL"))
        o.manualCombat = *v != "ABSTRACT";
    return o;
}

Error OptionsScreen::load() {
    return loadLayouts("options.wdf", {kAccept, kCancel, kTitle, kVolumes[0], kVolumes[1], kVolumes[2], kVolumes[3],
                                       kToolTips, kCombat, kRandomEvents, kAutosave, kStrategicLess, kStrategicMore,
                                       kTacticalLess, kTacticalMore});
}

void OptionsScreen::click(int32_t id) {
    GameOptions &o = _options;
    switch (id) {
    case kToolTips:
        o.toolTips = !o.toolTips;
        break;
    case kCombat:
        o.manualCombat = !o.manualCombat;
        break;
    case kRandomEvents:
        o.randomEvents = !o.randomEvents;
        break;
    case kAutosave:
        o.autosave = !o.autosave;
        break;
    default:
        // The timers' arrows: "None" is the only choice known yet.
        break;
    }
}

void OptionsScreen::setVolume(int32_t slider, int x) {
    const Placed *p = find(slider);
    const int cells = p ? p->widget->units : 0;
    if (cells <= 0)
        return;
    // The cell under the mouse and those left of it are lit; left of the
    // first, none.
    const int cellW = p->widget->w / cells;
    const int lit = std::clamp((x - p->x) / cellW + (x >= p->x ? 1 : 0), 0, cells);
    for (size_t i = 0; i < kVolumes.size(); ++i)
        if (kVolumes[i] == slider)
            _options.volume[i] = (lit * GameOptions::kMaxVolume + cells / 2) / cells;
}

OptionsScreen::Result OptionsScreen::handle(const Event &e) {
    if (e.type == EventType::kKeyDown && (e.key == key::kReturn || e.key == key::kKpEnter))
        return Result::kAccepted;
    if (e.type == EventType::kKeyDown && e.key == key::kEscape)
        return Result::kCancelled;
    if (e.type == EventType::kMouseMove && _dragging) {
        setVolume(_dragging, e.mouseX);
        return Result::kOpen;
    }
    if (e.button != MouseButton::kLeft)
        return Result::kOpen;
    if (e.type == EventType::kMouseDown) {
        _pressed = buttonAt(e.mouseX, e.mouseY);
        for (const int32_t id : kVolumes)
            if (const Placed *p = find(id); p && p->contains(e.mouseX, e.mouseY)) {
                _dragging = id;
                setVolume(id, e.mouseX);
            }
    } else if (e.type == EventType::kMouseUp) {
        _dragging = 0;
        // A button acts when the mouse comes up on it, as Windows buttons do.
        const int32_t id = _pressed && buttonAt(e.mouseX, e.mouseY) == _pressed ? _pressed : 0;
        _pressed = 0;
        if (id == kAccept)
            return Result::kAccepted;
        if (id == kCancel)
            return Result::kCancelled;
        if (id)
            click(id);
    }
    return Result::kOpen;
}

void OptionsScreen::draw(Surface &screen) const {
    drawLayouts(screen);
    const GameOptions &o = _options;
    // The layout draws each bar lit; the cells past its volume are covered
    // with the unlit picture.
    for (size_t i = 0; i < kVolumes.size(); ++i) {
        const Placed *p = find(kVolumes[i]);
        const image::Image *unlit = _assets.picture(kUnlitBar);
        const int cells = p ? p->widget->units : 0;
        if (!unlit || cells <= 0)
            continue;
        const int cellW = p->widget->w / cells;
        // Rounded up: the game's default 100 shows 33 cells, not 32.
        const int lit = (o.volume[i] * cells + GameOptions::kMaxVolume - 1) / GameOptions::kMaxVolume;
        blitClipped(screen, *unlit, p->x, p->y, p->x + lit * cellW, p->y, p->widget->w - lit * cellW, p->widget->h);
    }

    auto text = [&](int32_t box, int entry, const char *english) {
        drawText(screen, box, word(entry, english), fontColor(box));
    };
    text(kTitle, lex::kVolumeControl, "VOLUME CONTROL");
    text(kVolumeLabels[0], lex::kVoice, "VOICE");
    text(kVolumeLabels[1], lex::kVideo, "VIDEO");
    text(kVolumeLabels[2], lex::kMusic, "MUSIC");
    text(kVolumeLabels[3], lex::kSoundEffects, "SFX");
    text(kStrategicLabel, lex::kStrategicTimer, "STRATEGIC TIMER");
    text(kTacticalLabel, lex::kTacticalTimer, "TACTICAL TIMER");
    auto value = [&](int32_t box, int entry, const char *english) {
        drawText(screen, box, word(entry, english), kValueColor);
    };
    // Random Events too shows YES or NO here, where the game setup screen
    // shows ON or OFF.
    auto yesNo = [&](int32_t box, bool yes) { yes ? value(box, lex::kYes, "YES") : value(box, lex::kNo, "NO"); };
    yesNo(kToolTipsText, o.toolTips);
    yesNo(kRandomEventsText, o.randomEvents);
    yesNo(kAutosaveText, o.autosave);
    o.manualCombat ? value(kCombatText, lex::kManual, "MANUAL") : value(kCombatText, lex::kAutomatic, "AUTOMATIC");
    // "None" as the entry has it, where the game setup screen shows "NONE".
    drawText(screen, kStrategicText, word(lex::kNone, "None"), kTimerColor);
    drawText(screen, kTacticalText, word(lex::kNone, "None"), kTimerColor);
}

} // namespace reluva::botf
