// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game/animator.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace reluva::awe {

namespace {

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

} // namespace

float Animator::Slot::weight() const {
    float w = ease > 0 ? smooth(clock / ease) : 1.0f;
    if (outStart >= 0)
        w *= outLength > 0 ? 1 - smooth((clock - outStart) / outLength) : 0.0f;
    return w;
}

float Animator::Slot::time() const {
    const float d = animation ? animation->duration : 0;
    if (d <= 0)
        return 0;
    if (loops > 0 && clock >= d * float(loops))
        return d;
    return std::fmod(clock, d);
}

CurveLoop Animator::Slot::loop() const {
    CurveLoop l;
    l.duration = animation ? animation->duration : 0;
    if (loops == 0) {
        l.back = l.forward = true;
    } else if (loops > 1 && l.duration > 0) {
        const int index = std::min(loops - 1, int(clock / l.duration));
        l.back = index > 0;
        l.forward = index < loops - 1;
    }
    return l;
}

std::vector<float> Animator::buildMask(const Model &model, const Animation *animation, float weight,
                                       const std::vector<float> &poseMask) const {
    // The file's TrackMask weights by bone (0x42a2d4), a bone at a time
    // from the root: a weight given replaces the file's; a bone without
    // one takes its parent's, except the jaw's and eyes' masters, unless
    // the parent's is the 0.01 of the full-body animations: a gesture's
    // shoulders don't move the face. A file without a mask plays in full.
    const std::vector<Bone> &bones = model.bones;
    std::vector<float> mask(bones.size(), 1.0f);
    if (animation && !animation->mask.empty()) {
        for (size_t b = 0; b < bones.size(); b++) {
            const auto it = animation->mask.find(bones[b].name);
            float w = it == animation->mask.end() ? 0.0f : it->second;
            if (w != 0) {
                if (weight != 0)
                    w = weight;
            } else if (bones[b].parent >= 0) {
                const float parent = mask[size_t(bones[b].parent)];
                const std::string name = fs::toLower(bones[b].name);
                const bool master = name == "jaw_bones_master" || name == "eye_bones_master";
                if (parent > 0 && (!master || parent == 0.01f))
                    w = parent;
            }
            mask[b] = w;
        }
    }
    // Times the pose's mask, bone by bone (0x42a3da, 0x42a88a).
    if (!poseMask.empty())
        for (size_t b = 0; b < bones.size(); b++) {
            size_t i = b;
            if (&model != _model) {
                i = 0;
                while (i < _model->bones.size() && _model->bones[i].name != bones[b].name)
                    i++;
            }
            mask[b] *= i < poseMask.size() ? poseMask[i] : 0.0f;
        }
    return mask;
}

std::vector<float> Animator::modelMask(const Model &model, std::string_view name) {
    // Granny's ExtractTrackMask on the model's skeleton, then each bone
    // without a weight takes its parent's (0x42a08c). The member's name is
    // matched whatever its case: the animation files spell the game's
    // "TrackMaskTrackMask" both "TrackMasktrackmask" and
    // "TrackMaskTrackMask".
    const std::string n = fs::toLower(name);
    const std::map<std::string, float> *weights = nullptr;
    for (const auto &[k, v] : model.masks)
        if (fs::toLower(k) == n)
            weights = &v;
    if (!weights)
        return {};
    std::vector<float> mask(model.bones.size(), 0.0f);
    for (size_t b = 0; b < model.bones.size(); b++) {
        const auto it = weights->find(model.bones[b].name);
        float w = it == weights->end() ? 0.0f : it->second;
        if (w == 0 && model.bones[b].parent >= 0)
            w = std::max(0.0f, mask[size_t(model.bones[b].parent)]);
        mask[b] = w;
    }
    return mask;
}

bool Animator::play(std::string_view name, std::shared_ptr<const Animation> animation, int layer, bool base, int loops,
                    float ease, float weight) {
    if (!animation)
        return false;
    // A new base stops the one playing; one already ending (a finite
    // base handing over to idle1) ends as it was, and is told as done.
    if (base)
        for (Slot &s : _slots)
            if (s.base && s.state == kPlaying) {
                s.state = kStopped;
                s.outStart = s.clock;
                s.outLength = s.ease;
            }
    if (_slots.size() >= kSlots)
        return false;
    Slot s;
    s.name = fs::toLower(name);
    s.layer = layer;
    s.base = base;
    s.loops = std::max(0, loops);
    s.ease = ease;
    if (!base)
        s.poseMask = _poseMask;
    s.mask = buildMask(*_model, animation.get(), weight, s.poseMask);
    s.maskWeight = weight;
    s.animation = std::move(animation);
    _slots.push_back(std::move(s));
    return true;
}

void Animator::setCarry(const float carried[3], float turn) {
    if (_slots.empty())
        return;
    for (int k = 0; k < 3; k++)
        _slots.back().carried[k] = carried[k];
    _slots.back().carriedTurn = turn;
}

