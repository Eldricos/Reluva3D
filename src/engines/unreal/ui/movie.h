// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's movies, played with their sound.

#include "engines/unreal/render/view.h"

#include "video/avi.h"

#include <memory>

namespace reluva::unreal {

/** The viewer's page of the game's movies (the .avi files in its movies folder). */
class MoviePlayer : public Page {
public:
    /** Lists the .avi files in the game folder's movies folder (any case). */
    explicit MoviePlayer(ViewContext &ctx);
    ~MoviePlayer() override;
    const char *name() const override { return "Movies"; }
    size_t count() const override { return _files.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool ownsKey(uint32_t key) const override { return key == key::kEscape && playing(); }

    /**
     * Plays the movie whose file is named `name` (any case); false if
     * there's none, or it doesn't open.
     */
    bool play(const std::string &name);
    /** Whether a movie is playing. */
    bool playing() const { return _movie != nullptr; }

private:
    void play(size_t index);
    void stop();
    void draw();
    void showPicture();

    std::vector<std::filesystem::path> _files;
    size_t _index = 0;
    std::string _status;
    std::unique_ptr<avi::Decoder> _movie; // the one playing
    int _audioRate = 0;
    int _shown = -1;       // the last frame decoded
    uint64_t _startMs = 0; // when it started, by the clock (0: not yet)
    audio::SoundHandle _sound = audio::kNoSound;
};

} // namespace reluva::unreal
