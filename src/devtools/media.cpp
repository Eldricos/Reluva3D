// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's shared commands for games' media: a CD image's audio
// tracks, and AVI movies (common/cue.h, video/avi.h).

#include "audio/decode.h"
#include "audio/prologic.h"
#include "audio/wav.h"
#include "common/cue.h"
#include "image/image.h"
#include "common/stream.h"
#include "video/avi.h"
#include "devtools/tool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

// A stereo recording through the Pro Logic decoder model
// (audio/prologic.h): the four channels as mono WAV files
// (<prefix>-L.wav, -C, -R, -S), and the levels window by window, with a
// passive decode alongside for comparison.
int cmdProLogic(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool prologic <in.wav> <out prefix> [-passive] [-window ms] [-delay ms]\n"
                             "                            [-csv windows.csv] [-quiet]\n");
        return 2;
    }
    reluva::audio::ProLogicOptions options;
    double windowMs = 1000;
    const char *csvPath = nullptr;
    bool quiet = false;
    for (int a = 4; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-passive")
            options.active = false;
        else if (arg == "-window" && a + 1 < argc)
            windowMs = std::atof(argv[++a]);
        else if (arg == "-delay" && a + 1 < argc)
            options.surroundDelayMs = std::atof(argv[++a]);
        else if (arg == "-csv" && a + 1 < argc)
            csvPath = argv[++a];
        else if (arg == "-quiet")
            quiet = true;
        else {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            return 2;
        }
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "cannot open %s\n", argv[2]);
        return 1;
    }
    reluva::audio::Sound in;
    if (reluva::Error err = reluva::audio::decodeSound(data, in)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    if (in.channels != 2) {
        std::fprintf(stderr, "%s is not stereo\n", argv[2]);
        return 1;
    }
    using reluva::audio::ProLogicDecoder;
    reluva::audio::ProLogicOptions passiveOptions = options;
    passiveOptions.active = false;
    ProLogicDecoder dec(in.rate, options);
    ProLogicDecoder passive(in.rate, passiveOptions);
    const size_t frames = in.frames();
    std::vector<float> out(4 * frames);
    std::vector<float> outPassive(4);
    const size_t window = std::max<size_t>(1, size_t(windowMs * in.rate / 1000));
    FILE *csv = csvPath ? std::fopen(csvPath, "w") : nullptr;
    if (csvPath && !csv) {
        std::fprintf(stderr, "cannot write %s\n", csvPath);
        return 1;
    }
    if (csv)
        std::fprintf(csv, "ms,mid_db,side_db,L_db,C_db,R_db,S_db,S_passive_db,steering,direction,rear_pct\n");
    auto db = [](double e, size_t n) { return n ? 10 * std::log10(e / double(n) + 1e-12) : -120.0; };
    // Energy over the whole file: L, C, R, S, passive S; mid, side.
    double total[7] = {};
    size_t rearFrames = 0;
    size_t steeredFrames = 0;
    size_t loudRearWindows = 0;
    size_t windows = 0;
    if (!quiet)
        std::printf("    time   mid  side |     L     C     R     S  S pass | steer   dir rear%%\n");
    for (size_t start = 0; start < frames; start += window) {
        const size_t n = std::min(window, frames - start);
        // the direction: a mean of 2 theta, by steering
        double e[7] = {};
        double steer = 0;
        double dirX = 0;
        double dirY = 0;
        size_t rear = 0;
        for (size_t i = start; i < start + n; ++i) {
            float *o = &out[4 * i];
            dec.process(&in.samples[2 * i], o, 1);
            passive.process(&in.samples[2 * i], outPassive.data(), 1);
            for (int c = 0; c < 4; ++c)
                e[c] += double(o[c]) * o[c];
            e[4] += double(outPassive[3]) * outPassive[3];
            const double l = in.samples[2 * i] / 32768.0;
            const double r = in.samples[2 * i + 1] / 32768.0;
            e[5] += (l + r) * (l + r) / 2;
            e[6] += (l - r) * (l - r) / 2;
            steer += dec.steering();
            dirX += dec.steering() * std::cos(dec.direction() * 3.14159265358979 / 90);
            dirY += dec.steering() * std::sin(dec.direction() * 3.14159265358979 / 90);
            if (dec.steering() > 0.5) {
                ++steeredFrames;
                if (std::fabs(dec.direction() - 135) < 22.5) {
                    ++rear;
                    ++rearFrames;
                }
            }
        }
        for (int c = 0; c < 7; ++c)
            total[c] += e[c];
        const double front = e[0] + e[1] + e[2];
        if (e[3] * 10 > front && front > 0)
            ++loudRearWindows;
        ++windows;
        const double ms = double(start) * 1000 / in.rate;
        const double rearPct = 100.0 * double(rear) / double(n);
        double direction = std::atan2(dirY, dirX) * 90 / 3.14159265358979;
        if (direction < 0)
            direction += 180;
        if (!quiet)
            std::printf("%8.1f %5.1f %5.1f | %5.1f %5.1f %5.1f %5.1f %6.1f | %5.2f %5.1f %5.1f\n", ms / 1000,
                        db(e[5], n), db(e[6], n), db(e[0], n), db(e[1], n), db(e[2], n), db(e[3], n), db(e[4], n),
                        steer / double(n), direction, rearPct);
        if (csv)
            std::fprintf(csv, "%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.1f,%.1f\n", ms, db(e[5], n), db(e[6], n),
                         db(e[0], n), db(e[1], n), db(e[2], n), db(e[3], n), db(e[4], n), steer / double(n), direction,
                         rearPct);
    }
    if (csv)
        std::fclose(csv);
    const double front = total[0] + total[1] + total[2];
    auto rel = [](double a, double b) { return 10 * std::log10((a + 1e-20) / (b + 1e-20)); };
    std::printf("%s: %.1f s at %d Hz, %s decode, surround delay %.0f ms\n", argv[2], double(frames) / in.rate, in.rate,
                options.active ? "active" : "passive", options.surroundDelayMs);
    std::printf("side/mid %.1f dB; surround/front %.1f dB (passive %.1f dB); C/(L+R) %.1f dB\n",
                rel(total[6], total[5]), rel(total[3], front), rel(total[4], front),
                rel(total[1], total[0] + total[2]));
    std::printf("steered (gain > 0.5) %.1f%% of the time, to the surround (135 +- 22.5 degrees) %.2f%%;"
                " windows with the surround within 10 dB of the front: %zu of %zu\n",
                100.0 * double(steeredFrames) / double(std::max<size_t>(1, frames)),
                100.0 * double(rearFrames) / double(std::max<size_t>(1, frames)), loudRearWindows, windows);

    static const char *const kNames[4] = {"L", "C", "R", "S"};
    for (int c = 0; c < 4; ++c) {
        reluva::audio::Sound s;
        s.rate = in.rate;
        s.channels = 1;
        s.format = 1;
        s.samples.resize(frames);
        for (size_t i = 0; i < frames; ++i)
            s.samples[i] =
                static_cast<int16_t>(std::clamp<long>(std::lround(out[4 * i + c] * 32768.0), -32768L, 32767L));
        const std::string path = std::string(argv[3]) + "-" + kNames[c] + ".wav";
        const std::vector<uint8_t> bytes = reluva::audio::encodePcmWav(s);
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            return 1;
        }
    }
    std::printf("wrote %s-L.wav, -C, -R, -S\n", argv[3]);
    return 0;
}

const Commands commands(
    "",
    {
        {"avi-frames", "<file.avi> [output dir] [frame number... | all]",
         "an AVI's streams; decode every frame (Cinepak), timed, writing those asked for as TGA files", cmdAviFrames},
        {"cue-rip", "<file.cue> [output dir]",
         "list a CD image's tracks; write each audio track as trackNN.wav (games' CD music)", cmdCueRip},
        {"prologic", "<in.wav> <out prefix> [-passive] [-window ms] [-delay ms] [-csv file] [-quiet]",
         "a stereo recording through the Pro Logic decoder model: four mono WAVs (L, C, R, S) and levels by window",
         cmdProLogic},
    });

} // namespace
