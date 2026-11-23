// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's sound banks (.sdt).

#include "audio/sound.h"
#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/sdt.h"
#include "engines/tpw/game_files.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::tpw::SoundBank;

// Every sound bank of a game decoded; with a bank's name in Data, its
// sounds listed. Sounds that don't decode are listed, and fail it only
// when a bank can't be read at all: some of the game's own don't
// (docs/engines/tpw/games/simcoaster/formats/sdt.md).
int cmdSdtCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw sdt-check <game folder> [bank in Data]\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<std::string> banks;
    for (const std::string &name : files.list())
        if (reluva::fs::toLower(extensionOf(name)) == ".sdt" &&
            (argc < 4 || reluva::fs::toLower(name) == reluva::fs::toLower(argv[3])))
            banks.push_back(name);
    reluva::Md5 all;
    std::map<std::string, int> kinds;
    int failed = 0;
    int undecoded = 0;
    int unreadable = 0;
    int lengthsDiffer = 0;
    size_t sounds = 0;
    for (const std::string &name : banks) {
        std::vector<uint8_t> data;
        SoundBank bank;
        reluva::Error err = files.read(name, data);
        if (!err)
            err = bank.parse(std::move(data));
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        for (size_t i = 0; i < bank.entries().size(); ++i) {
            const SoundBank::Entry &e = bank.entries()[i];
            if (!e.readable) {
                ++unreadable;
                continue;
            }
            reluva::audio::Sound sound;
            if (reluva::Error decodeErr = bank.decode(i, sound)) {
                std::printf("doesn't decode: %s %zu %s: %s\n", name.c_str(), i, e.name.c_str(),
                            decodeErr.message.c_str());
                ++undecoded;
                continue;
            }
            ++sounds;
            const size_t frames = sound.samples.size() / size_t(std::max(1, sound.channels));
            // The MPEG decoder's frames come to the header's decoded size,
            // give or take one frame (1,152 samples a layer II frame).
            if (e.mpeg && std::llabs(static_cast<long long>(frames) -
                                     static_cast<long long>(e.pcmBytes / 2 / uint32_t(sound.channels))) > 1152) {
                std::printf("not its header's length: %s %zu %s: %zu samples, the header %u\n", name.c_str(), i,
                            e.name.c_str(), frames, e.pcmBytes / 2 / uint32_t(sound.channels));
                ++lengthsDiffer;
            }
            ++kinds[std::string(e.mpeg ? "MPEG" : "PCM") + (sound.channels == 2 ? " stereo " : " mono ") +
                    std::to_string(sound.rate) + " Hz"];
            all.update(sound.samples.data(), sound.samples.size() * sizeof(sound.samples[0]));
            if (argc > 3)
                std::printf("%4zu  %-16s %s %d ch %d Hz %7.2f s\n", i, e.name.c_str(), e.mpeg ? "MPEG" : "PCM ",
                            sound.channels, sound.rate, double(frames) / std::max(1, sound.rate));
        }
    }
    for (const auto &[kind, n] : kinds)
        std::printf("  %-24s %d\n", kind.c_str(), n);
    std::printf("%zu banks, %d failed; %zu sounds decoded, %d don't, %d entries without a header; %d MPEG sounds not "
                "their header's length; md5 %s\n",
                banks.size(), failed, sounds, undecoded, unreadable, lengthsDiffer, all.hexDigest().c_str());
    return failed || banks.empty() ? 1 : 0;
}

const Commands kCommands("tpw", {
                                    {"sdt-check", "<game folder> [bank in Data]",
                                     "decode every sound bank (.sdt), or list one's sounds", cmdSdtCheck},
                                });

} // namespace
