// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/actor.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

namespace {

constexpr float kDegrees = 57.29578f;

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// The steps, a degree at a time, from `from` to `to` turning left (the
// heading growing) and right.
void steps(float from, float to, float& left, float& right) {
    left = normalizeDegrees(to - from);
    right = normalizeDegrees(from - to);
}

} // namespace

float normalizeDegrees(float d) {
    d = std::fmod(d, 360.0f);
    return d < 0 ? d + 360.0f : d;
}

Actor::Actor(const Model& model, const Ac2Record* record, Loader load, Test test, unsigned seed)
    : animator_(model), loader_(std::move(load)), test_(std::move(test)), record_(record), random_(seed) {
    if (const Ac2Record* a = record ? record->part("ANIM") : nullptr) {
        auto f = [&](const char* name, float fallback) {
            return a->field(name) ? a->f(name) : fallback;
        };
        talkEase_ = f("talkEase", talkEase_);
        blinkMin_ = f("blinkMin", blinkMin_);
        blinkMax_ = f("blinkMax", blinkMax_);
        idleMin_ = f("idleMin", idleMin_);
        idleMax_ = f("idleMax", idleMax_);
        if (a->field("idle2"))
            chance2_ = a->i("idle2"), chance3_ = a->i("idle3"), chance4_ = a->i("idle4");
    }
    animator_.onEnding = [this](const Animator::Slot& s) { ending(s); };
    animator_.onFinished = [this](const Animator::Slot& s) {
        if (onAnimDone)
            onAnimDone(s.name);
    };
}

int Actor::rand() {
    // The C library's (MSVC's) generator, as the game's.
    random_ = random_ * 214013u + 2531011u;
    return int((random_ >> 16) & 0x7fff);
}

std::shared_ptr<const Animation> Actor::load(const std::string& name) {
    const auto it = loaded_.find(name);
    if (it != loaded_.end())
        return it->second;
    auto a = loader_ ? loader_(name) : nullptr;
    loaded_.emplace(name, a);
    return a;
}

bool Actor::isBase(std::string_view name) {
    // 0x4049e0.
    const std::string n = fs::toLower(name);
    if (contains(n, "idle1") && !contains(n, "talkidle1"))
        return true;
    for (const char* b : {"walk", "walkfast", "run", "sit", "standup", "sitdown", "situp", "eavesstart", "eavesidle",
                          "eavesend"})
        if (n == b)
            return true;
    return contains(n, "rotate");
}

std::string Actor::resolve(std::string_view name) {
    const std::string n = fs::toLower(name);
    if (record_) {
        // A renamed animation whose condition holds (0x404930).
        if (const Ac2Record* a = record_->part("ANIM"))
            for (const auto& [field, value] : a->fields) {
                if (field != "named" || value.pairs.empty())
                    continue;
                const auto& [condition, names] = value.pairs.front();
                const size_t slash = names.find(" / ");
                if (slash == std::string::npos)
                    continue;
                const std::string alias = fs::toLower(names.substr(0, slash));
                const std::string file = fs::toLower(names.substr(slash + 3));
                if (alias == n && !contains(file, ".wav") && (!test_ || test_(condition)))
                    return file;
            }
        // MODL's prefix ("sit": sitidle1), if it has that file.
        if (const Ac2Record* m = record_->part("MODL"); m && !m->text("anim2").empty()) {
            if (!test_ || test_(m->text("condition"))) {
                const std::string prefixed = fs::toLower(m->text("anim2")) + n;
                if (load(prefixed))
                    return prefixed;
            }
        }
    }
    return n;
}

std::string Actor::play(std::string_view name, int loops, float ease, float weight) {
    const std::string resolved = resolve(name);
    const auto animation = load(resolved);
    if (!animation)
        return {};
    const bool base = isBase(name);
    // The ease: base animations twice the talk ease; others a third of
    // their length, at most 2 seconds (0x429f50).
    if (ease == 0)
        ease = base ? 2 * talkEase_ : std::min(2.0f, animation->duration / 3);
    // A base animation sets the pose's mask that what plays over it is
    // limited to: the model's "TrackMask" + MODL's anim ("sit") while
    // MODL's condition holds, else none (0x429f96).
    if (base) {
        std::vector<float> mask;
        if (const Ac2Record* m = record_ ? record_->part("MODL") : nullptr; m && !m->text("anim").empty())
            if (!test_ || test_(m->text("condition")))
                mask = Animator::modelMask(animator_.model(), m->text("anim"));
        animator_.setPoseMask(std::move(mask));
    }
    if (!animator_.play(resolved, animation, Animator::Body, base, loops, ease, weight))
        return {};
    return resolved;
}

