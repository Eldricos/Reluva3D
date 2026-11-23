// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's sound banks (.sdt).

#include "audio/sound.h"
#include "common/fs.h"
#include "common/md5.h"
#include "devtools/tool.h"
#include "engines/tpw/formats/sdt.h"
#include "engines/tpw/formats/sound_map.h"
#include "engines/tpw/game_files.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::tpw::SoundBank;
using reluva::tpw::SoundMap;

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

// Every sound category of a game (cat_*SFX.map with its BANK.map) read,
// each sound it names found in its bank, and its length set against the
// bank's; with a category's effects map in Data, its effects listed.
int cmdSfxCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw sfx-check <game folder> [cat_*SFX.map in Data]\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::string suffix = "sfx.map";
    std::vector<std::string> maps;
    for (const std::string &name : files.list()) {
        const std::string lower = reluva::fs::toLower(name);
        const size_t slash = lower.find_last_of('/');
        if (lower.size() > suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0 &&
            lower.compare(slash + 1, 4, "cat_") == 0 && (argc < 4 || lower == reluva::fs::toLower(argv[3])))
            maps.push_back(name);
    }
    std::map<std::string, SoundBank> banks;
    reluva::Md5 all;
    int failed = 0;
    int missing = 0;
    int lengthsDiffer = 0;
    size_t effects = 0;
    size_t sounds = 0;
    for (const std::string &name : maps) {
        std::vector<uint8_t> sfx;
        std::vector<uint8_t> bankMap;
        SoundMap map;
        reluva::Error err = files.read(name, sfx);
        if (!err)
            err = files.read(name.substr(0, name.size() - suffix.size()) + "BANK.map", bankMap);
        if (!err)
            err = map.parse(sfx, bankMap);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        // The banks' names lead from the folder above the category's.
        const std::string folder = name.substr(0, name.find_last_of('/'));
        const std::string parent = folder.substr(0, folder.find_last_of('/') + 1);
        effects += map.effects().size();
        for (const SoundMap::Effect &effect : map.effects()) {
            all.update(&effect.id, sizeof effect.id);
            if (argc > 3)
                std::printf("%5u", effect.id);
            for (const SoundMap::Group &group : effect.groups)
                for (const SoundMap::Sound &s : group.sounds) {
                    ++sounds;
                    all.update(&s.entry, sizeof s.entry);
                    std::string file = s.bank < map.banks().size() ? map.banks()[s.bank] : std::string();
                    for (char &c : file)
                        if (c == '\\')
                            c = '/';
                    file = parent + file + "HD.sdt";
                    if (!banks.count(file)) {
                        std::vector<uint8_t> data;
                        if (!files.read(file, data))
                            (void)banks[file].parse(std::move(data));
                        else
                            banks[file];
                    }
                    const auto &entries = banks[file].entries();
                    if (s.bank >= map.banks().size() || s.entry >= entries.size() || !entries[s.entry].readable) {
                        std::printf("not found: %s: effect %u: %s %u\n", name.c_str(), effect.id, file.c_str(),
                                    s.entry);
                        ++missing;
                        continue;
                    }
                    const SoundBank::Entry &e = entries[s.entry];
                    const double seconds = double(e.mpeg ? e.pcmBytes : e.size) / 2 / e.channels / e.rate;
                    // The maps give milliseconds; a bank's MPEG lengths are
                    // whole frames.
                    if (std::abs(seconds * 1000 - s.millis) > 60) {
                        std::printf("not its bank's length: %s: effect %u: %s %u %s: %u ms, the bank %.0f\n",
                                    name.c_str(), effect.id, file.c_str(), s.entry, e.name.c_str(), s.millis,
                                    seconds * 1000);
                        ++lengthsDiffer;
                    }
                    if (argc > 3)
                        std::printf("  %s %u %s", file.substr(file.find_last_of('/') + 1).c_str(), s.entry,
                                    e.name.c_str());
                }
            if (argc > 3)
                std::printf("\n");
        }
    }
    std::printf("%zu categories, %d failed; %zu effects, %zu sounds, %d not found in their banks, %d not their "
                "bank's length; md5 %s\n",
                maps.size(), failed, effects, sounds, missing, lengthsDiffer, all.hexDigest().c_str());
    return failed || maps.empty() ? 1 : 0;
}

const Commands kCommands("tpw",
                         {
                             {"sdt-check", "<game folder> [bank in Data]",
                              "decode every sound bank (.sdt), or list one's sounds", cmdSdtCheck},
                             {"sfx-check", "<game folder> [cat_*SFX.map in Data]",
                              "read every sound category's maps, finding their sounds in the banks", cmdSfxCheck},
                         });

} // namespace
