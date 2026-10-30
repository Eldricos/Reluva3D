// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 command for videos (Bink).

#include "tools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "common/video/bink.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

// Decodes a Bink movie: its frames as raw YUV 4:2:0 (each plane cropped
// to the picture, as ffmpeg -pix_fmt yuv420p writes them) and its first
// audio track as 16-bit PCM, for comparing with another decoder.
int cmdBinkDecode(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool idtech3 bink-decode <file.bik> [-frames N] [-yuv out.yuv] [-pcm out.pcm]\n"
                             "       reluva-tool idtech3 bink-decode <game folder> [video/name.bik] [options]\n"
                             "  (a game folder alone lists its movies, inside its .pk3 files too)\n");
        return 2;
    }
    // A game folder: a movie by name from its files (.pk3s included), or
    // all of them listed with their revisions.
    const bool folder = std::filesystem::is_directory(argv[2]);
    const int first = folder && argc >= 4 && argv[3][0] != '-' ? 4 : 3;
    int frames = -1;
    std::string yuvPath, pcmPath;
    for (int i = first; i < argc; ++i) {
        if (std::strcmp(argv[i], "-frames") == 0 && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "-yuv") == 0 && i + 1 < argc)
            yuvPath = argv[++i];
        else if (std::strcmp(argv[i], "-pcm") == 0 && i + 1 < argc)
            pcmPath = argv[++i];
    }
    Bytes data;
    if (folder) {
        reluva::SearchSet set;
        if (!mountIdTech3(argv[2], set))
            return 1;
        if (first == 3) {
            int count = 0;
            for (const std::string& n : set.list()) {
                const std::string key = reluva::normalizeArchiveName(n);
                if (key.size() < 4 || key.compare(key.size() - 4, 4, ".bik") != 0)
                    continue;
                Bytes movie;
                std::string what = "too short to be a movie";
                if (reluva::Error e = set.read(key, movie))
                    what = "unreadable: " + e.message;
                else if (movie.size() >= 28)
                    what = std::string(reinterpret_cast<const char*>(movie.data()), 4) + ", " +
                           std::to_string(movie[20] | movie[21] << 8) + "x" + std::to_string(movie[24] | movie[25] << 8) +
                           ", " + std::to_string(movie.size()) + " bytes";
                std::printf("%s: %s\n", key.c_str(), what.c_str());
                ++count;
            }
            std::printf("%d movies\n", count);
            return 0;
        }
        std::string name = argv[3];
        if (name.find('/') == std::string::npos && name.find('\\') == std::string::npos)
            name = "video/" + name;
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".bik") != 0)
            name += ".bik";
        if (reluva::Error e = set.read(name, data)) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(), e.message.c_str());
            return 1;
        }
    } else if (!readFile(argv[2], data)) {
        return 1;
    }
    reluva::bink::Movie movie;
    if (reluva::Error e = movie.open(std::move(data))) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::printf("BIK%c %dx%d, %u frames at %u/%u, audio %d Hz x %d\n", movie.revision(), movie.width(),
                movie.height(), movie.frameCount(), movie.rateNumerator(), movie.rateDenominator(),
                movie.audioRate(), movie.audioChannels());
    std::FILE* yuv = yuvPath.empty() ? nullptr : std::fopen(yuvPath.c_str(), "wb");
    std::vector<int16_t> sound;
    const auto started = std::chrono::steady_clock::now();
    int n = 0;
    for (; frames < 0 || n < frames; ++n) {
        bool more = false;
        if (reluva::Error e = movie.nextFrame(more, &sound)) {
            std::fprintf(stderr, "frame %d: %s\n", n, e.message.c_str());
            break;
        }
        if (!more)
            break;
        if (!yuv)
            continue;
        // The newest frame's planes (after the swap, the "old" ones).
        reluva::bink::Movie& m = movie;
        for (int p = 0; p < 3; ++p) {
            const int w = p ? (m.width() + 1) / 2 : m.width(), h = p ? (m.height() + 1) / 2 : m.height();
            std::vector<uint8_t> rgbaUnused;
            const auto& plane = m.decodedPlane(p);
            for (int y = 0; y < h; ++y)
                std::fwrite(plane.data() + size_t(y) * size_t(m.pitch(p)), 1, size_t(w), yuv);
        }
    }
    if (yuv)
        std::fclose(yuv);
    if (!pcmPath.empty())
        if (std::FILE* f = std::fopen(pcmPath.c_str(), "wb")) {
            std::fwrite(sound.data(), 2, sound.size(), f);
            std::fclose(f);
        }
    std::printf("%d frames, %zu samples in %.2f s\n", n, sound.size(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    return 0;
}

const Commands commands("idtech3", {
    {"bink-decode", "<file.bik, or game folder [video/name]> [-frames N] [-yuv out] [-pcm out]",
     "decode a Bink movie: its frames as raw YUV 4:2:0, its first audio track as 16-bit PCM, for "
     "comparing with another decoder; a game folder alone lists its movies",
     cmdBinkDecode},
});

} // namespace
