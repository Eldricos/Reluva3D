// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Need for Speed engine's reluva-tool commands for the game's files:
// archives, pictures, text, fonts, models, tracks, sounds, music, movies;
// and a survey of the folders holding them.

#include "backends/networking/sockets/socket_net.h"
#include "audio/wav.h"
#include "common/events.h"
#include "common/fs.h"
#include "image/image.h"
#include "emulation/x86/pokes.h"
#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/formats/col.h"
#include "engines/nfs/formats/fce.h"
#include "engines/nfs/formats/ffn.h"
#include "engines/nfs/formats/frd.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/host/game_host.h"
#include "engines/nfs/formats/hrz.h"
#include "engines/nfs/formats/mad.h"
#include "engines/nfs/formats/mus.h"
#include "engines/nfs/pages/play_page.h"
#include "engines/nfs/formats/refpack.h"
#include "engines/nfs/formats/text_table.h"
#include "engines/nfs/pages/viewers.h"
#include "engines/nfs/formats/viv.h"
#include "devtools/tool.h"
#include "engines/nfs/tools/helpers.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using reluva::Error;
using reluva::nfs::ShapeBank;
using reluva::nfs::VivArchive;
using namespace reluva::nfs::tools;
namespace stdfs = std::filesystem;

int cmdVivList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs viv-list <file.viv>\n");
        return 2;
    }
    VivArchive viv;
    if (Error err = viv.open(argv[2]))
        return fail(err);
    for (const auto &e : viv.entries())
        std::printf("%9u %9u  %s\n", e.offset, e.size, e.name.c_str());
    std::printf("%zu files\n", viv.entries().size());
    return 0;
}

int cmdVivExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool nfs viv-extract <file.viv> <output dir>\n");
        return 2;
    }
    VivArchive viv;
    if (Error err = viv.open(argv[2]))
        return fail(err);
    std::vector<uint8_t> data;
    for (const auto &e : viv.entries()) {
        const stdfs::path rel = safeName(e.name);
        if (rel.empty()) {
            std::fprintf(stderr, "skipping unsafe name %s\n", e.name.c_str());
            continue;
        }
        if (Error err = viv.read(e.name, data))
            return fail(err);
        if (!writeFile(stdfs::path(argv[3]) / rel, data)) {
            std::fprintf(stderr, "can't write %s\n", (stdfs::path(argv[3]) / rel).string().c_str());
            return 1;
        }
    }
    std::printf("%zu files written to %s\n", viv.entries().size(), argv[3]);
    return 0;
}

int cmdFshInfo(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs fsh-info <file.fsh | file.qfs | file.viv name>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (Error err = load(argv[2], argc > 3 ? argv[3] : nullptr, data))
        return fail(err);
    ShapeBank bank;
    if (Error err = reluva::nfs::parseShapeBank(std::move(data), bank))
        return fail(err);
    std::printf("%s, %zu shapes\n", bank.id.c_str(), bank.shapes.size());
    for (const auto &s : bank.shapes) {
        std::printf("  %-4s %-15s %4dx%-4d centre %d,%d pos %d,%d", s.name.c_str(), reluva::nfs::shapeTypeName(s.type),
                    s.width, s.height, s.centerX, s.centerY, s.x, s.y);
        if (!s.palette.empty())
            std::printf(" palette %zu", s.palette.size() / 4);
        for (const auto &t : s.texts)
            std::printf(" \"%s\"", t.c_str());
        for (const auto &b : s.unknown)
            std::printf(" [block 0x%02x, %zu bytes]", b.type, b.data.size());
        std::printf("\n");
    }
    return 0;
}

int cmdFshDump(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool nfs fsh-dump <file.fsh | file.qfs | file.viv name> <output dir>\n");
        return 2;
    }
    const bool inViv = argc > 4;
    std::vector<uint8_t> data;
    if (Error err = load(argv[2], inViv ? argv[3] : nullptr, data))
        return fail(err);
    ShapeBank bank;
    if (Error err = reluva::nfs::parseShapeBank(std::move(data), bank))
        return fail(err);
    const stdfs::path outDir = argv[inViv ? 4 : 3];
    int written = 0;
    for (size_t i = 0; i < bank.shapes.size(); ++i) {
        const auto &s = bank.shapes[i];
        reluva::image::Image img;
        if (Error err = reluva::nfs::decodeShape(s, img)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            continue;
        }
        // Shape names may repeat or hold characters files can't, so they're
        // numbered too.
        std::string name = std::to_string(i) + "_";
        for (char c : s.name)
            name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        if (!writeFile(outDir / (name + ".tga"), reluva::image::encodeTga(img))) {
            std::fprintf(stderr, "can't write into %s\n", outDir.string().c_str());
            return 1;
        }
        ++written;
    }
    std::printf("%d pictures written to %s\n", written, outDir.string().c_str());
    return 0;
}

