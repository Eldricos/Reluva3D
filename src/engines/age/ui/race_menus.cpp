// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The race screen's own menus: the race paused (Esc), with mmlang.dll's
// words for the game's in-race menu; the results screens after a race or
// lesson (jpg/rshi_bk "Event Results", crshi_bk "Crash Course Results",
// with the texture/RESULT_* buttons); and the menus' music (UI.sgt) and
// voices (aud/aud22/UI*.wav). See docs/engines/age-menus.md.

#include "engines/age/ui/screen.h"

#include "audio/decode.h"
#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;

// The pause menu: mmlang.dll's words.
enum PauseItem { kResume, kRestart, kReplay, kOptions, kQuitRace, kPauseItems };
constexpr int kPauseTop = 180, kPauseStep = 34;
// The results' buttons (texture/RESULT_*, four states 90 high).
enum ResultButton { kResRestart, kResNext, kResReplay, kResMenu, kResExit, kResultButtons };
const char *const kResultPictures[kResultButtons] = {"RESULT_RESTART", "RESULT_NEXTRACE", "RESULT_REPLAY",
                                                     "RESULT_RACEMENU", "RESULT_EXITW"};
// Where they go: down the results screens' right side, under the logo.
constexpr int kResultX = 410, kResultTop = 92, kResultStep = 76, kResultState = 90;
// The results' box (measured on jpg/rshi_bk and crshi_bk).
constexpr int kBoxX = 46, kBoxY = 74;

// The menus' voices: which control says which word (aud/aud22/UI*.wav).
const std::pair<const char *, const char *> kVoices[] = {
    {"main_sp", "UIraces"},    {"main_mp", "UImulti"},     {"main_rpl", "UIreplay"},
    {"race_rec", "UIrecords"}, {"race_veh", "UIvehicles"}, {"host_dn", "UIvehicles"},
    {"mnav_opt", "UIoptions"}, {"vesel_godrive", "UIgo"},  {"DRIVER NAME", "UIdriver"},
};

std::string clockText(float seconds) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(std::max(0.0f, seconds), 60.0f));
    return buf;
}

void darken(Surface &o, int x, int y, int w, int h, uint8_t alpha) {
    for (int yy = std::max(0, y); yy < std::min(o.h, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(o.w, x + w); ++xx) {
            uint8_t *p = o.row(yy) + xx * 4;
            p[0] = 10, p[1] = 6, p[2] = 40, p[3] = alpha;
        }
}

} // namespace

void RaceScreen::playVoice(const std::string &clip) {
    if (!_ctx.mixer)
        return;
    std::vector<uint8_t> data;
    if (_ctx.files.read("aud/aud22/" + clip + ".22k.wav", data) &&
        _ctx.files.read("aud/aud11/" + clip + ".11k.wav", data))
        return;
    auto sound = std::make_shared<audio::Sound>();
    if (!audio::decodeSound(data, *sound)) {
        _ctx.mixer->stopType(audio::SoundType::kSpeech);
        _ctx.mixer->play(sound, audio::SoundType::kSpeech);
    }
}

void RaceScreen::menuSounds() {
    // The menus' tune while they're up; a word as a button's pointed at.
    if (!_front || !_ctx.mixer)
        return;
    if (!_music.playingMenu())
        _music.startMenu(_ctx.files, *_ctx.mixer);
    const std::string focus = _netMenus ? std::string() : _front->focusedWidget();
    if (focus != _lastFocus) {
        _lastFocus = focus;
        for (const auto &[widget, clip] : kVoices)
            if (focus == widget)
                playVoice(clip);
    }
}

bool RaceScreen::nextRaceOpen() const {
    if (_raceIndex + 1 >= int(_raceNames.size()))
        return false;
    if (lesson() && _judge.result() != LessonJudge::Result::kPassed)
        return false;
    return raceIsOpen(_raceIndex + 1);
}

void RaceScreen::instantReplay(ReplayReturn back) {
    if (_recording.empty())
        return;
    _replayReturn = back;
    _replayOfThisRace = true;
    _replaySaved = false;
    _paused = _results = false;
    startReplay(_recording);
}

