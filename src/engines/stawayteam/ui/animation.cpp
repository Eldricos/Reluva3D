// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/animation.h"

#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::stawayteam {

Error Animation::load(const Archive &data, const std::string &name, const std::string &sequence) {
    if (sequence.empty())
        return loadFile(data, "Cache/Sequences/" + name + ".seq16");
    return loadFrom(data, "Cache/Models/" + name + ".mdl16", sequence);
}

Error Animation::loadFile(const Archive &data, const std::string &path) {
    return loadFrom(data, path, "");
}

Error Animation::loadFrom(const Archive &data, const std::string &path, const std::string &sequence) {
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
    onGround = seq->object.integer("Render On Ground").value_or(0) != 0;
    shadow = seq->object.integer("Is Shadow").value_or(0) != 0;
    sortingY = int(seq->object.integer("Sorting Y Offset").value_or(0));
    cuts.clear();
    if (const TextObject *lines = seq->object.object("Cutting Lines"))
        for (const TextField *c : lines->items())
            if (c->object)
                cuts.push_back({int(c->object->integer("Cut Offset").value_or(0)),
                                int(c->object->integer("Strip Sorting Y Offset").value_or(0))});
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
            std::string sound;
            if (action && action->object("Footstep Noise")) {
                if (const TextField *s = action->field("Default Sound"); s && s->value != "!None.ogg")
                    sound = s->value;
                action = action->object("Footstep Noise");
            }
            if (action && action->number("Radius")) {
                Noise n;
                n.frame = frame;
                n.radius = float(action->number("Radius").value_or(0.0));
                n.age = float(action->number("Age").value_or(1.0));
                if (const TextField *a = action->field("Attribute"))
                    n.attribute = a->value;
                n.sound = sound;
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

void blitBy(Surface &screen, const Animation::Picture *p, int x, int y, float brightness, int opacity, int clipLeft,
            int clipRight) {
    if (!p)
        return;
    image::Image dim;
    if (brightness < 1.0f)
        dim = dimmed(p->image, brightness);
    const image::Image &whole = brightness < 1.0f ? dim : p->image;
    const int left = x - p->originX;
    const int from = int(std::clamp(int64_t(clipLeft) - left, int64_t(0), int64_t(whole.w)));
    const int to = int(std::clamp(int64_t(clipRight) - left, int64_t(0), int64_t(whole.w)));
    if (from == 0 && to == whole.w) {
        image::blit(screen, whole, left, y - p->originY, opacity);
        return;
    }
    if (from >= to)
        return;
    image::Image part;
    part.w = to - from;
    part.h = whole.h;
    part.rgba.resize(size_t(part.w) * size_t(part.h) * 4);
    for (int row = 0; row < whole.h; ++row)
        std::memcpy(part.pixel(0, row), whole.pixel(from, row), size_t(part.w) * 4);
    image::blit(screen, part, left + from, y - p->originY, opacity);
}

} // namespace reluva::stawayteam
