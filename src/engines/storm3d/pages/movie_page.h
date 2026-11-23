// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/page.h"
#include "video/bink.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The install's Bink movies (the `.bik` files in `animations/`): the cut scenes, the
 * briefings' faces, the credits.
 *   Left/Right: previous/next movie   Space: play or pause   Home: back to its start
 */
class MoviePage : public Page {
public:
    /**
     * Lists the movies; `files`, `sys` and `screen` must outlive it.
     * @param mixer where their sound plays; nullptr plays them silently
     */
    MoviePage(System &sys, Surface &screen, const GameFiles &files, audio::Mixer *mixer);

    const char *name() const override { return "Movies"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void deactivate() override { pause(); }
    void handleKey(uint32_t key) override;
    void update(uint64_t millis) override;

private:
    void open();
    void pause();
    void draw();

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<std::string> _names;
    size_t _current = 0;
    std::unique_ptr<bink::Movie> _movie; // the current one, opened; nullptr if it can't be read
    std::string _error;
    std::shared_ptr<audio::Sound> _sound; // its first audio track, whole
    std::vector<uint8_t> _rgba;           // its last frame decoded
    bool _playing = false;
    uint64_t _startMillis = 0; // the clock when frame 0 would have been shown
    audio::SoundHandle _soundHandle = audio::kNoSound;
};

} // namespace reluva::storm3d
