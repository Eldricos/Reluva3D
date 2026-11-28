// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"
#include "engines/nfs/formats/mus.h"
#include "video/mdec.h"

#include <cstdint>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * One of NFS II SE's movies (the .dct files in FeData/pc/movies): EA's SCHl sound
 * stream with a video chunk ("mTCD") before each of its blocks, the frames
 * the PlayStation's MDEC ones (docs/engines/nfs/games/nfs2se/formats/dct.md).
 */
class DctMovie {
public:
    /** Where a frame is in the file: its MDEC frame, after the chunk's 16 bytes of header. */
    struct Frame {
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    /**
     * Takes the file and finds its frames and sound. Fails if a chunk runs
     * past the end, there are no frames, or the frames' sizes differ.
     */
    Error open(std::vector<uint8_t> data);

    /** The frames, in the file's order. */
    const std::vector<Frame> &frames() const { return _frames; }
    /** The pictures' size, from the first frame's chunk. */
    ///@{
    int width() const { return _width; }
    int height() const { return _height; }
    ///@}
    /** Frames a second: the frames over the sound's length; 15 without sound. */
    double framesPerSecond() const;
    /** Whether the file has a sound stream. */
    bool hasAudio() const { return !_audio.segments().empty(); }

    /** Decodes frame `index` into `decoder`. */
    Error decodeFrame(mdec::Decoder &decoder, size_t index) const;
    /** The whole sound. */
    Error decodeAudio(audio::Sound &out) const;

private:
    std::vector<uint8_t> _data;
    std::vector<Frame> _frames;
    int _width = 0;
    int _height = 0;
    MusicStream _audio; // the SCHl stream, the video chunks between its blocks skipped
};

} // namespace reluva::nfs::nfs2se
