// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/options_screen.h"

#include <algorithm>

namespace reluva::botf {

namespace {

// options.wdf's widgets, matched to the manual's controls by their order on
// the screen.
constexpr int32_t kAccept = 8100;
constexpr int32_t kCancel = 8101;
constexpr int32_t kTitle = 8122;
// Voice, video, music, sound effects, as the manual lists them, top down.
constexpr std::array<int32_t, 4> kVolumes = {8102, 8103, 8104, 8105};
constexpr std::array<int32_t, 4> kVolumeLabels = {8123, 8124, 8125, 8126};
// The four settings' bars (their names are in their pictures) and the
// boxes for their values, beside them.
constexpr int32_t kAccelerated = 8107;
constexpr int32_t kAcceleratedText = 8160;
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

} // namespace

Error OptionsScreen::load() {
    return loadLayouts("options.wdf", {kAccept, kCancel, kTitle, kVolumes[0], kVolumes[1], kVolumes[2], kVolumes[3],
                                       kAccelerated, kCombat, kRandomEvents, kAutosave, kStrategicLess, kStrategicMore,
                                       kTacticalLess, kTacticalMore});
}

void OptionsScreen::click(int32_t id) {
    GameOptions &o = _options;
    switch (id) {
    case kAccelerated:
        o.accelerated3d = !o.accelerated3d;
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
        const int lit = (o.volume[i] * cells + GameOptions::kMaxVolume / 2) / GameOptions::kMaxVolume;
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
    auto yesNo = [&](int32_t box, bool yes) { yes ? text(box, lex::kYes, "YES") : text(box, lex::kNo, "NO"); };
    yesNo(kAcceleratedText, o.accelerated3d);
    yesNo(kAutosaveText, o.autosave);
    o.manualCombat ? text(kCombatText, lex::kManual, "MANUAL") : text(kCombatText, lex::kAutomatic, "AUTOMATIC");
    // As the game setup screen shows the same setting.
    o.randomEvents ? text(kRandomEventsText, lex::kOn, "ON") : text(kRandomEventsText, lex::kOff, "OFF");
    drawText(screen, kStrategicText, upperAscii(word(lex::kNone, "None")), fontColor(kStrategicText));
    drawText(screen, kTacticalText, upperAscii(word(lex::kNone, "None")), fontColor(kTacticalText));
}

} // namespace reluva::botf
