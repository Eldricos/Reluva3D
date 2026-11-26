// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/sound_effects.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/ui_model.h"
#include "graphics/soft3d/rasterizer.h"

#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The advisor in the park: a blue creature at the bottom right, who
 * pops up and says a line of the speech bank as the player first does
 * something, as the recording has him. See
 * docs/engines/tpw/games/simcoaster/systems/advisor.md.
 */
class Advisor {
public:
    /** What sets him off, each once a park. */
    enum class Cue {
        kBuyWindow,    // the Buy window opened
        kChallenges,   // the objectives' Challenges tab
        kVisitorStats, // Visitor Statistics opened
        kResearchLab,  // the Research Lab opened
        kPathTool,     // the path tool chosen
        kPlacing,      // an attraction bought, to be placed
        kMissionDone,  // the mission's done
    };
    /** Where he stands on the 640 × 480 screen, his head where the recording has it at 3:02 (inferred); his feet run off its foot. */
    static constexpr ScreenRect kPlace{485, 300, 160, 220};

    /** Reads the world's advisor (`levels/science/advisor/Advisor_SCI.MD2`) and the speech bank. */
    Error load(const SearchSet &files, const std::string &worldFolder);
    /** Where he speaks; nullptr to say nothing aloud, his lines timed as if he did. */
    void setMixer(audio::Mixer *mixer) { _mixer = mixer; }
    /** Says the cue's line, the first time it comes, after any he's saying. */
    void cue(Cue cue);
    /** Brings him on by `ms`: his line said, the next begun. */
    void advance(uint32_t ms);
    bool talking() const { return _line != 0; }
    /** Draws him while he talks. */
    void draw(Surface &screen) const;
    /** The lines he has begun, by their numbers in the speech category, in order. */
    const std::vector<uint32_t> &said() const { return _said; }
    /** The line a cue says. */
    static uint32_t lineOf(Cue cue);

private:
    void start(uint32_t line);

    LoadedModel _model;
    bool _loaded = false;
    std::vector<bool> _shown; // by the model's meshes: his body, not his props
    int _talkAnimation = -1;
    SoundEffects _speech;
    bool _speechLoaded = false;
    audio::Mixer *_mixer = nullptr;
    std::set<Cue> _cued;
    std::deque<uint32_t> _waiting;
    uint32_t _line = 0; // the line he's saying; 0 for none
    audio::SoundHandle _sound = audio::kNoSound;
    uint32_t _clock = 0;
    uint32_t _lineStarted = 0;
    std::vector<uint32_t> _said;
    mutable soft3d::Rasterizer _rasterizer;
};

} // namespace reluva::tpw
