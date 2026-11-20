// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace reluva::botf {

/**
 * The sample rate of the .snd archives' ADPCM sounds, which don't store one:
 * 22,050 Hz, like sfx.snd's WAV files, and confirmed by ear (speech and
 * music sound right at it, not at 11,025 or 44,100).
 */
constexpr int kDefaultAdpcmRate = 22050;

/**
 * sfx.snd, english.snd (german.snd) and music.snd: numbered sounds without
 * names (docs/engines/botf/formats/snd.md). The archive is kept by its
 * path, and opened for each read.
 *   sfx.snd, english.snd: u32 directory offset; the sounds; 16-byte entries
 *     to the end of the file
 *   music.snd: 16 (u32 count, u32 offset) pairs, each a list of the
 *     entries of one piece of music
 * An entry's sound is a RIFF WAVE file, or bare IMA ADPCM blocks: mono
 * 512-byte blocks, or stereo 1,024-byte ones when flags is 3.
 */
class SndArchive {
public:
    /** A sound in the directory. */
    struct Entry {
        uint32_t offset = 0;
        uint32_t size = 0;
        uint32_t decodedSize = 0; // ADPCM: the sound's size as a 16-bit PCM WAV file
        uint32_t flags = 0;       // sfx.snd 0 or 2, english.snd 0, music.snd 3
        int piece = -1;           // music.snd: which piece the entry belongs to
    };

    /** How many pieces music.snd's header lists. */
    static constexpr int kMusicPieces = 16;

    /** Reads the directory; `music` picks music.snd's layout. */
    Error open(const std::filesystem::path &path, bool music);

    /** Every sound; music.snd's piece by piece. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** Whether it was opened as music.snd. */
    bool isMusic() const { return _music; }
    /** music.snd: the entries (indices into entries()) of each piece, in order. */
    const std::vector<std::vector<size_t>> &pieces() const { return _pieces; }

    /** An entry's bytes as stored; fails for an entry that isn't there. */
    Error readRaw(size_t entry, std::vector<uint8_t> &out) const;
    /** Decodes an entry; ADPCM sounds are given `adpcmRate`. */
    Error decode(size_t entry, int adpcmRate, audio::Sound &out) const;
    /** music.snd: a whole piece, its entries one after the other; fails if they mix formats. */
    Error decodePiece(size_t piece, int adpcmRate, audio::Sound &out) const;

    /** The archive's file, as open() was given it. */
    const std::filesystem::path &path() const { return _path; }

private:
    std::filesystem::path _path;
    bool _music = false;
    std::vector<Entry> _entries;
    std::vector<std::vector<size_t>> _pieces;
};

/** Decodes one .snd entry's bytes (as SndArchive::readRaw gives them). */
Error decodeSndEntry(const std::vector<uint8_t> &data, const SndArchive::Entry &entry, int adpcmRate,
                     audio::Sound &out);

} // namespace reluva::botf
