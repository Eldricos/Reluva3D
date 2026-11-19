// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's shared commands for games' media: a CD image's audio
// tracks, and AVI movies (common/cue.h, video/avi.h).

#include "common/cue.h"
#include "image/image.h"
#include "common/stream.h"
#include "video/avi.h"
#include "devtools/tool.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace reluva::tool;

// An AVI's streams, and its video decoded from start to end, timed; the
// frames asked for written as frame_NNNN.tga.
int cmdAviFrames(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool avi-frames <file.avi> [output dir] [frame number... | all]\n");
        return 2;
    }
    Bytes data;
    if (!readFile(argv[2], data))
        return 1;
    reluva::avi::Decoder movie;
    if (!movie.load(std::make_unique<reluva::MemoryReadStream>(std::move(data)))) {
        std::fprintf(stderr, "%s: not an AVI file with a video stream\n", argv[2]);
        return 1;
    }
    std::string codec;
    for (int i = 0; i < 4; i++)
        codec += char(movie.videoCodec() >> (8 * i));
    const double fps = 1e6 / movie.frameMicros();
    std::printf("video: %s %dx%d, %d frames at %.3f a second (%.2f s)%s\n", codec.c_str(), movie.width(),
                movie.height(), movie.frameCount(), fps, movie.frameCount() / fps,
                movie.canDecode() ? "" : ", not decoded (only Cinepak is)");
    if (!movie.hasAudio())
        std::printf("no sound (or not PCM)\n");
    else
        std::printf("sound: %d Hz, %d channel(s), %d-bit, %.2f s\n", movie.audioRate(), movie.audioChannels(),
                    movie.audioBits(),
                    double(movie.audio().size()) / (movie.audioRate() * movie.audioChannels() * movie.audioBits() / 8));
    if (!movie.canDecode())
        return 0;
    std::set<int> wanted;
    bool all = false;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "all") == 0)
            all = true;
        else
            wanted.insert(std::atoi(argv[i]));
    }
    const std::filesystem::path dir = argc > 3 ? argv[3] : "";
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    const auto start = std::chrono::steady_clock::now();
    int written = 0;
    while (movie.decodeNextFrame()) {
        const int f = movie.currentFrame();
        if (dir.empty() || !(all || wanted.count(f)))
            continue;
        const reluva::Surface &s = movie.surface();
        reluva::image::Image picture;
        picture.w = s.w;
        picture.h = s.h;
        picture.rgba.resize(size_t(s.w) * size_t(s.h) * 4);
        for (int y = 0; y < s.h; y++)
            std::memcpy(&picture.rgba[size_t(y) * size_t(s.w) * 4], s.row(y), size_t(s.w) * 4);
        char name[32];
        std::snprintf(name, sizeof name, "frame_%04d.tga", f);
        const std::vector<uint8_t> tga = reluva::image::encodeTga(picture);
        std::ofstream(dir / name, std::ios::binary)
            .write(reinterpret_cast<const char *>(tga.data()), std::streamsize(tga.size()));
        ++written;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("all %d frames decoded in %.0f ms; %d written\n", movie.frameCount(), ms, written);
    return 0;
}

// A CD image's tracks, and its audio tracks written as track02.wav ... in
// the output folder, the names games' CD music is looked for under.
int cmdCueRip(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: reluva-tool cue-rip <file.cue> [output dir]\n");
        return 2;
    }
    std::vector<reluva::CueTrack> tracks;
    if (reluva::Error err = reluva::readCueSheet(argv[2], tracks)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const reluva::CueTrack &t : tracks) {
        const uint64_t frames = t.size / uint64_t(t.sectorSize);
        std::printf("track %02d  %-10s %8llu sectors  %2llu:%05.2f  %s @ %llu\n", t.number, t.mode.c_str(),
                    static_cast<unsigned long long>(frames), static_cast<unsigned long long>(frames / 75 / 60),
                    double(frames % (75 * 60)) / 75.0, t.file.filename().string().c_str(),
                    static_cast<unsigned long long>(t.offset));
    }
    if (argc < 4)
        return 0;
    const std::filesystem::path dir = argv[3];
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    int written = 0;
    for (const reluva::CueTrack &t : tracks) {
        if (!t.audio)
            continue;
        char name[32];
        std::snprintf(name, sizeof name, "track%02d.wav", t.number);
        if (reluva::Error err = reluva::writeCueTrackWav(t, dir / name)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        ++written;
    }
    std::printf("%d audio track(s) written to %s\n", written, dir.string().c_str());
    return 0;
}

const Commands
    commands("", {
                     {"avi-frames", "<file.avi> [output dir] [frame number... | all]",
                      "an AVI's streams; decode every frame (Cinepak), timed, writing those asked for as TGA files",
                      cmdAviFrames},
                     {"cue-rip", "<file.cue> [output dir]",
                      "list a CD image's tracks; write each audio track as trackNN.wav (games' CD music)", cmdCueRip},
                 });

} // namespace
