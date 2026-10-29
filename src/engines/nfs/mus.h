// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/types.h"
#include "engines/nfs/bnk.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::nfs {

// A music file (.mus): segments one after another, each an SCHl header
// (a PT header), an SCCl block count, SCDl data blocks and SCEl, as
// docs/formats/nfs/mus.md describes. The data is kept; segments are
// decoded when asked.
class MusicStream {
public:
    struct Segment {
        uint32_t offset = 0; // of its SCHl, in the file
        SoundHeader header;
        std::vector<std::pair<uint32_t, uint32_t>> blocks; // each SCDl's payload: offset, size
    };

    Error open(std::vector<uint8_t> data);
    const std::vector<Segment>& segments() const { return segments_; }
    // The segment starting at a file offset (as a map gives them), or -1.
    int segmentAt(uint32_t offset) const;
    Error decode(size_t segment, audio::Sound& out) const;

private:
    std::vector<uint8_t> data_;
    std::vector<Segment> segments_;
};

// A Pathfinder map (.map, and .lin for a linear order): for each segment,
// which one follows, by a control value from 0 to 100 (the race's
// intensity, presumably).
struct MusicMap {
    struct Route {
        int low = 0, high = 0; // the control value's range, inclusive
        int next = 0;          // section
    };
    struct Section {
        int part = 0; // the first byte: its row of events
        std::vector<Route> routes; // none: the music stops after it, unless an event is pending
        std::array<uint8_t, 28> raw{};
    };
    int version = 0;
    // The section the music starts at (header byte 5): a race's intro on
    // some tracks' maps, else 0 (mus.md).
    int start = 0;
    std::vector<Section> sections;
    // The events, a row for each part: the section event e jumps to from a
    // section of that part (nfs3.exe's SNDpathevent; mus.md). The game
    // sends 0 and 1 itself and the track's zones (4-9).
    std::vector<std::array<uint8_t, 16>> events;
    std::vector<uint32_t> offsets; // each section's segment, as a .mus file offset

    // The section after `section` for a control value, as the game picks
    // it: the first route whose range holds the value, else the same
    // section again; -1 if the section has no routes (the music ends).
    int next(int section, int value) const;
    // The section event `e` leads to from `section`; -1 if none.
    int event(int section, int e) const;
};

Error parseMusicMap(const std::vector<uint8_t>& data, MusicMap& out);

} // namespace reluva::nfs
