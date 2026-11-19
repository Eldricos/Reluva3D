// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Cue sheets as CD burning and ripping programs write them (CDRWIN's
// format): FILE, TRACK and INDEX commands, positions in minutes, seconds
// and frames of 1/75 s, one sector per frame.

#include "common/cue.h"

#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace reluva {

namespace {

constexpr int kFramesPerSecond = 75;

// A line's words; quoted ones may hold spaces.
std::vector<std::string> words(std::string_view line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        if (i >= line.size())
            break;
        std::string w;
        if (line[i] == '"') {
            const size_t end = line.find('"', i + 1);
            w = line.substr(i + 1, (end == std::string_view::npos ? line.size() : end) - i - 1);
            i = end == std::string_view::npos ? line.size() : end + 1;
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t')
                w += line[i++];
        }
        out.push_back(std::move(w));
    }
    return out;
}

bool parseMsf(const std::string& s, int64_t& frames) {
    int m = 0, sec = 0, f = 0;
    char c1 = 0, c2 = 0;
    std::istringstream in(s);
    if (!(in >> m >> c1 >> sec >> c2 >> f) || c1 != ':' || c2 != ':' || sec >= 60 || f >= kFramesPerSecond)
        return false;
    frames = (int64_t(m) * 60 + sec) * kFramesPerSecond + f;
    return true;
}

int sectorSizeOf(const std::string& mode) {
    if (mode == "AUDIO" || mode == "MODE1/2352" || mode == "MODE2/2352" || mode == "CDI/2352")
        return 2352;
    if (mode == "MODE1/2048" || mode == "MODE2/2048")
        return 2048;
    if (mode == "MODE2/2336" || mode == "CDI/2336")
        return 2336;
    if (mode == "MODE2/2324")
        return 2324;
    if (mode == "CDG")
        return 2448;
    return 0;
}

struct ParsedTrack {
    CueTrack track;
    size_t fileIndex = 0;
    int64_t firstIndex = -1; // INDEX 00 if given, else INDEX 01: where the track's sectors start
    int64_t index1 = -1;
};

} // namespace

Error parseCueSheet(std::string_view text, const std::filesystem::path& dir, std::vector<CueTrack>& out) {
    out.clear();
    struct File {
        std::filesystem::path path;
        bool bigEndian = false;
    };
    std::vector<File> files;
    std::vector<ParsedTrack> tracks;
    int lineNumber = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const std::vector<std::string> w = words(line);
        if (w.empty())
            continue;
        const std::string cmd = fs::toLower(w[0]);
        const auto bad = [&](const std::string& why) {
            return Error::fail("cue sheet line " + std::to_string(lineNumber) + ": " + why);
        };
        if (cmd == "file") {
            if (w.size() < 3)
                return bad("FILE needs a name and a type");
            const std::string type = fs::toLower(w[2]);
            if (type != "binary" && type != "motorola")
                return bad("FILE of type " + w[2] + " isn't supported, only BINARY and MOTOROLA");
            const auto found = fs::findFile(dir, w[1]);
            if (!found)
                return bad("can't find " + w[1] + " in " + dir.string());
            files.push_back({*found, type == "motorola"});
        } else if (cmd == "track") {
            if (w.size() < 3)
                return bad("TRACK needs a number and a mode");
            if (files.empty())
                return bad("TRACK before any FILE");
            ParsedTrack t;
            t.track.number = std::atoi(w[1].c_str());
            t.track.mode = w[2];
            for (char& c : t.track.mode)
                c = char(std::toupper(static_cast<unsigned char>(c)));
            t.track.audio = t.track.mode == "AUDIO";
            t.track.sectorSize = sectorSizeOf(t.track.mode);
            if (t.track.sectorSize == 0)
                return bad("unknown track mode " + w[2]);
            t.track.file = files.back().path;
            t.track.bigEndian = files.back().bigEndian;
            t.fileIndex = files.size() - 1;
            tracks.push_back(std::move(t));
        } else if (cmd == "index") {
            if (tracks.empty() || w.size() < 3)
                return bad("INDEX needs a track, a number and a position");
            int64_t frames = 0;
            if (!parseMsf(w[2], frames))
                return bad("bad position " + w[2]);
            ParsedTrack& t = tracks.back();
            if (t.fileIndex != files.size() - 1)
                return bad("track " + std::to_string(t.track.number) + " spans files, which isn't supported");
            const int n = std::atoi(w[1].c_str());
            if (t.firstIndex < 0)
                t.firstIndex = frames;
            if (n == 1)
                t.index1 = frames;
        }
        // REM, CATALOG, TITLE, PERFORMER, FLAGS, ISRC, PREGAP, POSTGAP and
        // the like don't change where the data is.
    }
    if (tracks.empty())
        return Error::fail("the cue sheet has no tracks");

    // Each track's sectors run from its first index to the next track's
    // first index in the same file, and sector sizes may differ between
    // tracks, so where each starts in bytes is a running total. Sectors
    // before a file's first index are taken to be its first track's.
    std::vector<uint64_t> regionStart(tracks.size());
    for (size_t i = 0; i < tracks.size(); ++i) {
        const ParsedTrack& t = tracks[i];
        if (t.index1 < 0)
            return Error::fail("track " + std::to_string(t.track.number) + " has no INDEX 01");
        const bool firstInFile = i == 0 || tracks[i - 1].fileIndex != t.fileIndex;
        if (firstInFile) {
            regionStart[i] = uint64_t(t.firstIndex) * uint64_t(t.track.sectorSize);
        } else {
            const ParsedTrack& prev = tracks[i - 1];
            if (t.firstIndex < prev.firstIndex)
                return Error::fail("track " + std::to_string(t.track.number) + " starts before the one before it");
            regionStart[i] =
                regionStart[i - 1] + uint64_t(t.firstIndex - prev.firstIndex) * uint64_t(prev.track.sectorSize);
        }
    }
    for (size_t i = 0; i < tracks.size(); ++i) {
        CueTrack track = tracks[i].track;
        std::error_code ec;
        const uint64_t fileSize = std::filesystem::file_size(track.file, ec);
        if (ec)
            return Error::fail("can't read the size of " + track.file.string());
        track.offset =
            regionStart[i] + uint64_t(tracks[i].index1 - tracks[i].firstIndex) * uint64_t(track.sectorSize);
        const bool lastInFile = i + 1 == tracks.size() || tracks[i + 1].fileIndex != tracks[i].fileIndex;
        const uint64_t end = lastInFile ? fileSize : regionStart[i + 1];
        if (track.offset > end || end > fileSize)
            return Error::fail("track " + std::to_string(track.number) + " lies beyond the end of " +
                               track.file.filename().string());
        track.size = end - track.offset;
        out.push_back(std::move(track));
    }
    return Error::success();
}

