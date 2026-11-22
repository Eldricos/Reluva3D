// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Sith commands for the files in a game's archives.

#include "audio/wav.h"
#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/sith/formats/bitmap.h"
#include "engines/sith/formats/colormap.h"
#include "engines/sith/formats/episode.h"
#include "engines/sith/formats/font.h"
#include "engines/sith/formats/material.h"
#include "engines/sith/formats/sound_class.h"
#include "engines/sith/formats/strings.h"
#include "engines/sith/game_files.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::Error;
namespace sith = reluva::sith;

// What a file of one kind is, in a line; an error if it can't be read.
using Describe = Error (*)(const std::vector<uint8_t> &, std::string &);

Error describeColormap(const std::vector<uint8_t> &data, std::string &line) {
    sith::Colormap cmp;
    if (Error err = sith::parseColormap(data, cmp))
        return err;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "tint %.2f %.2f %.2f%s", cmp.tint[0], cmp.tint[1], cmp.tint[2],
                  cmp.blend.empty() ? "" : ", blend table");
    line = buf;
    return Error::success();
}

Error describeBitmap(const std::vector<uint8_t> &data, std::string &line) {
    sith::Bitmap bm;
    if (Error err = sith::parseBitmap(data, bm))
        return err;
    line = std::to_string(bm.bitsPerPixel) + "-bit, " + std::to_string(bm.cels.size()) + " cels of " +
           std::to_string(bm.cels.empty() ? 0 : bm.cels[0].w) + "x" +
           std::to_string(bm.cels.empty() ? 0 : bm.cels[0].h) + ", palette mode " + std::to_string(bm.paletteMode);
    return Error::success();
}

Error describeMaterial(const std::vector<uint8_t> &data, std::string &line) {
    sith::Material mat;
    if (Error err = sith::parseMaterial(data, mat))
        return err;
    line = std::to_string(mat.cels.size()) + " cels, " + std::to_string(mat.textures.size()) + " textures";
    if (!mat.textures.empty())
        line += " of " + std::to_string(mat.textures[0].w) + "x" + std::to_string(mat.textures[0].h) + ", " +
                std::to_string(mat.textures[0].mips.size()) + " mips" +
                (mat.textures[0].transparent ? ", transparent" : "");
    return Error::success();
}

Error describeFont(const std::vector<uint8_t> &data, std::string &line) {
    sith::Font font;
    if (Error err = sith::parseFont(data, font))
        return err;
    line = std::to_string(font.glyphs.size()) + " glyphs, " + std::to_string(font.strip.bitsPerPixel) + "-bit, " +
           std::to_string(font.strip.cels[0].h) + " high";
    return Error::success();
}

Error describeStrings(const std::vector<uint8_t> &data, std::string &line) {
    sith::StringTable table;
    if (Error err = sith::parseStrings(data, table))
        return err;
    line = std::to_string(table.entries.size()) + " strings (MSGS " + std::to_string(table.declaredCount) + ")";
    return Error::success();
}

Error describeSoundClass(const std::vector<uint8_t> &data, std::string &line) {
    sith::SoundClass snd;
    if (Error err = sith::parseSoundClass(data, snd))
        return err;
    line = std::to_string(snd.entries.size()) + " sounds";
    return Error::success();
}

Error describeWav(const std::vector<uint8_t> &data, std::string &line) {
    reluva::audio::Sound sound;
    if (Error err = reluva::audio::decodeWav(data, sound))
        return err;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%d Hz, %d channel%s, %.2f s%s", sound.rate, sound.channels,
                  sound.channels == 1 ? "" : "s", double(sound.frames()) / sound.rate, sound.loopEnd ? ", loops" : "");
    line = buf;
    return Error::success();
}

Error describeEpisode(const std::vector<uint8_t> &data, std::string &line) {
    sith::Episode episode;
    if (Error err = sith::parseEpisode(data, episode))
        return err;
    line = "\"" + episode.title + "\", type " + std::to_string(episode.type) + ", " +
           std::to_string(episode.steps.size()) + " steps (SEQ " + std::to_string(episode.declaredSteps) + ")";
    return Error::success();
}

const std::map<std::string, Describe> kKinds = {
    {".cmp", describeColormap}, {".bm", describeBitmap},      {".mat", describeMaterial}, {".sft", describeFont},
    {".uni", describeStrings},  {".snd", describeSoundClass}, {".wav", describeWav},      {".jk", describeEpisode},
};

// Every colormap, picture, material, font, string table, sound, sound
// class and episode in the install's archives read, a line each (-q: only the failures), and a
// count of each kind.
int cmdFormats(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sith formats <install> [-q]\n");
        return 2;
    }
    const bool quiet = argc > 3 && std::string(argv[3]) == "-q";
    sith::GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::map<std::string, std::pair<int, int>> counts; // extension -> read, failed
    for (const auto &gob : files.archives())
        for (const auto &entry : gob->entries()) {
            const std::string name = reluva::fs::toLower(entry.name);
            const size_t dot = name.rfind('.');
            auto kind = dot == std::string::npos ? kKinds.end() : kKinds.find(name.substr(dot));
            if (kind == kKinds.end())
                continue;
            std::vector<uint8_t> data;
            std::string line;
            Error err = gob->read(entry.name, data);
            if (!err)
                err = kind->second(data, line);
            auto &[read, failed] = counts[kind->first];
            ++(err ? failed : read);
            if (err)
                std::printf("%s: %s: FAILED: %s\n", gob->describe().c_str(), entry.name.c_str(), err.message.c_str());
            else if (!quiet)
                std::printf("%s: %s: %s\n", gob->describe().c_str(), entry.name.c_str(), line.c_str());
        }
    int failures = 0;
    for (const auto &[ext, n] : counts) {
        std::printf("%s: %d read, %d failed\n", ext.c_str(), n.first, n.second);
        failures += n.second;
    }
    return failures ? 1 : 0;
}

// One string table's strings, a key and its string a line.
int cmdStrings(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool sith strings <install> <file.uni>\n");
        return 2;
    }
    sith::GameFiles files;
    std::vector<uint8_t> data;
    sith::StringTable table;
    Error err = files.open(argv[2]);
    if (!err)
        err = files.read(argv[3], data);
    if (!err)
        err = sith::parseStrings(data, table);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const auto &[key, value] : table.entries)
        std::printf("%s = %s\n", key.c_str(), value.c_str());
    return 0;
}

const Commands kCommands("sith",
                         {
                             {"formats", "<install> [-q]",
                              "read every colormap, picture, material, font, string table, sound, sound class and "
                              "episode in the archives",
                              cmdFormats},
                             {"strings", "<install> <file.uni>", "a string table's keys and strings", cmdStrings},
                         });

} // namespace