int cmdTextDump(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs text-dump <text.eng>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Can't read %s\n", argv[2]);
        return 1;
    }
    reluva::nfs::TextTable table;
    if (Error err = reluva::nfs::parseTextTable(data, table))
        return fail(err);
    for (size_t i = 0; i < table.strings.size(); ++i)
        std::printf("%5zu  %s\n", i, reluva::nfs::cp1252ToUtf8(table.strings[i]).c_str());
    return 0;
}

// Draws the font's every character (or the text given) on a dark
// background, for checking its glyphs and spacing.
int cmdFfnRender(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool nfs ffn-render <file.ffn> <out.tga> [text]\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Can't read %s\n", argv[2]);
        return 1;
    }
    reluva::nfs::Font font;
    if (Error err = reluva::nfs::parseFont(data, font))
        return fail(err);
    std::vector<std::string> lines;
    if (argc > 4) {
        lines.push_back(argv[4]); // as given: Windows-1252 bytes
    } else {
        for (int row = 32; row < 256; row += 32) {
            std::string line;
            for (int c = row; c < row + 32; ++c)
                if (font.has(uint8_t(c)))
                    line += char(c);
            lines.push_back(line);
        }
    }
    int width = 0;
    for (const auto &l : lines)
        width = std::max(width, font.measure(l));
    reluva::Surface surface;
    surface.create(width + 16, font.lineStep() * int(lines.size()) + 16, reluva::PixelFormat::RGBA32);
    for (size_t i = 0; i < surface.pixels.size(); i += 4) {
        surface.pixels[i] = surface.pixels[i + 1] = 40;
        surface.pixels[i + 2] = 60;
        surface.pixels[i + 3] = 255;
    }
    for (size_t i = 0; i < lines.size(); ++i)
        font.draw(surface, 8, 8 + font.lineStep() * int(i), lines[i], {255, 255, 255});
    reluva::image::Image img;
    img.w = surface.w;
    img.h = surface.h;
    img.rgba = surface.pixels;
    if (!writeFile(argv[3], reluva::image::encodeTga(img))) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    std::printf("line height %d, gap %d, pen %d,%d, flags %d, atlas %dx%d\n", font.lineHeight, font.lineGap, font.penX,
                font.penY, font.flags, font.atlasWidth, font.atlasHeight);
    return 0;
}

// Opens every file of the kinds Reluva reads under the folders given,
// .viv archives' contents included, and reports any that fail.
// With -hash, each kind's decoded contents as a checksum (FNV-1a, 64
// bits), files in the order of their paths: what changes when a reader
// does, with nothing of the game's own in it.
struct SurveyCount {
    int ok = 0, failed = 0;
    uint64_t hash = 0xcbf29ce484222325ull;
    void add(const void *data, size_t size) {
        const auto *b = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ b[i]) * 0x100000001b3ull;
    }
    template<typename T>
    void add(const std::vector<T> &v) {
        add(v.data(), v.size() * sizeof(T));
    }
    void add(int32_t v) { add(&v, 4); }
};

