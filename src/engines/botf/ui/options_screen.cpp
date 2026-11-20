// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/options_screen.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <set>
#include <utility>

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

// Each setting's list of choices, opened by clicking it: in the empty
// picture on its row (a screenshot of Tool Tips' shows it there), a
// background with a bar along its top, and the choices' buttons under it,
// the one set lit. Tactical combat has its own background, by its name.
struct SettingList {
    int32_t setting;
    int32_t place;
    const char *background;
    // Each choice: its lit and its plain picture. The first is "yes" (or
    // manual combat).
    std::array<std::pair<const char *, const char *>, 2> choices;
};
const std::array<SettingList, 4> kLists = {{
    {kToolTips, 8111, "asavebkg.tga", {{{"yes_on.tga", "yestxtop.tga"}, {"no_on.tga", "notxtop.tga"}}}},
    {kCombat, 8110, "tactbkg.tga", {{{"man_on.tga", "man-txt.tga"}, {"auto_on.tga", "autotxto.tga"}}}},
    {kRandomEvents, 8112, "asavebkg.tga", {{{"yes_on.tga", "yestxtop.tga"}, {"no_on.tga", "notxtop.tga"}}}},
    {kAutosave, 8113, "asavebkg.tga", {{{"yes_on.tga", "yestxtop.tga"}, {"no_on.tga", "notxtop.tga"}}}},
}};
// The choices' place in the list, from its corner, and their size and
// spacing (measured on the screenshot).
constexpr int kChoiceX = 20;
constexpr int kChoiceY = 36;
constexpr int kChoiceStep = 32;
constexpr int kChoiceW = 124;
constexpr int kChoiceH = 28;

const SettingList *listOf(int32_t setting) {
    for (const SettingList &l : kLists)
        if (l.setting == setting)
            return &l;
    return nullptr;
}

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
    // A setting opens its list of choices, or closes it if it's open. The
    // timers' arrows do nothing: "None" is the only choice known yet.
    if (listOf(id))
        _list = _list == id ? 0 : id;
}

bool OptionsScreen::listChoice(int x, int y) {
    const SettingList *list = listOf(_list);
    const Placed *place = list ? find(list->place) : nullptr;
    if (!place)
        return false;
    for (int i = 0; i < 2; ++i) {
        const int cx = place->x + kChoiceX;
        const int cy = place->y + kChoiceY + i * kChoiceStep;
        if (x < cx || y < cy || x >= cx + kChoiceW || y >= cy + kChoiceH)
            continue;
        GameOptions &o = _options;
        bool *setting = _list == kToolTips       ? &o.toolTips
                        : _list == kCombat       ? &o.manualCombat
                        : _list == kRandomEvents ? &o.randomEvents
                                                 : &o.autosave;
        *setting = i == 0;
        return true;
    }
    return false;
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
        // With a list open, a click chooses from it, or anywhere else closes
        // it (and does what it would have done).
        if (_list) {
            const int32_t list = _list;
            if (listChoice(e.mouseX, e.mouseY)) {
                _list = 0;
                sound(UiSound::kClick);
                return Result::kOpen;
            }
            if (buttonAt(e.mouseX, e.mouseY) != list)
                _list = 0;
        }
        _pressed = buttonAt(e.mouseX, e.mouseY);
        for (const int32_t id : kVolumes)
            if (const Placed *p = find(id); p && p->contains(e.mouseX, e.mouseY)) {
                _dragging = id;
                setVolume(id, e.mouseX);
            }
    } else if (e.type == EventType::kMouseUp) {
        // Letting go of a volume plays its sample (the manual; a recording
        // of the game): voice, video and effects, not music, which plays.
        if (_dragging == kVolumes[0])
            sound(UiSound::kVoiceVolume);
        else if (_dragging == kVolumes[1])
            sound(UiSound::kVideoVolume);
        else if (_dragging == kVolumes[3])
            sound(UiSound::kEffectsVolume);
        _dragging = 0;
        // A button acts when the mouse comes up on it, as Windows buttons do.
        const int32_t id = _pressed && buttonAt(e.mouseX, e.mouseY) == _pressed ? _pressed : 0;
        _pressed = 0;
        if (id == kAccept)
            return Result::kAccepted;
        if (id == kCancel)
            return Result::kCancelled;
        if (id) {
            sound(UiSound::kClick);
            click(id);
        }
    }
    return Result::kOpen;
}

void OptionsScreen::drawList(Surface &screen) const {
    const SettingList *list = listOf(_list);
    const Placed *place = list ? find(list->place) : nullptr;
    if (!place)
        return;
    if (const image::Image *bg = _assets.picture(list->background))
        blitClipped(screen, *bg, place->x, place->y, place->x, place->y, bg->w, bg->h);
    const GameOptions &o = _options;
    const bool first = _list == kToolTips       ? o.toolTips
                       : _list == kCombat       ? o.manualCombat
                       : _list == kRandomEvents ? o.randomEvents
                                                : o.autosave;
    for (int i = 0; i < 2; ++i) {
        const auto &[lit, plain] = list->choices[size_t(i)];
        const int x = place->x + kChoiceX;
        const int y = place->y + kChoiceY + i * kChoiceStep;
        if (const image::Image *img = _assets.picture((i == 0) == first ? lit : plain))
            blitClipped(screen, *img, x, y, x, y, kChoiceW, kChoiceH);
    }
}

void OptionsScreen::draw(Surface &screen) const {
    // The setting whose list is open shows its bar's lit picture.
    std::set<int32_t> lit;
    if (_list)
        lit.insert(_list);
    drawLayouts(screen, lit);
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
    drawList(screen); // over the values, as in the game
}

} // namespace reluva::botf
