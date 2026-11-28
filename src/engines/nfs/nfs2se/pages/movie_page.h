// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/nfs2se/formats/dct.h"
#include "engines/nfs/pages/viewers.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * NFS II SE's movies (.dct) with their sound, which keeps the time.
 *   Up/Down: movie   Space: pause   Left/Right: 5 seconds back/on
 *   Home: start   PageUp/PageDown: frame by frame (paused)
 */
class MoviePage : public Page {
public:
    /** Lists the .dct files of `files`, which must outlive the page; `mixer` null for no sound. */
    MoviePage(const GameFiles &files, audio::Mixer *mixer);
    const char *name() const override { return "Movies"; }
    bool empty() const override { return _movies.empty(); }
    void activate(System &sys) override;
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int movie);
    void seek(int frame, uint64_t nowMs);
    void showFrame(int frame);
    void stopSound();
    double frameMs() const { return 1000.0 / _loaded.framesPerSecond(); }

    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<FileRef> _movies;
    int _movie = -1;
    DctMovie _loaded;
    std::string _error;
    std::shared_ptr<audio::Sound> _audio;
    mdec::Decoder _decoder;
    int _shown = -1;
    Surface _picture;
    bool _paused = false;
    // The clock: the sound's position while it plays, else the time since
    // _startMs from _startFrame.
    int _startFrame = 0;
    uint64_t _startMs = 0;
    uint64_t _nowMs = 0;
    audio::SoundHandle _playing = audio::kNoSound;
};

} // namespace reluva::nfs::nfs2se
