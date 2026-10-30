// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The messages are mmlang.dll's, in groups by event (ids 192-245); what
// each lesson asks is read from them. See docs/engines/age-racing.md.

#include "engines/age/lesson.h"

#include <algorithm>

namespace reluva::age {

namespace {

// mmlang.dll's string ids.
namespace msg {
constexpr uint32_t kGameOver = 245;
constexpr uint32_t kTimesUp = 244;
constexpr uint32_t kGo = 243;
// Cop chase.
constexpr uint32_t kEvade = 193, kSurvived = 196, kLoseThem = 197, kChaseTimesUp = 198;
// Jump.
constexpr uint32_t kTakeTheJump = 206, kGoodJumping = 209;
// Destroy ("Bonnie & Collide", 616-623; 210-216, under the same name,
// are drive-cleanly messages no lesson uses), and the tail to lose after.
constexpr uint32_t kDestroy = 619, kDidIt = 620, kReachedDestination = 621, kDestroyTimesUp = 623, kLoseTheTail = 652;
// Follow.
constexpr uint32_t kFollowThatCar = 220, kYouWon = 221, kEscaped = 222;
// No touching.
constexpr uint32_t kDontTouch = 227, kTouchTimesUp = 228, kTouchWon = 229, kScraped = 230;
// Minimum speed.
constexpr uint32_t kMaintain = 232, kGreatDriving = 234, kUpToSpeed = 237, kTooSlow = 238;
// Passed (the results' "Pass").
constexpr uint32_t kPass = 651;
} // namespace msg

} // namespace

void LessonJudge::start(const LessonSettings& lesson) {
    lesson_ = lesson;
    result_ = Result::Going;
    penalty_ = 0;
    touchesAtStart_ = -1;
    upToSpeed_ = destroyed_ = false;
    switch (lesson.event) {
    case LessonEvent::Jump: show(msg::kTakeTheJump); break;
    case LessonEvent::Follow: show(msg::kFollowThatCar); break;
    case LessonEvent::CopChase: show(msg::kEvade); break;
    case LessonEvent::MinimumSpeed: show(lesson.extra[0] > 0 ? msg::kMaintain : msg::kGo); break;
    case LessonEvent::NoTouching: show(msg::kDontTouch); break;
    case LessonEvent::Destroy:
        show(msg::kDestroy);
        break;
    default: show(msg::kGo); break;
    }
}

float LessonJudge::timeLeft(float clock) const {
    return lesson_.timeLimit > 0 ? lesson_.timeLimit - std::max(0.0f, clock) - penalty_ : -1;
}

LessonJudge::Result LessonJudge::step(const Frame& f) {
    messageFor_ = std::max(0.0f, messageFor_ - f.dt);
    if (result_ != Result::Going || f.clock < 0)
        return result_;
    if (touchesAtStart_ < 0)
        touchesAtStart_ = f.carTouches;
    const LessonEvent e = lesson_.event;

    // Failing.
    if (f.busted || f.wrecked) {
        fail(msg::kGameOver);
        return result_;
    }
    if (e == LessonEvent::Destroy && !destroyed_) {
        // The car rammed into submission before it gets away or the time's
        // up; then done, or (police about) away from them to the end.
        if (f.targetDone) {
            fail(msg::kReachedDestination);
        } else if (lesson_.timeLimit > 0 && timeLeft(f.clock) <= 0) {
            fail(msg::kDestroyTimesUp);
        } else if (f.targetStopped) {
            destroyed_ = true;
            if (f.police > 0) {
                show(msg::kLoseTheTail);
            } else {
                result_ = Result::Passed;
                show(msg::kDidIt);
            }
        }
        return result_;
    }
    if (e == LessonEvent::MinimumSpeed && lesson_.extra[0] > 0) {
        if (f.checkpoint && f.speedMph < lesson_.extra[0]) {
            fail(msg::kTooSlow);
            return result_;
        }
        if (!upToSpeed_ && f.speedMph >= lesson_.extra[0]) {
            upToSpeed_ = true;
            show(msg::kUpToSpeed);
        }
    }
    if (e == LessonEvent::NoTouching && f.carTouches > touchesAtStart_) {
        fail(msg::kScraped);
        return result_;
    }
    if (e == LessonEvent::Follow && f.targetDistance > kEscape) {
        fail(msg::kEscaped);
        return result_;
    }
    const float left = timeLeft(f.clock);
    if (e == LessonEvent::Follow && lesson_.timeLimit > 0 && left <= 0) {
        // Kept up with it for the lesson's time.
        result_ = Result::Passed;
        show(msg::kYouWon);
        return result_;
    }
    if (lesson_.timeLimit > 0 && left <= 0) {
        fail(e == LessonEvent::CopChase     ? msg::kChaseTimesUp
             : e == LessonEvent::Destroy    ? msg::kDestroyTimesUp
             : e == LessonEvent::NoTouching ? msg::kTouchTimesUp
                                            : msg::kTimesUp);
        return result_;
    }

    // Passing: at the end (following, once the car's there with the player
    // near; chased, once the police are lost).
    if (e == LessonEvent::Follow) {
        if (f.targetDone && f.targetDistance >= 0 && f.targetDistance <= kEscape) {
            result_ = Result::Passed;
            show(msg::kYouWon);
        }
        return result_;
    }
    if (!f.finished)
        return result_;
    if ((e == LessonEvent::CopChase || e == LessonEvent::Destroy) && f.chasing > 0) {
        if (messageFor_ <= 0)
            show(msg::kLoseThem);
        return result_;
    }
    result_ = Result::Passed;
    switch (e) {
    case LessonEvent::Jump: show(msg::kGoodJumping); break;
    case LessonEvent::CopChase: show(msg::kSurvived); break;
    case LessonEvent::MinimumSpeed: show(msg::kGreatDriving); break;
    case LessonEvent::NoTouching: show(msg::kTouchWon); break;
    case LessonEvent::Destroy:
        show(msg::kSurvived);
        break;
    default: show(msg::kPass); break;
    }
    return result_;
}

} // namespace reluva::age
