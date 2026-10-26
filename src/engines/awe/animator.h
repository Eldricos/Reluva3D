// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/model.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A character's animations as the game plays them on Granny controls
// (motoe.exe's model instance, 0x4275d0..0x42a650): up to ten at once, on
// three layers. The body's (0) are blended bone by bone by their weights
// times their track masks: a full-body "base" animation (idle1, walk,
// rotatel) under gestures that weigh only the bones they move. The head's
// (1) and the eyes' (2) are laid over the body's pose, each bone pulled
// toward theirs by its mask. See docs/formats/awe/characters.md.

namespace reluva::awe {

class Animator {
public:
    static constexpr int kSlots = 10;
    enum Layer { Body = 0, Head = 1, Eyes = 2 };
    enum State { Playing = 0, Ending = 1, Stopped = 2 };

    struct Slot {
        std::string name; // as asked for, lower case ("idle1", "headleft")
        std::shared_ptr<const Animation> animation;
        int layer = Body;
        bool base = false;
        int loops = 1;      // 0: for ever
        float clock = 0;    // seconds since it started
        float ease = 0;     // the ease in, and out when stopped
        float outStart = -1, outLength = 0; // easing out from outStart, for outLength
        int state = Playing;
        std::vector<float> mask; // per model bone
        float maskWeight = 0;    // the weight it was played with
        // Its weight now: eased in and out (Granny's smooth curve).
        float weight() const;
        // Seconds into the animation.
        float time() const;
    };

    explicit Animator(const Model& model) : model_(&model) {}
    const Model& model() const { return *model_; }

    // Plays an animation (0x429f50): `loops` times (0: for ever), easing
    // in over `ease` seconds. `weight`, if not 0, replaces its mask's
    // weights (a gesture played lightly over what's under it). A base
    // animation eases the last base out. False if all ten slots are
    // taken.
    bool play(std::string_view name, std::shared_ptr<const Animation> animation, int layer, bool base, int loops,
              float ease, float weight = 0);
    // Eases out (over `seconds`, or each one's own ease if negative) and
    // stops the slots `which` picks; a stopped slot doesn't report its end.
    void stop(const std::function<bool(const Slot&)>& which, float seconds = -1);
    // Moves the clocks on. A finite animation eases out over its last
    // ease's time ("ending"), then ends ("finished", unless stopped).
    void update(float seconds);
    // The body's pose, with the head and eyes over it.
    void pose(Pose& out, const std::vector<BoneTurn>* turns = nullptr) const;
    // Another model posed by the same animations, by its bones' names: a
    // prop the character holds, whose bones its animations move too.
    void poseFor(const Model& other, Pose& out) const;

    const std::vector<Slot>& slots() const { return slots_; }
    // The slot playing `name` (not stopped), or null.
    const Slot* find(std::string_view name) const;
    // Whether a slot whose name contains `part` is playing.
    bool playing(std::string_view part) const;
    const Slot* base() const;

    std::function<void(const Slot&)> onEnding, onFinished;

private:
    std::vector<float> buildMask(const Model& model, const Animation* animation, float weight) const;
    // Blends the layer's slots: each bone's transform weighed by the
    // slots' weights times their masks; bones that weigh nothing stay at
    // rest. The strongest slot is returned through `strongest`.
    void blend(const Model& model, int layer, std::vector<BoneTransform>& out, std::vector<float>& total,
               const Slot** strongest) const;

    const Model* model_;
    std::vector<Slot> slots_;
};

} // namespace reluva::awe
