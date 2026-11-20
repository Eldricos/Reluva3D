// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::audio {

// A DLS (Downloadable Sounds, Level 1) collection: instruments made of
// regions of keys and velocities, each playing one of the collection's
// waves, with articulations (envelopes, LFO) as connection blocks. See
// docs/common/audio/dls.md.

/** How a wave is played: its root key and tuning, gain and loop. */
struct DlsSampleInfo {
    int unityNote = 60; // the key it plays at as recorded
    int fineTune = 0;   // cents
    int32_t gain = 0;   // 1/655360 dB (DLS's units), 0 as recorded
    bool looped = false;
    // in samples
    uint32_t loopStart = 0;
    uint32_t loopLength = 0;
};

/** One of a collection's waves: a PCM sample its regions play. */
struct DlsWave {
    int rate = 0;
    std::vector<int16_t> samples; // mono, 16-bit (8-bit ones widened; of stereo, the first channel)
    DlsSampleInfo info;           // its own wsmp
    std::string name;
};

/** One connection: a source (and a control) scaled into a destination. */
struct DlsConnection {
    uint16_t source = 0;
    uint16_t control = 0;
    uint16_t destination = 0;
    uint16_t transform = 0;
    int32_t scale = 0;
};

/** The keys and velocities an instrument plays a wave for, and how. */
struct DlsRegion {
    int keyLow = 0;
    int keyHigh = 127;
    int velocityLow = 0;
    int velocityHigh = 127;
    uint16_t options = 0;
    uint16_t keyGroup = 0;
    uint32_t wave = 0; // into DlsCollection::waves
    bool hasInfo = false;
    DlsSampleInfo info;                      // overrides the wave's own when hasInfo
    std::vector<DlsConnection> articulation; // a region's own, over the instrument's
};

/** An instrument: its bank and program, its regions and its articulation. */
struct DlsInstrument {
    uint32_t bank = 0;    // MIDI bank select (CC0 << 8 | CC32), and kDrums
    uint32_t program = 0; // 0-127
    std::string name;
    std::vector<DlsRegion> regions;
    std::vector<DlsConnection> articulation;

    static constexpr uint32_t kDrums = 0x80000000; // in `bank`: a drum kit
};

/** A whole collection, as readDls reads it. */
struct DlsCollection {
    std::vector<DlsInstrument> instruments;
    std::vector<DlsWave> waves;
    std::string name;

    /**
     * The instrument for a bank and program (bank as DlsInstrument::bank,
     * with kDrums), or null.
     */
    const DlsInstrument *find(uint32_t bank, uint32_t program) const;
};

/**
 * Reads a DLS file. A region whose wave isn't in the collection is left
 * out.
 * @return An error if it isn't a DLS collection, has no wave pool or no
 *         instruments, or a wave isn't PCM.
 */
Error readDls(const std::vector<uint8_t> &data, DlsCollection &out);

} // namespace reluva::audio