void Animator::stop(const std::function<bool(const Slot &)> &which, float seconds) {
    for (Slot &s : _slots)
        if (s.state != kStopped && which(s)) {
            s.state = kStopped;
            s.outStart = s.clock;
            s.outLength = seconds >= 0 ? seconds : s.ease;
        }
}

void Animator::update(float seconds) {
    std::vector<Slot> ending;
    std::vector<Slot> finished;
    for (Slot &s : _slots) {
        s.clock += seconds;
        const float d = s.animation->duration;
        if (s.loops == 0 || d <= 0)
            continue;
        const float total = d * float(s.loops);
        // Near its end it eases out over what's left (0x427afa).
        if (s.state == kPlaying && total - s.clock <= s.ease) {
            s.state = kEnding;
            s.outStart = s.clock;
            s.outLength = std::max(0.0f, total - s.clock);
            ending.push_back(s);
        }
    }
    // Ended: the finite ones at their end, the stopped once eased out.
    for (auto it = _slots.begin(); it != _slots.end();) {
        const float d = it->animation->duration;
        const bool done = (it->loops > 0 && it->clock >= d * float(it->loops)) ||
                          (it->state == kStopped && it->clock >= it->outStart + it->outLength) || d <= 0;
        if (!done) {
            ++it;
            continue;
        }
        if (it->state != kStopped)
            finished.push_back(*it);
        it = _slots.erase(it);
    }
    // Told after, as they may play others.
    for (const Slot &s : ending)
        if (onEnding)
            onEnding(s);
    for (const Slot &s : finished)
        if (onFinished)
            onFinished(s);
}

const Animator::Slot *Animator::find(std::string_view name) const {
    const std::string n = fs::toLower(name);
    for (const Slot &s : _slots)
        if (s.state != kStopped && s.name == n)
            return &s;
    return nullptr;
}

bool Animator::playing(std::string_view part) const {
    const std::string p = fs::toLower(part);
    for (const Slot &s : _slots)
        if (s.state != kStopped && s.name.find(p) != std::string::npos)
            return true;
    return false;
}

const Animator::Slot *Animator::base() const {
    const Slot *found = nullptr;
    for (const Slot &s : _slots)
        if (s.base && s.state != kStopped)
            found = &s;
    return found;
}

void Animator::carryBack(const Model &model, const Slot &s, const std::vector<float> &mask,
                         const std::vector<bool> &bound, std::vector<BoneTransform> &local) {
    // The skeleton back by as far as this loop has carried the root
    // (Bip01) so far, and turned back, so the character stays where its
    // placement puts it while the walk moves it (Actor::motion's drift,
    // the character's x and z): each slot so, two of a walk fading into
    // each other at different points of the loop stay together. Done to
    // the highest bone from the top (GrannyRootBone) down to the root
    // that the slot moves, its track there and its mask letting it
    // through: above it the bones are at rest, at the model's origin.
    const float d = s.animation->duration;
    if (d <= 0)
        return;
    size_t root = 0;
    while (root < model.bones.size() && model.bones[root].name != "Bip01")
        root++;
    if (root == model.bones.size())
        return;
    std::vector<size_t> chain{root};
    while (model.bones[chain.back()].parent >= 0)
        chain.push_back(size_t(model.bones[chain.back()].parent));
    size_t at = model.bones.size();
    for (auto it = chain.rbegin(); it != chain.rend() && at == model.bones.size(); ++it)
        if (bound[*it] && mask[*it] > 0)
            at = *it;
    if (at == model.bones.size())
        return;
    root = at;
    const float loop = std::fmod(s.time(), d) / d;
    const float a = -loop * s.carriedTurn / 57.29578f;
    const float c = std::cos(a);
    const float n = std::sin(a);
    BoneTransform &t = local[root];
    // Back along the way it went, then turned back: a turn's step aside
    // (rotater180's 115 units) is taken out where the turn has put it,
    // so the body stays on the spot as the character turns.
    const float x = t.position[0] - loop * s.carried[0];
    const float y = t.position[1] - loop * s.carried[1];
    t.position[0] = c * x - n * y;
    t.position[1] = n * x + c * y;
    t.position[2] -= loop * s.carried[2];
    // The turn about Z before the bone's own: (0, 0, sin a/2, cos a/2) times q.
    const float hz = std::sin(a / 2);
    const float hw = std::cos(a / 2);
    const float *q = t.orientation;
    const float r[4] = {hw * q[0] - hz * q[1], hw * q[1] + hz * q[0], hw * q[2] + hz * q[3], hw * q[3] - hz * q[2]};
    for (int k = 0; k < 4; k++)
        t.orientation[k] = r[k];
}

