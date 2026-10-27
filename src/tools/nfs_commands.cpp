// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tools/nfs_commands.h"

#include "common/events.h"
#include "common/image.h"
#include "engines/nfs/bnk.h"
#include "engines/nfs/col.h"
#include "engines/nfs/fce.h"
#include "engines/nfs/ffn.h"
#include "engines/nfs/frd.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/host/game_host.h"
#include "engines/nfs/hrz.h"
#include "engines/nfs/mad.h"
#include "engines/nfs/mus.h"
#include "engines/nfs/refpack.h"
#include "engines/nfs/text_table.h"
#include "engines/nfs/viewers.h"
#include "engines/nfs/viv.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::nfs::ShapeBank;
using reluva::nfs::VivArchive;
namespace stdfs = std::filesystem;

const char* const kUsageLines =
    "  reluva-tool viv-list <file.viv>\n"
    "  reluva-tool viv-extract <file.viv> <output dir>\n"
    "  reluva-tool fsh-info <file.fsh | file.qfs | file.viv name>\n"
    "  reluva-tool fsh-dump <file.fsh | file.qfs | file.viv name> <output dir>\n"
    "  reluva-tool text-dump <text.eng>\n"
    "  reluva-tool ffn-render <file.ffn> <out.tga> [text]\n"
    "  reluva-tool fce-info <file.fce | file.viv name>\n"
    "  reluva-tool fce-obj <file.fce | file.viv name> <out.obj>\n"
    "  reluva-tool frd-check <file.frd>...\n"
    "  reluva-tool frd-obj <file.frd> <out.obj> [detail 0-2]\n"
    "  reluva-tool bnk-info <file.bnk | file.viv name>\n"
    "  reluva-tool bnk-wav <file.bnk | file.viv name> <output dir>\n"
    "  reluva-tool mus-info <file.mus> [file.map | file.lin]\n"
    "  reluva-tool mus-wav <file.mus> <segment | all> <output dir>\n"
    "  reluva-tool mad-info <file.mad>\n"
    "  reluva-tool mad-frames <file.mad> <output dir> [first frame] [count]\n"
    "  reluva-tool mad-wav <file.mad> <out.wav>\n"
    "  reluva-tool nfs-host <game folder> [-args \"nfs3.exe\"] [-renderer softtria.dll] [-limit N] [-log N] [-save dir]\n"
    "                       [-frames dir] [-every N] [-watch address]... [-watchmem address size]\n"
    "                       [-slice instructions] [-dump memory.bin] [-keys ms:key,...]\n"
    "                       (keys: enter, up, a..., click, moveDX/DY) [-framelog frames.csv]\n"
    "                       [-timescale 0.1]\n"
    "  reluva-tool nfs-survey <folder>... [-v]\n"
    "  reluva-tool nfs-page <game folder> <out.tga> <page> [key...]\n";

std::string lowerAscii(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string safeObjName(const std::string& s) {
    std::string out;
    for (char c : s)
        out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return out;
}

int fail(const Error& err) {
    std::fprintf(stderr, "%s\n", err.message.c_str());
    return 1;
}

bool readFile(const stdfs::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const stdfs::path& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    stdfs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return static_cast<bool>(f);
}

// A name from an archive as a file name in an output folder, or empty if
// it could escape it.
stdfs::path safeName(const std::string& name) {
    const stdfs::path rel(name);
    if (rel.empty() || rel.is_absolute() || rel.has_root_name())
        return {};
    for (const auto& part : rel)
        if (part == "..")
            return {};
    return rel;
}

// A file's contents, or a file's in a .viv when `inner` is given.
Error load(const char* file, const char* inner, std::vector<uint8_t>& out) {
    if (!inner) {
        if (!readFile(file, out))
            return Error::fail(std::string("Can't read ") + file);
        return Error::success();
    }
    VivArchive viv;
    if (Error err = viv.open(file))
        return err;
    return viv.read(inner, out);
}

int cmdVivList(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool viv-list <file.viv>\n");
        return 2;
    }
    VivArchive viv;
    if (Error err = viv.open(argv[2]))
        return fail(err);
    for (const auto& e : viv.entries())
        std::printf("%9u %9u  %s\n", e.offset, e.size, e.name.c_str());
    std::printf("%zu files\n", viv.entries().size());
    return 0;
}

