// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Race page's own menus: the race paused (Esc), with mmlang.dll's
// words for the game's in-race menu; the results screens after a race or
// lesson (jpg/rshi_bk "Event Results", crshi_bk "Crash Course Results",
// with the texture/RESULT_* buttons); and the menus' music (UI.sgt) and
// voices (aud/aud22/UI*.wav). See docs/engines/age-menus.md.

#include "engines/age/viewers.h"

#include "common/audio/decode.h"
#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;

// The pause menu: mmlang.dll's words.
enum PauseItem { kResume, kRestart, kReplay, kOptions, kQuitRace, kPauseItems };
// The results' buttons (texture/RESULT_*, four states 90 high).
enum ResultButton { kResRestart, kResNext, kResReplay, kResMenu, kResExit, kResultButtons };
const char* const kResultPictures[kResultButtons] = {"RESULT_RESTART", "RESULT_NEXTRACE", "RESULT_REPLAY",
                                                     "RESULT_RACEMENU", "RESULT_EXITW"};
// Where they go: down the results screens' right side, under the logo.
constexpr int kResultX = 410, kResultTop = 92, kResultStep = 76, kResultState = 90;
// The results' box (measured on jpg/rshi_bk and crshi_bk).
constexpr int kBoxX = 46, kBoxY = 74;

// The menus' voices: which control says which word (aud/aud22/UI*.wav).
const std::pair<const char*, const char*> kVoices[] = {
    {"main_sp", "UIraces"},    {"main_mp", "UImulti"},     {"main_rpl", "UIreplay"},
    {"race_rec", "UIrecords"}, {"race_veh", "UIvehicles"}, {"host_dn", "UIvehicles"},
    {"mnav_opt", "UIoptions"}, {"vesel_godrive", "UIgo"},  {"DRIVER NAME", "UIdriver"},
};

std::string clockText(float seconds) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(std::max(0.0f, seconds), 60.0f));
    return buf;
}

void darken(Surface& o, int x, int y, int w, int h, uint8_t alpha) {
    for (int yy = std::max(0, y); yy < std::min(o.h, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(o.w, x + w); ++xx) {
            uint8_t* p = o.row(yy) + xx * 4;
            p[0] = 10, p[1] = 6, p[2] = 40, p[3] = alpha;
        }
}

} // namespace

void RaceViewer::playVoice(const std::string& clip) {
    if (!ctx_.mixer)
        return;
    std::vector<uint8_t> data;
    if (ctx_.files.read("aud/aud22/" + clip + ".22k.wav", data) &&
        ctx_.files.read("aud/aud11/" + clip + ".11k.wav", data))
        return;
    auto sound = std::make_shared<audio::Sound>();
    if (!audio::decodeSound(data, *sound)) {
        ctx_.mixer->stopType(audio::SoundType::Speech);
        ctx_.mixer->play(sound, audio::SoundType::Speech);
    }
}

void RaceViewer::menuSounds() {
    // The menus' tune while they're up; a word as a button's pointed at.
    if (!front_ || !ctx_.mixer)
        return;
    if (!music_.playingMenu())
        music_.startMenu(ctx_.files, *ctx_.mixer);
    const std::string focus = netMenus_ ? std::string() : front_->focusedWidget();
    if (focus != lastFocus_) {
        lastFocus_ = focus;
        for (const auto& [widget, clip] : kVoices)
            if (focus == widget)
                playVoice(clip);
    }
}

bool RaceViewer::nextRaceOpen() const {
    if (raceIndex_ + 1 >= int(raceNames_.size()))
        return false;
    if (lesson() && judge_.result() != LessonJudge::Result::Passed)
        return false;
    return raceIsOpen(raceIndex_ + 1);
}

void RaceViewer::instantReplay(ReplayReturn back) {
    if (recording_.empty())
        return;
    replayReturn_ = back;
    replayOfThisRace_ = true;
    replaySaved_ = false;
    paused_ = results_ = false;
    startReplay(recording_);
}

void RaceViewer::pauseEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    switch (ev.key) {
    case key::Up:
        pauseFocus_ = (pauseFocus_ + kPauseItems - 1) % kPauseItems;
        break;
    case key::Down:
        pauseFocus_ = (pauseFocus_ + 1) % kPauseItems;
        break;
    case key::Escape:
        paused_ = false;
        break;
    case key::Return:
    case key::KpEnter:
        switch (pauseFocus_) {
        case kResume:
            paused_ = false;
            break;
        case kRestart:
            paused_ = false;
            life_.stop();
            startRace();
            break;
        case kReplay:
            instantReplay(ReplayReturn::Race);
            break;
        case kOptions:
            if (game_)
                game_->open(GameMenus::Screen::Options);
            break;
        case kQuitRace:
            paused_ = false;
            endRace();
            break;
        }
        break;
    default:
        break;
    }
}

