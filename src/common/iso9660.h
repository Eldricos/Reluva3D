// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace reluva {

/**
 * Where a disc image's data track is: the image file, the track's first
 * sector in it, and how each sector holds its 2048 bytes of data.
 */
struct IsoTrack {
    std::filesystem::path file;
    uint64_t offset = 0;   // bytes to the track's first sector
    int sectorSize = 2048; // 2048, or 2352 (raw) or 2336 (mode 2)
    int dataOffset = 0;    // the data's place in a sector: 16 in a raw mode 1 sector, 24 in mode 2
};

/** A file on a disc's data track. */
struct IsoFile {
    std::string path; // from the disc's root, '/' between folders, without ";1"
    uint64_t size = 0;
    uint32_t sector = 0; // its first, counted from the track's
};

/** Reads a data track's sectors, a file's bytes at a time. */
class IsoTrackReader {
public:
    Error open(const IsoTrack &track);
    /** `n` bytes of the data from `at` on, in the sectors from `sector` on. */
    Error read(uint32_t sector, uint64_t at, void *out, size_t n);

private:
    IsoTrack _track;
    std::ifstream _in;
};

/**
 * A disc image's data track and its files (ISO 9660, its primary volume's
 * names), for taking a game's files off its CD's image without the CD.
 * Files over several extents (larger than 4 GB) aren't read.
 */
class IsoImage {
public:
    /** Reads the file system of `track`. */
    Error open(const IsoTrack &track);
    /**
     * The same, of the first data track of a cue sheet (.cue) whose
     * images are beside it, or of an image of 2048-byte sectors (.iso).
     */
    Error openFile(const std::filesystem::path &cueOrIso);

    const IsoTrack &track() const { return _track; }
    /** The volume's name ("SITH"). */
    const std::string &volume() const { return _volume; }
    /** Every file, folders left out, in the order of the disc's folders. */
    const std::vector<IsoFile> &files() const { return _files; }
    /** The file at `path` ('/' or '\' between folders), in any case; null if none. */
    const IsoFile *find(std::string_view path) const;
    /** The files directly in `folder` (in any case), by name. */
    std::vector<const IsoFile *> filesIn(std::string_view folder) const;
    /** All of a small file, up to `limit` bytes. */
    Error readAll(const IsoFile &file, std::vector<uint8_t> &out, uint64_t limit = 1u << 20) const;

private:
    Error readFolder(IsoTrackReader &reader, uint32_t sector, uint32_t size, const std::string &prefix, int depth);

    IsoTrack _track;
    std::string _volume;
    std::vector<IsoFile> _files;
};

/** The disc images in `folder`, by name: its cue sheets and .iso files. */
std::vector<std::filesystem::path> discImagesIn(const std::filesystem::path &folder);

} // namespace reluva