int cmdNfsSurvey(int argc, char **argv) {
    std::vector<stdfs::path> roots;
    bool verbose = false;
    bool hashes = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else if (std::strcmp(argv[i], "-hash") == 0)
            hashes = true;
        else
            roots.push_back(argv[i]);
    }
    if (roots.empty()) {
        std::fprintf(stderr, "usage: reluva-tool nfs survey <folder>... [-v] [-hash]\n");
        return 2;
    }

    using Count = SurveyCount;
    Count vivs;
    Count banks;
    Count pictures;
    Count tables;
    Count fonts;
    Count models;
    Count tracks;
    Count collisions;
    Count horizons;
    Count soundBanks;
    Count sounds;
    Count streams;
    Count segments;
    Count maps;
    Count movies;
    Count frames;
    Count movieAudio;
    std::vector<std::string> unsupported; // in EA's other compression schemes
    auto lower = [](std::string s) {
        for (char &c : s)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto report = [&](Count &count, const std::string &what, const Error &err) {
        if (err) {
            ++count.failed;
            std::printf("FAILED %s: %s\n", what.c_str(), err.message.c_str());
        } else {
            ++count.ok;
            if (verbose)
                std::printf("ok %s\n", what.c_str());
        }
    };
    // One file, by its extension; `what` names it in messages.
    auto check = [&](const std::string &name, std::vector<uint8_t> data, const std::string &what) {
        const std::string ext = lower(stdfs::path(name).extension().string());
        const std::string stem = lower(stdfs::path(name).stem().string());
        if (ext == ".fsh" || ext == ".qfs") {
            if (reluva::nfs::isOtherEaCompression(data)) {
                unsupported.push_back(what);
                return;
            }
            ShapeBank bank;
            const Error err = reluva::nfs::parseShapeBank(std::move(data), bank);
            report(banks, what, err);
            for (const auto &shape : bank.shapes) {
                reluva::image::Image img;
                const Error e = reluva::nfs::decodeShape(shape, img);
                report(pictures, what + ":" + shape.name, e);
                if (!e) {
                    pictures.add(img.w), pictures.add(img.h);
                    pictures.add(img.rgba);
                }
            }
        } else if (ext == ".ffn") {
            reluva::nfs::Font font;
            report(fonts, what, reluva::nfs::parseFont(data, font));
            fonts.add(data);
        } else if (ext == ".fce") {
            reluva::nfs::Model model;
            const Error e = reluva::nfs::parseModel(data, model);
            report(models, what, e);
            if (!e)
                for (const auto &v : model.vertices)
                    models.add(&v, sizeof v);
        } else if (ext == ".frd") {
            reluva::nfs::Track track;
            const Error e = reluva::nfs::parseTrack(data, track);
            report(tracks, what, e);
            if (!e)
                for (const auto &b : track.blocks) {
                    tracks.add(b.vertices);
                    tracks.add(b.colours);
                    tracks.add(int32_t(b.polygons.size()));
                }
        } else if (ext == ".col") {
            reluva::nfs::TrackCollision collision;
            const Error e = reluva::nfs::parseCollision(data, collision);
            report(collisions, what, e);
            if (!e)
                for (const auto &r : collision.road)
                    collisions.add(&r.position, sizeof r.position);
        } else if (ext == ".bnk") {
            reluva::nfs::SoundBank bank;
            const Error err = reluva::nfs::parseSoundBank(data, bank);
            report(soundBanks, what, err);
            for (size_t i = 0; i < bank.slots.size(); ++i)
                if (bank.slots[i]) {
                    reluva::audio::Sound sound;
                    const Error e = reluva::nfs::decodeBankSound(data, *bank.slots[i], sound);
                    report(sounds, what + ":" + std::to_string(i), e);
                    if (!e)
                        sounds.add(sound.samples);
                }
        } else if (ext == ".mus") {
            reluva::nfs::MusicStream stream;
            const Error err = stream.open(std::move(data));
            report(streams, what, err);
            for (size_t i = 0; i < stream.segments().size(); ++i) {
                reluva::audio::Sound sound;
                const Error e = stream.decode(i, sound);
                report(segments, what + ":" + std::to_string(i), e);
                if (!e)
                    segments.add(sound.samples);
            }
        } else if (ext == ".map" || ext == ".lin") {
            // Checked against the .mus beside it, whose segments it names.
            reluva::nfs::MusicMap map;
            Error err = reluva::nfs::parseMusicMap(data, map);
            std::vector<uint8_t> mus;
            const stdfs::path musPath = stdfs::path(what).replace_extension(".mus");
            reluva::nfs::MusicStream stream;
            if (!err && readFile(musPath, mus) && !stream.open(std::move(mus))) {
                bool match = map.offsets.size() == stream.segments().size();
                for (size_t i = 0; match && i < map.offsets.size(); ++i)
                    match = map.offsets[i] == stream.segments()[i].offset;
                if (!match)
                    err = Error::fail("its offsets don't match " + musPath.string() + "'s segments");
            }
            report(maps, what, err);
        } else if (ext == ".mad") {
            // Every frame decoded, and the audio.
            reluva::nfs::MadMovie movie;
            const Error err = movie.open(std::move(data));
            report(movies, what, err);
            reluva::nfs::MadDecoder decoder;
            for (size_t i = 0; !err && i < movie.frames().size(); ++i) {
                const Error e = movie.decodeFrame(decoder, i);
                report(frames, what + ":" + std::to_string(i), e);
                if (!e && hashes)
                    for (int plane = 0; plane < 3; ++plane) {
                        const int h = plane ? decoder.height() / 2 : decoder.height();
                        frames.add(decoder.plane(plane), size_t(decoder.stride(plane)) * size_t(h));
                    }
            }
            if (!err && movie.hasAudio()) {
                reluva::audio::Sound sound;
                const Error e = movie.decodeAudio(sound);
                report(movieAudio, what, e);
                if (!e)
                    movieAudio.add(sound.samples);
            }
        } else if (ext == ".hrz") {
            reluva::nfs::Horizon horizon;
            report(horizons, what, reluva::nfs::parseHorizon(std::string(data.begin(), data.end()), horizon));
        } else if (stem == "text") {
            reluva::nfs::TextTable table;
            const Error e = reluva::nfs::parseTextTable(data, table);
            report(tables, what, e);
            if (!e)
                for (const auto &t : table.strings)
                    tables.add(t.c_str(), t.size() + 1);
        }
    };

    for (const auto &root : roots) {
        std::error_code ec;
        std::vector<stdfs::path> paths; // in order, for the checksums
        for (const auto &entry : stdfs::recursive_directory_iterator(root, ec))
            if (entry.is_regular_file(ec))
                paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end(),
                  [](const stdfs::path &a, const stdfs::path &b) { return a.generic_string() < b.generic_string(); });
        for (const stdfs::path &path : paths) {
            const std::string ext = lower(path.extension().string());
            if (ext == ".viv") {
                VivArchive viv;
                const Error err = viv.open(path);
                report(vivs, path.string(), err);
                if (err)
                    continue;
                std::vector<uint8_t> data;
                for (const auto &e : viv.entries())
                    if (!viv.read(e.name, data)) {
                        vivs.add(data);
                        check(e.name, std::move(data), path.string() + ":" + e.name);
                    }
            } else if (ext == ".fsh" || ext == ".qfs" || ext == ".ffn" || ext == ".fce" || ext == ".frd" ||
                       ext == ".col" || ext == ".hrz" || ext == ".bnk" || ext == ".mus" || ext == ".map" ||
                       ext == ".lin" || ext == ".mad" || lower(path.stem().string()) == "text") {
                std::vector<uint8_t> data;
                if (!readFile(path, data)) {
                    report(banks, path.string(), Error::fail("can't read it"));
                    continue;
                }
                check(path.filename().string(), std::move(data), path.string());
            }
        }
    }

    const Count *all[] = {&vivs,       &banks,  &pictures, &tables,   &fonts, &models, &tracks, &collisions, &horizons,
                          &soundBanks, &sounds, &streams,  &segments, &maps,  &movies, &frames, &movieAudio};
    const char *names[] = {".viv archives", ".fsh/.qfs banks", "pictures",       "text tables",    "fonts",
                           ".fce models",   ".frd tracks",     ".col files",     ".hrz files",     ".bnk banks",
                           "sounds",        ".mus streams",    "music segments", ".map/.lin maps", ".mad movies",
                           "movie frames",  "movie audio"};
    int failed = 0;
    for (int i = 0; i < 17; ++i) {
        if (hashes)
            std::printf("%-16s %6d read, %d failed, checksum %016llx\n", names[i], all[i]->ok, all[i]->failed,
                        (unsigned long long)all[i]->hash);
        else
            std::printf("%-16s %6d read, %d failed\n", names[i], all[i]->ok, all[i]->failed);
        failed += all[i]->failed;
    }
    // Known and documented (docs/engines/nfs/games/nfs3/formats/refpack.md), so not failures.
    if (!unsupported.empty()) {
        std::printf("%zu files in a compression scheme not read yet:\n", unsupported.size());
        for (const auto &u : unsupported)
            std::printf("  %s\n", u.c_str());
    }
    return failed ? 1 : 0;
}

