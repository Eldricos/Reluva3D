// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Sith commands for the episodes and the CDs' cut scenes.

#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/sith/cut_scene.h"
#include "engines/sith/folders.h"
#include "engines/sith/formats/episode.h"
#include "engines/sith/game_files.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::Error;
namespace sith = reluva::sith;

// Each episode's steps, a line each, with where each cut scene was found.
int cmdEpisode(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sith episode <install>\n");
        return 2;
    }
    sith::GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::vector<sith::CutScene> cuts = sith::findCutScenes(sith::findFolders(argv[2], {}));
    int failures = 0;
    for (const auto &gob : files.archives()) {
        if (!gob->has("episode.jk"))
            continue;
        std::vector<uint8_t> data;
        sith::Episode episode;
        Error err = gob->read("episode.jk", data);
        if (!err)
            err = sith::parseEpisode(data, episode);
        if (err) {
            std::printf("%s: FAILED: %s\n", gob->describe().c_str(), err.message.c_str());
            ++failures;
            continue;
        }
        std::printf("%s: \"%s\", type %d, %zu steps\n", gob->describe().c_str(), episode.title.c_str(), episode.type,
                    episode.steps.size());
        for (const sith::Episode::Step &s : episode.steps) {
            std::string where;
            if (const std::string f = reluva::fs::toLower(s.file); f.ends_with(".smk") || f.ends_with(".san")) {
                where = "  (not on the CDs found)";
                for (const sith::CutScene &c : cuts)
                    if (reluva::fs::toLower(c.name) == reluva::fs::toLower(s.file))
                        where = "  (on CD " + std::to_string(c.cd) + ")";
            }
            std::printf("  %4d: CD %d level %2d %-6s %-20s powers %2d %2d  goto %4d %4d%s\n", s.line, s.cd, s.level,
                        s.kind.c_str(), s.file.c_str(), s.lightPower, s.darkPower, s.gotoA, s.gotoB, where.c_str());
        }
    }
    return failures ? 1 : 0;
}

// FNV-1a, 64 bits, over `size` bytes.
void addTo(uint64_t &hash, const void *data, size_t size) {
    const auto *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ b[i]) * 0x100000001b3ull;
}

// Every cut scene on the CDs found for the install: its picture, length
// and sound, and with -all every frame decoded, with a checksum of its
// pictures, palettes, sound and subtitle numbers: what changes when the
// decoder does, with nothing of the game's own in it.
int cmdMovies(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sith movies <install> [-all]\n");
        return 2;
    }
    const bool all = argc > 3 && std::string(argv[3]) == "-all";
    const sith::GameFolders folders = sith::findFolders(argv[2], {});
    const std::vector<sith::CutScene> cuts = sith::findCutScenes(folders);
    if (cuts.empty()) {
        std::fprintf(stderr, "no cut scenes: neither CD was found for %s\n", argv[2]);
        return 1;
    }
    int failures = 0;
    for (const sith::CutScene &cut : cuts) {
        sith::CutSceneVideo dec;
        std::string error;
        if (!dec.open(cut.path, error)) {
            std::printf("CD %d %s: FAILED: %s\n", cut.cd, cut.name.c_str(), error.c_str());
            ++failures;
            continue;
        }
        int frames = 0;
        size_t samples = 0;
        uint64_t hash = 0xcbf29ce484222325ull;
        bool ok = true;
        // The first frame is decoded as it opens.
        for (bool first = true; all && ok && (first || !dec.endOfVideo()); first = false) {
            if (!first && !dec.decodeNextFrame()) {
                ok = false;
                break;
            }
            ++frames;
            const std::vector<int16_t> audio = dec.takeAudio();
            samples += audio.size();
            addTo(hash, audio.data(), audio.size() * 2);
            const reluva::Surface &picture = dec.surface();
            for (int y = 0; y < picture.h; ++y)
                addTo(hash, picture.row(y), size_t(picture.w));
            addTo(hash, dec.palette(), 768);
            const std::vector<uint8_t> subtitle = dec.subtitleData();
            addTo(hash, subtitle.data(), subtitle.size());
        }
        std::printf("CD %d %-12s %dx%d, %d frames, %.2f fps, %.1f s, sound %s", cut.cd, cut.name.c_str(), dec.width(),
                    dec.height(), dec.frameCount(), 1e6 / dec.frameMicros(),
                    dec.frameCount() * double(dec.frameMicros()) / 1e6,
                    dec.hasAudio()
                        ? (std::to_string(dec.audioRate()) + " Hz " + (dec.audioStereo() ? "stereo" : "mono")).c_str()
                        : "none");
        if (all) {
            const int channels = dec.audioStereo() ? 2 : 1;
            std::printf(", %d frames decoded%s, %.1f s of sound, checksum %016llx", frames, ok ? "" : " (FAILED)",
                        dec.hasAudio() ? double(samples) / channels / dec.audioRate() : 0.0, (unsigned long long)hash);
        }
        std::printf("\n");
        failures += ok ? 0 : 1;
    }
    return failures ? 1 : 0;
}

const Commands kCommands("sith", {
                                     {"episode", "<install>",
                                      "each episode's steps, and the CD each cut scene was found on", cmdEpisode},
                                     {"movies", "<install> [-all]",
                                      "the cut scenes on the CDs found; with -all every frame decoded", cmdMovies},
                                 });

} // namespace
