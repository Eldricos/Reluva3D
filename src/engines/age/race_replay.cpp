// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Race page's replays: the race being driven is recorded frame by
// frame (its time and the keys); a replay runs the race again from the
// recording, with the game's replay controls (Play, Step, Rew, Stop, Cam,
// Save, Quit: mmlang.dll's REPLAY MENU) and a roadside camera besides
// the car's own. See docs/engines/age-racing.md.

#include "engines/age/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

constexpr int kFrameW = 640, kFrameH = 480;
constexpr int kMaxStepsPerUpdate = 16; // a replay catching up after a slow frame
constexpr float kTvAhead = 45, kTvSide = 10, kTvHeight = 4, kTvLeave = 70, kTvFov = 45;
constexpr float kDiverged = 0.05f; // metres from where the recording had the car

// The bar's buttons, in mmlang.dll's REPLAY MENU words.
enum Button { kPlay, kStep, kRew, kCam, kSave, kQuit, kButtons };

std::string clockText(float seconds) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(std::max(0.0f, seconds), 60.0f));
    return buf;
}

} // namespace

void RaceViewer::recordFrame(float dt) {
    if (netGame_)
        return;
    ReplayFrame f;
    f.dt = dt;
    f.keys = uint16_t(keys_ & 0xffff);
    f.shift = int8_t(shift_);
    f.flags = manual_ ? kReplayManual : 0;
    recording_.frames.push_back(f);
}

void RaceViewer::markRecording() {
    if (netGame_ || racers_.empty() || recording_.frames.size() % Replay::kMarkEvery != 0)
        return;
    recording_.marks.push_back(racers_.front().car->sim.position);
}

std::string RaceViewer::replayDefaultName() const {
    std::string car = playback_.car;
    for (const CarInfo& c : cars_)
        if (c.base == playback_.car)
            car = c.description;
    return race_.name + " (" + car + ")";
}

void RaceViewer::startReplay(Replay replay) {
    if (replay.empty())
        return;
    // The race as it was started, then run again from its frames.
    playback_ = std::move(replay);
    int city = -1, kind = -1, car = -1;
    for (size_t i = 0; i < cities_.size(); ++i)
        if (cities_[i].map == playback_.city)
            city = int(i);
    for (int k = 0; k < 4; ++k)
        if (int(racePageKind(k)) == playback_.kind)
            kind = k;
    for (size_t i = 0; i < cars_.size(); ++i)
        if (cars_[i].base == playback_.car)
            car = int(i);
    if (city < 0 || kind < 0 || car < 0) {
        error_ = "This replay's race or car isn't in this game";
        return;
    }
    replaying_ = true;
    if (city_ != city) {
        city_ = city;
        loadCity();
    }
    kind_ = kind;
    raceIndex_ = playback_.race;
    level_ = playback_.level;
    car_ = car;
    paint_ = playback_.paint;
    manual_ = (playback_.frames.front().flags & kReplayManual) != 0;
    raceNames_ = raceNames(ctx_.files, cities_[size_t(city_)].map, racePageKind(kind_));
    life_.stop();
    startRace();
    if (state_ != State::Countdown) {
        replaying_ = false;
        return;
    }
    replayFrame_ = 0;
    replayAccum_ = 0;
    replayPaused_ = false;
    replayNote_.clear();
    replayCam_ = int(cameras_.size()); // the roadside camera first
    replayFocus_ = kPlay;
    tvSet_ = false;
}

void RaceViewer::playFrame() {
    if (replayFrame_ >= playback_.frames.size())
        return;
    const ReplayFrame& f = playback_.frames[replayFrame_];
    keys_ = f.keys;
    shift_ = f.shift;
    manual_ = (f.flags & kReplayManual) != 0;
    stepRace(f.dt);
    ++replayFrame_;
    // Where the recording had the car: a replay that's gone its own way
    // (Reluva changed since it was saved) says so, once.
    if (replayFrame_ % Replay::kMarkEvery == 0 && replayNote_.empty() && !racers_.empty()) {
        const size_t mark = replayFrame_ / Replay::kMarkEvery - 1;
        if (mark < playback_.marks.size() &&
            length(racers_.front().car->sim.position - playback_.marks[mark]) > kDiverged)
            replayNote_ = "This replay has gone its own way from " + clockText(clock_) +
                          ": Reluva has changed since it was saved";
    }
}

void RaceViewer::stepReplay(float dt) {
    if (replayPaused_)
        return;
    replayAccum_ += dt;
    for (int n = 0; n < kMaxStepsPerUpdate && replayFrame_ < playback_.frames.size() &&
                    replayAccum_ >= playback_.frames[replayFrame_].dt;
         ++n) {
        replayAccum_ -= playback_.frames[replayFrame_].dt;
        playFrame();
    }
    if (replayFrame_ >= playback_.frames.size())
        replayPaused_ = true; // the end: stopped there
}

void RaceViewer::endReplay() {
    replayOfThisRace_ = false;
    replayQuitting_ = false;
    if (replayReturn_ == ReplayReturn::Menu) {
        replaying_ = false;
        keys_ = 0;
        endRace();
        return;
    }
    // Back to the race (or its results): run on to where it was left,
    // which is where it is when the recording's all played.
    while (replayFrame_ < playback_.frames.size())
        playFrame();
    replaying_ = false;
    keys_ = 0;
    if (replayReturn_ == ReplayReturn::Results)
        results_ = true;
    else
        paused_ = true;
    replayReturn_ = ReplayReturn::Menu;
}

