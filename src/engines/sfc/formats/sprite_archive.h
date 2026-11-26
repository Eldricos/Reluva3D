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
 * its own, then a section of the interface's objects (screens, buttons,
 * lists, animations...), which refers to each image by its size and
 * offset. The objects are found and named, by kind; their fields past
 * that aren't read yet, but an image's own (kind 1) gives its name.
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
    /** One record of the objects' section, as far as it's known. */
    struct Object {
        uint16_t kind = 0;   // 1 an image's; the others' meanings are guessed from their names
        uint32_t id = 0;     // 1, 2, 3... in the file's order
        std::string name;    // may be empty
        uint32_t offset = 0; // of the record, from the file's start
        uint32_t size = 0;   // to the next record, or the file's end
    };

    /** Reads the whole file and lists its images. */
    Error open(const std::filesystem::path &path);
    /** The same, from the file's bytes. */
    Error open(std::vector<uint8_t> data);

    /** The images, in the file's order. */
    const std::vector<Image> &images() const { return _images; }
    /** Where the objects' section starts. */
    uint32_t objectsOffset() const { return _objects; }
    /** The objects' records, in the file's order. */
    const std::vector<Object> &objects() const { return _records; }

    /**
     * An image as RGBA32: transparent where its runs skip pixels, opaque
     * elsewhere.
     */
    Error decode(const Image &image, Surface &out) const;

private:
    Error readObjects();

    std::vector<uint8_t> _data;
    std::vector<Image> _images;
    std::vector<Object> _records;
    uint32_t _objects = 0;
};

} // namespace reluva::sfc