int cmdFceInfo(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs fce-info <file.fce | file.viv name>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (Error err = load(argv[2], argc > 3 ? argv[3] : nullptr, data))
        return fail(err);
    reluva::nfs::Model model;
    if (Error err = reluva::nfs::parseModel(data, model))
        return fail(err);
    std::printf("%zu vertices, %zu triangles, half size %.3f %.3f %.3f\n", model.vertices.size(),
                model.triangles.size(), model.halfSize[0], model.halfSize[1], model.halfSize[2]);
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const auto &p = model.parts[i];
        std::printf("  part %2zu %-26s at %6.2f %6.2f %6.2f  %4u vertices %4u triangles\n", i, p.name.c_str(),
                    p.position[0], p.position[1], p.position[2], p.vertexCount, p.triangleCount);
    }
    for (const auto &dm : model.dummies)
        std::printf("  dummy %-8s at %6.2f %6.2f %6.2f\n", dm.name.c_str(), dm.position[0], dm.position[1],
                    dm.position[2]);
    for (size_t i = 0; i < model.primaryColours.size(); ++i) {
        const auto &a = model.primaryColours[i].hsbx;
        std::printf("  colour %zu: %u %u %u %u", i, a[0], a[1], a[2], a[3]);
        if (i < model.secondaryColours.size()) {
            const auto &b = model.secondaryColours[i].hsbx;
            std::printf(", second %u %u %u %u", b[0], b[1], b[2], b[3]);
        }
        std::printf("\n");
    }
    std::map<uint32_t, int> pages;
    std::map<uint32_t, int> flags;
    for (const auto &t : model.triangles) {
        ++pages[t.texturePage];
        ++flags[t.flags];
    }
    std::printf("  triangles by texture page:");
    for (const auto &[k, n] : pages)
        std::printf(" %u:%d", k, n);
    std::printf("\n  triangles by flags:");
    for (const auto &[k, n] : flags)
        std::printf(" 0x%x:%d", k, n);
    std::printf("\n");
    return 0;
}