void RaceViewer::resultsEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    auto enabled = [&](int b) { return b != kResNext || nextRaceOpen(); };
    switch (ev.key) {
    case key::Up:
    case key::Down: {
        const int step = ev.key == key::Down ? 1 : kResultButtons - 1;
        do
            resultsFocus_ = (resultsFocus_ + step) % kResultButtons;
        while (!enabled(resultsFocus_));
        break;
    }
    case 'r': // as in the lessons: R restarts
        results_ = false;
        life_.stop();
        startRace();
        break;
    case key::Escape:
        results_ = false;
        endRace();
        break;
    case key::Return:
    case key::KpEnter:
        switch (resultsFocus_) {
        case kResRestart:
            results_ = false;
            life_.stop();
            startRace();
            break;
        case kResNext:
            if (nextRaceOpen()) {
                results_ = false;
                conditionsChanged_ = false; // the next race's own
                ++raceIndex_;
                life_.stop();
                startRace();
            }
            break;
        case kResReplay:
            instantReplay(ReplayReturn::Results);
            break;
        case kResMenu:
            results_ = false;
            endRace();
            break;
        case kResExit:
            if (game_)
                game_->open(GameMenus::Screen::Quit);
            break;
        }
        break;
    default:
        break;
    }
}

void RaceViewer::drawPauseMenu() {
    // The race stopped behind, darkened; the game's in-race words.
    darken(overlay_, 0, 0, kFrameW, kFrameH, 140);
    const uint32_t ids[kPauseItems] = {473, uint32_t(lesson() ? 653 : 464), 465, 466, 468};
    const char* const fallback[kPauseItems] = {"Resume Driving", lesson() ? "Restart Lesson" : "Restart Race",
                                               "Instant Replay", "Options", "Quit to Race Menu"};
    drawCentred(strings_.get(463, "MAIN MENU"), 120, {255, 230, 140}, 3);
    for (int i = 0; i < kPauseItems; ++i)
        drawCentred(strings_.get(ids[i], fallback[i]), 180 + i * 34,
                    i == pauseFocus_ ? Color{255, 255, 120} : Color{220, 220, 255}, 2);
}

void RaceViewer::drawResults() {
    // The game's results screen: who finished where (or the lesson's
    // verdict) in its box, its buttons down the right.
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    if (!menuPictures_)
        menuPictures_ = std::make_unique<MenuPictures>(ctx_.files);
    MenuPictures& pictures = *menuPictures_;
    if (const image::Image* bg = pictures.jpg(lesson() ? "crshi_bk" : "rshi_bk"))
        drawPicture(overlay_, *bg, 0, 0, kFrameW, kFrameH);
    int y = kBoxY;
    font::drawText(overlay_, kBoxX, y, race_.name, {255, 230, 140}, 2);
    y += 24;
    font::drawText(overlay_, kBoxX, y, raceKindName(race_.kind) + std::string(", ") + cities_[size_t(city_)].name,
                   {180, 200, 255});
    y += 20;
    font::drawText(overlay_, kBoxX, y, outcome_, {255, 255, 120});
    y += 24;
    char line[120];
    if (lesson()) {
        const bool passed = judge_.result() == LessonJudge::Result::Passed;
        font::drawText(overlay_, kBoxX, y, passed ? strings_.get(651, "Pass") : "Failed",
                       passed ? Color{120, 255, 120} : Color{255, 120, 120}, 2);
        y += 26;
        std::snprintf(line, sizeof line, "Time %s", clockText(std::max(0.0f, clock_)).c_str());
        font::drawText(overlay_, kBoxX, y, line, {230, 230, 230});
        if (judge_.penalty() > 0) {
            std::snprintf(line, sizeof line, "Penalty %.0f s", judge_.penalty());
            font::drawText(overlay_, kBoxX, y + 16, line, {255, 160, 120});
        }
    } else {
        std::vector<const Racer*> order;
        for (const Racer& r : racers_)
            order.push_back(&r);
        std::stable_sort(order.begin(), order.end(),
                         [&](const Racer* a, const Racer* b) { return progressOf(*a) > progressOf(*b); });
        for (size_t i = 0; i < order.size() && y < kBoxY + 290; ++i) {
            const Racer& r = *order[i];
            std::snprintf(line, sizeof line, "%zu. %-30.30s %s", i + 1, r.name.c_str(),
                          r.finishTime >= 0 ? clockText(r.finishTime).c_str() : strings_.get(499, "DNF").c_str());
            font::drawText(overlay_, kBoxX, y, line, r.player ? Color{255, 255, 120} : Color{230, 230, 230});
            y += 16;
        }
    }
    if (!beatenAll_.empty())
        font::drawText(overlay_, kBoxX, kBoxY + 290, beatenAll_, {120, 255, 120});
    // Bottom left, the event's description, or the lesson's.
    const std::string& map = cities_[size_t(city_)].map;
    const std::string desc = lesson() ? (map == "london" ? "lon_cc" : "sf_cc") + std::to_string(raceIndex_)
                             : race_.kind == RaceKind::Blitz   ? "race_btz"
                             : race_.kind == RaceKind::Circuit ? "race_cir"
                                                               : "race_cp";
    if (const image::Image* d = pictures.jpg(desc))
        drawPicture(overlay_, *d, 40, 396);
    for (int b = 0; b < kResultButtons; ++b) {
        const image::Image* im = pictures.texture(kResultPictures[b]);
        if (!im)
            continue;
        const bool enabled = b != kResNext || nextRaceOpen();
        const int state = !enabled ? 3 : b == resultsFocus_ ? 1 : 0;
        drawButtonState(overlay_, *im, kResultX + (230 - im->w), kResultTop + b * kResultStep, im->h / kResultState,
                        state);
    }
}

} // namespace reluva::age