void Actor::activate(bool walking) {
    // Counts its idle variations (0x405560): idle2.., talkidle2...
    idles_ = talkIdles_ = 0;
    while (idles_ < 9 && load(resolve("idle" + std::to_string(idles_ + 1))))
        idles_++;
    while (talkIdles_ < 9 && load(resolve("talkidle" + std::to_string(talkIdles_ + 1))))
        talkIdles_++;
    play(walking ? "walk" : "idle1", 0, 0);
    animator_.update(0.25f);
    blinkLeft_ = blinkMin_ + float(rand() % std::max(1, int(blinkMax_ - blinkMin_)));
    resetIdleTimer();
}

void Actor::resetIdleTimer() {
    // 0x404100: shorter while talking.
    idleLeft_ = idleMin_ + float(rand() % std::max(1, int(idleMax_ - idleMin_)));
    idleLeft_ += animator_.playing("talk") ? -2.0f : 2.0f;
}

std::string Actor::anim(std::string_view name, float weight, int loops, float ease) {
    // Finite gestures fade over 0.3 s first (0x4048f0).
    animator_.stop([](const Animator::Slot& s) { return !s.base && s.loops > 0 && s.layer == Animator::Body; }, 0.3f);
    return play(name, loops, ease, weight);
}

void Actor::turnHead(float heading, float body, int upDown) {
    // 0x4412b0: the shorter way, a weight of a degree in 90 (at most 45).
    float left, right;
    steps(body, normalizeDegrees(heading), left, right);
    if (std::min(left, right) < 2)
        return;
    const bool toLeft = left < right;
    const float delta = std::min(toLeft ? left : right, 45.0f);
    std::string name = toLeft ? "headleft" : "headright";
    if (upDown == 1)
        name += "up";
    else if (upDown == 2)
        name += "down";
    headAnimation(name, delta / 90);
}

void Actor::headAnimation(std::string_view name, float weight) {
    // 0x441270: the head's last eases out; this one holds for ever.
    headHome();
    const auto animation = load(fs::toLower(name));
    if (animation)
        animator_.play(name, animation, Animator::Head, false, 0, 0.5f, weight);
}

void Actor::headHome() {
    animator_.stop([](const Animator::Slot& s) { return s.layer == Animator::Head || contains(s.name, "head"); });
}

void Actor::look(std::string_view where, float weight) {
    // 0x4411f0: the eyes' last eases out; eyehome has no file.
    animator_.stop([](const Animator::Slot& s) { return s.layer == Animator::Eyes || contains(s.name, "eye"); });
    const std::string name = "eye" + fs::toLower(where);
    if (const auto animation = load(name))
        animator_.play(name, animation, Animator::Eyes, false, 0, 0.5f, std::min(weight, 1.0f));
}

Actor::Motion Actor::motion(const Animation& animation) {
    const auto it = motions_.find(&animation);
    if (it != motions_.end())
        return it->second;
    Motion m;
    const Model& model = animator_.model();
    int root = -1;
    for (size_t b = 0; b < model.bones.size(); b++)
        if (model.bones[b].name == "Bip01")
            root = int(b);
    if (root >= 0 && animation.duration > 0) {
        Pose start, end;
        computePose(model, &animation, 0, start);
        computePose(model, &animation, animation.duration, end);
        const Mat4& a = start.world[size_t(root)];
        const Mat4& b = end.world[size_t(root)];
        for (int k = 0; k < 3; k++)
            m.drift[k] = b[size_t(12 + k)] - a[size_t(12 + k)];
        // The turn about the model's up (Z): where the root's most level
        // axis points, before and after.
        const int axis = std::abs(a[2]) < std::abs(a[6]) ? 0 : 4;
        const float ya = std::atan2(a[size_t(axis + 1)], a[size_t(axis)]);
        const float yb = std::atan2(b[size_t(axis + 1)], b[size_t(axis)]);
        m.turn = std::remainder(yb - ya, 6.2831853f) * kDegrees;
    }
    motions_.emplace(&animation, m);
    return m;
}

