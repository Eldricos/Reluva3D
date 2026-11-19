// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game/actor.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

namespace {

constexpr float kDegrees = 57.29578f;

bool contains(std::string_view s, std::string_view part) {
    return s.find(part) != std::string_view::npos;
}

// The steps, a degree at a time, from `from` to `to` turning left (the
// heading growing) and right.
void steps(float from, float to, float &left, float &right) {
    left = normalizeDegrees(to - from);
    right = normalizeDegrees(from - to);
}

} // namespace

float normalizeDegrees(float d) {
    d = std::fmod(d, 360.0f);
    return d < 0 ? d + 360.0f : d;
}

Actor::Actor(const Model &model, const Ac2Record *record, Loader load, Test test, unsigned seed)
    : _animator(model), _loader(std::move(load)), _test(std::move(test)), _record(record), _random(seed) {
    if (const Ac2Record *a = record ? record->part("ANIM") : nullptr) {
        auto f = [&](const char *name, float fallback) { return a->field(name) ? a->f(name) : fallback; };
        _talkEase = f("talkEase", _talkEase);
        _blinkMin = f("blinkMin", _blinkMin);
        _blinkMax = f("blinkMax", _blinkMax);
        _idleMin = f("idleMin", _idleMin);
        _idleMax = f("idleMax", _idleMax);
        if (a->field("idle2"))
            _chance2 = a->i("idle2"), _chance3 = a->i("idle3"), _chance4 = a->i("idle4");
    }
    _animator.onEnding = [this](const Animator::Slot &s) { ending(s); };
    _animator.onFinished = [this](const Animator::Slot &s) {
        if (onAnimDone)
            onAnimDone(s.name);
    };
}

int Actor::rand() {
    // The C library's (MSVC's) generator, as the game's.
    _random = _random * 214013u + 2531011u;
    return int((_random >> 16) & 0x7fff);
}

std::shared_ptr<const Animation> Actor::load(const std::string &name) {
    const auto it = _loaded.find(name);
    if (it != _loaded.end())
        return it->second;
    auto a = _loader ? _loader(name) : nullptr;
    _loaded.emplace(name, a);
    return a;
}

bool Actor::isBase(std::string_view name) {
    // 0x4049e0.
    const std::string n = fs::toLower(name);
    if (contains(n, "idle1") && !contains(n, "talkidle1"))
        return true;
    for (const char *b :
         {"walk", "walkfast", "run", "sit", "standup", "sitdown", "situp", "eavesstart", "eavesidle", "eavesend"})
        if (n == b)
            return true;
    return contains(n, "rotate");
}

