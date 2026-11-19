// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The race screen's replays: the race being driven is recorded frame by
// frame (its time and the keys); a replay runs the race again from the
// recording, with the game's replay controls (Play, Step, Rew, Stop, Cam,
// Save, Quit: mmlang.dll's REPLAY MENU) and a roadside camera besides
// the car's own. See docs/engines/age-racing.md.

#include "engines/age/ui/screen.h"

#include "graphics/font.h"

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
// The bar: its top, and where its first word starts.
constexpr int kBarY = 88, kBarFirstX = 110;
constexpr double kRewind = 5; // seconds Rew goes back

std::string clockText(float seconds) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", int(seconds) / 60, std::fmod(std::max(0.0f, seconds), 60.0f));
    return buf;
}

} // namespace

void RaceScreen::recordFrame(float dt) {
    if (_netGame)
        return;
    ReplayFrame f;
    f.dt = dt;
    f.keys = uint16_t(_keys & 0xffff);
    f.shift = int8_t(_shift);
    f.flags = _manual ? kReplayManual : 0;
    f.padSteer = int8_t(std::lround(_pad.steer * 127));
    f.padThrottle = uint8_t(std::lround(_pad.throttle * 255));
    f.padBrake = uint8_t(std::lround(_pad.brake * 255));
    _recording.frames.push_back(f);
}

void RaceScreen::markRecording() {
    if (_netGame || _racers.empty() || _recording.frames.size() % Replay::kMarkEvery != 0)
        return;
    _recording.marks.push_back(_racers.front().car->sim.position);
}

std::string RaceScreen::replayDefaultName() const {
    std::string car = _playback.car;
    for (const CarInfo &c : _cars)
        if (c.base == _playback.car)
            car = c.description;
    return _race.name + " (" + car + ")";
}

void RaceScreen::startReplay(Replay replay) {
    if (replay.empty())
        return;
    // The race as it was started, then run again from its frames.
    _playback = std::move(replay);
    int city = -1, kind = -1, car = -1;
    for (size_t i = 0; i < _cities.size(); ++i)
        if (_cities[i].map == _playback.city)
            city = int(i);
    for (int k = 0; k < kRacePageKinds; ++k)
        if (int(racePageKind(k)) == _playback.kind)
            kind = k;
    for (size_t i = 0; i < _cars.size(); ++i)
        if (_cars[i].base == _playback.car)
            car = int(i);
    if (city < 0 || kind < 0 || car < 0) {
        _error = "This replay's race or car isn't in this game";
        return;
    }
    _replaying = true;
    if (_city != city) {
        _city = city;
        loadCity();
    }
    _kind = kind;
    _raceIndex = _playback.race;
    _level = _playback.level;
    _car = car;
    _paint = _playback.paint;
    _manual = (_playback.frames.front().flags & kReplayManual) != 0;
    _raceNames = raceNames(_ctx.files, _cities[size_t(_city)].map, racePageKind(_kind));
    _life.stop();
    startRace();
    if (_state != State::kCountdown) {
        _replaying = false;
        return;
    }
    _replayFrame = 0;
    _replayAccum = 0;
    _replayPaused = false;
    _replayNote.clear();
    _replayCam = int(_cameras.size()); // the roadside camera first
    _replayFocus = kPlay;
    _tvSet = false;
}

void RaceScreen::playFrame() {
    if (_replayFrame >= _playback.frames.size())
        return;
    const ReplayFrame &f = _playback.frames[_replayFrame];
    _keys = f.keys;
    _shift = f.shift;
    _manual = (f.flags & kReplayManual) != 0;
    _pad.steer = float(f.padSteer) / 127;
    _pad.throttle = float(f.padThrottle) / 255;
    _pad.brake = float(f.padBrake) / 255;
    stepRace(f.dt);
    ++_replayFrame;
    // Where the recording had the car: a replay that's gone its own way
    // (Reluva changed since it was saved) says so, once.
    if (_replayFrame % Replay::kMarkEvery == 0 && _replayNote.empty() && !_racers.empty()) {
        const size_t mark = _replayFrame / Replay::kMarkEvery - 1;
        if (mark < _playback.marks.size() &&
            length(_racers.front().car->sim.position - _playback.marks[mark]) > kDiverged)
            _replayNote = "This replay has gone its own way from " + clockText(_clock) +
                          ": Reluva has changed since it was saved";
    }
}