void RaceScreen::pauseEvent(const Event &ev) {
    int key = ev.key;
    if (ev.type == EventType::kMouseDown) {
        // A line clicked: as Enter on it.
        const int i = (ev.mouseY - kPauseTop + kPauseStep / 4) / kPauseStep;
        if (ev.mouseY < kPauseTop - kPauseStep / 4 || i >= kPauseItems || ev.mouseX < 100 || ev.mouseX >= kFrameW - 100)
            return;
        _pauseFocus = i;
        key = key::kReturn;
    } else if (ev.type != EventType::kKeyDown)
        return;
    switch (key) {
    case key::kUp:
        _pauseFocus = (_pauseFocus + kPauseItems - 1) % kPauseItems;
        break;
    case key::kDown:
        _pauseFocus = (_pauseFocus + 1) % kPauseItems;
        break;
    case key::kEscape:
        _paused = false;
        break;
    case key::kReturn:
    case key::kKpEnter:
        switch (_pauseFocus) {
        case kResume:
            _paused = false;
            break;
        case kRestart:
            _paused = false;
            _life.stop();
            startRace();
            break;
        case kReplay:
            instantReplay(ReplayReturn::kRace);
            break;
        case kOptions:
            if (_game)
                _game->open(GameMenus::Screen::kOptions);
            break;
        case kQuitRace:
            _paused = false;
            endRace();
            break;
        }
        break;
    default:
        break;
    }
}

void RaceScreen::resultsEvent(const Event &ev) {
    auto enabled = [&](int b) { return b != kResNext || nextRaceOpen(); };
    int key = ev.key;
    if (ev.type == EventType::kMouseDown) {
        // A button clicked: as Enter on it.
        const int b = (ev.mouseY - kResultTop) / kResultStep;
        if (ev.mouseY < kResultTop || b >= kResultButtons || !enabled(b) || ev.mouseX < kResultX ||
            ev.mouseX >= kResultX + 230)
            return;
        _resultsFocus = b;
        key = key::kReturn;
    } else if (ev.type != EventType::kKeyDown)
        return;
    switch (key) {
    case key::kUp:
    case key::kDown: {
        const int step = ev.key == key::kDown ? 1 : kResultButtons - 1;
        do
            _resultsFocus = (_resultsFocus + step) % kResultButtons;
        while (!enabled(_resultsFocus));
        break;
    }
    case 'r': // as in the lessons: R restarts
        _results = false;
        _life.stop();
        startRace();
        break;
    case key::kEscape:
        _results = false;
        endRace();
        break;
    case key::kReturn:
    case key::kKpEnter:
        switch (_resultsFocus) {
        case kResRestart:
            _results = false;
            _life.stop();
            startRace();
            break;
        case kResNext:
            if (nextRaceOpen()) {
                _results = false;
                _conditionsChanged = false; // the next race's own
                ++_raceIndex;
                _life.stop();
                startRace();
            }
            break;
        case kResReplay:
            instantReplay(ReplayReturn::kResults);
            break;
        case kResMenu:
            _results = false;
            endRace();
            break;
        case kResExit:
            if (_game)
                _game->open(GameMenus::Screen::kQuit);
            break;
        }
        break;
    default:
        break;
    }
}

void RaceScreen::drawPauseMenu() {
    // The race stopped behind, darkened; the game's in-race words.
    darken(_overlay, 0, 0, kFrameW, kFrameH, 140);
    const uint32_t ids[kPauseItems] = {473, uint32_t(lesson() ? 653 : 464), 465, 466, 468};
    const char *const fallback[kPauseItems] = {"Resume Driving", lesson() ? "Restart Lesson" : "Restart Race",
                                               "Instant Replay", "Options", "Quit to Race Menu"};
    drawCentred(_strings.get(463, "MAIN MENU"), 120, {255, 230, 140}, 3);
    for (int i = 0; i < kPauseItems; ++i)
        drawCentred(_strings.get(ids[i], fallback[i]), kPauseTop + i * kPauseStep,
                    i == _pauseFocus ? Color{255, 255, 120} : Color{220, 220, 255}, 2);
}

