// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for Star Trek: Armada's sounds and movies
// (docs/engines/storm3d/games/armada/formats/sounds.md, movies.md).
//
//   reluva-tool storm3d sound-check <game folder>
//   reluva-tool storm3d movie-check <game folder> [name filter]
//   reluva-tool storm3d movie-frame <game folder> <name> <frame> <out.png>

#include "audio/wav.h"
#include "common/text.h"
#include "engines/storm3d/game_files.h"
#include "devtools/tool.h"
#include "image/image.h"
#include "video/bink.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::storm3d::GameFiles;

int cmdSoundCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d sound-check <game folder>\n");
        return 2;
    }
    GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int read = 0;
    int failed = 0;
    double seconds = 0;
    std::map<std::string, int> kinds; // "22050 Hz 8-bit mono" -> how many
    std::vector<uint8_t> data;
    for (const std::string &name : files.list()) {
        if (!reluva::text::hasSuffixIgnoreCase(name, ".wav"))
            continue;
        reluva::audio::Sound sound;
        if (Error err = files.read(name, data); err || (err = reluva::audio::decodeWav(data, sound))) {
            std::printf("not read: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        seconds += double(sound.frames()) / sound.rate;
        ++kinds[reluva::text::format("%d Hz %d-bit %s", sound.rate, sound.sourceBits,
                                     sound.channels == 2 ? "stereo" : "mono")];
    }
    for (const auto &[kind, n] : kinds)
        std::printf("%6d  %s\n", n, kind.c_str());
    std::printf("%d sounds read, %d failed; %.1f minutes\n", read, failed, seconds / 60);
    return failed ? 1 : 0;
}

int cmdMovieCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d movie-check <game folder> [name filter]\n");
        return 2;
    }
    GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::string filter = argc > 3 ? reluva::text::toLower(argv[3]) : "";
    int read = 0;
    int failed = 0;
    std::vector<uint8_t> data;
    std::vector<uint8_t> rgba;
    for (const std::string &name : files.list()) {
        if (!reluva::text::hasSuffixIgnoreCase(name, ".bik") ||
            reluva::text::toLower(name).find(filter) == std::string::npos)
            continue;
        reluva::bink::Movie movie;
        std::vector<int16_t> sound;
        Error err = files.read(name, data);
        if (!err)
            err = movie.open(data);
        if (!err && movie.audioRate() > 0)
            err = movie.decodeAllAudio(sound);
        uint32_t frames = 0;
        for (bool more = true; !err && more;) {
            err = movie.nextFrame(more);
            if (!err && more) {
                movie.toRgba(rgba);
                ++frames;
            }
        }
        if (err) {
            std::printf("not read: %s, at frame %u: %s\n", name.c_str(), frames, err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        const int channels = movie.audioChannels();
        std::printf("%-36s %3dx%-3d %5u frames %6.2f fps, %s\n", name.c_str(), movie.width(), movie.height(), frames,
                    double(movie.rateNumerator()) / movie.rateDenominator(),
                    movie.audioRate() ? reluva::text::format("%d Hz %s, %.1f s of sound", movie.audioRate(),
                                                             channels == 2 ? "stereo" : "mono",
                                                             double(sound.size()) / channels / movie.audioRate())
                                            .c_str()
                                      : "no sound");
    }
    std::printf("%d movies decoded whole, %d failed\n", read, failed);
    return failed ? 1 : 0;
}

int cmdMovieFrame(int argc, char **argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: reluva-tool storm3d movie-frame <game folder> <name> <frame> <out.png>\n");
        return 2;
    }
    GameFiles files;
    std::vector<uint8_t> data;
    reluva::bink::Movie movie;
    Error err = files.open(argv[2]);
    if (!err)
        err = files.read(argv[3], data);
    if (!err)
        err = movie.open(data);
    const long wanted = std::strtol(argv[4], nullptr, 10);
    bool more = true;
    while (!err && more && movie.frameNumber() < wanted)
        err = movie.nextFrame(more);
    if (!err && movie.frameNumber() != wanted)
        err = Error::fail(std::string(argv[3]) + " has " + std::to_string(movie.frameCount()) + " frames");
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    reluva::image::Image img;
    img.w = movie.width();
    img.h = movie.height();
    movie.toRgba(img.rgba);
    const std::vector<uint8_t> png = reluva::image::encodePng(img);
    std::ofstream out(argv[5], std::ios::binary);
    if (!out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()))) {
        std::fprintf(stderr, "Cannot write %s\n", argv[5]);
        return 1;
    }
    return 0;
}

const reluva::tool::Commands
    kCommands("storm3d",
              {
                  {"sound-check", "<game folder>", "decode every sound, and count them by kind", cmdSoundCheck},
                  {"movie-check", "<game folder> [name filter]",
                   "decode every frame and the sound of each movie, and report each", cmdMovieCheck},
                  {"movie-frame", "<game folder> <name> <frame> <out.png>",
                   "write one frame of a movie (from 0) as PNG", cmdMovieFrame},
              });

} // namespace
