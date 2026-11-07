// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Clue's resource files: PACK compression, DDRV archives, EIMG images and
// ANIM animations. See docs/engines/clue/res.md.

#include "common/image.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::clue {

// A PACK file's contents, or the data itself if it isn't packed.
Error unpack(const std::vector<uint8_t>& data, std::vector<uint8_t>& out);

// A DDRV archive ("data drive"): files found by name through the
// directory at its end. .res, .a (once unpacked), .o and .lvl are these.
class DataDrive {
public:
    struct Entry {
        std::string name;    // as stored: Imsidlee_01, /World.dat
        uint64_t time = 0;   // a Windows FILETIME
        uint32_t offset = 0;
        uint32_t size = 0;   // ANIM records' are 4 bytes more than they hold
    };

    // Reads a file from disk, unpacking it if it's packed.
    Error open(const std::filesystem::path& path);
    Error load(std::vector<uint8_t> data); // already read and unpacked

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(std::string_view name) const; // ignoring case
    // An entry's bytes, cut to the end of the data where its size runs over.
    std::vector<uint8_t> read(const Entry& entry) const;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    std::vector<uint8_t> data_;
    std::vector<Entry> entries_;
};

// An EIMG record: a 16-bit run-length image. Both versions (0x100 in the
// fonts and some of the interface, 0x200 elsewhere) decode to this.
struct Image {
    int version = 0;
    image::Image pixels; // RGBA, transparent where nothing is drawn
    // Where the pixels sit in a cell of their own size: a font's glyph in
    // its character box. Version 0x200 images fill their cell.
    int cellW = 0, cellH = 0;
    int offsetX = 0, offsetY = 0;
};
Error decodeImage(const std::vector<uint8_t>& record, Image& out);

// An ANIM record: frames made of layers of images, with each frame's
// anchor and time (its ISOA block) when it has them.
struct Animation {
    static constexpr uint16_t kNoImage = 0xffff; // a part showing nothing in that frame

    struct Part {           // a layer's image in one frame
        uint16_t image = 0; // into images, or kNoImage
        int16_t x = 0, y = 0;
        // Its own draw flags (EagleM's text form: "Flags"); 0 in all but a
        // few. Clue.exe draws the house's with them in place of the object's
        // (0x4285E8): the high 16 bits a pixel function of ImageRLE16.dll's,
        // 2 a quarter of the picture over what is behind, 3 half, 4 three
        // quarters (the fountain's water). EagleM's windows draw without.
        uint32_t flags = 0;
    };
    // A frame's place. In the long ISOA form, also the box the object fills
    // in the world, which its anchor is the back corner of (least x and y,
    // the floor): sizeX by sizeY on the floor and height high. Screen x is
    // world x - y, screen y (x + y) / 2 - z, so the box's image spans
    // -sizeY .. sizeX across and rises height above the anchor.
    struct Frame {
        int16_t anchorX = 0, anchorY = 0; // where the frame's top left is, from the anchor point
        uint16_t sizeX = 0, sizeY = 0, height = 0;
        int16_t solid = 0;                // 1 for objects, -1 for flat things (floors), 0 without a box
        // The long form's bytes 4-13 as read: 4-6 a world offset (Clue.exe's
        // "RelativeWorld", 0 in all), 7 the number of boxes, 8-9 the box's
        // number, 10-13 its x and y from the anchor (unknown[3], unknown[4]).
        uint16_t unknown[5] = {};
        std::vector<std::array<uint8_t, 7>> extras;
    };

    std::string name;      // the record's own; often uninitialised memory, so empty then
    // Its speed and how it plays, as EagleM.dll's text form of it names
    // them ("FPS", "Flags"): frames a second (0 in most of the interface),
    // and 0 once, 1 round and round, 2 to the end and back.
    enum Flags : uint8_t { Once = 0, Loop = 1, BackAndForth = 2 };
    int framesPerSecond = 0;
    uint8_t flags = Once;
    int width = 0, height = 0;
    std::vector<std::string> images; // "drive:Iname"
    std::vector<std::vector<Part>> layers; // [layer][frame]
    std::vector<Frame> frames;
    bool isometric = false; // has an ISOA block
    bool shortForm = false; // its ISOA block has 10-byte frames

    size_t frameCount() const { return frames.size(); }
    // The frame shown `ms` after it started. `world`: as Clue.exe plays the
    // house's objects (0x427E60, 0x446E30): a frame each 1000 / FPS ms (10
    // FPS if it has none), by its flags, the last frame held when once is
    // done. Else as EagleM.dll plays the windows' (0x1000BFF0): round and
    // round whatever its flags, still if it has no FPS.
    size_t frameAt(uint64_t ms, bool world) const;
    // How long once takes, by the same rule (its frames' time in all).
    uint64_t onceMs() const;
};
Error parseAnimation(const std::vector<uint8_t>& record, Animation& out);

// "c_scarlet:Imsidlee_01" split into its drive and its name in it.
struct ResourceName {
    std::string drive; // c_scarlet
    std::string name;  // Imsidlee_01: its first letter is the kind, I image, A animation, O object
};
bool splitResourceName(std::string_view full, ResourceName& out);

} // namespace reluva::clue