void RaceViewer::replayButton(int which) {
    switch (which) {
    case kPlay:
        if (replayFrame_ >= playback_.frames.size()) {
            startReplay(playback_); // from the start again
            return;
        }
        replayPaused_ = !replayPaused_;
        break;
    case kStep:
        replayPaused_ = true;
        playFrame();
        break;
    case kRew:
        startReplay(playback_);
        break;
    case kCam:
        replayCam_ = (replayCam_ + 1) % (int(cameras_.size()) + 1);
        cameraReset_ = true;
        tvSet_ = false;
        break;
    case kSave:
        if (game_)
            game_->nameReplay(playback_.name.empty() ? replayDefaultName() : playback_.name);
        break;
    case kQuit:
        // The race just driven: asked whether to keep it (the game's REPLAY
        // SAVE MENU).
        if (replayOfThisRace_ && !replaySaved_ && game_) {
            replayQuitting_ = true;
            game_->askToSaveReplay(replayDefaultName());
        } else
            endReplay();
        break;
    default:
        break;
    }
}

bool RaceViewer::replayEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return true;
    switch (ev.key) {
    case key::Left:
        replayFocus_ = (replayFocus_ + kButtons - 1) % kButtons;
        break;
    case key::Right:
        replayFocus_ = (replayFocus_ + 1) % kButtons;
        break;
    case key::Return:
    case key::KpEnter:
        replayButton(replayFocus_);
        break;
    case key::Space:
        replayButton(kPlay);
        break;
    case key::Backspace:
        replayButton(kRew);
        break;
    case '.':
        replayButton(kStep);
        break;
    case key::Escape:
        replayButton(kQuit);
        break;
    default:
        if (controlOf(ev.key) == Control::ChangeCamera)
            replayButton(kCam);
        break;
    }
    return true;
}

void RaceViewer::gameMenuEvent(const Event& ev) {
    switch (game_->handle(ev)) {
    case GameMenus::Action::Quit:
        quit_ = true;
        break;
    case GameMenus::Action::LoadReplay: {
        Replay r;
        if (Error err = loadReplay(game_->replayFile(), r)) {
            error_ = err.message;
            break;
        }
        replayOfThisRace_ = false;
        replaySaved_ = true;
        startReplay(std::move(r));
        break;
    }
    case GameMenus::Action::SaveReplay: {
        playback_.name = game_->replayName();
        if (Error err = saveReplay(ctx_.savePath, playback_))
            replayNote_ = err.message;
        else {
            replayNote_ = "Saved as \"" + playback_.name + "\"";
            replaySaved_ = true;
        }
        if (replayQuitting_)
            endReplay();
        break;
    }
    case GameMenus::Action::Closed:
        if (replayQuitting_)
            endReplay(); // not saved
        break;
    default:
        break;
    }
}

CameraView RaceViewer::replayCamera(float dt, float& fov) {
    const CarSim& sim = racers_.front().car->sim;
    if (replayCam_ < int(cameras_.size())) {
        const CarCamera& cam = cameras_[size_t(replayCam_)];
        fov = cam.fov;
        return updateCarCamera(cam, sim, dt, cameraPos_, cameraReset_);
    }
    // The roadside camera: set down ahead of the car, to one side, looking
    // at it as it comes and goes; another once it's well past.
    Vec3 forward = -sim.orientation.z;
    forward[1] = 0;
    forward = normalized(forward);
    if (!tvSet_ || length(sim.position - tvAt_) > kTvLeave) {
        tvSide_ = -tvSide_;
        const Vec3 right{-forward[2], 0, forward[0]};
        Vec3 at = sim.position + forward * kTvAhead + right * (kTvSide * tvSide_);
        CollisionWorld::RayHit hit;
        if (world_.raycast(at + Vec3{0, 30, 0}, {0, -1, 0}, 60, hit))
            at[1] = hit.point[1];
        tvAt_ = at + Vec3{0, kTvHeight, 0};
        tvSet_ = true;
    }
    fov = kTvFov;
    CameraView v;
    v.eye = tvAt_;
    v.target = sim.position + Vec3{0, 1, 0};
    return v;
}

void RaceViewer::drawReplayBar() {
    // Below the race's own lines: the game's REPLAY MENU and its buttons,
    // the time.
    const int y = 88;
    for (int r = y - 4; r < y + 22; ++r)
        for (int x = 0; x < kFrameW; ++x) {
            uint8_t* p = overlay_.row(r) + x * 4;
            p[0] = 20, p[1] = 10, p[2] = 60, p[3] = 200;
        }
    font::drawText(overlay_, 6, y + 4, strings_.get(483, "REPLAY MENU"), {255, 230, 140});
    const char* const fallback[kButtons] = {"Play", "Step", "Rew", "Cam", "Save", "Quit"};
    const uint32_t ids[kButtons] = {484, 485, 486, 488, 489, 490};
    int x = 110;
    for (int b = 0; b < kButtons; ++b) {
        std::string word = b == kPlay && !replayPaused_ ? strings_.get(487, "Stop") : strings_.get(ids[b], fallback[b]);
        const bool focused = b == replayFocus_;
        font::drawText(overlay_, x, y + 4, focused ? "[" + word + "]" : " " + word + " ",
                       focused ? Color{255, 255, 120} : Color{220, 220, 255});
        x += int(word.size() + 3) * font::kAdvance;
    }
    font::drawText(overlay_, kFrameW - 110, y + 4,
                   clockText(std::max(0.0f, clock_)) + " / " + clockText(playback_.duration() - 3), {255, 255, 255});
    if (!replayNote_.empty())
        font::drawText(overlay_, 6, y + 26, replayNote_, {255, 200, 120});
}

} // namespace reluva::age
