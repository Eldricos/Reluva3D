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
 * offset. The objects are read as far as they're worked out: each one's
 * kind, name and class; an image's own record; a group's children and
 * where each goes; a picture or button's images for each of its states.
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
    /** A group's child, placed from the group's origin. */
    struct Child {
        uint32_t id = 0; // the child's record
        int32_t x = 0;   // where its image's hot spot goes, or its own origin
        int32_t y = 0;
        uint32_t depth = 0; // higher is further back
    };
    /** One record of the objects' section, as far as it's known. */
    struct Object {
        uint16_t kind = 0;           // kKind...; others' meanings are guessed from their names
        uint32_t id = 0;             // 1, 2, 3... in the file's order
        std::string name;            // may be empty
        std::string className;       // the program's class for it ("SFCPushBtn"); empty for an image's and many others
        uint32_t offset = 0;         // of the record, from the file's start
        uint32_t size = 0;           // to the next record, or the file's end
        int32_t image = -1;          // kKindImage: its index in images()
        std::vector<Child> children; // kKindScreen and kKindButtonGroup
        /**
         * kKindButton and kKindPicture: image records (kind 1) for its
         * states, its pressed look and its disabled look (the last two
         * often empty; what the states are depends on its class).
         */
        std::vector<uint32_t> frames[3];
    };
    /** The kinds of record whose fields are read. */
    ///@{
    static constexpr uint16_t kKindImage = 1;
    static constexpr uint16_t kKindScreen = 3; // a group: a screen, a panel, a set of icons
    static constexpr uint16_t kKindButton = 8;
    static constexpr uint16_t kKindPicture = 10; // a picture, toggle or multi-state indicator
    static constexpr uint16_t kKindButtonGroup = 17;
    ///@}

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
    /** A record by its number; nullptr if there's none. */
    const Object *object(uint32_t id) const { return id >= 1 && id <= _records.size() ? &_records[id - 1] : nullptr; }

    /**
     * Draws an object as the interface would at rest, onto RGBA32 `out`
     * with its origin at (x, y): an image at its hot spot, a group's
     * children at their places, furthest first, a button or picture by
     * its first state's first image. Text the program fills in isn't
     * drawn. Fails only if an image it draws doesn't decode.
     */
    Error compose(uint32_t id, Surface &out, int x, int y) const;

    /**
     * An image as RGBA32: transparent where its runs skip pixels, opaque
     * elsewhere.
     */
    Error decode(const Image &image, Surface &out) const;

private:
    Error readObjects();
    void readFields(Object &o);
    Error compose(uint32_t id, Surface &out, int x, int y, int depth) const;

    std::vector<uint8_t> _data;
    std::vector<Image> _images;
    std::vector<Object> _records;
    uint32_t _objects = 0;
};

} // namespace reluva::sfc