bool Actor::rotateTo(float heading, float from) {
    target_ = normalizeDegrees(heading);
    float left, right;
    steps(normalizeDegrees(from), target_, left, right);
    if (std::min(left, right) < 2)
        return false;
    // The shorter way; a tie goes right. The turning animation by how
    // far: 45, 90, 180 where it has them (0x441730, 0x441690).
    direction_ = left < right ? 1 : -1;
    const float way = direction_ > 0 ? left : right;
    const std::string stem = direction_ > 0 ? "rotatel" : "rotater";
    std::string name = stem;
    const std::string sized = stem + (way <= 50 ? "45" : way <= 100 ? "90" : "180");
    if (load(sized))
        name = sized;
    play(name, 0, 0);
    // At the animation's own pace (its root's turn), else 200 degrees a
    // second (0x441d90).
    pace_ = 200;
    if (const Animator::Slot* s = animator_.find(name); s && s->animation->duration > 0) {
        const float turn = std::abs(motion(*s->animation).turn);
        if (turn > 1)
            pace_ = turn / s->animation->duration;
    }
    rotating_ = true;
    return true;
}

void Actor::advance(float seconds) { animator_.update(seconds); }

void Actor::lineStart() {
    animator_.stop([](const Animator::Slot& s) { return !s.base && s.loops > 0 && s.layer == Animator::Body; }, 0.3f);
    if (!animator_.playing("eavesidle") && !animator_.playing("talkidle1"))
        play("talkidle1", 0, 0);
}

void Actor::mouth(bool on) {
    if (on) {
        if (!animator_.find("talk"))
            play("talk", 0, talkEase_);
    } else {
        animator_.stop([](const Animator::Slot& s) { return s.name == "talk"; }, talkEase_);
    }
}

void Actor::lineEnd() {
    // 0x404760: the mouth over 0.15 s, the talking idles over their own.
    mouth(false);
    animator_.stop([](const Animator::Slot& s) { return contains(s.name, "talkidle"); });
}

void Actor::ending(const Animator::Slot& s) {
    // 0x405050: after a gesture the idle timer starts again; a finite
    // base hands over to idle1; a talking idle's variation to talkidle1.
    if (s.name == "blink" || s.name == "talk")
        return;
    resetIdleTimer();
    if (s.base) {
        play(s.name == "eavesstart" ? "eavesidle" : s.name == "situp" ? "situpidle1" : "idle1", 0, 0);
    } else if (contains(s.name, "talkidle")) {
        play("talkidle1", 0, 0);
    }
}

void Actor::idleVariation() {
    // 0x404350: 50/30/20 among the second, third and later variations.
    const int r = rand() % 100;
    auto pick = [&](int count, const char* stem) {
        std::string n;
        if (r < chance2_ || count == 1)
            n = std::string(stem) + "2";
        else if (r < chance2_ + chance3_ || count == 2)
            n = std::string(stem) + "3";
        else
            n = std::string(stem) + std::to_string(count <= 4 ? 4 : 4 + rand() % (count - 3));
        return n;
    };
    if (animator_.playing("talkidle1")) {
        if (talkIdles_ < 1)
            return;
        animator_.stop([](const Animator::Slot& s) { return contains(s.name, "talkidle1"); }, 0.3f);
        play(pick(talkIdles_, "talkidle"), 1, 0);
        return;
    }
    if (idles_ < 2 || conversation_ || !animator_.playing("idle1"))
        return;
    for (const char* busy : {"idle2", "idle3", "idle4", "doorknobl", "doorknobr"})
        if (animator_.playing(busy))
            return;
    play(pick(idles_, "idle"), 1, 0);
}

void Actor::update(float seconds, float& heading) {
    // Blinks: every 5 to 7 seconds, once or twice (0x404210).
    if ((blinkLeft_ -= seconds) <= 0) {
        blinkLeft_ = blinkMin_ + float(rand() % std::max(1, int(blinkMax_ - blinkMin_)));
        // Played as any other (0x404670): renamed or prefixed if so.
        play("blink", (rand() & 1) + 1, 0.01f);
    }
    if ((idles_ > 1 || talkIdles_ > 1) && (idleLeft_ -= seconds) <= 0) {
        resetIdleTimer();
        idleVariation();
    }
    animator_.update(seconds);
    if (!rotating_)
        return;
    heading = normalizeDegrees(heading);
    float left, right;
    steps(heading, target_, left, right);
    const float left0 = direction_ > 0 ? left : right;
    const float step = pace_ * seconds;
    if (left0 - step < 2) {
        // There (0x4420b0): idle1, and the rotate is done.
        heading = target_;
        rotating_ = false;
        play("idle1", 0, 0);
        if (onRotateDone)
            onRotateDone();
        return;
    }
    heading = normalizeDegrees(heading + float(direction_) * step);
}

} // namespace reluva::awe
