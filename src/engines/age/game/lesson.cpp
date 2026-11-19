// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The messages are mmlang.dll's, in groups by event (ids 192-245); what
// each lesson asks is read from them. See docs/engines/age-racing.md.

#include "engines/age/game/lesson.h"

#include <algorithm>

namespace reluva::age {

namespace {

// mmlang.dll's string ids.
namespace msg {
constexpr uint32_t kGameOver = 245;
constexpr uint32_t kTimesUp = 244;
constexpr uint32_t kGo = 243;
// Cop chase.
constexpr uint32_t kEvade = 193;
constexpr uint32_t kSurvived = 196;
constexpr uint32_t kLoseThem = 197;
constexpr uint32_t kChaseTimesUp = 198;
// Jump.
constexpr uint32_t kTakeTheJump = 206;
constexpr uint32_t kGoodJumping = 209;
// Destroy ("Bonnie & Collide", 616-623; 210-216, under the same name,
// are drive-cleanly messages no lesson uses), and the tail to lose after.
constexpr uint32_t kDestroy = 619;
constexpr uint32_t kDidIt = 620;
constexpr uint32_t kReachedDestination = 621;
constexpr uint32_t kDestroyTimesUp = 623;
constexpr uint32_t kLoseTheTail = 652;
// Follow.
constexpr uint32_t kFollowThatCar = 220;
constexpr uint32_t kYouWon = 221;
constexpr uint32_t kEscaped = 222;
// No touching.
constexpr uint32_t kDontTouch = 227;
constexpr uint32_t kTouchTimesUp = 228;
constexpr uint32_t kTouchWon = 229;
constexpr uint32_t kScraped = 230;
// Minimum speed.
constexpr uint32_t kMaintain = 232;
constexpr uint32_t kGreatDriving = 234;
constexpr uint32_t kUpToSpeed = 237;
constexpr uint32_t kTooSlow = 238;
// Passed (the results' "Pass").
constexpr uint32_t kPass = 651;
} // namespace msg

} // namespace

void LessonJudge::start(const LessonSettings &lesson) {
    _lesson = lesson;
    _result = Result::kGoing;
    _penalty = 0;
    _touchesAtStart = -1;
    _upToSpeed = _destroyed = false;
    switch (lesson.event) {
    case LessonEvent::kJump:
        show(msg::kTakeTheJump);
        break;
    case LessonEvent::kFollow:
        show(msg::kFollowThatCar);
        break;
    case LessonEvent::kCopChase:
        show(msg::kEvade);
        break;
    case LessonEvent::kMinimumSpeed:
        show(lesson.extra[0] > 0 ? msg::kMaintain : msg::kGo);
        break;
    case LessonEvent::kNoTouching:
        show(msg::kDontTouch);
        break;
    case LessonEvent::kDestroy:
        show(msg::kDestroy);
        break;
    default:
        show(msg::kGo);
        break;
    }
}

float LessonJudge::timeLeft(float clock) const {
    // A follow lesson isn't timed: its TimeLimit (36 s in Straight, With
    // Chaser) is no clock, as the footage shows its car followed for 88 s.
    if (_lesson.event == LessonEvent::kFollow)
        return -1;
    return _lesson.timeLimit > 0 ? _lesson.timeLimit - std::max(0.0f, clock) - _penalty : -1;
}

LessonJudge::Result LessonJudge::step(const Frame &f) {
    _messageFor = std::max(0.0f, _messageFor - f.dt);
    if (_result != Result::kGoing || f.clock < 0)
        return _result;
    if (_touchesAtStart < 0)
        _touchesAtStart = f.carTouches;
    const LessonEvent e = _lesson.event;

    // Failing.
    if (f.busted || f.wrecked) {
        fail(msg::kGameOver);
        return _result;
    }
    if (e == LessonEvent::kDestroy && !_destroyed) {
        // The car rammed into submission before it gets away or the time's
        // up; then done, or (police about) away from them to the end.
        if (f.targetDone) {
            fail(msg::kReachedDestination);
        } else if (_lesson.timeLimit > 0 && timeLeft(f.clock) <= 0) {
            fail(msg::kDestroyTimesUp);
        } else if (f.targetStopped) {
            _destroyed = true;
            if (f.police > 0) {
                show(msg::kLoseTheTail);
            } else {
                _result = Result::kPassed;
                show(msg::kDidIt);
            }
        }
        return _result;
    }
    if (e == LessonEvent::kMinimumSpeed && _lesson.extra[0] > 0) {
        if (f.checkpoint && f.speedMph < _lesson.extra[0]) {
            fail(msg::kTooSlow);
            return _result;
        }
        if (!_upToSpeed && f.speedMph >= _lesson.extra[0]) {
            _upToSpeed = true;
            show(msg::kUpToSpeed);
        }
    }
    if (e == LessonEvent::kNoTouching && f.carTouches > _touchesAtStart) {
        fail(msg::kScraped);
        return _result;
    }
    if (e == LessonEvent::kFollow && f.targetDistance > kEscape) {
        fail(msg::kEscaped);
        return _result;
    }
    const float left = timeLeft(f.clock);
    if (e != LessonEvent::kFollow && _lesson.timeLimit > 0 && left <= 0) {
        fail(e == LessonEvent::kCopChase     ? msg::kChaseTimesUp
             : e == LessonEvent::kDestroy    ? msg::kDestroyTimesUp
             : e == LessonEvent::kNoTouching ? msg::kTouchTimesUp
                                             : msg::kTimesUp);
        return _result;
    }

    // Passing: at the end (following, once the car's there with the player
    // near; chased, once the police are lost).
    if (e == LessonEvent::kFollow) {
        if (f.targetDone && f.targetDistance >= 0 && f.targetDistance <= kEscape) {
            _result = Result::kPassed;
            show(msg::kYouWon);
        }
        return _result;
    }
    if (!f.finished)
        return _result;
    if ((e == LessonEvent::kCopChase || e == LessonEvent::kDestroy) && f.chasing > 0) {
        if (_messageFor <= 0)
            show(msg::kLoseThem);
        return _result;
    }
    _result = Result::kPassed;
    switch (e) {
    case LessonEvent::kJump:
        show(msg::kGoodJumping);
        break;
    case LessonEvent::kCopChase:
        show(msg::kSurvived);
        break;
    case LessonEvent::kMinimumSpeed:
        show(msg::kGreatDriving);
        break;
    case LessonEvent::kNoTouching:
        show(msg::kTouchWon);
        break;
    case LessonEvent::kDestroy:
        show(msg::kSurvived);
        break;
    default:
        show(msg::kPass);
        break;
    }
    return _result;
}

} // namespace reluva::age
