// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Starfleet Command commands for the game's file formats:
// listing them, and reading every one of a kind to check its reader.

#include "audio/decode.h"
#include "audio/sound.h"
#include "audio/wav.h"
#include "common/fs.h"
#include "common/zip.h"
#include "common/stream.h"
#include "devtools/tool.h"
#include "engines/sfc/formats/bft_font.h"
#include "engines/sfc/formats/model.h"
#include "engines/sfc/formats/sprite_archive.h"
#include "engines/sfc/formats/text_tables.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

// Every file under a folder whose extension is `ext` (lower case, with
// its dot), sorted by path.
std::vector<std::filesystem::path> filesWith(const std::filesystem::path &root, const std::string &ext) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec))
        if (entry.is_regular_file() && fs::toLower(entry.path().extension().string()) == ext)
            out.push_back(entry.path());
    std::sort(out.begin(), out.end());
    return out;
}

int openSprites(const char *folder, sfc::SpriteArchive &archive) {
    const auto path = fs::findFile(folder, "Assets/Sprites/sprites.q3");
    if (!path) {
        std::fprintf(stderr, "%s: no Assets/Sprites/sprites.q3\n", folder);
        return 1;
    }
    if (Error err = archive.open(*path)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    return 0;
}

// sprites.q3's images, each decoded: its header and name, and whether it
// decodes.
int cmdSprites(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc sprites <game folder> [-q]\n");
        return 2;
    }
    const bool quiet = argc > 3 && std::string(argv[3]) == "-q";
    sfc::SpriteArchive archive;
    if (int rc = openSprites(argv[2], archive))
        return rc;
    size_t failed = 0;
    size_t named = 0;
    size_t eight = 0;
    Surface s;
    for (size_t i = 0; i < archive.images().size(); ++i) {
        const auto &image = archive.images()[i];
        const Error err = archive.decode(image, s);
        failed += err ? 1 : 0;
        named += image.name.empty() ? 0 : 1;
        eight += image.bits == 8 ? 1 : 0;
        if (!quiet || err)
            std::printf("%5zu  %9u  %4ux%-4u  %2u bits  hot %4d,%-4d  %s%s\n", i, image.offset, image.width,
                        image.height, image.bits, image.hotX, image.hotY, image.name.c_str(),
                        err ? ("  ERROR " + err.message).c_str() : "");
    }
    std::printf("%zu images (%zu with 8 bits a pixel, %zu named), objects from %u; %zu failed\n",
                archive.images().size(), eight, named, archive.objectsOffset(), failed);
    return failed ? 1 : 0;
}

// One of sprites.q3's images, as a PNG.
int cmdSpritePng(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool sfc sprite-png <game folder> <index> <out.png>\n");
        return 2;
    }
    sfc::SpriteArchive archive;
    if (int rc = openSprites(argv[2], archive))
        return rc;
    const size_t index = std::strtoul(argv[3], nullptr, 10);
    if (index >= archive.images().size()) {
        std::fprintf(stderr, "no image %zu: there are %zu\n", index, archive.images().size());
        return 1;
    }
    Surface s;
    if (Error err = archive.decode(archive.images()[index], s)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    image::Image img;
    img.w = s.w;
    img.h = s.h;
    img.rgba = s.pixels;
    if (!writeFile(argv[4], image::encodePng(img))) {
        std::fprintf(stderr, "can't write %s\n", argv[4]);
        return 1;
    }
    return 0;
}

// Every .bft font under a folder, read whole.
int cmdFonts(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc fonts <game folder>\n");
        return 2;
    }
    const auto files = filesWith(argv[2], ".bft");
    size_t failed = 0;
    for (const auto &path : files) {
        std::vector<uint8_t> data;
        sfc::BftFont font;
        Error err = readFile(path, data) ? font.read(data) : Error::fail("can't read it");
        failed += err ? 1 : 0;
        std::printf("%-24s line %3d  half %3d  %3zu glyphs%s\n", path.filename().string().c_str(), font.lineHeight(),
                    font.half(), font.glyphs().size(), err ? ("  ERROR " + err.message).c_str() : "");
    }
    std::printf("%zu fonts, %zu failed\n", files.size(), failed);
    return failed ? 1 : 0;
}

