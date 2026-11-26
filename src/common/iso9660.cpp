// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/iso9660.h"

#include "common/cue.h"
#include "common/fs.h"

#include <algorithm>
#include <cstring>

namespace reluva {

namespace {

namespace stdfs = std::filesystem;

constexpr uint32_t kSectorData = 2048;
// The volume descriptors start at sector 16, after the system area.
constexpr uint32_t kFirstDescriptor = 16;
// ISO 9660 allows 8 levels of folders; more is a broken or looping image.
constexpr int kMostDepth = 16;

uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// A name as the disc keeps it ("JK_.CD;1", "VIDEO", "README."), as a
// folder lists it.
std::string plainName(std::string_view name) {
    name = fs::withoutIsoVersion(name);
    if (!name.empty() && name.back() == '.')
        name.remove_suffix(1);
    return std::string(name);
}

std::string slashed(std::string_view path) {
    std::string out(path);
    std::replace(out.begin(), out.end(), '\\', '/');
    while (!out.empty() && out.front() == '/')
        out.erase(out.begin());
    while (!out.empty() && out.back() == '/')
        out.pop_back();
    return fs::toLower(out);
}

// Where a cue sheet's data track's 2048 bytes sit in its sectors.
int dataOffsetOf(const std::string &mode, int sectorSize) {
    if (sectorSize == 2048)
        return 0;
    if (mode == "MODE1/2352")
        return 16;
    if (mode == "MODE2/2352")
        return 24;
    if (mode == "MODE2/2336")
        return 8;
    return -1;
}

} // namespace

Error IsoTrackReader::open(const IsoTrack &track) {
    _track = track;
    _in.close();
    _in.clear();
    _in.open(track.file, std::ios::binary);
    if (!_in)
        return Error::fail("Couldn't read " + track.file.string());
    return Error::success();
}

Error IsoTrackReader::read(uint32_t sector, uint64_t at, void *out, size_t n) {
    auto *dest = static_cast<uint8_t *>(out);
    uint64_t sectorAt = sector + at / kSectorData;
    uint64_t within = at % kSectorData;
    while (n > 0) {
        // Whole sectors of bare data run on, so read them together.
        const uint64_t run = _track.sectorSize == int(kSectorData) ? n : std::min<uint64_t>(n, kSectorData - within);
        _in.clear();
        _in.seekg(std::streamoff(_track.offset + sectorAt * uint64_t(_track.sectorSize) + uint64_t(_track.dataOffset) +
                                 within));
        if (!_in.read(reinterpret_cast<char *>(dest), std::streamsize(run)))
            return Error::fail("Couldn't read " + _track.file.string() + ": it ends early");
        dest += run;
        n -= size_t(run);
        ++sectorAt;
        within = 0;
    }
    return Error::success();
}

Error IsoImage::open(const IsoTrack &track) {
    _track = track;
    _volume.clear();
    _files.clear();
    IsoTrackReader reader;
    if (Error e = reader.open(track))
        return e;
    // The descriptors until the set's end (type 255), for the primary (1).
    uint8_t d[kSectorData];
    for (uint32_t s = kFirstDescriptor; s < kFirstDescriptor + 32; ++s) {
        if (Error e = reader.read(s, 0, d, sizeof d))
            return Error::fail(track.file.string() + " isn't a CD's data track");
        if (std::memcmp(d + 1, "CD001", 5) != 0)
            break;
        if (d[0] == 255)
            break;
        if (d[0] != 1)
            continue;
        std::string volume(reinterpret_cast<const char *>(d + 40), 32);
        volume.erase(volume.find_last_not_of(' ') + 1);
        _volume = volume;
        const uint8_t *root = d + 156;
        return readFolder(reader, le32(root + 2), le32(root + 10), "", 0);
    }
    return Error::fail(track.file.string() + " has no ISO 9660 file system");
}

Error IsoImage::readFolder(IsoTrackReader &reader, uint32_t sector, uint32_t size, const std::string &prefix,
                           int depth) {
    if (depth > kMostDepth || size > (64u << 20))
        return Error::fail("A folder of " + _track.file.string() + " is broken");
    std::vector<uint8_t> data((size + kSectorData - 1) / kSectorData * kSectorData);
    if (Error e = reader.read(sector, 0, data.data(), data.size()))
        return e;
    struct Folder {
        uint32_t sector;
        uint32_t size;
        std::string path;
    };
    std::vector<Folder> folders;
    for (size_t p = 0; p < data.size();) {
        const uint8_t length = data[p];
        // A record never crosses into the next sector: zeros fill the rest.
        if (length == 0) {
            p = (p / kSectorData + 1) * kSectorData;
            continue;
        }
        if (length < 34 || p + length > data.size() || 33u + data[p + 32] > length)
            return Error::fail("A folder of " + _track.file.string() + " is broken");
        const uint8_t *r = &data[p];
        const std::string_view raw(reinterpret_cast<const char *>(r + 33), r[32]);
        p += length;
        if (raw.size() == 1 && (raw[0] == 0 || raw[0] == 1)) // the folder itself, its parent
            continue;
        const std::string path = prefix + plainName(raw);
        if (r[25] & 2)
            folders.push_back({le32(r + 2), le32(r + 10), path});
        else
            _files.push_back({path, le32(r + 10), le32(r + 2)});
    }
    for (const Folder &f : folders)
        if (Error e = readFolder(reader, f.sector, f.size, f.path + "/", depth + 1))
            return e;
    return Error::success();
}

Error IsoImage::openFile(const stdfs::path &cueOrIso) {
    if (fs::toLower(cueOrIso.extension().string()) != ".cue") {
        IsoTrack track;
        track.file = cueOrIso;
        return open(track);
    }
    std::vector<CueTrack> tracks;
    if (Error e = readCueSheet(cueOrIso, tracks))
        return e;
    for (const CueTrack &t : tracks) {
        if (t.audio)
            continue;
        IsoTrack track;
        track.file = t.file;
        track.offset = t.offset;
        track.sectorSize = t.sectorSize;
        track.dataOffset = dataOffsetOf(t.mode, t.sectorSize);
        if (t.size == 0)
            return Error::fail(cueOrIso.filename().string() + "'s data track, " + t.file.filename().string() +
                               ", isn't beside it");
        if (track.dataOffset < 0)
            return Error::fail(cueOrIso.filename().string() + "'s data track is " + t.mode + ", which isn't read");
        return open(track);
    }
    return Error::fail(cueOrIso.filename().string() + " has no data track");
}

const IsoFile *IsoImage::find(std::string_view path) const {
    const std::string want = slashed(path);
    for (const IsoFile &f : _files)
        if (fs::toLower(f.path) == want)
            return &f;
    return nullptr;
}

std::vector<const IsoFile *> IsoImage::filesIn(std::string_view folder) const {
    const std::string prefix = slashed(folder) + "/";
    std::vector<const IsoFile *> out;
    for (const IsoFile &f : _files) {
        const std::string lower = fs::toLower(f.path);
        if (lower.size() > prefix.size() && lower.compare(0, prefix.size(), prefix) == 0 &&
            lower.find('/', prefix.size()) == std::string::npos)
            out.push_back(&f);
    }
    std::sort(out.begin(), out.end(),
              [](const IsoFile *a, const IsoFile *b) { return fs::windowsNameLess(a->path, b->path); });
    return out;
}

Error IsoImage::readAll(const IsoFile &file, std::vector<uint8_t> &out, uint64_t limit) const {
    if (file.size > limit)
        return Error::fail(file.path + " is larger than expected");
    IsoTrackReader reader;
    if (Error e = reader.open(_track))
        return e;
    out.resize(size_t(file.size));
    return reader.read(file.sector, 0, out.data(), out.size());
}

std::vector<stdfs::path> discImagesIn(const stdfs::path &folder) {
    std::vector<stdfs::path> out;
    std::error_code ec;
    for (const auto &e : stdfs::directory_iterator(folder, ec)) {
        const std::string ext = fs::toLower(e.path().extension().string());
        if (e.is_regular_file(ec) && (ext == ".cue" || ext == ".iso"))
            out.push_back(e.path());
    }
    std::sort(out.begin(), out.end(), [](const stdfs::path &a, const stdfs::path &b) {
        return fs::windowsNameLess(a.filename().string(), b.filename().string());
    });
    return out;
}

} // namespace reluva
