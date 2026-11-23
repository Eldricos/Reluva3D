// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's movies, EA's
// (.tgq: TQI pictures, EA-XA sound), and its opening.

#include "audio/sound.h"
#include "common/fs.h"
#include "common/headless_system.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/opening.h"
#include "image/image.h"
#include "video/ea_movie.h"
#include "video/tqi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

// A movie decoded, every frame and its sound, with a checksum of them;
// with a frame's number and a file, that frame written out as a TGA.
reluva::Error checkMovie(const std::string &name, std::vector<uint8_t> data, reluva::Md5 &all, int dumpFrame = -1,
                         const char *dumpTo = nullptr) {
    reluva::video::EaMovie movie;
    if (reluva::Error err = movie.open(std::move(data)))
        return err;
    reluva::tqi::Decoder decoder;
    for (size_t i = 0; i < movie.frameCount(); ++i) {
        if (reluva::Error err = decoder.decode(movie.frame(i), movie.frameSize(i)))
            return reluva::Error::fail("frame " + std::to_string(i) + ": " + err.message);
        const reluva::Surface &s = decoder.surface();
        all.update(s.pixels.data(), s.pixels.size());
        if (int(i) == dumpFrame && dumpTo) {
            reluva::image::Image img;
            img.w = s.w;
            img.h = s.h;
            img.rgba = s.pixels;
            const std::vector<uint8_t> tga = reluva::image::encodeTga(img);
            std::ofstream(dumpTo, std::ios::binary)
                .write(reinterpret_cast<const char *>(tga.data()), std::streamsize(tga.size()));
        }
    }
    size_t samples = 0;
    for (size_t s = 0; s < movie.sound().segments().size(); ++s) {
        reluva::audio::Sound sound;
        if (reluva::Error err = movie.sound().decode(s, sound))
            return reluva::Error::fail("sound: " + err.message);
        all.update(sound.samples.data(), sound.samples.size() * sizeof(sound.samples[0]));
        samples += sound.samples.size() / size_t(sound.channels);
    }
    std::printf("%s: %zu frames, %dx%d, %d a second; sound %zu samples\n", name.c_str(), movie.frameCount(),
                decoder.width(), decoder.height(), reluva::video::EaMovie::kFramesPerSecond, samples);
    return reluva::Error::success();
}

// A .tgq file decoded; with a frame's number and a file, that frame written.
int cmdTgqCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw tgq-check <file.tgq> [frame out.tga]\n");
        return 2;
    }
    Bytes data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "can't read %s\n", argv[2]);
        return 1;
    }
    reluva::Md5 all;
    if (reluva::Error err =
            checkMovie(argv[2], data, all, argc > 4 ? std::atoi(argv[3]) : -1, argc > 4 ? argv[4] : nullptr)) {
        std::fprintf(stderr, "%s: %s\n", argv[2], err.message.c_str());
        return 1;
    }
    std::printf("md5 %s\n", all.hexDigest().c_str());
    return 0;
}

// Every movie of a game's Data folder decoded, for the game-files test.
int cmdMovieCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw movie-check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::Md5 all;
    int failed = 0;
    int read = 0;
    for (const std::string &name : files.list()) {
        if (reluva::fs::toLower(extensionOf(name)) != ".tgq")
            continue;
        std::vector<uint8_t> data;
        reluva::Error err = files.read(name, data);
        if (!err)
            err = checkMovie(name, std::move(data), all);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
    }
    std::printf("%d movies decoded, %d failed; md5 %s\n", read, failed, all.hexDigest().c_str());
    return failed || read == 0 ? 1 : 0;
}

// The game's opening run headless, by a clock that moves only as it waits:
// what it showed, and when it ended.
int cmdOpeningRun(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw opening-run <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    struct Screen : reluva::HeadlessSystem {
        reluva::Md5 all;
        int shown = 0;
        int width = 0;
        void updateScreen(const reluva::Surface &s) override {
            HeadlessSystem::updateScreen(s);
            all.update(s.pixels.data(), s.pixels.size());
            ++shown;
            width = s.w;
        }
    } sys;
    const bool open = reluva::tpw::playOpening(sys, nullptr, argv[2], files);
    std::printf("opening: %d screens shown at %dx%d, ended at %llu ms%s; md5 %s\n", sys.shown, sys.width, sys.height(),
                static_cast<unsigned long long>(sys.getMillis()), open ? "" : ", window closed",
                sys.all.hexDigest().c_str());
    return 0;
}

const Commands kCommands("tpw",
                         {
                             {"tgq-check", "<file.tgq> [frame out.tga]",
                              "decode an EA movie, every frame and its sound; or write a frame out", cmdTgqCheck},
                             {"movie-check", "<game folder>", "decode every movie (.tgq) of a game", cmdMovieCheck},
                             {"opening-run", "<game folder>", "run the game's opening headless", cmdOpeningRun},
                         });

} // namespace