Error readCueSheet(const std::filesystem::path& cue, std::vector<CueTrack>& out) {
    std::ifstream f(cue, std::ios::binary);
    if (!f)
        return Error::fail("can't open " + cue.string());
    std::stringstream text;
    text << f.rdbuf();
    return parseCueSheet(text.str(), cue.parent_path(), out);
}

Error writeCueTrackWav(const CueTrack& track, const std::filesystem::path& wav) {
    if (!track.audio)
        return Error::fail("track " + std::to_string(track.number) + " isn't audio");
    std::ifstream in(track.file, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + track.file.string());
    in.seekg(std::streamoff(track.offset));
    std::ofstream out(wav, std::ios::binary);
    if (!out)
        return Error::fail("can't write " + wav.string());
    const uint64_t size = track.size & ~uint64_t(3); // whole stereo frames
    if (size > 0xffffffffull - 36)
        return Error::fail("track " + std::to_string(track.number) + " is too long for a WAV file");
    const auto put32 = [&](uint32_t v) {
        const char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
        out.write(b, 4);
    };
    const auto put16 = [&](uint16_t v) {
        const char b[2] = {char(v), char(v >> 8)};
        out.write(b, 2);
    };
    out.write("RIFF", 4);
    put32(uint32_t(36 + size));
    out.write("WAVEfmt ", 8);
    put32(16);
    put16(1);          // PCM
    put16(2);          // stereo
    put32(44100);
    put32(44100 * 4);  // bytes per second
    put16(4);          // bytes per frame
    put16(16);         // bits per sample
    out.write("data", 4);
    put32(uint32_t(size));
    std::vector<char> buf(1 << 20);
    for (uint64_t left = size; left > 0;) {
        const size_t n = size_t(std::min<uint64_t>(left, buf.size()));
        if (!in.read(buf.data(), std::streamsize(n)))
            return Error::fail("can't read track " + std::to_string(track.number) + " from " + track.file.string());
        if (track.bigEndian)
            for (size_t i = 0; i + 1 < n; i += 2)
                std::swap(buf[i], buf[i + 1]);
        out.write(buf.data(), std::streamsize(n));
        left -= n;
    }
    if (!out)
        return Error::fail("can't write " + wav.string());
    return Error::success();
}

} // namespace reluva
