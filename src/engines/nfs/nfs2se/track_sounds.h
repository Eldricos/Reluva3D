// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/nfs2se/formats/trk.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * A track's placed sounds (extrablock 17) as nfs2sen.exe plays them for a
 * listener (0x434030; docs/engines/nfs/games/nfs2se/formats/trk.md), from
 * the track's TRAMBnn.BNK on a mixer: each block's first, for the blocks
 * of the listener's block's neighbourhood (extrablock 4) in its order;
 * the volume 2^18 / the distance squared, at most 127, and the pan by how
 * far to the side; the first loop met on a voice of its own, and now and
 * then a one-shot.
 */
class TrackSounds {
public:
    /** Plays on `mixer` (none: silent) from `files`; both must outlive it. */
    TrackSounds(const GameFiles &files, audio::Mixer *mixer) : _files(files), _mixer(mixer) {}
    ~TrackSounds() { stop(); }
    TrackSounds(const TrackSounds &) = delete;
    TrackSounds &operator=(const TrackSounds &) = delete;

    /** The track trNN's (`stem`, lower case) sounds and bank, the last's stopped. */
    void load(const std::string &stem, const Track &track);
    /** Stops what's playing. */
    void stop();
    /**
     * For a listener at `eye` in the block `block`, `right` the way its
     * right ear faces (level, length 1): what plays at the frame count
     * `frame`, which the game steps once a frame; called again for the
     * same count, nothing.
     */
    void update(uint32_t frame, size_t block, const std::array<float, 3> &eye, const std::array<float, 3> &right);
    /** How many sounds the track places. */
    size_t placed() const { return _placed.size(); }
    /** Whether the track's bank was read. */
    bool loaded() const { return !_data.empty(); }

private:
    std::shared_ptr<audio::Sound> decoded(int slot);

    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<AmbientSound> _placed;          // all of them
    std::vector<int> _first;                    // each block's first, into _placed; -1 none
    std::vector<std::vector<uint16_t>> _nearby; // each block's neighbourhood
    std::vector<uint8_t> _data;                 // TRAMBnn.BNK
    SoundBank _bank;
    std::map<int, std::shared_ptr<audio::Sound>> _sounds; // decoded, by place in the bank
    audio::SoundHandle _loop = audio::kNoSound;
    int _loopSound = -1;
    uint32_t _frame = ~0u;
    uint32_t _random = 1;
    int _oneShotPhase = 0;
};

} // namespace reluva::nfs::nfs2se
