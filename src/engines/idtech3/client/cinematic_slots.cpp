// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/cinematic_slots.h"

#include "common/archive.h"

#include <cmath>

namespace reluva::idtech3 {

int32_t CinematicSlots::play(const std::string &name, int32_t x, int32_t y, int32_t w, int32_t h, int32_t bits,
                             int32_t nowMs) {
    // As CIN_PlayCinematic (0x404940): "video/%s" for a name without a
    // folder, ".roq" by default.
    std::string path = name.find_first_of("/\\") == std::string::npos ? "video/" + name : name;
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        path += ".roq";
    if (!(bits & kSystem))
        for (int i = 0; i < kSlots; ++i)
            if (_slots[size_t(i)].name == path)
                return i;
    // The first free slot; with none the original ends the game
    // ("CIN_HandleForVideo: none free"), here it's refused.
    int found = -1;
    for (int i = 0; i < kSlots && found < 0; ++i)
        if (_slots[size_t(i)].name.empty())
            found = i;
    if (found < 0)
        return -1;
    Slot &s = _slots[size_t(found)];
    s = Slot{};
    if (_files.read(path, s.file))
        return -1;
    s.extension = path.substr(path.find_last_of('.'));
    if (!restart(s, nowMs))
        return -1;
    s.name = path;
    s.extents = {x, y, w, h};
    s.loop = (bits & kLoop) != 0;
    s.hold = (bits & kHold) != 0;
    s.status = kPlay;
    return found;
}

bool CinematicSlots::restart(Slot &s, int32_t nowMs) {
    if (s.movie.open(s.file, s.extension))
        return false;
    s.start = nowMs;
    return true;
}

int32_t CinematicSlots::stop(int32_t slot) {
    // As CIN_StopCinematic (0x4038d0): let go, its last frame kept.
    if (!valid(slot))
        return kEnded;
    Slot &s = _slots[size_t(slot)];
    s.status = kIdle;
    s.name.clear();
    return kEnded;
}

int32_t CinematicSlots::run(int32_t slot, int32_t nowMs) {
    // As CIN_RunCinematic (0x403a60): the frames due by now; at the end
    // held, round again, or let go, its last frame kept.
    if (!valid(slot))
        return kEnded;
    Slot &s = _slots[size_t(slot)];
    if (s.status == kIdle)
        return kIdle;
    const double rate = double(s.movie.rateNumerator()) / double(std::max(1u, s.movie.rateDenominator()));
    const auto due = int(std::floor(double(nowMs - s.start) * rate / 1000.0));
    bool decoded = false;
    while (s.movie.frameNumber() < due) {
        bool more = false;
        if (Error err = s.movie.nextFrame(more); err || !more) {
            if (s.hold) {
                s.status = kIdle;
            } else if (s.loop && restart(s, nowMs)) {
                break;
            } else {
                s.status = kIdle;
                s.name.clear();
            }
            break;
        }
        decoded = true;
    }
    if (decoded)
        s.movie.toRgba(s.rgba);
    return s.status;
}

void CinematicSlots::setExtents(int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (valid(slot))
        _slots[size_t(slot)].extents = {x, y, w, h};
}

bool CinematicSlots::frame(int32_t slot, Frame &out) const {
    // As CIN_DrawCinematic (0x405570): the last frame, where the extents
    // say, in the program's screen as they are.
    if (!valid(slot) || _slots[size_t(slot)].rgba.empty())
        return false;
    const Slot &s = _slots[size_t(slot)];
    out.rgba = &s.rgba;
    out.cols = s.movie.width();
    out.rows = s.movie.height();
    out.x = float(s.extents[0]);
    out.y = float(s.extents[1]);
    out.w = float(s.extents[2]);
    out.h = float(s.extents[3]);
    return true;
}

void CinematicSlots::clear() {
    for (Slot &s : _slots)
        s = Slot{};
}

} // namespace reluva::idtech3
