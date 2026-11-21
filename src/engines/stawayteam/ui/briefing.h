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
#include "engines/stawayteam/ui/static_text.h"
#include "video/movie_player.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

/**
 * The briefing before a mission (`CStarTrekBriefingMenu`, 0x473900): its
 * movie, and its script's lines typed out a line at a time beside the
 * speaker's portrait, with their voice-overs; CONTINUE (or Space) goes on,
 * REPLAY starts again, END BRIEFING leaves for the team selection, EXIT
 * for the main menu. Laid out by Rules.txt's `Briefing Screen`
 * (docs/engines/stawayteam/systems/briefing.md).
 */
class BriefingScreen {
public:
    /** What the player chose that leaves the screen. */
    enum class Choice { kNone, kEndBriefing, kExit };

    /**
     * The briefing of `mission`, started at `now` (milliseconds), its movie
     * from the install's `DATA/Movies`; `data`, `rules`, `options` and
     * `mixer` (nullptr: no sound) must outlive it.
     */
    BriefingScreen(const Archive &data, const std::filesystem::path &install, const TextObject &rules,
                   const Mission &mission, const GameOptions &options, audio::Mixer *mixer, uint64_t now);
    ~BriefingScreen();

    /** What `ev` does to it at `now`. */
    Choice handleEvent(const Event &ev, uint64_t now);
    /** Moves it on to `now`, then draws it. */
    void draw(Surface &screen, uint64_t now);

    /** The line shown (its number in the mission's briefing), and how much of it is typed out. */
    size_t line() const { return _line; }
    bool typed() const;

private:
    // Shows line `line` and starts its voice-over (0x472bc0).
    void showLine(size_t line);
    // The movie from its start, and the first line (0x474ba1).
    void replay(uint64_t now);
    void stopSounds();
    void update(uint64_t now);
    void drawText(Surface &screen);

    const Archive &_data;
    const TextObject &_rules;
    const TextObject *_def;
    Mission _mission;
    const GameOptions &_options;
    audio::Mixer *_mixer;
    image::Image _background;
    image::Image _portraitBackground;
    image::Image _portrait;
    std::vector<StaticText> _texts;
    Font _textFont;   // Briefing Text Font
    Font _titleFont;  // Placar Blue
    Font _promptFont; // Trek Small
    std::vector<Font> _buttonFonts;
    ButtonGroup _buttons; // EXIT, REPLAY, CONTINUE, END BRIEFING
    std::string _title;
    std::string _prompt;
    std::vector<uint8_t> _movieData; // kept to start it again
    std::unique_ptr<video::MoviePlayer> _movie;
    std::shared_ptr<audio::Sound> _music;
    audio::SoundHandle _musicHandle = audio::kNoSound;
    std::shared_ptr<audio::Sound> _spaceSound;
    audio::SoundHandle _voice = audio::kNoSound;
    bool _hadVoice = false;
    float _sinceVoice = 0.0f; // seconds since the voice-over ended
    size_t _line = 0;
    std::string _text;   // the line's whole text
    float _typed = 0.0f; // characters typed out
    bool _advance = false;
    int _scroll = 0; // lines scrolled past
    uint64_t _last = 0;
    std::unique_ptr<ConfirmDialog> _confirm;
};

} // namespace reluva::stawayteam
