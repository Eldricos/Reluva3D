// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/snd_archive.h"

#include "audio/wav.h"

#include <cstring>
#include <fstream>
#include <string>

namespace reluva::botf {

namespace {

constexpr size_t kEntrySize = 16;
constexpr size_t kMusicHeaderSize = SndArchive::kMusicPieces * 8;
constexpr size_t kWavHeaderSize = 44; // what decodedSize counts besides the samples

uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24;
}

Error readAt(const std::filesystem::path &path, uint64_t offset, size_t size, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + path.string());
    out.resize(size);
    in.seekg(std::streamoff(offset));
    if (!in.read(reinterpret_cast<char *>(out.data()), std::streamsize(size)))
        return Error::fail(path.string() + ": can't read " + std::to_string(size) + " bytes at " +
                           std::to_string(offset));
    return Error::success();
}

SndArchive::Entry readEntry(const uint8_t *p) {
    SndArchive::Entry e;
    e.offset = u32(p);
    e.size = u32(p + 4);
    e.decodedSize = u32(p + 8);
    e.flags = u32(p + 12);
    return e;
}

} // namespace

Error SndArchive::open(const std::filesystem::path &path, bool music) {
    _path = path;
    _music = music;
    _entries.clear();
    _pieces.clear();
    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec)
        return Error::fail("can't open " + path.string());
    auto bad = [&](const std::string &why) { return Error::fail(path.string() + ": " + why); };

    if (!music) {
        std::vector<uint8_t> head;
        if (fileSize < 4)
            return bad("too small for a sound archive");
        if (Error err = readAt(path, 0, 4, head))
            return err;
        const uint32_t dirOffset = u32(head.data());
        if (dirOffset < 4 || dirOffset > fileSize || (fileSize - dirOffset) % kEntrySize)
            return bad("no directory at " + std::to_string(dirOffset));
        std::vector<uint8_t> dir;
        if (Error err = readAt(path, dirOffset, size_t(fileSize - dirOffset), dir))
            return err;
        for (size_t p = 0; p < dir.size(); p += kEntrySize)
            _entries.push_back(readEntry(&dir[p]));
    } else {
        std::vector<uint8_t> head;
        if (fileSize < kMusicHeaderSize)
            return bad("too small for music.snd");
        if (Error err = readAt(path, 0, kMusicHeaderSize, head))
            return err;
        _pieces.resize(kMusicPieces);
        for (int piece = 0; piece < kMusicPieces; ++piece) {
            const uint32_t count = u32(&head[size_t(piece) * 8]);
            const uint32_t offset = u32(&head[size_t(piece) * 8 + 4]);
            if (offset > fileSize || count > (fileSize - offset) / kEntrySize)
                return bad("piece " + std::to_string(piece) + "'s list doesn't fit in the file");
            std::vector<uint8_t> list;
            if (Error err = readAt(path, offset, size_t(count) * kEntrySize, list))
                return err;
            for (size_t p = 0; p < list.size(); p += kEntrySize) {
                _pieces[size_t(piece)].push_back(_entries.size());
                _entries.push_back(readEntry(&list[p]));
                _entries.back().piece = piece;
            }
        }
    }
    for (size_t i = 0; i < _entries.size(); ++i)
        if (uint64_t(_entries[i].offset) + _entries[i].size > fileSize)
            return bad("sound " + std::to_string(i) + " runs past the end of the file");
    return Error::success();
}

Error SndArchive::readRaw(size_t entry, std::vector<uint8_t> &out) const {
    if (entry >= _entries.size())
        return Error::fail(_path.string() + ": no sound " + std::to_string(entry));
    return readAt(_path, _entries[entry].offset, _entries[entry].size, out);
}

Error SndArchive::decode(size_t entry, int adpcmRate, audio::Sound &out) const {
    std::vector<uint8_t> data;
    if (Error err = readRaw(entry, data))
        return err;
    if (Error err = decodeSndEntry(data, _entries[entry], adpcmRate, out))
        return Error::fail(_path.string() + ": sound " + std::to_string(entry) + ": " + err.message);
    return Error::success();
}

Error SndArchive::decodePiece(size_t piece, int adpcmRate, audio::Sound &out) const {
    out = audio::Sound{};
    if (piece >= _pieces.size())
        return Error::fail(_path.string() + ": no piece " + std::to_string(piece));
    for (size_t entry : _pieces[piece]) {
        audio::Sound part;
        if (Error err = decode(entry, adpcmRate, part))
            return err;
        if (out.channels && (part.channels != out.channels || part.rate != out.rate))
            return Error::fail(_path.string() + ": piece " + std::to_string(piece) + " mixes sound formats");
        out.rate = part.rate;
        out.channels = part.channels;
        out.format = part.format;
        out.samples.insert(out.samples.end(), part.samples.begin(), part.samples.end());
    }
    return Error::success();
}

Error decodeSndEntry(const std::vector<uint8_t> &data, const SndArchive::Entry &entry, int adpcmRate,
                     audio::Sound &out) {
    if (data.size() >= 12 && std::memcmp(data.data(), "RIFF", 4) == 0)
        return audio::decodeWav(data, out);
    const int channels = entry.flags == 3 ? 2 : 1;
    const size_t frames =
        entry.decodedSize > kWavHeaderSize ? (entry.decodedSize - kWavHeaderSize) / (2 * size_t(channels)) : 0;
    return audio::decodeImaAdpcm(data.data(), data.size(), channels, adpcmRate, 512 * size_t(channels), frames, out);
}

} // namespace reluva::botf
