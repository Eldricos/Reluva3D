// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Birth of the Federation commands for its own archives:
// alt.res's pictures and the .snd archives' sounds, listed and extracted
// (docs/engines/botf/formats/alt-res.md, snd.md).

#include "audio/decode.h"
#include "audio/sound.h"
#include "audio/wav.h"
#include "devtools/tool.h"
#include "engines/botf/formats/alt_res.h"
#include "engines/botf/formats/snd_archive.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

using namespace reluva::tool;

void writeFile(const std::filesystem::path &path, const Bytes &data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
}

// Every picture in alt.res, each decompressed to check it, and written out
// when a folder is given.
int cmdAltList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool botf alt-list <alt.res> [output folder]\n");
        return 2;
    }
    reluva::botf::AltResArchive alt;
    if (reluva::Error err = alt.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::filesystem::path outDir = argc > 3 ? argv[3] : "";
    if (!outDir.empty())
        std::filesystem::create_directories(outDir);
    int failed = 0;
    for (const auto &e : alt.entries()) {
        Bytes data;
        const reluva::Error err = alt.read(e, data);
        std::printf("%-16s offset %9u packed %8u size %8u index %2u flag %u check 0x%04x  %s\n", e.name.c_str(),
                    e.offset, e.compressedSize, e.size, e.index, e.flag, e.check, err ? err.message.c_str() : "ok");
        if (err) {
            ++failed;
            continue;
        }
        if (!outDir.empty())
            writeFile(outDir / e.name, data);
    }
    std::printf("%zu files, %d failed\n", alt.entries().size(), failed);
    return failed ? 1 : 0;
}

// Every sound in a .snd archive, decoded, and written out as WAV files when
// a folder is given. The ADPCM ones' rate isn't stored: -r tries another.
int cmdSndList(int argc, char **argv) {
    bool music = false;
    int rate = reluva::botf::kDefaultAdpcmRate;
    const char *file = nullptr;
    std::filesystem::path outDir;
    for (int a = 2; a < argc; ++a) {
        if (std::strcmp(argv[a], "-m") == 0)
            music = true;
        else if (std::strcmp(argv[a], "-r") == 0 && a + 1 < argc)
            rate = int(parseNumber(argv[++a]));
        else if (!file)
            file = argv[a];
        else
            outDir = argv[a];
    }
    if (!file || rate <= 0) {
        std::fprintf(stderr, "usage: reluva-tool botf snd-list <file.snd> [-m] [-r rate] [output folder]\n");
        return 2;
    }
    reluva::botf::SndArchive snd;
    if (reluva::Error err = snd.open(file, music)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    if (!outDir.empty())
        std::filesystem::create_directories(outDir);
    int failed = 0;
    const auto &entries = snd.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        reluva::audio::Sound sound;
        const reluva::Error err = snd.decode(i, rate, sound);
        char piece[24] = "";
        if (e.piece >= 0)
            std::snprintf(piece, sizeof piece, "piece %2d ", e.piece);
        std::printf("%4zu %soffset %9u size %8u decoded %8u flags %u  ", i, piece, e.offset, e.size, e.decodedSize,
                    e.flags);
        if (err) {
            std::printf("%s\n", err.message.c_str());
            ++failed;
            continue;
        }
        std::printf("%s %d Hz %s %.2f s\n", reluva::audio::formatName(sound.format), sound.rate,
                    sound.channels == 2 ? "stereo" : "mono", double(sound.frames()) / sound.rate);
        if (!outDir.empty()) {
            char name[32];
            std::snprintf(name, sizeof name, "%04zu.wav", i);
            writeFile(outDir / name, reluva::audio::encodePcmWav(sound));
        }
    }
    std::printf("%zu sounds, %d failed\n", entries.size(), failed);
    return failed ? 1 : 0;
}

const Commands
    kCommands("botf", {
                          {"alt-list", "<alt.res> [output folder]",
                           "list alt.res's pictures, decompressing each (and extract them)", cmdAltList},
                          {"snd-list", "<file.snd> [-m] [-r rate] [output folder]",
                           "list a .snd archive's sounds, decoding each (and write them as WAV files); -m: music.snd",
                           cmdSndList},
                      });

} // namespace