void RaceScreen::stepReplay(float dt) {
    if (_replayPaused)
        return;
    _replayAccum += dt;
    for (int n = 0; n < kMaxStepsPerUpdate && _replayFrame < _playback.frames.size() &&
                    _replayAccum >= _playback.frames[_replayFrame].dt;
         ++n) {
        _replayAccum -= _playback.frames[_replayFrame].dt;
        playFrame();
    }
    if (_replayFrame >= _playback.frames.size())
        _replayPaused = true; // the end: stopped there
}

void RaceScreen::endReplay() {
    _replayOfThisRace = false;
    _replayQuitting = false;
    if (_replayReturn == ReplayReturn::kMenu) {
        _replaying = false;
        _keys = 0;
        _pad = {};
        endRace();
        return;
    }
    // Back to the race (or its results): run on to where it was left,
    // which is where it is when the recording's all played.
    while (_replayFrame < _playback.frames.size())
        playFrame();
    _replaying = false;
    _keys = 0;
    _pad = {};
    if (_replayReturn == ReplayReturn::kResults)
        _results = true;
    else
        _paused = true;
    _replayReturn = ReplayReturn::kMenu;
}

void RaceScreen::replayButton(int which) {
    switch (which) {
    case kPlay:
        if (_replayFrame >= _playback.frames.size()) {
            startReplay(_playback); // from the start again
            return;
        }
        _replayPaused = !_replayPaused;
        break;
    case kStep:
        _replayPaused = true;
        playFrame();
        break;
    case kRew: {
        // Back a few seconds: the race started again and run on (unseen)
        // to there, as a replay can't be stepped backwards.
        double back = kRewind;
        size_t target = _replayFrame;
        while (target > 0 && back > 0)
            back -= _playback.frames[--target].dt;
        const int cam = _replayCam, focus = _replayFocus;
        const bool paused = _replayPaused;
        startReplay(_playback);
        if (!_replaying)
            break;
        while (_replayFrame < target)
            playFrame();
        _replayCam = cam;
        _replayFocus = focus;
        _replayPaused = paused;
        _cameraReset = true;
        _tvSet = false;
        break;
    }
    case kCam:
        _replayCam = (_replayCam + 1) % (int(_cameras.size()) + 1);
        _cameraReset = true;
        _tvSet = false;
        break;
    case kSave:
        if (_game)
            _game->nameReplay(_playback.name.empty() ? replayDefaultName() : _playback.name);
        break;
    case kQuit:
        // The race just driven: asked whether to keep it (the game's REPLAY
        // SAVE MENU).
        if (_replayOfThisRace && !_replaySaved && _game) {
            _replayQuitting = true;
            _game->askToSaveReplay(replayDefaultName());
        } else
            endReplay();
        break;
    default:
        break;
    }
}

bool RaceScreen::replayEvent(const Event &ev) {
    if (ev.type == EventType::kMouseDown) {
        // A word on the bar clicked.
        if (ev.mouseY < kBarY - 4 || ev.mouseY >= kBarY + 22)
            return true;
        int x = kBarFirstX;
        for (int b = 0; b < kButtons; ++b) {
            const int w = int(replayWord(b).size() + 3) * font::kAdvance;
            if (ev.mouseX >= x && ev.mouseX < x + w) {
                _replayFocus = b;
                replayButton(b);
                break;
            }
            x += w;
        }
        return true;
    }
    if (ev.type != EventType::kKeyDown)
        return true;
    switch (ev.key) {
    case key::kLeft:
        _replayFocus = (_replayFocus + kButtons - 1) % kButtons;
        break;
    case key::kRight:
        _replayFocus = (_replayFocus + 1) % kButtons;
        break;
    case key::kReturn:
    case key::kKpEnter:
        replayButton(_replayFocus);
        break;
    case key::kSpace:
        replayButton(kPlay);
        break;
    case key::kBackspace:
        replayButton(kRew);
        break;
    case '.':
        replayButton(kStep);
        break;
    case key::kEscape:
        replayButton(kQuit);
        break;
    default:
        if (controlOf(ev.key) == Control::kChangeCamera)
            replayButton(kCam);
        break;
    }
    return true;
}

