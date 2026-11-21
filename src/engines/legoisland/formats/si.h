// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::legoisland {

/**
 * What an SI object is, by its record's type: a medium the game plays, or
 * a container whose children play in order, together, or one of them
 * (docs/engines/legoisland/formats/si.md).
 */
enum class SiType : uint16_t {
    kAnimation = 3, // an FLC or Smacker animation
    kSound = 4,
    kSerial = 6, // children one after another
    kParallel = 7,
    kEvent = 8,
    kSelect = 9, // one child, chosen by a variable
    kStill = 10,
    kObject = 11, // a 3D model, animation or texture
};

/** An SI object: a medium, or a container of other objects, as its MxOb record holds it. */
struct SiObject {
    uint16_t type = 0;     // an SiType, or a value not seen on the CD
    std::string presenter; // the class that plays it; empty for the default for its type
    std::string name;
    uint32_t id = 0; // its chunks' object id, unique within the file
    uint32_t flags = 0;
    int32_t startTime = 0; // in milliseconds, from its container's start
    int32_t duration = 0;  // in milliseconds; -1 for as long as it's wanted
    uint32_t loops = 1;
    std::array<double, 3> location{};
    std::array<double, 3> direction{};
    std::array<double, 3> up{};
    std::string extra; // its "extra" settings: key:value pairs

    /** A medium's: the file it was made from, and how it plays. */
    ///@{
    std::string fileName; // on the developers' machines: "q:\lego\media\..."
    std::string format;   // its FourCC: "FLC", "SMK", "WAV", "STL", "EVT" or "OBJ"
    uint32_t frameRate = 0;
    uint32_t paletteManaged = 0;
    uint32_t volume = 0; // a sound's
    ///@}

    /** A select's: the variable naming the child to play, and each child's value. */
    ///@{
    std::string variable;
    std::vector<std::string> choices;
    ///@}

    std::vector<SiObject> children;

    /** Whether it has media of its own, rather than children. */
    bool isMedia() const { return !format.empty(); }
};

/** One MxCh chunk of an object's data, as the file holds it, without its data. */
struct SiChunk {
    uint32_t objectId = 0;
    uint16_t flags = 0;
    int32_t time = 0;        // in milliseconds; -1 for a header
    uint32_t remaining = 0;  // the bytes of this piece's data from this chunk on
    uint64_t dataOffset = 0; // in the file
    uint32_t dataSize = 0;   // in this chunk

    /** The flags seen: the object's last chunk, and a piece of data split across chunks. */
    ///@{
    static constexpr uint16_t kEnd = 0x02;
    static constexpr uint16_t kSplit = 0x10;
    ///@}
};

/** One piece of an object's data, its split chunks joined: its header, a frame, a second of sound. */
struct SiPiece {
    uint16_t flags = 0;
    int32_t time = 0;
    std::vector<uint8_t> data;
};

/**
 * An SI ("Interleaf") file: its header, the tree of objects, and an index
 * of every chunk of their data, read from the file when asked for
 * (docs/engines/legoisland/formats/si.md). The file is kept by its path.
 */
class SiFile {
public:
    /** Reads the header and the objects, and indexes the chunks. */
    Error open(const std::filesystem::path &path);

    /** The header's version, "2.2" on the CD. */
    ///@{
    uint16_t majorVersion() const { return _major; }
    uint16_t minorVersion() const { return _minor; }
    ///@}
    /** The size of the buffers the file is read into, in bytes, and how many the game keeps. */
    ///@{
    uint32_t bufferSize() const { return _bufferSize; }
    uint32_t bufferCount() const { return _bufferCount; }
    ///@}
    /** The objects at the top of the tree, one per stream. */
    const std::vector<SiObject> &objects() const { return _objects; }
    /** Where each stream starts, as the MxOf table lists them; 0 for an unused entry. */
    const std::vector<uint32_t> &streamOffsets() const { return _streamOffsets; }
    /** Every chunk, in the file's order. */
    const std::vector<SiChunk> &chunks() const { return _chunks; }

    /** The object with id `id` anywhere in the tree, or null. */
    const SiObject *find(uint32_t id) const;
    /** Every object in the tree, parents before their children. */
    std::vector<const SiObject *> all() const;

    /**
     * An object's data, its split chunks joined, in the file's order, but
     * not its end chunk (whose time is returned in `endTime`, if asked).
     */
    Error readPieces(uint32_t id, std::vector<SiPiece> &out, int32_t *endTime = nullptr) const;

private:
    std::filesystem::path _path;
    uint16_t _major = 0;
    uint16_t _minor = 0;
    uint32_t _bufferSize = 0;
    uint32_t _bufferCount = 0;
    std::vector<uint32_t> _streamOffsets;
    std::vector<SiObject> _objects;
    std::vector<SiChunk> _chunks;
};

/** Reads one MxOb record's payload (after its tag and size), children included. */
Error parseSiObject(const uint8_t *data, size_t size, SiObject &out);

/**
 * A medium's pieces as the standard file it was made from: a .bmp, .wav,
 * .flc or .smk. Fails for a format with no such file (EVT, OBJ).
 */
Error rebuildMediaFile(const SiObject &object, const std::vector<SiPiece> &pieces, std::vector<uint8_t> &out);

/** The extension rebuildMediaFile() gives `object`'s file ("bmp"), or empty if none. */
std::string mediaExtension(const SiObject &object);

/** An event's (EVT's) data: a setting's name and its value. */
struct SiEvent {
    std::string name;  // "HIT_WALL_SOUND", "tempBackgroundColor", "patch"
    std::string value; // "HitWall", "set 0 0 0", "patch"
};

/**
 * Reads an event's pieces: a header of 16 bytes, the same in every one on
 * the CD, then the name and the value, each ending with a NUL
 * (docs/engines/legoisland/formats/si.md).
 */
Error parseEvent(const std::vector<SiPiece> &pieces, SiEvent &out);

/**
 * A 3D object's (OBJ's) data: its pieces joined, without the 0 byte every
 * one has at its end (docs/engines/legoisland/formats/obj.md).
 */
std::vector<uint8_t> objectData(const std::vector<SiPiece> &pieces);

/** What a 3D object's data is, by its source file's extension: "ani", "mod", "tex", "gph", or empty (inline). */
std::string objectKind(const SiObject &object);

} // namespace reluva::legoisland
