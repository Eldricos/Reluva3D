// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/animation.h"

#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

Error Animation::load(const Archive &data, const std::string &name, const std::string &sequence) {
    const std::string path = "Cache/" + std::string(sequence.empty() ? "Sequences/" : "Models/") + name +
                             (sequence.empty() ? ".seq16" : ".mdl16");
    std::vector<uint8_t> bytes;
    if (Error err = data.read(path, bytes))
        return err;
    BinaryObject object;
    if (Error err = readBinaryObject(bytes.data(), bytes.size(), "", object))
        return Error::fail(path + ": " + err.message);
    const BinaryObject *seq = &object;
    if (!sequence.empty()) {
        seq = nullptr;
        for (const NamedSequence &s : object.sequences)
            if (s.name == sequence && s.sequence)
                seq = s.sequence.get();
        if (!seq)
            return Error::fail(path + " has no " + sequence);
    }
    if (!seq->sequence)
        return Error::fail(path + ": not a sequence");
    fps = float(seq->object.number("Frames Per Second").value_or(15.0));
    // A rotation sequence keeps each direction's frames together, the
    // first direction's first (formats/binary-object.md).
    frames.clear();
    for (const auto &frame : seq->sequence->frames)
        frames.push_back(frame ? Picture{frameImage(*frame), frame->originX, frame->originY} : Picture{});
    moves = seq->sequence->frameExtra;
    // Its Trigger, an action for each of a direction's frames: the walks'
    // footsteps, a CAddFootstepNoiseAction in the footstep's action or
    // a noise action of their own.
    noises.clear();
    if (const TextObject *triggers = seq->object.object("Trigger")) {
        int frame = 0;
        for (const TextField *t : triggers->items()) {
            const TextObject *action = t->object ? t->object->object("Action") : nullptr;
            if (action && action->object("Footstep Noise"))
                action = action->object("Footstep Noise");
            if (action && action->number("Radius")) {
                Noise n;
                n.frame = frame;
                n.radius = float(action->number("Radius").value_or(0.0));
                n.age = float(action->number("Age").value_or(1.0));
                if (const TextField *a = action->field("Attribute"))
                    n.attribute = a->value;
                noises.push_back(n);
            }
            ++frame;
        }
    }
    const long count = seq->object.integer("Num Rotations").value_or(1);
    rotations = count > 0 && frames.size() % size_t(count) == 0 ? int(count) : 1;
    return Error::success();
}

const Animation::Picture *Animation::frame(int frame, int direction) const {
    const int per = perDirection();
    if (per == 0)
        return nullptr;
    const int d = ((direction % rotations) + rotations) % rotations;
    const Picture &p = frames[size_t(d * per + ((frame % per) + per) % per)];
    return p.image.w ? &p : nullptr;
}

const Animation::Picture *Animation::at(uint64_t ms, int offset, int direction) const {
    return frame(int(uint64_t(double(ms) * fps / 1000.0) % 1000000) + offset, direction);
}

float Animation::cycleDistance(int direction) const {
    const int per = perDirection();
    if (per == 0 || moves.size() != frames.size())
        return 0.0f;
    const int d = ((direction % rotations) + rotations) % rotations;
    const auto &m = moves[size_t(d * per)];
    return std::hypot(float(m[0]), float(m[1]));
}

int Animation::direction(float radians) const {
    const float turn = 6.28318531f;
    float a = std::fmod(radians, turn);
    if (a < 0)
        a += turn;
    return int(std::lround(a / turn * float(rotations))) % rotations;
}

image::Image cachePicture(const Archive &data, const std::string &name) {
    std::vector<uint8_t> bytes;
    Frame frame;
    if (data.read("Cache/" + name + ".frm16", bytes) || readFrame(bytes.data(), bytes.size(), frame))
        return {};
    return frameImage(frame);
}

void blitBy(Surface &screen, const Animation::Picture *p, int x, int y, float brightness, int opacity) {
    if (!p)
        return;
    if (brightness < 1.0f)
        image::blit(screen, dimmed(p->image, brightness), x - p->originX, y - p->originY, opacity);
    else
        image::blit(screen, p->image, x - p->originX, y - p->originY, opacity);
}

} // namespace reluva::stawayteam
