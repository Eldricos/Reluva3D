// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's movies, played with their sound.

#include "engines/unreal/render/view.h"

#include "video/avi.h"

#include <memory>

namespace reluva::unreal {

class MoviePlayer : public Page {
public:
    explicit MoviePlayer(ViewContext &ctx);
    ~MoviePlayer() override;
    const char *name() const override { return "Movies"; }
    size_t count() const override { return files_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool ownsKey(uint32_t key) const override { return key == key::kEscape && playing(); }

    // Plays the movie whose file is named `name` (any case); false if
    // there's none.
    bool play(const std::string &name);
    bool playing() const { return movie_ != nullptr; }

private:
    void play(size_t index);
    void stop();
    void draw();
    void showPicture();

    std::vector<std::filesystem::path> files_;
    size_t index_ = 0;
    std::string status_;
    std::unique_ptr<avi::Decoder> movie_; // the one playing
    int audioRate_ = 0;
    int shown_ = -1;       // the last frame decoded
    uint64_t startMs_ = 0; // when it started, by the clock (0: not yet)
    audio::SoundHandle sound_ = audio::kNoSound;
};

} // namespace reluva::unreal
