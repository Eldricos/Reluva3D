// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/advisor.h"

#include "common/fs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace reluva::tpw {

namespace {

constexpr const char *kSpeechFolder = "global/Speech";
constexpr const char *kSpeechCategory = "speech";
// The cues' lines, by their numbers in the speech category, as the
// recording's sound matches them where the player first does each
// (inferred: one sighting each).
constexpr std::array<std::pair<Advisor::Cue, uint32_t>, 7> kLines{{
    {Advisor::Cue::kBuyWindow, 380},
    {Advisor::Cue::kChallenges, 959},
    {Advisor::Cue::kVisitorStats, 965},
    {Advisor::Cue::kResearchLab, 864},
    {Advisor::Cue::kPathTool, 376},
    {Advisor::Cue::kPlacing, 502},
    {Advisor::Cue::kMissionDone, 1109},
}};
// His body's meshes; the model has his props too, his hats, his mouths
// for each sound and his shut eyes, which the game shows by what he says
// (guessed: the recording mostly shows him bare).
constexpr std::array<const char *, 9> kBody{"Body",           "Bug Head",   "Left Antennae",
                                            "Right Antennae", "Left Eye",   "Right Eye",
                                            "Mouth - Normal", "Right hand", "Left Hand"};
// How long a line lasts when there's nothing to play it on.
constexpr uint32_t kSilentLineMillis = 6000;
// His view: turned to face us, from a little above.
constexpr float kPitch = 0.15f;
constexpr float kDistance = 64;
constexpr float kLookAtHeight = -6;

} // namespace

Error Advisor::load(const SearchSet &files, const std::string &worldFolder) {
    // The world's own, as Advisor_SCI.MD2: the shortest model in its folder,
    // the others its animations.
    const std::string folder = fs::toLower(worldFolder + "/advisor/");
    std::string model;
    for (const std::string &name : files.list()) {
        const std::string low = fs::toLower(name);
        if (low.compare(0, folder.size(), folder) == 0 && low.size() > 4 &&
            low.compare(low.size() - 4, 4, ".md2") == 0 && (model.empty() || name.size() < model.size()))
            model = name;
    }
    if (model.empty())
        return Error::fail("no advisor in " + worldFolder);
    if (Error err = _model.load(files, model))
        return err;
    _shown.assign(_model.model.meshes().size(), false);
    for (size_t m = 0; m < _shown.size(); ++m)
        _shown[m] = std::find(kBody.begin(), kBody.end(), _model.model.meshes()[m].name) != kBody.end();
    _talkAnimation = _model.animations.empty() ? -1 : 0;
    _loaded = true;
    _speechLoaded = !_speech.load(files, kSpeechFolder, {kSpeechCategory});
    return Error::success();
}

uint32_t Advisor::lineOf(Cue cue) {
    for (const auto &[c, line] : kLines)
        if (c == cue)
            return line;
    return 0;
}

void Advisor::cue(Cue cue) {
    if (!_cued.insert(cue).second)
        return;
    if (_line)
        _waiting.push_back(lineOf(cue));
    else
        start(lineOf(cue));
}

void Advisor::start(uint32_t line) {
    _line = line;
    _lineStarted = _clock;
    _said.push_back(line);
    _sound = _mixer && _speechLoaded ? _speech.play(*_mixer, line, audio::SoundType::kSpeech) : audio::kNoSound;
}

void Advisor::advance(uint32_t ms) {
    _clock += ms;
    if (!_line)
        return;
    const bool done =
        _sound != audio::kNoSound ? !_mixer->isPlaying(_sound) : _clock - _lineStarted >= kSilentLineMillis;
    if (!done)
        return;
    _line = 0;
    _sound = audio::kNoSound;
    if (!_waiting.empty()) {
        const uint32_t next = _waiting.front();
        _waiting.pop_front();
        start(next);
    }
}

// Drawn into his corner over what's there; the model stands along its z,
// as the gates do, and faces its −y.
void Advisor::draw(Surface &screen) const {
    if (!_loaded || !_line)
        return;
    Surface view;
    view.create(kPlace.w, kPlace.h, PixelFormat::kRGBA32);
    const float frame = _talkAnimation < 0
                            ? 0
                            : std::fmod(float(_clock - _lineStarted) * Animation::kFramesPerSecond / 1000,
                                        float(std::max(1u, _model.animations[size_t(_talkAnimation)].frames())));
    std::vector<gfx::Mat4> transforms = meshTransforms(_model, _talkAnimation, frame);
    const gfx::Mat4 upright{1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1};
    for (size_t m = 0; m < transforms.size(); ++m)
        transforms[m] = _shown[m] ? gfx::multiply(upright, transforms[m]) : gfx::Mat4{};
    const std::vector<std::vector<gfx::Vec3>> positions = meshPositions(_model, _talkAnimation, frame);
    const soft3d::OrbitCamera camera({0, 0, kLookAtHeight}, 0, kPitch, kDistance, view.w, view.h);
    _rasterizer.begin(view);
    drawMeshes(_rasterizer, camera, _model, true, defaultLight(), &transforms, &positions);
    for (int y = 0; y < view.h && kPlace.y + y < screen.h; ++y)
        for (int x = 0; x < view.w && kPlace.x + x < screen.w; ++x) {
            const uint8_t *src = view.row(y) + x * 4;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0)
                continue;
            std::copy(src, src + 3, screen.row(kPlace.y + y) + (kPlace.x + x) * 4);
        }
}

} // namespace reluva::tpw