int cmdVivExtract(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool viv-extract <file.viv> <output dir>\n");
        return 2;
    }
    VivArchive viv;
    if (Error err = viv.open(argv[2]))
        return fail(err);
    std::vector<uint8_t> data;
    for (const auto& e : viv.entries()) {
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

int cmdFshInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool fsh-info <file.fsh | file.qfs | file.viv name>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (Error err = load(argv[2], argc > 3 ? argv[3] : nullptr, data))
        return fail(err);
    ShapeBank bank;
    if (Error err = reluva::nfs::parseShapeBank(std::move(data), bank))
        return fail(err);
    std::printf("%s, %zu shapes\n", bank.id.c_str(), bank.shapes.size());
    for (const auto& s : bank.shapes) {
        std::printf("  %-4s %-15s %4dx%-4d centre %d,%d pos %d,%d", s.name.c_str(),
                    reluva::nfs::shapeTypeName(s.type), s.width, s.height, s.centerX, s.centerY, s.x, s.y);
        if (!s.palette.empty())
            std::printf(" palette %zu", s.palette.size() / 4);
        for (const auto& t : s.texts)
            std::printf(" \"%s\"", t.c_str());
        for (const auto& b : s.unknown)
            std::printf(" [block 0x%02x, %zu bytes]", b.type, b.data.size());
        std::printf("\n");
    }
    return 0;
}