// Writes a model as Wavefront OBJ, one group per part, placed and turned
// right-handed (z mirrored) as Reluva draws it.
int cmdFceObj(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool nfs fce-obj <file.fce | file.viv name> <out.obj>\n");
        return 2;
    }
    const bool inViv = argc > 4;
    std::vector<uint8_t> data;
    if (Error err = load(argv[2], inViv ? argv[3] : nullptr, data))
        return fail(err);
    reluva::nfs::Model model;
    if (Error err = reluva::nfs::parseModel(data, model))
        return fail(err);
    std::ofstream out(argv[inViv ? 4 : 3]);
    out << "# " << argv[2] << (inViv ? std::string(" ") + argv[3] : "") << "\n";
    for (const auto &p : model.parts)
        for (uint32_t i = p.firstVertex; i < p.firstVertex + p.vertexCount; ++i) {
            const auto &v = model.vertices[i];
            out << "v " << v[0] + p.position[0] << ' ' << v[1] + p.position[1] << ' ' << -(v[2] + p.position[2])
                << "\n";
        }
    for (const auto &t : model.triangles)
        for (int k = 0; k < 3; ++k)
            out << "vt " << t.u[k] << ' ' << 1 - t.v[k] << "\n";
    size_t uv = 1;
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const auto &p = model.parts[i];
        out << "g part" << i << (p.name.empty() ? "" : "_") << safeObjName(p.name) << "\n";
        for (uint32_t t = p.firstTriangle; t < p.firstTriangle + p.triangleCount; ++t, uv += 3) {
            const auto &tri = model.triangles[t];
            out << "f";
            for (int k = 0; k < 3; ++k)
                out << ' ' << tri.vertices[k] + 1 << '/' << uv + k;
            out << "\n";
        }
    }
    if (!out) {
        std::fprintf(stderr, "can't write %s\n", argv[inViv ? 4 : 3]);
        return 1;
    }
    return 0;
}

int cmdFrdCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs frd-check <file.frd>...\n");
        return 2;
    }
    int failed = 0;
    for (int i = 2; i < argc; ++i) {
        std::vector<uint8_t> data;
        reluva::nfs::Track track;
        Error err = readFile(argv[i], data) ? reluva::nfs::parseTrack(data, track) : Error::fail("can't read it");
        if (err) {
            std::printf("%s: FAILED: %s\n", argv[i], err.message.c_str());
            ++failed;
            continue;
        }
        size_t vertices = 0;
        size_t road = 0;
        size_t lanes = 0;
        size_t objects = 0;
        size_t extras = 0;
        size_t moving = 0;
        size_t lights = 0;
        size_t sounds = 0;
        for (const auto &b : track.blocks) {
            vertices += b.vertices.size();
            road += b.polygons[4].size();
            lanes += b.polygons[6].size();
            for (const auto &layer : b.objectLayers)
                for (const auto &o : layer)
                    objects += o.polygons.size();
            lights += b.lights.size() / 16;
            sounds += b.sounds.size() / 16;
        }
        for (const auto &g : track.extraObjects)
            for (const auto &o : g) {
                ++extras;
                moving += o.type == 3;
            }
        std::printf("%s: %zu blocks, %zu vertices, %zu road polygons (high detail), %zu lane polygons, %zu object "
                    "polygons, %zu placed objects (%zu moving), %zu textures, %zu lights, %zu sounds\n",
                    argv[i], track.blocks.size(), vertices, road, lanes, objects, extras, moving, track.textures.size(),
                    lights, sounds);
    }
    return failed ? 1 : 0;
}

// Writes a track's road and objects as Wavefront OBJ, mirrored as Reluva
// draws it, for checking its shape in other programs.
int cmdFrdObj(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool nfs frd-obj <file.frd> <out.obj> [detail 0-2]\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Can't read %s\n", argv[2]);
        return 1;
    }
    reluva::nfs::Track track;
    if (Error err = reluva::nfs::parseTrack(data, track))
        return fail(err);
    const int detail = argc > 4 ? std::clamp(std::atoi(argv[4]), 0, 2) : 2;
    std::ofstream out(argv[3]);
    size_t base = 1;
    size_t uv = 1;
    // Each polygon's corners in the order the game queues them (frd.md,
    // Textures: a block's as stored, a placed object's 1, 0, 3, 2), corner
    // k with the texture's u, v k; written last to first, since with z
    // mirrored the game's front faces turn the other way (as the Tracks
    // page winds them), so OBJ's counter-clockwise front is the game's.
    auto emit = [&](const std::vector<reluva::nfs::Track::Vec3> &verts, const reluva::nfs::Track::Vec3 &offset,
                    const std::vector<reluva::nfs::Track::Polygon> &polys, const char *group, bool placed) {
        out << "g " << group << "\n";
        for (const auto &v : verts)
            out << "v " << v[0] + offset[0] << ' ' << v[1] + offset[1] << ' ' << -(v[2] + offset[2]) << "\n";
        for (const auto &p : polys) {
            const auto &t = track.textures[p.texture];
            for (int k = 0; k < 4; ++k)
                out << "vt " << t.corners[2 * k] << ' ' << 1 - t.corners[2 * k + 1] << "\n";
            out << "f";
            for (int k = 3; k >= 0; --k)
                out << ' ' << base + p.vertices[size_t(reluva::nfs::polygonSlot(k, placed))] << '/' << uv + k;
            out << "\n";
            uv += 4;
        }
        base += verts.size();
    };
    for (size_t i = 0; i < track.blocks.size(); ++i) {
        const auto &b = track.blocks[i];
        std::vector<reluva::nfs::Track::Polygon> polys = b.polygons[2 * detail];
        for (const auto &layer : b.objectLayers)
            for (const auto &o : layer)
                polys.insert(polys.end(), o.polygons.begin(), o.polygons.end());
        emit(b.vertices, {0, 0, 0}, polys, ("block" + std::to_string(i)).c_str(), false);
    }
    for (const auto &g : track.extraObjects)
        for (const auto &o : g)
            if (o.type == 4)
                emit(o.vertices, o.reference, o.polygons, "object", true);
    if (!out) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    return 0;
}

