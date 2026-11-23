// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/page.h"

#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The install's sounds: effects, music and speech, all WAV files.
 *   Up/Down: previous/next   PageUp/PageDown: 20 at a time   Home/End: first/last
 *   Space: play or stop   A: play each as it's picked, on/off
 */
class SoundPage : public Page {
public:
    /**
     * Lists the sounds; `files`, `sys` and `screen` must outlive it.
     * @param mixer where they're played; nullptr lists them without playing
     */
    SoundPage(System &sys, Surface &screen, const GameFiles &files, audio::Mixer *mixer);

    const char *name() const override { return "Sounds"; }
    size_t count() const override { return _names.size(); }
    void activate() override { draw(); }
    void deactivate() override { stop(); }
    void handleKey(uint32_t key) override;
    void update(uint64_t millis) override;

private:
    void play();
    void stop();
    void draw();

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<std::string> _names;
    size_t _current = 0;
    bool _auto = false;
    audio::SoundHandle _playing = audio::kNoSound;
    std::string _about; // the current sound's format and length, once read
};

} // namespace reluva::storm3d
