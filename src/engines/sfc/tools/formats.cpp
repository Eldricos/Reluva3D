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
#include "engines/sfc/formats/intro_movie.h"
#include "engines/sfc/formats/model.h"
#include "engines/sfc/formats/settings.h"
#include "engines/sfc/formats/sprite_archive.h"
#include "engines/sfc/formats/text_tables.h"
#include "image/image.h"
#include "video/bink.h"

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
    // The objects by kind: how many, how many named, one name as an example.
    std::map<uint16_t, std::pair<size_t, size_t>> kinds;
    std::map<uint16_t, std::string> example;
    for (const auto &o : archive.objects()) {
        auto &[count, withName] = kinds[o.kind];
        ++count;
        withName += o.name.empty() ? 0 : 1;
        if (example[o.kind].empty())
            example[o.kind] = o.name;
    }
    for (const auto &[kind, counts] : kinds)
        std::printf("objects of kind %2u: %5zu, %5zu named (%s)\n", kind, counts.first, counts.second,
                    example[kind].c_str());
    std::printf("%zu objects\n", archive.objects().size());
    // Their fields: groups' children, buttons' and pictures' images by
    // list, and how many records have a class named.
    size_t groups = 0;
    size_t children = 0;
    size_t withFrames = 0;
    size_t frames[3] = {};
    size_t classed = 0;
    for (const auto &o : archive.objects()) {
        groups += o.children.empty() ? 0 : 1;
        children += o.children.size();
        withFrames += o.frames[0].empty() && o.frames[1].empty() && o.frames[2].empty() ? 0 : 1;
        for (int i = 0; i < 3; ++i)
            frames[i] += o.frames[i].size();
        classed += o.className.empty() ? 0 : 1;
    }
    std::printf("%zu groups with %zu children; %zu buttons and pictures with frames (%zu states, %zu pressed, %zu "
                "disabled); %zu with a class\n",
                groups, children, withFrames, frames[0], frames[1], frames[2], classed);
    return failed ? 1 : 0;
}

// An object of sprites.q3 drawn as the interface draws it at rest (a
// screen, a panel, a button), as a PNG.
int cmdSpriteScreen(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool sfc sprite-screen <game folder> <name or number> <out.png>\n");
        return 2;
    }
    sfc::SpriteArchive archive;
    if (int rc = openSprites(argv[2], archive))
        return rc;
    const std::string want = argv[3];
    uint32_t id = 0;
    for (const auto &o : archive.objects())
        if (o.name == want && o.kind != sfc::SpriteArchive::kKindImage) {
            id = o.id;
            break;
        }
    if (!id && !want.empty() && std::all_of(want.begin(), want.end(), [](char c) { return c >= '0' && c <= '9'; }))
        id = static_cast<uint32_t>(std::strtoul(want.c_str(), nullptr, 10));
    if (!archive.object(id)) {
        std::fprintf(stderr, "no object named or numbered %s\n", want.c_str());
        return 1;
    }
    // The screens are laid out from their top left at 640x480 and up; 1600x1200 holds the largest.
    Surface s;
    s.create(1600, 1200, PixelFormat::kRGBA32);
    if (Error err = archive.compose(id, s, 0, 0)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    // Cut to what was drawn.
    int right = 0;
    int bottom = 0;
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x)
            if (s.row(y)[4 * x + 3]) {
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
    image::Image img;
    img.w = std::max(right, 1);
    img.h = std::max(bottom, 1);
    img.rgba.resize(size_t(img.w) * img.h * 4);
    for (int y = 0; y < img.h; ++y)
        std::copy(s.row(y), s.row(y) + 4 * img.w, img.rgba.begin() + 4 * size_t(y) * img.w);
    if (!writeFile(argv[4], image::encodePng(img))) {
        std::fprintf(stderr, "can't write %s\n", argv[4]);
        return 1;
    }
    return 0;
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

// Every settings file under a folder (.gf, and the campaigns' .mct), read
// whole: its sections and keys.
int cmdSettings(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc settings <game folder>\n");
        return 2;
    }
    auto files = filesWith(argv[2], ".gf");
    for (const auto &path : filesWith(argv[2], ".mct"))
        files.push_back(path);
    size_t failed = 0;
    size_t keys = 0;
    for (const auto &path : files) {
        std::vector<uint8_t> data;
        sfc::SettingsFile file;
        Error err = readFile(path, data) ? file.read(data) : Error::fail("can't read it");
        failed += err ? 1 : 0;
        size_t count = 0;
        for (const auto &s : file.sections)
            count += s.entries.size();
        keys += count;
        std::printf("%-32s %3zu sections %4zu keys%s\n", path.filename().string().c_str(), file.sections.size() - 1,
                    count, err ? ("  ERROR " + err.message).c_str() : "");
    }
    std::printf("%zu settings files, %zu keys, %zu failed\n", files.size(), keys, failed);
    return failed ? 1 : 0;
}