// A sound as a 16-bit PCM WAV file.
std::vector<uint8_t> wavFile(const reluva::audio::Sound &s) {
    std::vector<uint8_t> out;
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.push_back(uint8_t(v >> (8 * i)));
    };
    auto u16 = [&](unsigned v) {
        out.push_back(uint8_t(v));
        out.push_back(uint8_t(v >> 8));
    };
    const uint32_t bytes = uint32_t(s.samples.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    u32(36 + bytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    u32(16);
    u16(1);
    u16(unsigned(s.channels));
    u32(uint32_t(s.rate));
    u32(uint32_t(s.rate * s.channels * 2));
    u16(unsigned(s.channels * 2));
    u16(16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    u32(bytes);
    for (int16_t v : s.samples)
        u16(uint16_t(v));
    return out;
}

const char *compressionName(int c) {
    switch (c) {
    case reluva::nfs::SoundHeader::kPcm16:
        return "PCM";
    case reluva::nfs::SoundHeader::kEaXa:
        return "EA-XA";
    case reluva::nfs::SoundHeader::kMicroTalk:
        return "MicroTalk";
    default:
        return "?";
    }
}

int cmdBnkInfo(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs bnk-info <file.bnk | file.viv name>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (Error err = load(argv[2], argc > 3 ? argv[3] : nullptr, data))
        return fail(err);
    reluva::nfs::SoundBank bank;
    if (Error err = reluva::nfs::parseSoundBank(data, bank))
        return fail(err);
    std::printf("version %d, %zu slots\n", bank.version, bank.slots.size());
    for (size_t i = 0; i < bank.slots.size(); ++i) {
        if (!bank.slots[i])
            continue;
        const auto &h = *bank.slots[i];
        std::printf("  %3zu %-9s %d ch %5d Hz %8u samples (%.2f s) at %u", i, compressionName(h.compression),
                    h.channels, h.rate, h.samples, double(h.samples) / h.rate, h.dataOffset);
        if (h.loopEnd)
            std::printf(" loop %u-%u", h.loopStart, h.loopEnd);
        std::printf("  tags");
        for (const auto &[tag, value] : h.tags)
            if (tag < 0x82 || tag > 0x88)
                std::printf(" %02x=%u", tag, value);
        std::printf("\n");
    }
    return 0;
}

int cmdBnkWav(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool nfs bnk-wav <file.bnk | file.viv name> <output dir>\n");
        return 2;
    }
    const bool inViv = argc > 4;
    std::vector<uint8_t> data;
    if (Error err = load(argv[2], inViv ? argv[3] : nullptr, data))
        return fail(err);
    reluva::nfs::SoundBank bank;
    if (Error err = reluva::nfs::parseSoundBank(data, bank))
        return fail(err);
    const stdfs::path outDir = argv[inViv ? 4 : 3];
    int written = 0;
    int failed = 0;
    for (size_t i = 0; i < bank.slots.size(); ++i) {
        if (!bank.slots[i])
            continue;
        reluva::audio::Sound sound;
        if (Error err = reluva::nfs::decodeBankSound(data, *bank.slots[i], sound)) {
            std::fprintf(stderr, "sound %zu: %s\n", i, err.message.c_str());
            ++failed;
            continue;
        }
        char name[32];
        std::snprintf(name, sizeof name, "%03zu.wav", i);
        if (!writeFile(outDir / name, wavFile(sound))) {
            std::fprintf(stderr, "can't write into %s\n", outDir.string().c_str());
            return 1;
        }
        ++written;
    }
    std::printf("%d sounds written to %s\n", written, outDir.string().c_str());
    return failed ? 1 : 0;
}

int cmdMusInfo(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs mus-info <file.mus> [file.map | file.lin]\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Can't read %s\n", argv[2]);
        return 1;
    }
    reluva::nfs::MusicStream stream;
    if (Error err = stream.open(std::move(data)))
        return fail(err);
    reluva::nfs::MusicMap map;
    const bool haveMap = argc > 3;
    if (haveMap) {
        std::vector<uint8_t> m;
        if (!readFile(argv[3], m)) {
            std::fprintf(stderr, "Can't read %s\n", argv[3]);
            return 1;
        }
        if (Error err = reluva::nfs::parseMusicMap(m, map))
            return fail(err);
    }
    std::printf("%zu segments\n", stream.segments().size());
    int mismatched = 0;
    for (size_t i = 0; i < stream.segments().size(); ++i) {
        const auto &s = stream.segments()[i];
        std::printf("  %3zu at %9u  %-5s %d ch %5d Hz %7u samples (%.2f s), %zu blocks", i, s.offset,
                    compressionName(s.header.compression), s.header.channels, s.header.rate, s.header.samples,
                    double(s.header.samples) / s.header.rate, s.blocks.size());
        if (haveMap && i < map.sections.size()) {
            const auto &sec = map.sections[i];
            if (map.offsets[i] != s.offset)
                ++mismatched;
            std::printf("  part %d", sec.part);
            for (const auto &r : sec.routes)
                std::printf("  %d-%d: %d", r.low, r.high, r.next);
            if (sec.routes.empty())
                std::printf("  (no route)");
        }
        std::printf("\n");
    }
    if (haveMap) {
        std::printf("map: %zu sections, %zu events; offsets %s the segments'\n", map.sections.size(), map.events.size(),
                    mismatched == 0 && map.sections.size() == stream.segments().size() ? "match" : "DON'T match");
        return mismatched ? 1 : 0;
    }
    return 0;
}

