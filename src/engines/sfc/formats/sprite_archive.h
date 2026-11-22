// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::sfc {

/**
 * The interface's pictures, `Assets/Sprites/sprites.q3`
 * (docs/engines/sfc/games/sfc2/formats/sprites-q3.md): images packed back
 * to back, each run-length coded in 16-bit RGB555 or with a palette of
 * its own, then a section of the interface's objects, which refers to
 * each image by its size and offset. The objects aren't read yet; only
 * the names some of them give their images.
 */
class SpriteArchive {
public:
    /** One image: where it is, and what its header says. */
    struct Image {
        uint32_t offset = 0; // of its header, from the file's start
        uint32_t size = 0;   // the header and all that follows it
        uint16_t width = 0;
        uint16_t height = 0;
        int16_t hotX = 0; // from its top left; the centre in most (what it's for is guessed)
        int16_t hotY = 0;
        uint8_t bits = 0; // 16 (RGB555) or 8 (its own palette)
        std::string name; // empty when no object names it
    };

    /** Reads the whole file and lists its images. */
    Error open(const std::filesystem::path &path);
    /** The same, from the file's bytes. */
    Error open(std::vector<uint8_t> data);

    /** The images, in the file's order. */
    const std::vector<Image> &images() const { return _images; }
    /** Where the objects' section starts. */
    uint32_t objectsOffset() const { return _objects; }

    /**
     * An image as RGBA32: transparent where its runs skip pixels, opaque
     * elsewhere.
     */
    Error decode(const Image &image, Surface &out) const;

private:
    void findNames();

    std::vector<uint8_t> _data;
    std::vector<Image> _images;
    uint32_t _objects = 0;
};

} // namespace reluva::sfc
