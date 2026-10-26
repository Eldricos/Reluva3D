// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/ac2.h"
#include "engines/awe/animator.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A character's behaviour in motoe.exe (its ANIM part, 0x403e10..0x405940,
// and the turns of 0x4411f0..0x4420b0): which file an animation's name
// plays, full-body or over it; blinking and idle variations; its head
// turned and its eyes; its body rotated at the pace of its turning
// animation; talking. The scripts' character verbs (anim, turn, look,
// rotate, animate) end here. See docs/formats/awe/characters.md.

namespace reluva::awe {

class Actor {
public:
    // How it loads `<who>_<name>.gr2`: null if there's no such file.
    using Loader = std::function<std::shared_ptr<const Animation>(const std::string& name)>;
    // Whether a condition holds (the renamed animations', MODL's).
    using Test = std::function<bool(const std::string& condition)>;

    // Its settings from the scene's record (MODL, ANIM; defaults else).
    Actor(const Model& model, const Ac2Record* record, Loader load, Test test, unsigned seed = 1);

    Animator& animator() { return animator_; }
    const Animator& animator() const { return animator_; }

    // A full-body animation (idle1, walk, rotatel...: 0x4049e0), which
    // replaces the last; else one laid over it.
    static bool isBase(std::string_view name);
    // Plays an animation (0x404b00): its renamed file, or with MODL's
    // prefix (sitidle1), or itself; `loops` 0 for ever; `ease` 0 for the
    // default; `weight` 0 for its mask's. The name played, "" if none.
    std::string play(std::string_view name, int loops, float ease, float weight = 0);
    // Placed in a scene (0x4052d0): idle1 for ever, or walk.
    void activate(bool walking = false);

    // "anim <name> [w] [loops] [ease]" (0x40c367): finite gestures fade
    // first. The name it plays as ("" if none: no file); onAnimDone(that
    // name) when it ends.
    std::string anim(std::string_view name, float weight, int loops, float ease);
    // "turn": the head toward a heading (degrees, the game's: 0 faces +Z,
    // more is to the character's left), up (1), down (2) or level (0)
    // (0x4412b0); within 2 degrees of the body's (`body`), nothing.
    void turnHead(float heading, float body, int upDown);
    // "turn up|down [w]" (headup, headdown), and "turn home".
    void headAnimation(std::string_view name, float weight);
    void headHome();
    // "look <left|right|home> [w]": the eyes (0x4411f0).
    void look(std::string_view where, float weight);
    // "rotate": the body from `from` to a heading in degrees (0x441c90),
    // the shorter way with the turning animation for how far; false if
    // it's within 2 degrees already. onRotateDone() when it gets there.
    bool rotateTo(float heading, float from);
    bool rotating() const { return rotating_; }
    // "animate <seconds>": the animations' clocks jump on.
    void advance(float seconds);

    // Dialogue: a line starts (0x404720), the mouth moves or not
    // (0x4046c0, 0x4046f0), the line ends (0x404760).
    void lineStart();
    void mouth(bool on);
    void lineEnd();
    // While a conversation is on screen idle variations wait.
    void setConversation(bool on) { conversation_ = on; }

    // Moves on by `seconds`: blinks, idle variations, the animations, a
    // rotation (the heading in degrees is the character's, turned here).
    void update(float seconds, float& heading);

    // How far a base animation carries the body in one play: along the
    // model's axes, and turned (degrees). From its root bone (Bip01).
    struct Motion {
        float drift[3] = {0, 0, 0};
        float turn = 0;
    };
    Motion motion(const Animation& animation);

    std::function<void(const std::string& name)> onAnimDone;
    std::function<void()> onRotateDone;

private:
    std::shared_ptr<const Animation> load(const std::string& name);
    std::string resolve(std::string_view name);
    int rand();
    void resetIdleTimer();
    void idleVariation();
    void ending(const Animator::Slot& s);

    Animator animator_;
    Loader loader_;
    Test test_;
    const Ac2Record* record_;
    std::map<std::string, std::shared_ptr<const Animation>> loaded_;
    std::map<const Animation*, Motion> motions_;
    float talkEase_ = 0.15f, blinkMin_ = 5, blinkMax_ = 8, idleMin_ = 8, idleMax_ = 12;
    int chance2_ = 50, chance3_ = 30, chance4_ = 20;
    int idles_ = 0, talkIdles_ = 0;
    float blinkLeft_ = 0, idleLeft_ = 0;
    bool conversation_ = false;
    bool rotating_ = false;
    float target_ = 0;   // degrees
    int direction_ = 1;  // +1 left, -1 right
    float pace_ = 200;   // degrees a second
    unsigned random_;
};

// Degrees in [0, 360).
float normalizeDegrees(float d);

} // namespace reluva::awe