int cmdFshDump(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool fsh-dump <file.fsh | file.qfs | file.viv name> <output dir>\n");
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
        const auto& s = bank.shapes[i];
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

int cmdTextDump(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool text-dump <text.eng>\n");
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
int cmdFfnRender(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool ffn-render <file.ffn> <out.tga> [text]\n");
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
    for (const auto& l : lines)
        width = std::max(width, font.measure(l));
    reluva::Surface surface;
    surface.create(width + 16, font.lineHeight * int(lines.size()) + 16, reluva::PixelFormat::RGBA32);
    for (size_t i = 0; i < surface.pixels.size(); i += 4) {
        surface.pixels[i] = surface.pixels[i + 1] = 40;
        surface.pixels[i + 2] = 60;
        surface.pixels[i + 3] = 255;
    }
    for (size_t i = 0; i < lines.size(); ++i)
        font.draw(surface, 8, 8 + font.lineHeight * int(i), lines[i], {255, 255, 255});
    reluva::image::Image img;
    img.w = surface.w;
    img.h = surface.h;
    img.rgba = surface.pixels;
    if (!writeFile(argv[3], reluva::image::encodeTga(img))) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    std::printf("line height %d, flags %d, atlas %dx%d\n", font.lineHeight, font.flags, font.atlasWidth,
                font.atlasHeight);
    return 0;
}

// Opens every file of the kinds Reluva reads under the folders given,
// .viv archives' contents included, and reports any that fail.
int cmdNfsSurvey(int argc, char** argv) {
    std::vector<stdfs::path> roots;
    bool verbose = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else
            roots.push_back(argv[i]);
    }
    if (roots.empty()) {
        std::fprintf(stderr, "usage: reluva-tool nfs-survey <folder>... [-v]\n");
        return 2;
    }

    struct Count {
        int ok = 0, failed = 0;
    };
    Count vivs, banks, pictures, tables, fonts, models, tracks, collisions, horizons, soundBanks, sounds, streams,
        segments, maps, movies, frames, movieAudio;
    std::vector<std::string> unsupported; // in EA's other compression schemes
    auto lower = [](std::string s) {
        for (char& c : s)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto report = [&](Count& count, const std::string& what, const Error& err) {
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
    auto check = [&](const std::string& name, std::vector<uint8_t> data, const std::string& what) {
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
            for (const auto& shape : bank.shapes) {
                reluva::image::Image img;
                report(pictures, what + ":" + shape.name, reluva::nfs::decodeShape(shape, img));
            }
        } else if (ext == ".ffn") {
            reluva::nfs::Font font;
            report(fonts, what, reluva::nfs::parseFont(data, font));
        } else if (ext == ".fce") {
            reluva::nfs::Model model;
            report(models, what, reluva::nfs::parseModel(data, model));
        } else if (ext == ".frd") {
            reluva::nfs::Track track;
            report(tracks, what, reluva::nfs::parseTrack(data, track));
        } else if (ext == ".col") {
            reluva::nfs::TrackCollision collision;
            report(collisions, what, reluva::nfs::parseCollision(data, collision));
        } else if (ext == ".bnk") {
            reluva::nfs::SoundBank bank;
            const Error err = reluva::nfs::parseSoundBank(data, bank);
            report(soundBanks, what, err);
            for (size_t i = 0; i < bank.slots.size(); ++i)
                if (bank.slots[i]) {
                    reluva::audio::Sound sound;
                    report(sounds, what + ":" + std::to_string(i), reluva::nfs::decodeBankSound(data, *bank.slots[i], sound));
                }
        } else if (ext == ".mus") {
            reluva::nfs::MusicStream stream;
            const Error err = stream.open(std::move(data));
            report(streams, what, err);
            for (size_t i = 0; i < stream.segments().size(); ++i) {
                reluva::audio::Sound sound;
                report(segments, what + ":" + std::to_string(i), stream.decode(i, sound));
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
            for (size_t i = 0; !err && i < movie.frames().size(); ++i)
                report(frames, what + ":" + std::to_string(i), movie.decodeFrame(decoder, i));
            if (!err && movie.hasAudio()) {
                reluva::audio::Sound sound;
                report(movieAudio, what, movie.decodeAudio(sound));
            }
        } else if (ext == ".hrz") {
            reluva::nfs::Horizon horizon;
            report(horizons, what, reluva::nfs::parseHorizon(std::string(data.begin(), data.end()), horizon));
        } else if (stem == "text") {
            reluva::nfs::TextTable table;
            report(tables, what, reluva::nfs::parseTextTable(data, table));
        }
    };

    for (const auto& root : roots) {
        std::error_code ec;
        for (const auto& entry : stdfs::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file(ec))
                continue;
            const stdfs::path& path = entry.path();
            const std::string ext = lower(path.extension().string());
            if (ext == ".viv") {
                VivArchive viv;
                const Error err = viv.open(path);
                report(vivs, path.string(), err);
                if (err)
                    continue;
                std::vector<uint8_t> data;
                for (const auto& e : viv.entries())
                    if (!viv.read(e.name, data))
                        check(e.name, std::move(data), path.string() + ":" + e.name);
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

    const Count* all[] = {&vivs,       &banks,      &pictures, &tables,  &fonts,    &models, &tracks,
                          &collisions, &horizons,   &soundBanks, &sounds, &streams, &segments, &maps,
                          &movies,     &frames,     &movieAudio};
    const char* names[] = {".viv archives", ".fsh/.qfs banks", "pictures",    "text tables", "fonts",
                           ".fce models",   ".frd tracks",     ".col files",  ".hrz files",  ".bnk banks",
                           "sounds",        ".mus streams",    "music segments", ".map/.lin maps",
                           ".mad movies",   "movie frames",    "movie audio"};
    int failed = 0;
    for (int i = 0; i < 17; ++i) {
        std::printf("%-16s %6d read, %d failed\n", names[i], all[i]->ok, all[i]->failed);
        failed += all[i]->failed;
    }
    // Known and documented (docs/formats/nfs/refpack.md), so not failures.
    if (!unsupported.empty()) {
        std::printf("%zu files in a compression scheme not read yet:\n", unsupported.size());
        for (const auto& u : unsupported)
            std::printf("  %s\n", u.c_str());
    }
    return failed ? 1 : 0;
}

int cmdFceInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool fce-info <file.fce | file.viv name>\n");
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
        const auto& p = model.parts[i];
        std::printf("  part %2zu %-26s at %6.2f %6.2f %6.2f  %4u vertices %4u triangles\n", i, p.name.c_str(),
                    p.position[0], p.position[1], p.position[2], p.vertexCount, p.triangleCount);
    }
    for (const auto& dm : model.dummies)
        std::printf("  dummy %-8s at %6.2f %6.2f %6.2f\n", dm.name.c_str(), dm.position[0], dm.position[1],
                    dm.position[2]);
    for (size_t i = 0; i < model.primaryColours.size(); ++i) {
        const auto& a = model.primaryColours[i].hsbx;
        std::printf("  colour %zu: %u %u %u %u", i, a[0], a[1], a[2], a[3]);
        if (i < model.secondaryColours.size()) {
            const auto& b = model.secondaryColours[i].hsbx;
            std::printf(", second %u %u %u %u", b[0], b[1], b[2], b[3]);
        }
        std::printf("\n");
    }
    std::map<uint32_t, int> pages, flags;
    for (const auto& t : model.triangles) {
        ++pages[t.texturePage];
        ++flags[t.flags];
    }
    std::printf("  triangles by texture page:");
    for (const auto& [k, n] : pages)
        std::printf(" %u:%d", k, n);
    std::printf("\n  triangles by flags:");
    for (const auto& [k, n] : flags)
        std::printf(" 0x%x:%d", k, n);
    std::printf("\n");
    return 0;
}

// Writes a model as Wavefront OBJ, one group per part, placed and turned
// right-handed (z mirrored) as Reluva draws it.
int cmdFceObj(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool fce-obj <file.fce | file.viv name> <out.obj>\n");
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
    for (const auto& p : model.parts)
        for (uint32_t i = p.firstVertex; i < p.firstVertex + p.vertexCount; ++i) {
            const auto& v = model.vertices[i];
            out << "v " << v[0] + p.position[0] << ' ' << v[1] + p.position[1] << ' ' << -(v[2] + p.position[2]) << "\n";
        }
    for (const auto& t : model.triangles)
        for (int k = 0; k < 3; ++k)
            out << "vt " << t.u[k] << ' ' << 1 - t.v[k] << "\n";
    size_t uv = 1;
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const auto& p = model.parts[i];
        out << "g part" << i << (p.name.empty() ? "" : "_") << safeObjName(p.name) << "\n";
        for (uint32_t t = p.firstTriangle; t < p.firstTriangle + p.triangleCount; ++t, uv += 3) {
            const auto& tri = model.triangles[t];
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

int cmdFrdCheck(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool frd-check <file.frd>...\n");
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
        size_t vertices = 0, road = 0, lanes = 0, objects = 0, extras = 0, moving = 0, lights = 0, sounds = 0;
        for (const auto& b : track.blocks) {
            vertices += b.vertices.size();
            road += b.polygons[4].size();
            lanes += b.polygons[6].size();
            for (const auto& layer : b.objectLayers)
                for (const auto& o : layer)
                    objects += o.polygons.size();
            lights += b.lights.size() / 16;
            sounds += b.sounds.size() / 16;
        }
        for (const auto& g : track.extraObjects)
            for (const auto& o : g) {
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
int cmdFrdObj(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool frd-obj <file.frd> <out.obj> [detail 0-2]\n");
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
    size_t base = 1, uv = 1;
    auto emit = [&](const std::vector<reluva::nfs::Track::Vec3>& verts, const reluva::nfs::Track::Vec3& offset,
                    const std::vector<reluva::nfs::Track::Polygon>& polys, const char* group) {
        out << "g " << group << "\n";
        for (const auto& v : verts)
            out << "v " << v[0] + offset[0] << ' ' << v[1] + offset[1] << ' ' << -(v[2] + offset[2]) << "\n";
        for (const auto& p : polys) {
            const auto& t = track.textures[p.texture];
            for (int k = 0; k < 4; ++k)
                out << "vt " << t.corners[2 * k] << ' ' << 1 - t.corners[2 * k + 1] << "\n";
            out << "f";
            for (int k = 0; k < 4; ++k)
                out << ' ' << base + p.vertices[k] << '/' << uv + k;
            out << "\n";
            uv += 4;
        }
        base += verts.size();
    };
    for (size_t i = 0; i < track.blocks.size(); ++i) {
        const auto& b = track.blocks[i];
        std::vector<reluva::nfs::Track::Polygon> polys = b.polygons[2 * detail];
        for (const auto& layer : b.objectLayers)
            for (const auto& o : layer)
                polys.insert(polys.end(), o.polygons.begin(), o.polygons.end());
        emit(b.vertices, {0, 0, 0}, polys, ("block" + std::to_string(i)).c_str());
    }
    for (const auto& g : track.extraObjects)
        for (const auto& o : g)
            if (o.type == 4)
                emit(o.vertices, o.reference, o.polygons, "object");
    if (!out) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    return 0;
}

// A sound as a 16-bit PCM WAV file.
std::vector<uint8_t> wavFile(const reluva::audio::Sound& s) {
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

const char* compressionName(int c) {
    switch (c) {
    case reluva::nfs::SoundHeader::kPcm16: return "PCM";
    case reluva::nfs::SoundHeader::kEaXa: return "EA-XA";
    case reluva::nfs::SoundHeader::kMicroTalk: return "MicroTalk";
    default: return "?";
    }
}

int cmdBnkInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool bnk-info <file.bnk | file.viv name>\n");
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
        const auto& h = *bank.slots[i];
        std::printf("  %3zu %-9s %d ch %5d Hz %8u samples (%.2f s) at %u", i, compressionName(h.compression),
                    h.channels, h.rate, h.samples, double(h.samples) / h.rate, h.dataOffset);
        if (h.loopLength)
            std::printf(" loop %u+%u", h.loopStart, h.loopLength);
        std::printf("  tags");
        for (const auto& [tag, value] : h.tags)
            if (tag < 0x82 || tag > 0x88)
                std::printf(" %02x=%u", tag, value);
        std::printf("\n");
    }
    return 0;
}

int cmdBnkWav(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool bnk-wav <file.bnk | file.viv name> <output dir>\n");
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
    int written = 0, failed = 0;
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

int cmdMusInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool mus-info <file.mus> [file.map | file.lin]\n");
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
        const auto& s = stream.segments()[i];
        std::printf("  %3zu at %9u  %-5s %d ch %5d Hz %7u samples (%.2f s), %zu blocks", i, s.offset,
                    compressionName(s.header.compression), s.header.channels, s.header.rate, s.header.samples,
                    double(s.header.samples) / s.header.rate, s.blocks.size());
        if (haveMap && i < map.sections.size()) {
            const auto& sec = map.sections[i];
            if (map.offsets[i] != s.offset)
                ++mismatched;
            std::printf("  kind %d", sec.kind);
            for (const auto& r : sec.routes)
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

int cmdMusWav(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool mus-wav <file.mus> <segment | all> <output dir>\n");
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

int cmdMadInfo(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool mad-info <file.mad>\n");
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
    for (const auto& f : movie.frames())
        ++kinds[f.kind];
    const double ms = movie.frameDuration() / 65536.0;
    std::printf("%dx%d, %zu frames (%d intra, %d inter, %d unreferenced), %.3f ms each (%.3f fps), %.2f s\n",
                movie.width(), movie.height(), movie.frames().size(), kinds['k'], kinds['m'], kinds['e'], ms,
                ms > 0 ? 1000 / ms : 0.0, ms * movie.frames().size() / 1000);
    if (movie.hasAudio()) {
        const auto& h = movie.audioHeader();
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

int cmdMadFrames(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool mad-frames <file.mad> <output dir> [first frame] [count]\n");
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
    int written = 0;
    for (size_t i = 0; i < movie.frames().size() && i < first + count; ++i) {
        if (Error err = movie.decodeFrame(decoder, i))
            return fail(err);
        if (i < first)
            continue;
        reluva::Surface s;
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

int cmdMadWav(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool mad-wav <file.mad> <out.wav>\n");
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

// Runs the retail nfs3.exe in the x86 interpreter (Phase 4's spike) and
// reports how far it gets: the system calls it makes, the files it opens,
// what it asks of THRASH, and where it stops.
int cmdNfsHost(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs-host <game folder> [-args \"nfs3.exe -d3d\"] [-limit N] [-log N] "
                             "[-save dir] [-trace N]\n");
        return 2;
    }
    reluva::nfs::GameHost::Options options;
    options.saveFolder = stdfs::temp_directory_path() / "reluva_nfs3_save";
    int traceCount = 32, every = 50;
    std::vector<uint32_t> watch; // addresses at which to print the registers
    std::string dumpFile;        // the emulated memory, written at the end
    uint32_t slice = 0;          // instructions between thread changes (0: the default)
    uint32_t watchMem = 0, watchMemSize = 0; // memory whose stores are printed
    std::string keyScript;                   // "ms:key,ms:key...": each pressed for 100 ms
    std::string frameLogFile;                // each frame's time and instructions, as CSV
    std::string framesDir;
    for (int a = 3; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-args" && a + 1 < argc)
            options.commandLine = argv[++a];
        else if (arg == "-limit" && a + 1 < argc)
            options.instructionLimit = std::strtoull(argv[++a], nullptr, 0);
        else if (arg == "-log" && a + 1 < argc)
            options.callLog = std::atoi(argv[++a]);
        else if (arg == "-save" && a + 1 < argc)
            options.saveFolder = argv[++a];
        else if (arg == "-trace" && a + 1 < argc)
            traceCount = std::atoi(argv[++a]);
        else if (arg == "-frames" && a + 1 < argc)
            framesDir = argv[++a];
        else if (arg == "-watch" && a + 1 < argc)
            watch.push_back(uint32_t(std::strtoul(argv[++a], nullptr, 0)));
        else if (arg == "-timescale" && a + 1 < argc)
            options.timeScale = std::atof(argv[++a]);
        else if (arg == "-framelog" && a + 1 < argc)
            frameLogFile = argv[++a];
        else if (arg == "-keys" && a + 1 < argc)
            keyScript = argv[++a];
        else if (arg == "-slice" && a + 1 < argc)
            slice = uint32_t(std::strtoul(argv[++a], nullptr, 0));
        else if (arg == "-watchmem" && a + 2 < argc) {
            watchMem = uint32_t(std::strtoul(argv[++a], nullptr, 0));
            watchMemSize = uint32_t(std::strtoul(argv[++a], nullptr, 0));
        } else if (arg == "-dump" && a + 1 < argc)
            dumpFile = argv[++a];
        else if (arg == "-every" && a + 1 < argc)
            every = std::max(1, std::atoi(argv[++a]));
    }
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    std::printf("install %s, CD %s, writes to %s\n", folders.install.string().c_str(),
                folders.cd.empty() ? "(none)" : folders.cd.string().c_str(), options.saveFolder.string().c_str());
    reluva::nfs::GameHost host(folders, options);
    host.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
    // Frames shown: every so many written as pictures.
    int shown = 0;
    host.onFrame = [&](const reluva::Surface& frame) {
        if (framesDir.empty() || shown++ % every)
            return;
        reluva::image::Image img;
        img.w = frame.w;
        img.h = frame.h;
        img.rgba = frame.pixels;
        char name[32];
        std::snprintf(name, sizeof name, "frame%05d.tga", shown - 1);
        writeFile(stdfs::path(framesDir) / name, reluva::image::encodeTga(img));
    };
    std::vector<uint32_t> recent(size_t(std::max(traceCount, 1)));
    size_t recentAt = 0;
    int watched = 0;
    host.cpu().trace = [&](reluva::x86::Cpu& c) {
        recent[recentAt++ % recent.size()] = c.eip;
        if (!watch.empty() && watched < 50 && std::find(watch.begin(), watch.end(), c.eip) != watch.end()) {
            ++watched;
            std::printf("  at %08x: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x\n", c.eip,
                        c.reg[0], c.reg[1], c.reg[2], c.reg[3], c.reg[4], c.reg[5], c.reg[6], c.reg[7]);
        }
    };
    if (slice)
        host.process().slice = slice;
    for (size_t at = 0; at < keyScript.size();) {
        const size_t comma = keyScript.find(',', at);
        const std::string item = keyScript.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        at = comma == std::string::npos ? keyScript.size() : comma + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos)
            continue;
        const uint64_t ms = std::strtoull(item.c_str(), nullptr, 10);
        const std::string key = item.substr(colon + 1);
        int dx = 0, dy = 0;
        if (key == "click") {
            host.queueMouseButton(true, ms);
            host.queueMouseButton(false, ms + 100);
        } else if (std::sscanf(key.c_str(), "move%d/%d", &dx, &dy) == 2) {
            host.queueMouseMove(dx, dy, ms);
        } else if (!host.queueKey(key, true, ms) || !host.queueKey(key, false, ms + 100)) {
            std::fprintf(stderr, "unknown key '%s'\n", key.c_str());
        }
    }
    if (watchMemSize) {
        host.cpu().watchStart = watchMem;
        host.cpu().watchEnd = watchMem + watchMemSize;
        host.cpu().onWatchedStore = [&host](reluva::x86::Cpu& c, uint32_t address, uint32_t value, int size) {
            const auto* t = host.process().currentThread();
            std::printf("  store %08x <- %08x (%d bytes) at %08x [thread %u] esp %08x\n", address, value, size, c.eip,
                        t ? t->id : 0, c.reg[reluva::x86::Cpu::ESP]);
        };
    }
    if (Error err = host.load())
        return fail(err);
    uint32_t exitCode = 0;
    const auto start = std::chrono::steady_clock::now();
    const Error err = host.run(exitCode);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (err) {
        std::printf("stopped: %s\nlast instructions:", err.message.c_str());
        for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0; i < recentAt; ++i)
            std::printf(" %08x", recent[i % recent.size()]);
        const auto& r = host.cpu().faultReg;
        std::printf("\nregisters: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x\n", r[0],
                    r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    } else {
        std::printf("the game ended the process, exit code %u\n", exitCode);
    }
    if (!dumpFile.empty())
        writeFile(dumpFile, host.cpu().memory());
    const uint64_t n = host.cpu().executed();
    std::printf("%llu instructions in %.1f s (%.1f million a second)\n", (unsigned long long)n, seconds,
                seconds > 0 ? n / seconds / 1e6 : 0.0);
    std::vector<std::pair<uint64_t, std::string>> counts;
    for (const auto& [name, count] : host.callCounts)
        counts.push_back({count, name});
    std::sort(counts.rbegin(), counts.rend());
    std::printf("system calls (%zu kinds):", counts.size());
    for (size_t i = 0; i < counts.size() && i < 400; ++i)
        std::printf("%s %s %llu", i ? "," : "", counts[i].second.c_str(), (unsigned long long)counts[i].first);
    std::printf("\n");
    const auto& t = host.thrash();
    if (!t.dll.empty() || !t.calls.empty()) {
        std::printf("THRASH (%s): %llu frames, %llu textures; %.0f%% of all instructions in its functions\n  calls:",
                    t.dll.c_str(), (unsigned long long)t.frames, (unsigned long long)t.textures,
                    100.0 * double(t.rendererInstructions) / double(std::max<uint64_t>(host.cpu().executed(), 1)));
        for (const auto& [name, count] : t.calls)
            std::printf(" %s %llu", name.c_str(), (unsigned long long)count);
        std::printf("\n");
        if (!frameLogFile.empty()) {
            std::string csv = "ms,instructions,renderer\n";
            for (const auto& f : t.frameLog)
                csv += std::to_string(f.ms) + "," + std::to_string(f.instructions) + "," + std::to_string(f.renderer) + "\n";
            writeFile(frameLogFile, std::vector<uint8_t>(csv.begin(), csv.end()));
        }
        // The frames in one-second steps: how many, and their instructions.
        uint64_t second = 0, frames = 0, instructions = 0, renderer = 0;
        auto flush = [&] {
            if (frames)
                std::printf("  second %3llu: %3llu frames, %6.2f million instructions each, %6.2f of them the renderer's\n",
                            (unsigned long long)second, (unsigned long long)frames, instructions / double(frames) / 1e6,
                            renderer / double(frames) / 1e6);
        };
        for (const auto& f : t.frameLog) {
            if (f.ms / 1000 != second) {
                flush();
                second = f.ms / 1000;
                frames = instructions = renderer = 0;
            }
            ++frames;
            instructions += f.instructions;
            renderer += f.renderer;
        }
        flush();
    }
    return err ? 1 : 0;
}

// Draws one of the engine's viewer pages after the keys given, as the
// engine would, without a window: for checking pages and taking pictures
// of them.
int cmdNfsPage(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool nfs-page <game folder> <out.tga> <page> [key...]\n"
                             "  keys: left right up down pgup pgdn home end, or a letter or symbol\n");
        return 2;
    }
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameFiles files;
    files.scan(folders);
    auto pages = reluva::nfs::makePages("Need for Speed III: Hot Pursuit", folders, files);
    reluva::nfs::Page* page = nullptr;
    for (auto& p : pages)
        if (lowerAscii(p->name()) == lowerAscii(argv[4]))
            page = p.get();
    if (!page) {
        std::fprintf(stderr, "no page '%s'; there are:", argv[4]);
        for (auto& p : pages)
            std::fprintf(stderr, " %s", p->name());
        std::fprintf(stderr, "\n");
        return 1;
    }
    const std::pair<const char*, uint32_t> names[] = {
        {"left", reluva::key::Left},  {"right", reluva::key::Right},   {"up", reluva::key::Up},
        {"down", reluva::key::Down},  {"pgup", reluva::key::PageUp},   {"pgdn", reluva::key::PageDown},
        {"home", reluva::key::Home},  {"end", reluva::key::End}};
    for (int i = 5; i < argc; ++i) {
        uint32_t key = 0;
        for (const auto& [name, code] : names)
            if (std::strcmp(argv[i], name) == 0)
                key = code;
        if (!key && std::strlen(argv[i]) == 1)
            key = static_cast<unsigned char>(argv[i][0]);
        if (!key) {
            std::fprintf(stderr, "unknown key '%s'\n", argv[i]);
            return 2;
        }
        page->handleKey(key, 0);
    }
    reluva::Surface screen;
    screen.create(640, 480, reluva::PixelFormat::RGBA32);
    page->draw(screen);
    reluva::image::Image img;
    img.w = screen.w;
    img.h = screen.h;
    img.rgba = screen.pixels;
    if (!writeFile(argv[3], reluva::image::encodeTga(img))) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    return 0;
}

} // namespace

const char* const kNfsUsage = kUsageLines;

int runNfsCommand(int argc, char** argv) {
    if (argc < 2)
        return -1;
    const char* cmd = argv[1];
    if (std::strcmp(cmd, "viv-list") == 0)
        return cmdVivList(argc, argv);
    if (std::strcmp(cmd, "viv-extract") == 0)
        return cmdVivExtract(argc, argv);
    if (std::strcmp(cmd, "fsh-info") == 0)
        return cmdFshInfo(argc, argv);
    if (std::strcmp(cmd, "fsh-dump") == 0)
        return cmdFshDump(argc, argv);
    if (std::strcmp(cmd, "text-dump") == 0)
        return cmdTextDump(argc, argv);
    if (std::strcmp(cmd, "ffn-render") == 0)
        return cmdFfnRender(argc, argv);
    if (std::strcmp(cmd, "fce-info") == 0)
        return cmdFceInfo(argc, argv);
    if (std::strcmp(cmd, "fce-obj") == 0)
        return cmdFceObj(argc, argv);
    if (std::strcmp(cmd, "frd-check") == 0)
        return cmdFrdCheck(argc, argv);
    if (std::strcmp(cmd, "frd-obj") == 0)
        return cmdFrdObj(argc, argv);
    if (std::strcmp(cmd, "bnk-info") == 0)
        return cmdBnkInfo(argc, argv);
    if (std::strcmp(cmd, "bnk-wav") == 0)
        return cmdBnkWav(argc, argv);
    if (std::strcmp(cmd, "mus-info") == 0)
        return cmdMusInfo(argc, argv);
    if (std::strcmp(cmd, "mus-wav") == 0)
        return cmdMusWav(argc, argv);
    if (std::strcmp(cmd, "nfs-host") == 0)
        return cmdNfsHost(argc, argv);
    if (std::strcmp(cmd, "mad-info") == 0)
        return cmdMadInfo(argc, argv);
    if (std::strcmp(cmd, "mad-frames") == 0)
        return cmdMadFrames(argc, argv);
    if (std::strcmp(cmd, "mad-wav") == 0)
        return cmdMadWav(argc, argv);
    if (std::strcmp(cmd, "nfs-survey") == 0)
        return cmdNfsSurvey(argc, argv);
    if (std::strcmp(cmd, "nfs-page") == 0)
        return cmdNfsPage(argc, argv);
    return -1;
}
