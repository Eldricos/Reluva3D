// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/animator.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

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

std::vector<float> Animator::buildMask(const Model& model, const Animation* animation, float weight) const {
    // The file's TrackMask weights by bone; a bone with none takes its
    // parent's, except the jaw's and eyes' masters, unless the parent's is
    // the 0.01 of the full-body animations (0x429f50): a gesture's
    // shoulders don't move the face.
    const std::vector<Bone>& bones = model.bones;
    std::vector<float> mask(bones.size(), 1.0f);
    if (!animation || animation->mask.empty())
        return mask;
    for (size_t b = 0; b < bones.size(); b++) {
        const auto it = animation->mask.find(bones[b].name);
        float w = it == animation->mask.end() ? 0.0f : it->second;
        if (w == 0 && bones[b].parent >= 0) {
            const float parent = mask[size_t(bones[b].parent)];
            const std::string name = fs::toLower(bones[b].name);
            const bool master = name == "jaw_bones_master" || name == "eye_bones_master";
            if (!master || std::abs(parent - 0.01f) < 1e-6f)
                w = parent;
        }
        mask[b] = w;
    }
    if (weight != 0)
        for (float& w : mask)
            if (w != 0)
                w = weight;
    return mask;
}

bool Animator::play(std::string_view name, std::shared_ptr<const Animation> animation, int layer, bool base,
                    int loops, float ease, float weight) {
    if (!animation)
        return false;
    if (base)
        for (Slot& s : slots_)
            if (s.base && s.state != Stopped) {
                s.state = Stopped;
                s.outStart = s.clock;
                s.outLength = s.ease;
            }
    if (slots_.size() >= kSlots)
        return false;
    Slot s;
    s.name = fs::toLower(name);
    s.layer = layer;
    s.base = base;
    s.loops = std::max(0, loops);
    s.ease = ease;
    s.mask = buildMask(*model_, animation.get(), weight);
    s.maskWeight = weight;
    s.animation = std::move(animation);
    slots_.push_back(std::move(s));
    return true;
}

void Animator::stop(const std::function<bool(const Slot&)>& which, float seconds) {
    for (Slot& s : slots_)
        if (s.state != Stopped && which(s)) {
            s.state = Stopped;
            s.outStart = s.clock;
            s.outLength = seconds >= 0 ? seconds : s.ease;
        }
}

void Animator::update(float seconds) {
    std::vector<Slot> ending, finished;
    for (Slot& s : slots_) {
        s.clock += seconds;
        const float d = s.animation->duration;
        if (s.loops == 0 || d <= 0)
            continue;
        const float total = d * float(s.loops);
        // Near its end it eases out over what's left (0x427afa).
        if (s.state == Playing && total - s.clock <= s.ease) {
            s.state = Ending;
            s.outStart = s.clock;
            s.outLength = std::max(0.0f, total - s.clock);
            ending.push_back(s);
        }
    }
    // Ended: the finite ones at their end, the stopped once eased out.
    for (auto it = slots_.begin(); it != slots_.end();) {
        const float d = it->animation->duration;
        const bool done = (it->loops > 0 && it->clock >= d * float(it->loops)) ||
                          (it->state == Stopped && it->clock >= it->outStart + it->outLength) || d <= 0;
        if (!done) {
            ++it;
            continue;
        }
        if (it->state != Stopped)
            finished.push_back(*it);
        it = slots_.erase(it);
    }
    // Told after, as they may play others.
    for (const Slot& s : ending)
        if (onEnding)
            onEnding(s);
    for (const Slot& s : finished)
        if (onFinished)
            onFinished(s);
}

const Animator::Slot* Animator::find(std::string_view name) const {
    const std::string n = fs::toLower(name);
    for (const Slot& s : slots_)
        if (s.state != Stopped && s.name == n)
            return &s;
    return nullptr;
}

bool Animator::playing(std::string_view part) const {
    const std::string p = fs::toLower(part);
    for (const Slot& s : slots_)
        if (s.state != Stopped && s.name.find(p) != std::string::npos)
            return true;
    return false;
}

const Animator::Slot* Animator::base() const {
    const Slot* found = nullptr;
    for (const Slot& s : slots_)
        if (s.base && s.state != Stopped)
            found = &s;
    return found;
}

void Animator::blend(const Model& model, int layer, std::vector<BoneTransform>& out, std::vector<float>& total,
                     const Slot** strongest) const {
    const size_t n = model.bones.size();
    out.assign(n, BoneTransform());
    for (BoneTransform& t : out) {
        t.position[0] = t.position[1] = t.position[2] = 0;
        t.orientation[3] = 0;
        std::fill(std::begin(t.scaleShear), std::end(t.scaleShear), 0.0f);
    }
    total.assign(n, 0.0f);
    float best = -1;
    std::vector<BoneTransform> local;
    for (const Slot& s : slots_) {
        if (s.layer != layer)
            continue;
        const float w = s.weight();
        if (w <= 0)
            continue;
        if (w > best && strongest)
            best = w, *strongest = &s;
        sampleLocal(model, s.animation.get(), s.time(), local);
        const std::vector<float> other = &model == model_ ? std::vector<float>() : buildMask(model, s.animation.get(), s.maskWeight);
        const std::vector<float>& mask = &model == model_ ? s.mask : other;
        for (size_t b = 0; b < n; b++) {
            const float bw = w * mask[b];
            if (bw <= 0)
                continue;
            BoneTransform& o = out[b];
            const BoneTransform& l = local[b];
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
        BoneTransform& o = out[b];
        if (total[b] < 1e-4f) {
            o = model.bones[b].rest;
            continue;
        }
        for (float& v : o.position)
            v /= total[b];
        for (float& v : o.scaleShear)
            v /= total[b];
        float len = 0;
        for (float v : o.orientation)
            len += v * v;
        len = std::sqrt(len);
        if (len > 0)
            for (float& v : o.orientation)
                v /= len;
        else
            o.orientation[3] = 1;
    }
}

void Animator::pose(Pose& out, const std::vector<BoneTurn>* turns) const {
    std::vector<BoneTransform> body, layer;
    std::vector<float> total;
    blend(*model_, Body, body, total, nullptr);
    // The head and eyes over it (GrannyModulationCompositeLocalPose): each
    // bone toward the layer's, by the strongest slot's mask times how far
    // the layer has eased in (its weight over its ease time, at most 1).
    for (int l : {Head, Eyes}) {
        const Slot* strongest = nullptr;
        blend(*model_, l, layer, total, &strongest);
        if (!strongest)
            continue;
        float factor = 0;
        for (const Slot& s : slots_)
            if (s.layer == l)
                factor = std::max(factor, s.ease > 0 ? s.weight() / s.ease : s.weight());
        factor = std::min(1.0f, factor);
        for (size_t b = 0; b < body.size(); b++) {
            const float t = strongest->mask[b] * factor;
            if (t <= 0)
                continue;
            BoneTransform& o = body[b];
            const BoneTransform& p = layer[b];
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
                for (float& v : o.orientation)
                    v /= len;
            for (int k = 0; k < 9; k++)
                o.scaleShear[k] += (p.scaleShear[k] - o.scaleShear[k]) * t;
        }
    }
    poseFromLocal(*model_, std::move(body), out, turns);
}

void Animator::poseFor(const Model& other, Pose& out) const {
    std::vector<BoneTransform> body;
    std::vector<float> total;
    blend(other, Body, body, total, nullptr);
    poseFromLocal(other, std::move(body), out);
}

} // namespace reluva::awe