void Animator::blend(const Model &model, int layer, std::vector<BoneTransform> &out, std::vector<float> &total,
                     const Slot **strongest) const {
    const size_t n = model.bones.size();
    out.assign(n, BoneTransform());
    for (BoneTransform &t : out) {
        t.position[0] = t.position[1] = t.position[2] = 0;
        t.orientation[3] = 0;
        std::fill(std::begin(t.scaleShear), std::end(t.scaleShear), 0.0f);
    }
    total.assign(n, 0.0f);
    float best = -1;
    std::vector<BoneTransform> local;
    std::vector<bool> bound;
    for (const Slot &s : _slots) {
        if (s.layer != layer)
            continue;
        const float w = s.weight();
        if (w <= 0)
            continue;
        if (w > best && strongest)
            best = w, *strongest = &s;
        // A base animation's root carries the character along (a walk's
        // whole loop's distance): the game takes that motion out (its
        // track groups accumulate it, and GrannyUpdateModelMatrix moves
        // the model), Reluva by the drift it measures on the root played
        // once (Actor::motion). So the root doesn't wrap round.
        sampleLocal(model, s.animation.get(), s.time(), local, s.loop(), !s.base, &bound);
        const std::vector<float> other =
            &model == _model ? std::vector<float>() : buildMask(model, s.animation.get(), s.maskWeight, s.poseMask);
        const std::vector<float> &mask = &model == _model ? s.mask : other;
        if (s.carriedTurn != 0 || s.carried[0] != 0 || s.carried[1] != 0 || s.carried[2] != 0)
            carryBack(model, s, mask, bound, local);
        for (size_t b = 0; b < n; b++) {
            // A bone the animation has no track for isn't its: Granny binds
            // only the tracks it has, so a talk's head and jaw (its 13 of
            // 88 bones) leave the body to the idle under it.
            const float bw = bound[b] ? w * mask[b] : 0.0f;
            if (bw <= 0)
                continue;
            BoneTransform &o = out[b];
            const BoneTransform &l = local[b];
            // Quaternions on the same side as what's summed so far.
            const float dot = o.orientation[0] * l.orientation[0] + o.orientation[1] * l.orientation[1] +
                              o.orientation[2] * l.orientation[2] + o.orientation[3] * l.orientation[3];
            const float sign = dot < 0 ? -1.0f : 1.0f;
            for (int k = 0; k < 3; k++)
                o.position[k] += bw * l.position[k];
            for (int k = 0; k < 4; k++)
                o.orientation[k] += sign * bw * l.orientation[k];
            for (int k = 0; k < 9; k++)
                o.scaleShear[k] += bw * l.scaleShear[k];
            total[b] += bw;
        }
    }
    // Normalised; below Granny's threshold, the rest pose.
    for (size_t b = 0; b < n; b++) {
        BoneTransform &o = out[b];
        if (total[b] < 1e-4f) {
            o = model.bones[b].rest;
            continue;
        }
        for (float &v : o.position)
            v /= total[b];
        for (float &v : o.scaleShear)
            v /= total[b];
        float len = 0;
        for (float v : o.orientation)
            len += v * v;
        len = std::sqrt(len);
        if (len > 0)
            for (float &v : o.orientation)
                v /= len;
        else
            o.orientation[3] = 1;
    }
}

void Animator::pose(Pose &out, const std::vector<BoneTurn> *turns) const {
    std::vector<BoneTransform> body;
    std::vector<BoneTransform> layer;
    std::vector<float> total;
    blend(*_model, kBody, body, total, nullptr);
    // The head and eyes over it (GrannyModulationCompositeLocalPose): each
    // bone toward the layer's, by the strongest slot's mask times how far
    // the layer has eased in (its weight over its ease time, at most 1).
    for (int l : {kHead, kEyes}) {
        const Slot *strongest = nullptr;
        blend(*_model, l, layer, total, &strongest);
        if (!strongest)
            continue;
        float factor = 0;
        for (const Slot &s : _slots)
            if (s.layer == l)
                factor = std::max(factor, s.ease > 0 ? s.weight() / s.ease : s.weight());
        factor = std::min(1.0f, factor);
        for (size_t b = 0; b < body.size(); b++) {
            const float t = strongest->mask[b] * factor;
            if (t <= 0)
                continue;
            BoneTransform &o = body[b];
            const BoneTransform &p = layer[b];
            for (int k = 0; k < 3; k++)
                o.position[k] += (p.position[k] - o.position[k]) * t;
            float dot = 0;
            for (int k = 0; k < 4; k++)
                dot += o.orientation[k] * p.orientation[k];
            const float sign = dot < 0 ? -1.0f : 1.0f;
            float len = 0;
            for (int k = 0; k < 4; k++) {
                o.orientation[k] += (sign * p.orientation[k] - o.orientation[k]) * t;
                len += o.orientation[k] * o.orientation[k];
            }
            len = std::sqrt(len);
            if (len > 0)
                for (float &v : o.orientation)
                    v /= len;
            for (int k = 0; k < 9; k++)
                o.scaleShear[k] += (p.scaleShear[k] - o.scaleShear[k]) * t;
        }
    }
    poseFromLocal(*_model, std::move(body), out, turns);
}

void Animator::poseFor(const Model &other, Pose &out) const {
    std::vector<BoneTransform> body;
    std::vector<float> total;
    blend(other, kBody, body, total, nullptr);
    poseFromLocal(other, std::move(body), out);
}

} // namespace reluva::awe
