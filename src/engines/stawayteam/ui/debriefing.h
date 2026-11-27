// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/objectives_window.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * The debriefing after a mission completed (`CStarTrekDebriefingMenu`,
 * 0x48a780): three panels fading in one after another, the mission's
 * success and the time it took, its objectives, and its debriefing's
 * lines typed out beside the speaker's portrait with their voice-overs;
 * CONTINUE goes through the lines and then on, REPLAY starts them again.
 * Laid out by Rules.txt's `Debriefing Screen`
 * (docs/engines/stawayteam/systems/objectives.md).
 */
class DebriefingScreen {
public:
    /** What the player chose that leaves the screen. */
    enum class Choice { kNone, kContinue };

    /**
     * The debriefing of `mission`, its objectives as they ended, taken
     * `seconds`, started at `now`; `data`, `rules`, `options` and `mixer`
     * (nullptr: no sound) must outlive it.
     */
    DebriefingScreen(const Archive &data, const TextObject &rules, const Mission &mission, const GameOptions &options,
                     audio::Mixer *mixer, std::vector<ObjectiveLine> objectives, float seconds, uint64_t now);
    ~DebriefingScreen();

    Choice handleEvent(const Event &ev, uint64_t now);
    /** Moves it on to `now`, then draws it. */
    void draw(Surface &screen, uint64_t now);

    /** The line shown (its number in the debriefing), and whether it's typed out. */
    size_t line() const { return _line; }
    bool typed() const { return _typed >= float(_text.size()); }
    /** How far each panel has faded in (0-1): the mission's, the objectives', the text's. */
    float shown(size_t panel) const;

private:
    void showLine(size_t line);
    void stopSounds();
    void update(uint64_t now);

    const Archive &_data;
    const TextObject *_def;
    Mission _mission;
    const GameOptions &_options;
    audio::Mixer *_mixer;
    std::vector<ObjectiveLine> _objectives;
    float _seconds;
    image::Image _background;
    image::Image _missionPanel;
    image::Image _objectivePanel;
    image::Image _textPanel;
    image::Image _portraitBackground;
    image::Image _portrait;
    ObjectiveList _list;
    Font _titleFont; // Placar Condensed
    Font _lineFont;  // Placar Blue
    Font _textFont;  // Debriefing Text Font
    std::array<Font, 2> _buttonFonts;
    ButtonGroup _buttons; // CONTINUE, REPLAY
    std::shared_ptr<audio::Sound> _fadeSound;
    std::shared_ptr<audio::Sound> _ambience;
    audio::SoundHandle _ambienceHandle = audio::kNoSound;
    audio::SoundHandle _voice = audio::kNoSound;
    bool _hadVoice = false;
    float _sinceVoice = 0.0f; // seconds since the voice-over ended
    float _time = 0.0f;       // seconds since it opened
    size_t _line = 0;
    std::string _text;
    float _typed = 0.0f;
    uint64_t _last = 0;
};

} // namespace reluva::stawayteam