// Every campaign map (.mvm) under a folder: its size, and its hexes' bases
// and planets by kind.
int cmdMaps(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc maps <game folder>\n");
        return 2;
    }
    const auto files = filesWith(argv[2], ".mvm");
    size_t failed = 0;
    for (const auto &path : files) {
        std::vector<uint8_t> data;
        sfc::MetaverseMap map;
        Error err = readFile(path, data) ? map.read(data) : Error::fail("can't read it");
        failed += err ? 1 : 0;
        const auto count = [&map](const std::vector<std::string> &names, int32_t sfc::MetaverseMap::Cell::*field) {
            std::map<int32_t, size_t> n;
            for (const auto &c : map.cells)
                ++n[c.*field];
            std::string out;
            for (const auto &[i, k] : n)
                if (i != 0)
                    out += " " + std::to_string(k) + " " +
                           (size_t(i) < names.size() ? names[size_t(i)] : "#" + std::to_string(i)) + ",";
            if (!out.empty())
                out.pop_back();
            return out;
        };
        std::printf("%-16s %3ux%-3u bases:%s; planets:%s%s\n", path.filename().string().c_str(), map.width, map.height,
                    count(map.bases, &sfc::MetaverseMap::Cell::base).c_str(),
                    count(map.planets, &sfc::MetaverseMap::Cell::planet).c_str(),
                    err ? ("  ERROR " + err.message).c_str() : "");
    }
    std::printf("%zu maps, %zu failed\n", files.size(), failed);
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

// The opening movie, out of SFC2Intro.exe: its header, every frame and
// its sound decoded; with -frame, that frame as a PNG.
int cmdIntro(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sfc intro <game folder> [-frame n out.png]...\n");
        return 2;
    }
    std::map<uint32_t, std::string> pngs;
    for (int a = 3; a + 2 < argc; a += 3)
        if (std::string(argv[a]) == "-frame")
            pngs[uint32_t(std::strtoul(argv[a + 1], nullptr, 0))] = argv[a + 2];
    const auto path = fs::findFile(argv[2], "SFC2Intro.exe");
    std::vector<uint8_t> program;
    std::vector<uint8_t> data;
    if (!path || !readFile(*path, program)) {
        std::fprintf(stderr, "no SFC2Intro.exe in %s\n", argv[2]);
        return 1;
    }
    if (Error err = sfc::introMovie(program, data)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const size_t bytes = data.size();
    bink::Movie movie;
    if (Error err = movie.open(std::move(data))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("Bink revision %c, %d x %d, %u frames at %u/%u a second, %zu bytes; audio %d Hz, %d channels\n",
                movie.revision(), movie.width(), movie.height(), movie.frameCount(), movie.rateNumerator(),
                movie.rateDenominator(), bytes, movie.audioRate(), movie.audioChannels());
    std::vector<int16_t> sound;
    if (Error err = movie.decodeAllAudio(sound))
        std::printf("audio: %s\n", err.message.c_str());
    else if (movie.audioChannels() > 0)
        std::printf("audio: %.2f s\n", double(sound.size()) / movie.audioChannels() / movie.audioRate());
    std::vector<uint8_t> rgba;
    uint32_t decoded = 0;
    for (bool more = true;;) {
        if (Error err = movie.nextFrame(more)) {
            std::printf("frame %u: %s\n", decoded, err.message.c_str());
            return 1;
        }
        if (!more)
            break;
        if (auto it = pngs.find(decoded); it != pngs.end()) {
            movie.toRgba(rgba);
            image::Image img;
            img.w = movie.width();
            img.h = movie.height();
            img.rgba = rgba;
            writeFile(it->second, image::encodePng(img));
        }
        ++decoded;
    }
    std::printf("%u frames decoded\n", decoded);
    return decoded == movie.frameCount() ? 0 : 1;
}

const Commands kCommands(
    "sfc",
    {
        {"sprites", "<game folder> [-q]", "list sprites.q3's images and decode each (-q: only the failures)",
         cmdSprites},
        {"sprite-png", "<game folder> <index> <out.png>", "write one of sprites.q3's images as a PNG", cmdSpritePng},
        {"sprite-screen", "<game folder> <name or number> <out.png>",
         "draw one of sprites.q3's objects (a screen, a button) as a PNG", cmdSpriteScreen},
        {"fonts", "<game folder>", "read every .bft font under a folder", cmdFonts},
        {"models", "<game folder>", "read every .mod model under a folder", cmdModels},
        {"settings", "<game folder>", "read every settings file (.gf, .mct) under a folder", cmdSettings},
        {"maps", "<game folder>", "read every campaign map (.mvm) under a folder", cmdMaps},
        {"sounds", "<game folder>", "decode every WAV in Assets/Sound's zips", cmdSounds},
        {"table", "<file>", "show a tab-separated table or a string table", cmdTable},
        {"intro", "<game folder> [-frame n out.png]...", "decode the opening movie in SFC2Intro.exe", cmdIntro},
    });

} // namespace
