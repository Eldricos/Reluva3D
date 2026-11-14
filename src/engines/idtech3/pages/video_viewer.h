// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/cinematic.h"
#include "engines/idtech3/client/page.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// The data viewer's Videos page: every video in the game's files (Elite
// Force's Bink, Jedi Outcast's RoQ) played with its sound, stretched over
// the screen as the games show their cinematics.
class VideoViewer : public Page {
public:
    explicit VideoViewer(PageContext &ctx);
    ~VideoViewer() override;
    const char *name() const override { return "Videos"; }
    size_t count() const override { return _names.size(); }
    void activate() override { play(_index); }
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;

private:
    void play(int index);
    void stop();
    void draw();

    std::vector<std::string> _names;
    int _index = 0;
    std::unique_ptr<CinematicMovie> _movie;
    std::vector<uint8_t> _rgba;
    std::string _error;
    uint64_t _startMs = 0;
    bool _ended = false;
    audio::SoundHandle _sound = audio::kNoSound;
};

} // namespace reluva::idtech3