void RaceScreen::drawResults() {
    // The game's results screen: who finished where (or the lesson's
    // verdict) in its box, its buttons down the right.
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    if (!_menuPictures)
        _menuPictures = std::make_unique<MenuPictures>(_ctx.files);
    MenuPictures &pictures = *_menuPictures;
    if (const image::Image *bg = pictures.jpg(lesson() ? "crshi_bk" : "rshi_bk"))
        drawPicture(_overlay, *bg, 0, 0, kFrameW, kFrameH);
    int y = kBoxY;
    font::drawText(_overlay, kBoxX, y, _race.name, {255, 230, 140}, 2);
    y += 24;
    font::drawText(_overlay, kBoxX, y, raceKindName(_race.kind) + std::string(", ") + _cities[size_t(_city)].name,
                   {180, 200, 255});
    y += 20;
    font::drawText(_overlay, kBoxX, y, _outcome, {255, 255, 120});
    y += 24;
    char line[120];
    if (lesson()) {
        const bool passed = _judge.result() == LessonJudge::Result::kPassed;
        font::drawText(_overlay, kBoxX, y, passed ? _strings.get(651, "Pass") : "Failed",
                       passed ? Color{120, 255, 120} : Color{255, 120, 120}, 2);
        y += 26;
        std::snprintf(line, sizeof line, "Time %s", clockText(std::max(0.0f, _clock)).c_str());
        font::drawText(_overlay, kBoxX, y, line, {230, 230, 230});
        if (_judge.penalty() > 0) {
            std::snprintf(line, sizeof line, "Penalty %.0f s", _judge.penalty());
            font::drawText(_overlay, kBoxX, y + 16, line, {255, 160, 120});
        }
    } else {
        std::vector<const Racer *> order;
        for (const Racer &r : _racers)
            order.push_back(&r);
        std::stable_sort(order.begin(), order.end(),
                         [&](const Racer *a, const Racer *b) { return progressOf(*a) > progressOf(*b); });
        // The rows come in one a second, as the game's do; its opponents
        // go by their places on the grid ("Opponent 1"...: strings 13-20).
        const size_t shown = size_t(_resultsTime) + 1;
        for (size_t i = 0; i < order.size() && i < shown && y < kBoxY + 290; ++i) {
            const Racer &r = *order[i];
            const size_t slot = size_t(order[i] - _racers.data());
            const std::string name = r.player || _netGame || slot < 1 || slot > 8
                                         ? r.name
                                         : _strings.get(uint32_t(12 + slot), "Opponent " + std::to_string(slot));
            std::snprintf(line, sizeof line, "%zu. %-30.30s %s", i + 1, name.c_str(),
                          r.finishTime >= 0 ? clockText(r.finishTime).c_str() : _strings.get(499, "DNF").c_str());
            font::drawText(_overlay, kBoxX, y, line, r.player ? Color{255, 255, 120} : Color{230, 230, 230});
            y += 16;
        }
    }
    if (!_beatenAll.empty())
        font::drawText(_overlay, kBoxX, kBoxY + 290, _beatenAll, {120, 255, 120});
    // What the race opened, in the box beside the description (as on the
    // game's screens), wrapped.
    if (!_unlockMessage.empty()) {
        constexpr int kX = 290, kY = 404;
        constexpr size_t kWide = 44;
        std::istringstream words(_unlockMessage);
        std::string word, text;
        int lineY = kY;
        while (words >> word) {
            if (!text.empty() && text.size() + 1 + word.size() > kWide) {
                font::drawText(_overlay, kX, lineY, text, {255, 255, 120});
                lineY += font::kLineHeight + 2;
                text.clear();
            }
            text += (text.empty() ? "" : " ") + word;
        }
        if (!text.empty())
            font::drawText(_overlay, kX, lineY, text, {255, 255, 120});
    }
    // Bottom left, the event's description, or the lesson's.
    const std::string &map = _cities[size_t(_city)].map;
    const std::string desc = lesson() ? (map == "london" ? "lon_cc" : "sf_cc") + std::to_string(_raceIndex)
                             : _race.kind == RaceKind::kBlitz   ? "race_btz"
                             : _race.kind == RaceKind::kCircuit ? "race_cir"
                                                                : "race_cp";
    if (const image::Image *d = pictures.jpg(desc))
        drawPicture(_overlay, *d, 40, 396);
    for (int b = 0; b < kResultButtons; ++b) {
        const image::Image *im = pictures.texture(kResultPictures[b]);
        if (!im)
            continue;
        const bool enabled = b != kResNext || nextRaceOpen();
        const int state = !enabled ? 3 : b == _resultsFocus ? 1 : 0;
        drawButtonState(_overlay, *im, kResultX + (230 - im->w), kResultTop + b * kResultStep, im->h / kResultState,
                        state);
    }
}

} // namespace reluva::age