int cmdMusWav(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool nfs mus-wav <file.mus> <segment | all> <output dir>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Can't read %s\n", argv[2]);
        return 1;
    }
    reluva::nfs::MusicStream stream;
    if (Error err = stream.open(std::move(data)))
        return fail(err);
    const bool all = std::strcmp(argv[3], "all") == 0;
    const size_t only = all ? 0 : size_t(std::atoi(argv[3]));
    int written = 0;
    for (size_t i = 0; i < stream.segments().size(); ++i) {
        if (!all && i != only)
            continue;
        reluva::audio::Sound sound;
        if (Error err = stream.decode(i, sound))
            return fail(err);
        char name[32];
        std::snprintf(name, sizeof name, "%03zu.wav", i);
        if (!writeFile(stdfs::path(argv[4]) / name, wavFile(sound))) {
            std::fprintf(stderr, "can't write into %s\n", argv[4]);
            return 1;
        }
        ++written;
    }
    std::printf("%d segments written to %s\n", written, argv[4]);
    return 0;
}

int cmdMadInfo(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs mad-info <file.mad>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Can't read %s\n", argv[2]);
        return 1;
    }
    reluva::nfs::MadMovie movie;
    if (Error err = movie.open(std::move(data)))
        return fail(err);
    std::map<char, int> kinds;
    for (const auto &f : movie.frames())
        ++kinds[f.kind];
    const double ms = movie.frameDuration() / 65536.0;
    std::printf("%dx%d, %zu frames (%d intra, %d inter, %d unreferenced), %.3f ms each (%.3f fps), %.2f s\n",
                movie.width(), movie.height(), movie.frames().size(), kinds['k'], kinds['m'], kinds['e'], ms,
                ms > 0 ? 1000 / ms : 0.0, ms * movie.frames().size() / 1000);
    if (movie.hasAudio()) {
        const auto &h = movie.audioHeader();
        std::printf("audio: %s, %d ch, %d Hz, %u samples (%.2f s)\n", compressionName(h.compression), h.channels,
                    h.rate, h.samples, double(h.samples) / h.rate);
    }
    // Every frame decodes?
    reluva::nfs::MadDecoder decoder;
    for (size_t i = 0; i < movie.frames().size(); ++i)
        if (Error err = movie.decodeFrame(decoder, i)) {
            std::fprintf(stderr, "frame %zu: %s\n", i, err.message.c_str());
            return 1;
        }
    std::printf("all frames decode\n");
    return 0;
}