void RaceScreen::gameMenuEvent(const Event &ev) {
    const GameMenus::Action action = _game->handle(ev);
    if (const std::string clip = _game->takeVoice(); !clip.empty())
        playVoice(clip);
    switch (action) {
    case GameMenus::Action::kQuit:
        _quit = true;
        break;
    case GameMenus::Action::kLoadReplay: {
        Replay r;
        if (Error err = loadReplay(_game->replayFile(), r)) {
            _error = err.message;
            break;
        }
        _replayOfThisRace = false;
        _replaySaved = true;
        startReplay(std::move(r));
        break;
    }
    case GameMenus::Action::kSaveReplay: {
        _playback.name = _game->replayName();
        if (Error err = saveReplay(_ctx.savePath, _playback))
            _replayNote = err.message;
        else {
            _replayNote = "Saved as \"" + _playback.name + "\"";
            _replaySaved = true;
        }
        if (_replayQuitting)
            endReplay();
        break;
    }
    case GameMenus::Action::kClosed:
        if (_replayQuitting)
            endReplay(); // not saved
        break;
    case GameMenus::Action::kDriverChanged:
        switchDriver();
        break;
    default:
        break;
    }
}

CameraView RaceScreen::replayCamera(float dt, float &fov) {
    const CarSim &sim = _racers.front().car->sim;
    if (_replayCam < int(_cameras.size())) {
        const CarCamera &cam = _cameras[size_t(_replayCam)];
        fov = cam.fov;
        return updateCarCamera(cam, sim, dt, _cameraPos, _cameraReset);
    }
    // The roadside camera: set down ahead of the car, to one side, looking
    // at it as it comes and goes; another once it's well past.
    Vec3 forward = -sim.orientation.z;
    forward[1] = 0;
    forward = normalized(forward);
    if (!_tvSet || length(sim.position - _tvAt) > kTvLeave) {
        _tvSide = -_tvSide;
        const Vec3 right{-forward[2], 0, forward[0]};
        Vec3 at = sim.position + forward * kTvAhead + right * (kTvSide * _tvSide);
        CollisionWorld::RayHit hit;
        if (_world.raycast(at + Vec3{0, 30, 0}, {0, -1, 0}, 60, hit))
            at[1] = hit.point[1];
        _tvAt = at + Vec3{0, kTvHeight, 0};
        _tvSet = true;
    }
    fov = kTvFov;
    CameraView v;
    v.eye = _tvAt;
    v.target = sim.position + Vec3{0, 1, 0};
    return v;
}

std::string RaceScreen::replayWord(int button) const {
    const char *const fallback[kButtons] = {"Play", "Step", "Rew", "Cam", "Save", "Quit"};
    const uint32_t ids[kButtons] = {484, 485, 486, 488, 489, 490};
    return button == kPlay && !_replayPaused ? _strings.get(487, "Stop") : _strings.get(ids[button], fallback[button]);
}

void RaceScreen::drawReplayBar() {
    // Below the race's own lines: the game's REPLAY MENU and its buttons,
    // the time.
    const int y = kBarY;
    for (int r = y - 4; r < y + 22; ++r)
        for (int x = 0; x < kFrameW; ++x) {
            uint8_t *p = _overlay.row(r) + x * 4;
            p[0] = 20, p[1] = 10, p[2] = 60, p[3] = 200;
        }
    font::drawText(_overlay, 6, y + 4, _strings.get(483, "REPLAY MENU"), {255, 230, 140});
    int x = kBarFirstX;
    for (int b = 0; b < kButtons; ++b) {
        const std::string word = replayWord(b);
        const bool focused = b == _replayFocus;
        font::drawText(_overlay, x, y + 4, focused ? "[" + word + "]" : " " + word + " ",
                       focused ? Color{255, 255, 120} : Color{220, 220, 255});
        x += int(word.size() + 3) * font::kAdvance;
    }
    font::drawText(_overlay, kFrameW - 110, y + 4,
                   clockText(std::max(0.0f, _clock)) + " / " + clockText(_playback.duration() - (cruising() ? 0 : 3)),
                   {255, 255, 255});
    if (!_replayNote.empty())
        font::drawText(_overlay, 6, y + 26, _replayNote, {255, 200, 120});
}

} // namespace reluva::age
