// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/formats/race.h"

#include <cstdint>

namespace reluva::age {

/**
 * Judges a Crash Course lesson as it's driven: passed, failed or going,
 * and which of the game's messages (mmlang.dll string ids) to show. The
 * rules are Reluva's reading of each event's messages; see
 * docs/engines/age/games/mm2/systems/crash-course.md.
 */
class LessonJudge {
public:
    static constexpr float kEscape = 120;    // metres behind the car to follow, and it's escaped
    static constexpr float kMessageTime = 3; // seconds a message shows

    /** How the lesson stands. */
    enum class Result { kGoing, kPassed, kFailed };

    /** What happened this frame. */
    struct Frame {
        float dt = 0;
        float clock = 0;         // seconds since the start
        bool checkpoint = false; // the player reached a checkpoint this frame
        bool finished = false;   // the player has reached the last (this frame or before)
        float speedMph = 0;
        float impact = 0;   // the player's hardest knock this frame
        int carTouches = 0; // times it's run into a car since the start
        int chasing = 0;    // police chasing it
        bool busted = false;
        bool wrecked = false;
        float targetDistance = -1;  // metres to the car to follow (-1: none)
        bool targetDone = false;    // that car has reached its end
        bool targetStopped = false; // the car to destroy is wrecked, over or stopped
        int police = 0;             // police in the lesson
    };

    /** Starts judging a lesson, with its first message. */
    void start(const LessonSettings &lesson);
    /**
     * Judges a frame (before the start, while the clock is below 0, nothing
     * changes).
     * @return The result, which stays once passed or failed.
     */
    Result step(const Frame &f);
    /** The result so far. */
    Result result() const { return _result; }

    /**
     * Seconds added to the clock as penalties: always none, as the game's
     * message for one (216) is in a group of messages no lesson uses.
     */
    float penalty() const { return _penalty; }
    /**
     * Seconds left (penalties counted), or -1 with no limit (a Follow
     * lesson's TimeLimit isn't one).
     */
    float timeLeft(float clock) const;
    /**
     * The message to show now (0: none), and the minimum speed in mph for
     * MinimumSpeed lessons' "Maintain %.0f".
     */
    ///@{
    uint32_t message() const { return _messageFor > 0 ? _message : 0; }
    float minimumSpeed() const { return _lesson.extra[0]; }
    ///@}
    /** A Destroy lesson's car has been rammed into submission. */
    bool destroyed() const { return _destroyed; }

private:
    void show(uint32_t id) {
        _message = id;
        _messageFor = kMessageTime;
    }
    void fail(uint32_t id) {
        _result = Result::kFailed;
        show(id);
    }

    LessonSettings _lesson;
    Result _result = Result::kGoing;
    float _penalty = 0;
    float _messageFor = 0;
    uint32_t _message = 0;
    int _touchesAtStart = -1;
    bool _upToSpeed = false;
    bool _destroyed = false;
};

} // namespace reluva::age