std::string Actor::resolve(std::string_view name) {
    const std::string n = fs::toLower(name);
    if (_record) {
        // A renamed animation whose condition holds (0x404930).
        if (const Ac2Record *a = _record->part("ANIM"))
            for (const auto &[field, value] : a->fields) {
                if (field != "named" || value.pairs.empty())
                    continue;
                const auto &[condition, names] = value.pairs.front();
                const size_t slash = names.find(" / ");
                if (slash == std::string::npos)
                    continue;
                const std::string alias = fs::toLower(names.substr(0, slash));
                const std::string file = fs::toLower(names.substr(slash + 3));
                if (alias == n && !contains(file, ".wav") && (!_test || _test(condition)))
                    return file;
            }
        // MODL's prefix ("sit": sitidle1), if it has that file.
        if (const Ac2Record *m = _record->part("MODL"); m && !m->text("anim2").empty()) {
            if (!_test || _test(m->text("condition"))) {
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
        ease = base ? 2 * _talkEase : std::min(2.0f, animation->duration / 3);
    // A base animation sets the pose's mask that what plays over it is
    // limited to: the model's "TrackMask" + MODL's anim ("sit") while
    // MODL's condition holds, else none (0x429f96).
    if (base) {
        std::vector<float> mask;
        if (const Ac2Record *m = _record ? _record->part("MODL") : nullptr; m && !m->text("anim").empty())
            if (!_test || _test(m->text("condition")))
                mask = Animator::modelMask(_animator.model(), m->text("anim"));
        _animator.setPoseMask(std::move(mask));
    }
    if (!_animator.play(resolved, animation, Animator::kBody, base, loops, ease, weight))
        return {};
    return resolved;
}

void Actor::activate(bool walking) {
    // Counts its idle variations (0x405560): idle2.., talkidle2...
    _idles = _talkIdles = 0;
    while (_idles < 9 && load(resolve("idle" + std::to_string(_idles + 1))))
        _idles++;
    while (_talkIdles < 9 && load(resolve("talkidle" + std::to_string(_talkIdles + 1))))
        _talkIdles++;
    play(walking ? "walk" : "idle1", 0, 0);
    _animator.update(0.25f);
    _blinkLeft = _blinkMin + float(rand() % std::max(1, int(_blinkMax - _blinkMin)));
    resetIdleTimer();
}

void Actor::resetIdleTimer() {
    // 0x404100: shorter while talking.
    _idleLeft = _idleMin + float(rand() % std::max(1, int(_idleMax - _idleMin)));
    _idleLeft += _animator.playing("talk") ? -2.0f : 2.0f;
}

void Actor::stopGestures() {
    _animator.stop([](const Animator::Slot &s) { return !s.base && s.loops > 0 && s.layer == Animator::kBody; }, 0.3f);
}

std::string Actor::anim(std::string_view name, float weight, int loops, float ease) {
    // Finite gestures fade over 0.3 s first (0x4048f0).
    stopGestures();
    return play(name, loops, ease, weight);
}

void Actor::turnHead(float heading, float body, int upDown) {
    // 0x4412b0: the shorter way, a weight of a degree in 90 (at most 45).
    float left;
    float right;
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
        _animator.play(name, animation, Animator::kHead, false, 0, 0.5f, weight);
}

void Actor::headHome() {
    _animator.stop([](const Animator::Slot &s) { return s.layer == Animator::kHead || contains(s.name, "head"); });
}

void Actor::look(std::string_view where, float weight) {
    // 0x4411f0: the eyes' last eases out; eyehome has no file.
    _animator.stop([](const Animator::Slot &s) { return s.layer == Animator::kEyes || contains(s.name, "eye"); });
    const std::string name = "eye" + fs::toLower(where);
    if (const auto animation = load(name))
        _animator.play(name, animation, Animator::kEyes, false, 0, 0.5f, std::min(weight, 1.0f));
}

Actor::Motion Actor::motion(const Animation &animation) {
    const auto it = _motions.find(&animation);
    if (it != _motions.end())
        return it->second;
    Motion m;
    const Model &model = _animator.model();
    int root = -1;
    for (size_t b = 0; b < model.bones.size(); b++)
        if (model.bones[b].name == "Bip01")
            root = int(b);
    if (root >= 0 && animation.duration > 0) {
        // The mask it plays under as the base: a bone with none stays at
        // rest (Animator::blend).
        const Animator::Slot *base = _animator.base();
        const std::vector<float> *mask = base && base->animation.get() == &animation ? &base->mask : nullptr;
        auto measure = [&](bool masked, float drift[3], float &turn) {
            auto posed = [&](float t, Pose &out) {
                std::vector<BoneTransform> local;
                sampleLocal(model, &animation, t, local);
                if (masked && mask)
                    for (size_t b = 0; b < local.size() && b < mask->size(); b++)
                        if ((*mask)[b] <= 0)
                            local[b] = model.bones[b].rest;
                poseFromLocal(model, std::move(local), out);
            };
            Pose start;
            Pose end;
            posed(0, start);
            posed(animation.duration, end);
            const Mat4 &a = start.world[size_t(root)];
            const Mat4 &b = end.world[size_t(root)];
            for (int k = 0; k < 3; k++)
                drift[k] = b[size_t(12 + k)] - a[size_t(12 + k)];
            // The turn about the model's up (Z): where the root's most
            // level axis points, followed through the loop, as the game
            // adds up the root's turn frame by frame (0x441f70). (A turn of
            // a whole circle in a loop, as the vendors' rotatel, ends where
            // it began.)
            const int axis = std::abs(a[2]) < std::abs(a[6]) ? 0 : 4;
            auto yaw = [&](const Mat4 &w) { return std::atan2(w[size_t(axis + 1)], w[size_t(axis)]); };
            constexpr int kSteps = 32;
            float total = 0;
            float last = yaw(a);
            for (int k = 1; k <= kSteps; k++) {
                Pose p;
                posed(animation.duration * float(k) / kSteps, p);
                const float y = k == kSteps ? yaw(b) : yaw(p.world[size_t(root)]);
                total += std::remainder(y - last, 6.2831853f);
                last = y;
            }
            turn = total * kDegrees;
        };
        measure(false, m.drift, m.turn);
        measure(true, m.carried, m.carriedTurn);
    }
    _motions.emplace(&animation, m);
    return m;
}

bool Actor::rotateTo(float heading, float from) {
    _target = normalizeDegrees(heading);
    float left;
    float right;
    steps(normalizeDegrees(from), _target, left, right);
    if (std::min(left, right) < 2)
        return false;
    // The shorter way; a tie goes right. The turning animation by how
    // far: 45, 90, 180 where it has them (0x441730, 0x441690).
    _direction = left < right ? 1 : -1;
    const float way = _direction > 0 ? left : right;
    const std::string stem = _direction > 0 ? "rotatel" : "rotater";
    std::string name = stem;
    const std::string sized = stem + (way <= 50 ? "45" : way <= 100 ? "90" : "180");
    if (load(sized))
        name = sized;
    play(name, 0, 0);
    // At the animation's own pace (its root's turn), else 200 degrees a
    // second (0x441d90).
    _pace = 200;
    if (const Animator::Slot *s = _animator.find(name); s && s->animation->duration > 0) {
        const float turn = std::abs(motion(*s->animation).turn);
        if (turn > 1)
            _pace = turn / s->animation->duration;
    }
    _rotating = true;
    return true;
}

void Actor::advance(float seconds) {
    _animator.update(seconds);
}

void Actor::lineStart() {
    _animator.stop([](const Animator::Slot &s) { return !s.base && s.loops > 0 && s.layer == Animator::kBody; }, 0.3f);
    if (!_animator.playing("eavesidle") && !_animator.playing("talkidle1"))
        play("talkidle1", 0, 0);
}

void Actor::mouth(bool on) {
    if (on) {
        if (!_animator.find("talk"))
            play("talk", 0, _talkEase);
    } else {
        _animator.stop([](const Animator::Slot &s) { return s.name == "talk"; }, _talkEase);
    }
}

void Actor::lineEnd() {
    // 0x404760: the mouth over 0.15 s, the talking idles over their own.
    mouth(false);
    _animator.stop([](const Animator::Slot &s) { return contains(s.name, "talkidle"); });
}

void Actor::ending(const Animator::Slot &s) {
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
    auto pick = [&](int count, const char *stem) {
        std::string n;
        if (r < _chance2 || count == 1)
            n = std::string(stem) + "2";
        else if (r < _chance2 + _chance3 || count == 2)
            n = std::string(stem) + "3";
        else
            n = std::string(stem) + std::to_string(count <= 4 ? 4 : 4 + rand() % (count - 3));
        return n;
    };
    if (_animator.playing("talkidle1")) {
        if (_talkIdles < 1)
            return;
        _animator.stop([](const Animator::Slot &s) { return contains(s.name, "talkidle1"); }, 0.3f);
        play(pick(_talkIdles, "talkidle"), 1, 0);
        return;
    }
    if (_idles < 2 || _conversation || !_animator.playing("idle1"))
        return;
    for (const char *busy : {"idle2", "idle3", "idle4", "doorknobl", "doorknobr"})
        if (_animator.playing(busy))
            return;
    play(pick(_idles, "idle"), 1, 0);
}

void Actor::update(float seconds, float &heading) {
    // Blinks: every 5 to 7 seconds, once or twice (0x404210).
    if ((_blinkLeft -= seconds) <= 0) {
        _blinkLeft = _blinkMin + float(rand() % std::max(1, int(_blinkMax - _blinkMin)));
        // Played as any other (0x404670): renamed or prefixed if so.
        play("blink", (rand() & 1) + 1, 0.01f);
    }
    if ((_idles > 1 || _talkIdles > 1) && (_idleLeft -= seconds) <= 0) {
        resetIdleTimer();
        idleVariation();
    }
    _animator.update(seconds);
    if (!_rotating)
        return;
    heading = normalizeDegrees(heading);
    float left;
    float right;
    steps(heading, _target, left, right);
    const float left0 = _direction > 0 ? left : right;
    const float step = _pace * seconds;
    if (left0 - step < 2) {
        // There (0x4420b0): idle1, and the rotate is done.
        heading = _target;
        _rotating = false;
        play("idle1", 0, 0);
        if (onRotateDone)
            onRotateDone();
        return;
    }
    heading = normalizeDegrees(heading + float(_direction) * step);
}

} // namespace reluva::awe