int cmdMadFrames(int argc, char **argv) {
    // Options last: -game, nfs3.exe's own arithmetic (mad.md, The game's
    // decoder); -display 1-3, shown at twice the size as the game shows it
    // in that mode (The game's display step).
    bool game = false;
    int display = 0;
    for (bool more = true; more;) {
        more = false;
        if (argc > 4 && std::string(argv[argc - 1]) == "-game") {
            game = more = true;
            --argc;
        } else if (argc > 5 && std::string(argv[argc - 2]) == "-display") {
            display = std::clamp(std::atoi(argv[argc - 1]), 1, 3);
            argc -= 2;
            more = true;
        }
    }
    if (argc < 4) {
        std::fprintf(
            stderr,
            "usage: reluva-tool nfs mad-frames <file.mad> <output dir> [first frame] [count] [-game] [-display 1-3]\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Can't read %s\n", argv[2]);
        return 1;
    }
    reluva::nfs::MadMovie movie;
    if (Error err = movie.open(std::move(data)))
        return fail(err);
    const size_t first = argc > 4 ? size_t(std::atoi(argv[4])) : 0;
    const size_t count = argc > 5 ? size_t(std::atoi(argv[5])) : movie.frames().size();
    // Frames refer to those before, so decoding starts at the beginning.
    reluva::nfs::MadDecoder decoder;
    if (game)
        decoder.setArithmetic(reluva::nfs::MadDecoder::Arithmetic::Game);
    int written = 0;
    for (size_t i = 0; i < movie.frames().size() && i < first + count; ++i) {
        if (Error err = movie.decodeFrame(decoder, i))
            return fail(err);
        if (i < first)
            continue;
        reluva::Surface s;
        if (display)
            decoder.toGameSurface(s, static_cast<reluva::nfs::MadDecoder::Display>(display));
        else
            decoder.toSurface(s);
        reluva::image::Image img;
        img.w = s.w;
        img.h = s.h;
        img.rgba = std::move(s.pixels);
        char name[32];
        std::snprintf(name, sizeof name, "%05zu.tga", i);
        if (!writeFile(stdfs::path(argv[3]) / name, reluva::image::encodeTga(img))) {
            std::fprintf(stderr, "can't write into %s\n", argv[3]);
            return 1;
        }
        ++written;
    }
    std::printf("%d frames written to %s\n", written, argv[3]);
    return 0;
}

int cmdMadWav(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool nfs mad-wav <file.mad> <out.wav>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!readFile(argv[2], data)) {
        std::fprintf(stderr, "Can't read %s\n", argv[2]);
        return 1;
    }
    reluva::nfs::MadMovie movie;
    if (Error err = movie.open(std::move(data)))
        return fail(err);
    reluva::audio::Sound sound;
    if (Error err = movie.decodeAudio(sound))
        return fail(err);
    if (!writeFile(argv[3], wavFile(sound))) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    std::printf("%zu frames of audio written to %s\n", sound.frames(), argv[3]);
    return 0;
}

const reluva::tool::Commands kCommands(
    "nfs",
    {
        {"viv-list", "<file.viv>", "list a .viv archive's files", cmdVivList},
        {"viv-extract", "<file.viv> <output dir>", "write a .viv archive's files to a folder", cmdVivExtract},
        {"fsh-info", "<file.fsh | file.qfs | file.viv name>",
         "list a shape bank's pictures (.fsh, .qfs, or one in a .viv)", cmdFshInfo},
        {"fsh-dump", "<file.fsh | file.qfs | file.viv name> <output dir>", "write a shape bank's pictures as TGA files",
         cmdFshDump},
        {"text-dump", "<text.eng>", "print a front-end string table", cmdTextDump},
        {"ffn-render", "<file.ffn> <out.tga> [text]", "draw text in one of the game's fonts, as a TGA", cmdFfnRender},
        {"survey", "<folder>... [-v] [-hash]",
         "what an install or CD holds: each kind of file, read and counted (-hash: each kind's decoded contents as a "
         "checksum)",
         cmdNfsSurvey},
        {"fce-info", "<file.fce | file.viv name>", "a car model's parts, vertices, triangles and colours", cmdFceInfo},
        {"fce-obj", "<file.fce | file.viv name> <out.obj>", "a car model as a Wavefront OBJ", cmdFceObj},
        {"frd-check", "<file.frd>...", "read tracks' .frd files and report what they hold", cmdFrdCheck},
        {"frd-obj", "<file.frd> <out.obj> [detail 0-2]", "a track as a Wavefront OBJ, at a level of detail", cmdFrdObj},
        {"bnk-info", "<file.bnk | file.viv name>", "a sound bank's sounds: format, rate, length, loop", cmdBnkInfo},
        {"bnk-wav", "<file.bnk | file.viv name> <output dir>", "a sound bank's sounds as WAV files", cmdBnkWav},
        {"mus-info", "<file.mus> [file.map | file.lin]", "a music stream's sections, with its map or linear order",
         cmdMusInfo},
        {"mus-wav", "<file.mus> <segment | all> <output dir>", "music sections as WAV files", cmdMusWav},
        {"mad-info", "<file.mad>", "a movie's frames and sound", cmdMadInfo},
        {"mad-frames", "<file.mad> <output dir> [first frame] [count] [-game] [-display 1-3]",
         "a movie's frames as TGA files", cmdMadFrames},
        {"mad-wav", "<file.mad> <out.wav>", "a movie's sound as a WAV file", cmdMadWav},
    });

} // namespace
