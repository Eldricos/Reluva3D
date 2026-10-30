// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's New Worlds commands for its sounds and fonts.

#include "tools/tool.h"

#include "common/audio/decode.h"
#include "common/audio/wav.h"
#include "engines/newworlds/cat_archive.h"
#include "engines/newworlds/fif.h"
#include "engines/newworlds/model_textures.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;

int cmdWavInfo(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool wav-info <file.wav | file.mp3 | file.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    auto describe = [](const std::string& name, const std::vector<uint8_t>& data, const std::string& note = "") {
        reluva::audio::Sound s;
        if (reluva::Error err = reluva::audio::decodeSound(data, s)) {
            std::printf("%-28s %s\n", name.c_str(), err.message.c_str());
            return false;
        }
        std::printf("%-28s %s, %d channel%s, %d Hz, %zu frames (%.2f s)%s\n", name.c_str(),
                    reluva::audio::formatName(s.format), s.channels, s.channels == 1 ? "" : "s", s.rate, s.frames(),
                    double(s.frames()) / s.rate, note.c_str());
        return true;
    };
    const std::string path = argv[2];
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".cat") == 0) {
        nw::CatArchive cat;
        if (reluva::Error err = cat.open(path)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        // Some sounds are stored many times over, under the same or other
        // names: note each copy's first.
        unsigned count = 0, good = 0;
        std::map<std::string, unsigned> first; // bytes -> entry number
        std::vector<uint8_t> data;
        for (size_t i = 0; i < cat.entries().size(); ++i) {
            const auto& e = cat.entries()[i];
            if (e.tag != nw::cattag::Wave)
                continue;
            ++count;
            if (cat.read(e, data)) {
                std::printf("%-28s cannot be read\n", e.name.c_str());
                continue;
            }
            const auto [it, added] = first.emplace(std::string(data.begin(), data.end()), unsigned(i));
            const std::string note =
                added ? "" : " copy of " + cat.entries()[it->second].name + " (entry " + std::to_string(it->second) + ")";
            good += describe(e.name, data, note);
        }
        std::printf("%u sounds (%zu different), %u decode\n", count, first.size(), good);
        return good == count ? 0 : 1;
    }
    std::vector<uint8_t> data;
    if (!readFile(path.c_str(), data)) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return 1;
    }
    return describe(path, data) ? 0 : 1;
}

int cmdWavDecode(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool wav-decode <in.wav | in.mp3> <out.wav>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "cannot open %s\n", argv[2]);
        return 1;
    }
    reluva::audio::Sound s;
    if (reluva::Error err = reluva::audio::decodeSound(data, s)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::vector<uint8_t> out = reluva::audio::encodePcmWav(s);
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", argv[3]);
        return 1;
    }
    std::printf("%zu frames at %d Hz written to %s\n", s.frames(), s.rate, argv[3]);
    return 0;
}

int cmdFifInfo(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool fif-info <file.fif | file.cat>\n");
        return 2;
    }
    namespace nw = reluva::newworlds;
    auto summary = [](const nw::GameFont& f) {
        size_t drawn = 0;
        int widest = 0;
        for (const auto& g : f.glyphs) {
            drawn += g.height > 0;
            widest = std::max(widest, g.width);
        }
        char line[160];
        std::snprintf(line, sizeof line, "codes %u-%zu, %zu drawn, line height %d, size %d, widest %d", f.first,
                      f.first + f.glyphs.size() - 1, drawn, f.lineHeight, f.size, widest);
        return std::string(line);
    };
    const std::string path = argv[2];
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".cat") == 0) {
        auto cat = std::make_unique<nw::CatArchive>();
        if (reluva::Error err = cat->open(path)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        unsigned listed = 0;
        for (const auto& e : cat->entries())
            listed += e.tag == nw::cattag::Fif;
        nw::Archives archives;
        archives.push_back(std::move(cat));
        const auto fonts = nw::loadGameFonts(archives);
        for (const auto& f : fonts)
            std::printf("%-16s %dx%d image; %s\n", f.name.c_str(), f.imageWidth, f.imageHeight, summary(f).c_str());
        std::printf("%u font entries, %zu different fonts read\n", listed, fonts.size());
        return 0;
    }
    Bytes d;
    if (!readFile(path.c_str(), d))
        return 1;
    nw::GameFont f;
    if (reluva::Error err = nw::parseFif(d, f)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("%s\n", summary(f).c_str());
    for (size_t i = 0; i < f.glyphs.size(); ++i) {
        const auto& g = f.glyphs[i];
        const uint32_t code = f.first + uint32_t(i);
        std::printf("  %3u %c  x %3d y %3d  %2d x %2d%s\n", code, code < 127 ? char(code) : ' ', g.x, g.y, g.width,
                    g.height, g.height ? "" : "  (none)");
    }
    return 0;
}

const Commands commands("newworlds", {
    {"wav-info", "<file.wav | file.mp3 | file.cat>",
     "show a sound's format and length, or survey an archive's",
     cmdWavInfo},
    {"wav-decode", "<in.wav | in.mp3> <out.wav>",
     "decode a sound to 16-bit PCM",
     cmdWavDecode},
    {"fif-info", "<file.fif | file.cat>",
     "show a font's glyph table, or list an archive's fonts",
     cmdFifInfo},
});

} // namespace
