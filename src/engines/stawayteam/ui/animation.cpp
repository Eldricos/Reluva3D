// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/animation.h"

#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/ui/widgets.h"

#include <algorithm>

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
    const size_t rotations = size_t(std::max(1L, seq->object.integer("Num Rotations").value_or(1)));
    const size_t perDirection = std::max<size_t>(1, seq->sequence->frames.size() / rotations);
    frames.clear();
    for (size_t i = 0; i < perDirection && i < seq->sequence->frames.size(); ++i)
        if (const auto &frame = seq->sequence->frames[i])
            frames.push_back({frameImage(*frame), frame->originX, frame->originY});
    return Error::success();
}

const Animation::Picture *Animation::at(uint64_t ms, int offset) const {
    if (frames.empty())
        return nullptr;
    const auto n = uint64_t(frames.size());
    return &frames[size_t((uint64_t(double(ms) * fps / 1000.0) + uint64_t(offset)) % n)];
}

image::Image cachePicture(const Archive &data, const std::string &name) {
    std::vector<uint8_t> bytes;
    Frame frame;
    if (data.read("Cache/" + name + ".frm16", bytes) || readFrame(bytes.data(), bytes.size(), frame))
        return {};
    return frameImage(frame);
}

void blitBy(Surface &screen, const Animation::Picture *p, int x, int y, float brightness) {
    if (!p)
        return;
    if (brightness < 1.0f)
        image::blit(screen, dimmed(p->image, brightness), x - p->originX, y - p->originY);
    else
        image::blit(screen, p->image, x - p->originX, y - p->originY);
}

} // namespace reluva::stawayteam
