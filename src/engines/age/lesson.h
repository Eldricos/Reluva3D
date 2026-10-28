// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/race.h"

#include <cstdint>

namespace reluva::age {

// Judges a Crash Course lesson as it's driven: passed, failed or going,
// and which of the game's messages (mmlang.dll string ids) to show. The
// rules are Reluva's reading of each event's messages; see
// docs/engines/age-racing.md.
class LessonJudge {
public:
    static constexpr float kPenalty = 5;     // seconds a knock costs in a Clean lesson
    static constexpr float kKnock = 1500;    // an impulse (kg m/s) that counts as a knock there
    static constexpr float kEscape = 120;    // metres behind the car to follow, and it's escaped
    static constexpr float kMessageTime = 3; // seconds a message shows

    enum class Result { Going, Passed, Failed };

    // What happened this frame.
    struct Frame {
        float dt = 0;
        float clock = 0;         // seconds since the start
        bool checkpoint = false; // the player reached a checkpoint this frame
        bool finished = false;   // the player has reached the last (this frame or before)
        float speedMph = 0;
        float impact = 0;   // the player's hardest knock this frame
        int carTouches = 0; // times it's run into a car since the start
        int chasing = 0;    // police chasing it
        bool busted = false, wrecked = false;
        float targetDistance = -1; // metres to the car to follow (-1: none)
        bool targetDone = false;   // that car has reached its end
    };

    void start(const LessonSettings& lesson);
    Result step(const Frame& f);
    Result result() const { return result_; }

    // Seconds added for knocks.
    float penalty() const { return penalty_; }
    // Seconds left (penalties counted), or -1 with no limit.
    float timeLeft(float clock) const;
    // The message to show now (0: none), and the minimum speed for
    // MinimumSpeed lessons' "Maintain %.0f".
    uint32_t message() const { return messageFor_ > 0 ? message_ : 0; }
    float minimumSpeed() const { return lesson_.extra[0]; }

private:
    void show(uint32_t id) {
        message_ = id;
        messageFor_ = kMessageTime;
    }
    void fail(uint32_t id) {
        result_ = Result::Failed;
        show(id);
    }

    LessonSettings lesson_;
    Result result_ = Result::Going;
    float penalty_ = 0, knockWait_ = 0, messageFor_ = 0;
    uint32_t message_ = 0;
    int touchesAtStart_ = -1;
    bool upToSpeed_ = false;
};

} // namespace reluva::age
