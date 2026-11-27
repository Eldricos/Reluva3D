// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "video/smacker.h"
#include "video/smush.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * A cut scene, whichever video it is: Jedi Knight's Smacker (.smk) or
 * Mysteries of the Sith's SMUSH (.san). Both give 8-bit pictures with a
 * palette, sound, and a subtitle's number a frame.
 */
class CutSceneVideo {
public:
    /** Reads `file` and decodes its first frame; false, with why in `error`, if it doesn't. */
    bool open(const std::filesystem::path &file, std::string &error);

    bool isSmush() const { return _smush != nullptr; }
    int width() const;
    int height() const;
    int frameCount() const;
    int currentFrame() const;
    uint32_t frameMicros() const;
    bool endOfVideo() const;
    bool decodeNextFrame();
    const Surface &surface() const;
    const uint8_t *palette() const;
    bool hasAudio() const;
    int audioRate() const;
    bool audioStereo() const;
    std::vector<int16_t> takeAudio();

    /**
     * The frame's subtitle: a Smacker video's second track, 4 bytes (a
     * string's place in cutstrings.uni, its top bit set for a title);
     * a SMUSH video's TRES number, as 4 bytes; empty for none.
     */
    std::vector<uint8_t> subtitleData() const;

private:
    std::unique_ptr<smacker::Decoder> _smacker;
    std::unique_ptr<smush::Decoder> _smush;
};

} // namespace reluva::sith
