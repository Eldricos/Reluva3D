// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/sound.h"
#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/pages/file_list.h"
#include "engines/stawayteam/pages/page.h"

#include <memory>

namespace reluva::stawayteam {

/**
 * The data viewer's page of sounds: every .ogg and .wav in DATA.dat. The
 * file keys of FileList; Space plays or stops the sound, A plays each as
 * it's chosen. Without a mixer (no sound output) they're listed only.
 */
class SoundViewer : public Page {
public:
    /** `sys`, `screen`, `data` and `mixer` must outlive it. */
    SoundViewer(System &sys, Surface &screen, const Archive &data, audio::Mixer *mixer);

    size_t count() const override { return _files.size(); }
    void activate() override;
    void deactivate() override { stop(); }
    void handleKey(uint32_t key) override;
    void update(uint64_t nowMs) override;

private:
    void load();
    void play();
    void stop();
    void draw();

    System &_sys;
    Surface &_screen;
    const Archive &_data;
    audio::Mixer *_mixer;
    FileList _files;
    std::shared_ptr<audio::Sound> _sound;
    std::string _error;
    audio::SoundHandle _playing = audio::kNoSound;
    uint64_t _startMs = 0;
    uint64_t _nowMs = 0;
    uint64_t _drawnMs = 0;
    bool _autoPlay = false;
};

} // namespace reluva::stawayteam