// Every .mod model under a folder, read whole: its levels, points,
// materials, and textures not found beside it.
int cmdModels(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc models <game folder>\n");
        return 2;
    }
    const auto files = filesWith(argv[2], ".mod");
    size_t failed = 0;
    size_t missing = 0;
    for (const auto &path : files) {
        std::vector<uint8_t> data;
        sfc::Model model;
        Error err = readFile(path, data) ? model.read(data) : Error::fail("can't read it");
        failed += err ? 1 : 0;
        std::string levels;
        for (const auto &mesh : model.meshes())
            levels += " " + std::to_string(mesh.triangles.size());
        std::string absent;
        for (const auto &m : model.materials())
            for (const std::string *name : {&m.texture, &m.illumination})
                if (!name->empty() && !fs::findFile(path.parent_path(), *name + ".bmp")) {
                    absent += " " + *name;
                    ++missing;
                }
        std::printf("%-18s r %7.2f  triangles%s  %zu points  %zu materials%s%s\n", path.filename().string().c_str(),
                    model.radius(), levels.c_str(), model.points().size(), model.materials().size(),
                    absent.empty() ? "" : ("  no texture" + absent).c_str(),
                    err ? ("  ERROR " + err.message).c_str() : "");
    }
    std::printf("%zu models, %zu failed, %zu textures not found\n", files.size(), failed, missing);
    return failed ? 1 : 0;
}

// A tab-separated table or a string table: its rows.
int cmdTable(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc table <file>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "can't read %s\n", argv[2]);
        return 1;
    }
    // The string tables are UTF-16, their second byte a zero.
    if (data.size() > 1 && data[1] == 0) {
        sfc::StringTable table;
        if (Error err = table.read(data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        for (const auto &e : table.entries)
            for (size_t i = 0; i < e.text.size(); ++i)
                std::printf("%-28s %s\n", i ? "" : e.key.c_str(), e.text[i].c_str());
        std::printf("%zu entries\n", table.entries.size());
        return 0;
    }
    sfc::TabTable table;
    if (Error err = table.read(data)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    size_t rows = 0;
    for (const auto &row : table.rows) {
        if (!sfc::TabTable::isData(row))
            continue;
        ++rows;
        std::string line;
        for (const auto &cell : row)
            line += cell + " | ";
        std::printf("%s\n", line.c_str());
    }
    std::printf("%zu rows the game reads, of %zu\n", rows, table.rows.size());
    return 0;
}

// Every WAV in Assets/Sound's zips, decoded: how many of each kind, and
// those that don't decode.
int cmdSounds(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc sounds <game folder>\n");
        return 2;
    }
    size_t failed = 0;
    for (const char *name : {"Assets/Sound/SFCSounds.zip", "Assets/Sound/SFCVoice.zip", "Assets/Sound/sfcmusic.zip"}) {
        const auto path = fs::findFile(argv[2], name);
        ZipArchive zip;
        if (!path || zip.open(*path)) {
            std::printf("%s: not found\n", name);
            continue;
        }
        std::map<std::string, size_t> kinds;
        for (const std::string &n : zip.list()) {
            if (std::filesystem::path(fs::toLower(n)).extension() != ".wav")
                continue;
            std::vector<uint8_t> data;
            audio::Sound sound;
            Error err = zip.read(n, data);
            if (!err)
                err = audio::decodeWav(data, sound);
            if (err) {
                ++failed;
                std::printf("  %s: ERROR %s\n", n.c_str(), err.message.c_str());
                continue;
            }
            ++kinds[std::string(audio::formatName(sound.format)) + " " + std::to_string(sound.rate) + " Hz " +
                    (sound.channels == 2 ? "stereo" : "mono")];
        }
        std::printf("%s\n", name);
        for (const auto &[kind, n] : kinds)
            std::printf("  %5zu  %s\n", n, kind.c_str());
    }
    std::printf("%zu failed\n", failed);
    return failed ? 1 : 0;
}

const Commands kCommands("sfc", {
                                    {"sprites", "<game folder> [-q]",
                                     "list sprites.q3's images and decode each (-q: only the failures)", cmdSprites},
                                    {"sprite-png", "<game folder> <index> <out.png>",
                                     "write one of sprites.q3's images as a PNG", cmdSpritePng},
                                    {"fonts", "<game folder>", "read every .bft font under a folder", cmdFonts},
                                    {"models", "<game folder>", "read every .mod model under a folder", cmdModels},
                                    {"sounds", "<game folder>", "decode every WAV in Assets/Sound's zips", cmdSounds},
                                    {"table", "<file>", "show a tab-separated table or a string table", cmdTable},
                                });

} // namespace
