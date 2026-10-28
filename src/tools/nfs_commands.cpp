// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tools/nfs_commands.h"

#include "backends/sockets/socket_net.h"
#include "common/audio/wav.h"
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
#include "engines/nfs/play_page.h"
#include "engines/nfs/refpack.h"
#include "engines/nfs/text_table.h"
#include "engines/nfs/viewers.h"
#include "engines/nfs/viv.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <atomic>
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
#include <thread>
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
    "                       [-timescale 0.1] [-thrashlog fromframe toframe] [-native] [-wav sound.wav]\n"
    "                       [-joystick name] (keys also joy:A=V...: axes 0-5 = V, button bB; +key/-key held)\n"
    "                       [-wheel] [-ff] (the joystick a wheel; with force feedback) [-forcelog forces.csv]\n"
    "                       [-net] (the network) [-netaddr a.b.c.d] [-broadcast a.b.c.d,...]\n"
    "                       [-deterministic instructionsPerMs] (a clock of instructions; the end state hashed)\n"
    "                       [-hotspots from to] (instructions counted by where they run, 16-byte blocks)\n"
    "                       [-gpulog] (THRASH recorded as for the GPU; each frame's list summed up)\n"
    "  reluva-tool nfs-native-check <game folder> [trials]\n"
    "  reluva-tool nfs-huff-check <game folder> [out dir]\n"
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
    int traceCount = 0, every = 50; // traceCount: the last instructions kept, for a failure (0: none)
    std::vector<uint32_t> watch; // addresses at which to print the registers
    std::string dumpFile;        // the emulated memory, written at the end
    uint32_t slice = 0;          // instructions between thread changes (0: the default)
    uint32_t watchMem = 0, watchMemSize = 0; // memory whose stores are printed
    std::string keyScript;                   // "ms:key,ms:key...": each pressed for 100 ms
    std::string frameLogFile;                // each frame's time and instructions, as CSV
    std::string wavFile;                     // what the game's sound buffers played
    uint64_t hotFrom = 0, hotTo = 0;         // instructions counted by where they run, over this range
    bool gpuLog = false;
    std::string framesDir;
    std::string forceLogFile; // the forces the game asks for, each time they change, as CSV
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
        else if (arg == "-thrashlog" && a + 2 < argc) {
            options.thrashLogFrom = std::strtoull(argv[++a], nullptr, 0);
            options.thrashLogTo = std::strtoull(argv[++a], nullptr, 0);
        } else if (arg == "-native") {
            options.nativeThrash = true;
        } else if (arg == "-deterministic" && a + 1 < argc) {
            options.instructionsPerMs = uint32_t(std::strtoul(argv[++a], nullptr, 0));
        } else if (arg == "-timescale" && a + 1 < argc)
            options.timeScale = std::atof(argv[++a]);
        else if (arg == "-framelog" && a + 1 < argc)
            frameLogFile = argv[++a];
        else if (arg == "-wav" && a + 1 < argc)
            wavFile = argv[++a];
        else if (arg == "-hotspots" && a + 2 < argc) {
            hotFrom = std::strtoull(argv[++a], nullptr, 10);
            hotTo = std::strtoull(argv[++a], nullptr, 10);
        } else if (arg == "-gpulog") { // THRASH recorded as for the GPU, each frame's list summed up
            options.nativeThrash = true;
            options.gpuThrash = true;
            gpuLog = true;
        } else if (arg == "-joystick" && a + 1 < argc)
            options.joystick = argv[++a];
        else if (arg == "-net")
            options.sockets = reluva::net::systemSockets();
        else if ((arg == "-netaddr" || arg == "-broadcast") && a + 1 < argc) {
            const std::string list = argv[++a];
            for (size_t at = 0; at < list.size();) {
                const size_t comma = std::min(list.find(',', at), list.size());
                reluva::net::Address parsed;
                if (!reluva::net::parseAddress(list.substr(at, comma - at), 0, parsed))
                    return fail(reluva::Error::fail("not an address: " + list.substr(at, comma - at)));
                if (arg == "-netaddr")
                    options.netAddress = parsed.ip;
                else
                    options.broadcastTo.push_back(parsed.ip);
                at = comma + 1;
            }
        } else if (arg == "-wheel")
            options.joystickWheel = true;
        else if (arg == "-ff")
            options.forceFeedback = true;
        else if (arg == "-forcelog" && a + 1 < argc)
            forceLogFile = argv[++a];
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
    // The forces, as each frame finds them: a row whenever they change.
    std::string forceCsv = "ms,constant,spring,centre,saturation,damper,vibration,period,square,autocentre\n";
    reluva::GamepadForce lastForce;
    int forceChanges = 0;
    float strongestPush = 0, strongestVibration = 0;
    host.onFrame = [&](const reluva::Surface& frame) {
        if (options.forceFeedback) {
            const reluva::GamepadForce f = host.gamepadForce();
            if (!(f == lastForce) || forceChanges == 0) {
                char row[200];
                std::snprintf(row, sizeof row, "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%d,%d\n",
                              (unsigned long long)host.process().now(), f.constant, f.spring, f.springCentre,
                              f.springSaturation, f.damper, f.vibration, f.vibrationPeriodMs, int(f.vibrationSquare),
                              int(f.autoCentre));
                forceCsv += row;
                ++forceChanges;
                lastForce = f;
                strongestPush = std::max(strongestPush, std::fabs(f.constant));
                strongestVibration = std::max(strongestVibration, f.vibration);
            }
        }
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
    if (gpuLog)
        host.onThrashFrame = [&](reluva::nfs::ThrashFrame&& f) {
            size_t textured = 0;
            for (const auto& b : f.batches)
                textured += b.texture ? b.count : 0;
            std::printf("  list at %llu ms: %zu batches, %zu vertices (%zu textured), %zu textures%s, flip %d,%d-%d,%d\n",
                        (unsigned long long)host.process().now(), f.batches.size(), f.vertices.size(), textured,
                        f.textures.size(), f.resetTextures ? " after a reset" : "", f.flip[0], f.flip[1], f.flip[2],
                        f.flip[3]);
        };
    // Where instructions run, by 16-byte block, over a range of the run.
    std::map<uint32_t, uint64_t> hot;
    if (hotTo > hotFrom)
        host.cpu().trace = [&](reluva::x86::Cpu& c) {
            if (c.executed() >= hotFrom && c.executed() < hotTo)
                ++hot[c.eip & ~0xfu];
        };
    // Tracing every instruction costs: only when asked for.
    if (traceCount > 0 || !watch.empty())
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
    reluva::GamepadState pad; // the joystick's state, as the script sets it
    pad.wheel = options.joystickWheel;
    pad.forceFeedback = options.forceFeedback;
    for (size_t at = 0; at < keyScript.size();) {
        const size_t comma = keyScript.find(',', at);
        const std::string item = keyScript.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        at = comma == std::string::npos ? keyScript.size() : comma + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos)
            continue;
        const uint64_t ms = std::strtoull(item.c_str(), nullptr, 10);
        const std::string key = item.substr(colon + 1);
        int dx = 0, dy = 0, index = 0;
        float value = 0;
        if (key.rfind("joy:", 0) == 0) { // joy:A=V (axis A 0-5) or joy:bB=V (button B), the pad's state so far
            if (std::sscanf(key.c_str(), "joy:b%d=%f", &index, &value) == 2 && index >= 0 && index < 32)
                pad.buttons = value != 0 ? pad.buttons | 1u << index : pad.buttons & ~(1u << index);
            else if (std::sscanf(key.c_str(), "joy:%d=%f", &index, &value) == 2 && index >= 0 && index < 6)
                pad.axes[index] = value;
            host.setJoystick(pad, ms);
        } else if (key == "click") {
            host.queueMouseButton(true, ms);
            host.queueMouseButton(false, ms + 100);
        } else if (std::sscanf(key.c_str(), "move%d/%d", &dx, &dy) == 2) {
            host.queueMouseMove(dx, dy, ms);
        } else if ((key[0] == '+' || key[0] == '-') && key.size() > 1) { // held, let go
            if (!host.queueKey(key.substr(1), key[0] == '+', ms))
                std::fprintf(stderr, "unknown key '%s'\n", key.c_str());
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
    // The sound, pulled as an audio device would: every 10 ms, what has
    // played since.
    reluva::audio::Sound sound;
    sound.rate = 22050;
    sound.channels = 2;
    std::atomic<bool> running{true};
    std::thread soundThread;
    if (!wavFile.empty())
        soundThread = std::thread([&] {
            auto stream = host.soundStream();
            const auto t0 = std::chrono::steady_clock::now();
            size_t pulled = 0;
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                const size_t due = size_t(s * sound.rate);
                if (due <= pulled)
                    continue;
                sound.samples.resize(due * 2);
                stream->read(sound.samples.data() + pulled * 2, int(due - pulled), sound.rate);
                pulled = due;
            }
        });
    const auto start = std::chrono::steady_clock::now();
    const Error err = host.run(exitCode);
    running = false;
    if (soundThread.joinable()) {
        soundThread.join();
        writeFile(wavFile, reluva::audio::encodePcmWav(sound));
        const auto stats = host.soundStats();
        std::printf("sound: %llu blocks of 256 bytes played, %llu of them again without being written since "
                    "(%llu not silent; those of a streamed buffer played as silence)\n",
                    (unsigned long long)stats.blocks, (unsigned long long)stats.stale,
                    (unsigned long long)stats.staleAudible);
        std::map<uint64_t, int> bySecond;
        for (uint64_t ms : stats.staleAudibleMs)
            ++bySecond[ms / 1000];
        if (!bySecond.empty()) {
            std::printf("  not silent, by second of the game's clock:");
            for (const auto& [second, n] : bySecond)
                std::printf(" %llus:%d", (unsigned long long)second, n);
            std::printf("\n");
        }
    }
    if (options.instructionsPerMs) {
        // The state the run ended in, for comparing builds: memory and
        // registers hashed (FNV-1a).
        uint64_t h = 0xcbf29ce484222325ull;
        for (uint8_t byte : host.cpu().memory())
            h = (h ^ byte) * 0x100000001b3ull;
        for (uint32_t r : host.cpu().reg)
            h = (h ^ r) * 0x100000001b3ull;
        std::printf("state: %016llx after %llu instructions, eip %08x\n", (unsigned long long)h,
                    (unsigned long long)host.cpu().executed(), host.cpu().eip);
    }
    if (options.forceFeedback) {
        std::printf("forces: %d changes, the strongest push %.2f, the strongest vibration %.2f\n", forceChanges,
                    strongestPush, strongestVibration);
        if (!forceLogFile.empty())
            writeFile(forceLogFile, std::vector<uint8_t>(forceCsv.begin(), forceCsv.end()));
    }
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
    for (const auto& [name, count] : host.callCounts())
        counts.push_back({count, name});
    std::sort(counts.rbegin(), counts.rend());
    std::printf("system calls (%zu kinds):", counts.size());
    for (size_t i = 0; i < counts.size() && i < 400; ++i)
        std::printf("%s %s %llu", i ? "," : "", counts[i].second.c_str(), (unsigned long long)counts[i].first);
    std::printf("\n");
    if (!hot.empty()) {
        std::vector<std::pair<uint64_t, uint32_t>> ranked;
        uint64_t total = 0;
        for (const auto& [block, n] : hot) {
            ranked.push_back({n, block});
            total += n;
        }
        std::sort(ranked.rbegin(), ranked.rend());
        std::printf("hot spots (%llu instructions):\n", (unsigned long long)total);
        for (size_t i = 0; i < ranked.size() && i < 40; ++i) {
            const auto* m = host.process().moduleContaining(ranked[i].second);
            std::printf("  %08x %5.1f%%  %s\n", ranked[i].second, 100.0 * double(ranked[i].first) / double(total),
                        m ? m->name.c_str() : "");
        }
    }
    const auto& t = host.thrash();
    if (!t.dll.empty() || !t.calls.empty()) {
        std::printf("THRASH (%s): %llu frames, %llu textures; %.0f%% of all instructions in its functions\n  calls:",
                    t.dll.c_str(), (unsigned long long)t.frames, (unsigned long long)t.textures,
                    100.0 * double(t.rendererInstructions) / double(std::max<uint64_t>(host.cpu().executed(), 1)));
        for (const auto& [name, count] : t.calls) {
            auto it = t.callInstructions.find(name);
            std::printf(" %s %llu", name.c_str(), (unsigned long long)count);
            if (it != t.callInstructions.end() && it->second > 1000000)
                std::printf(" (%.0fM)", it->second / 1e6);
        }
        std::printf("\n  texture formats:");
        for (const auto& [format, count] : t.textureFormats)
            std::printf(" %d: %llu", format, (unsigned long long)count);
        std::printf("\n");
        if (!frameLogFile.empty()) {
            std::string csv = "ms,instructions,renderer\n";
            for (const auto& f : t.frameLog)
                csv += std::to_string(f.ms) + "," + std::to_string(f.instructions) + "," + std::to_string(f.renderer) + "\n";
            writeFile(frameLogFile, std::vector<uint8_t>(csv.begin(), csv.end()));
        }
        // The frames in one-second steps: how many, and their instructions.
        // ... and the time no thread could run (all waiting), and how often.
        // ... and the shares of the game's own thread and the timer's.
        uint64_t second = 0, frames = 0, instructions = 0, renderer = 0, idleMicros = 0, idleWaits = 0, own = 0,
                 timer = 0;
        auto flush = [&] {
            if (frames)
                std::printf("  second %3llu: %3llu frames, %6.2f million instructions each, %6.2f of them the renderer's,"
                            " idle %4llu ms (%llu waits); game thread %2.0f%%, timer %2.0f%%\n",
                            (unsigned long long)second, (unsigned long long)frames, instructions / double(frames) / 1e6,
                            renderer / double(frames) / 1e6, (unsigned long long)(idleMicros / 1000),
                            (unsigned long long)idleWaits, 100.0 * double(own) / double(std::max<uint64_t>(instructions, 1)),
                            100.0 * double(timer) / double(std::max<uint64_t>(instructions, 1)));
        };
        for (const auto& f : t.frameLog) {
            if (f.ms / 1000 != second) {
                flush();
                second = f.ms / 1000;
                frames = instructions = renderer = idleMicros = idleWaits = own = timer = 0;
            }
            ++frames;
            instructions += f.instructions;
            renderer += f.renderer;
            idleMicros += f.idleMicros;
            idleWaits += f.idleWaits;
            own += f.ownThread;
            timer += f.timerThread;
        }
        flush();
    }
    return err ? 1 : 0;
}

// Runs each of nfs3.exe's routines done natively (GameHost::Options::
// nativeRoutines) against its original in the interpreter, on the same
// random inputs, in two copies of the game loaded side by side; every byte
// they write, and the registers, must come out the same.
// EA's Huffman schemes (30 FB, 32 FB, 34 FB; refpack.md): every file so
// compressed in the game's folders, decompressed by the game's own routine
// (0x50ed50, run in the interpreter) and by Reluva's, which must agree; the
// game's results written to `out dir` if given.
int cmdNfsHuffCheck(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs-huff-check <game folder> [out dir]\n");
        return 2;
    }
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameFiles files;
    files.scan(folders);
    reluva::nfs::GameHost::Options options;
    options.saveFolder = stdfs::temp_directory_path() / "reluva-huff-check";
    reluva::nfs::GameHost host(folders, options);
    if (reluva::Error e = host.load())
        return fail(e);
    auto& c = host.cpu();
    const uint32_t stack = host.process().memory().alloc(1 << 16);
    int checked = 0, mismatches = 0;
    for (const reluva::nfs::FileRef& f : files.files()) {
        std::vector<uint8_t> data;
        if (files.read(f, data) || !reluva::nfs::isEaHuffman(data))
            continue;
        const uint32_t size = uint32_t(data[2]) << 16 | uint32_t(data[3]) << 8 | data[4];
        const uint32_t in = host.process().memory().alloc(uint32_t(data.size()) + 16);
        const uint32_t out = host.process().memory().alloc(size + 4096);
        if (!in || !out)
            return fail(reluva::Error::fail("no memory"));
        for (size_t i = 0; i < data.size(); ++i)
            c.write8(in + uint32_t(i), data[i]);
        c.reg[reluva::x86::Cpu::EAX] = in;
        c.reg[reluva::x86::Cpu::EDX] = out;
        c.reg[reluva::x86::Cpu::EBX] = 1;
        c.reg[reluva::x86::Cpu::ESP] = stack + (1 << 16) - 64;
        uint32_t result = 0;
        if (reluva::Error e = c.call(0x50ed50, {}, result)) {
            std::printf("%s: the game's routine failed: %s\n", f.name.c_str(), e.message.c_str());
            continue;
        }
        std::vector<uint8_t> game(result);
        for (uint32_t i = 0; i < result; ++i)
            game[i] = c.read8(out + i);
        std::vector<uint8_t> ours;
        const reluva::Error err = reluva::nfs::decompressEaHuffman(data, ours);
        size_t differ = 0;
        for (size_t i = 0; i < std::min(ours.size(), game.size()); ++i)
            differ += ours[i] != game[i];
        const bool same = !err && ours == game;
        std::printf("%s: %02x fb, %zu bytes to %u: %s\n", f.name.c_str(), data[0], data.size(), result,
                    same ? "the same as the game's"
                         : (err ? err.message.c_str()
                                : ("DIFFERENT (" + std::to_string(ours.size()) + " bytes, " + std::to_string(differ) +
                                   " differ)").c_str()));
        mismatches += !same;
        if (argc > 3) {
            std::string name = f.name;
            for (char& ch : name)
                if (ch == '/' || ch == '\\')
                    ch = '_';
            writeFile(stdfs::path(argv[3]) / (name + ".out"), game);
        }
        ++checked;
    }
    std::printf("%d files, %d not the same\n", checked, mismatches);
    return mismatches ? 1 : 0;
}

int cmdNfsNativeCheck(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs-native-check <game folder> [trials]\n");
        return 2;
    }
    const int trials = argc > 3 ? std::max(1, std::atoi(argv[3])) : 300;
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameHost::Options interpreted, native;
    interpreted.nativeRoutines = false;
    interpreted.saveFolder = native.saveFolder = stdfs::temp_directory_path() / "reluva-native-check";
    reluva::nfs::GameHost a(folders, interpreted), b(folders, native);
    if (reluva::Error e = a.load())
        return fail(e);
    if (reluva::Error e = b.load())
        return fail(e);
    // The same memory in each: a stack, and room for a routine's buffers.
    reluva::nfs::GameHost* hosts[2] = {&a, &b};
    uint32_t stack = 0, area = 0;
    for (auto* h : hosts) {
        stack = h->process().memory().alloc(1 << 16);
        area = h->process().memory().alloc(1 << 16);
    }
    for (auto* h : hosts)
        if (h->process().memory().alloc(16) == 0)
            return fail(reluva::Error::fail("no memory"));
    uint64_t seed = 0x9e3779b97f4a7c15ull;
    auto random = [&seed] {
        seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17;
        return seed;
    };
    auto randomFloat = [&](double range) { return float((double(random() % 2000001) / 1000000.0 - 1.0) * range); };
    struct Routine {
        const char* name;
        uint32_t address;
        int kind; // 0 resample stereo, 1 resample mono, 2 mix, 3 to 16 bits
        uint32_t scratch;
        bool stereoSource;
    };
    const Routine routines[] = {
        {"resample stereo", 0x527330, 0, 0x56b138, false}, {"resample mono", 0x5273c0, 1, 0x56b148, false},
        {"mix mono", 0x520aee, 2, 0, false},               {"mix mono full", 0x520b7e, 2, 0, false},
        {"mix mono left", 0x520bed, 2, 0, false},          {"mix mono right", 0x520c69, 2, 0, false},
        {"mix mono, right full", 0x520ce6, 2, 0, false},   {"mix mono, left full", 0x520d6b, 2, 0, false},
        {"mix stereo", 0x520eae, 2, 0, true},              {"mix stereo full", 0x520f40, 2, 0, true},
        {"mix stereo left", 0x520fb1, 2, 0, true},         {"mix stereo right", 0x52102d, 2, 0, true},
        {"mix stereo, right full", 0x5210ac, 2, 0, true},  {"mix stereo, left full", 0x521133, 2, 0, true},
        {"mix to 16 bits", 0x521d00, 3, 0, false},
    };
    // Where things go in the area: the source, the destination, then the
    // routine's position, fraction and gains.
    const uint32_t src = area, dst = area + 0x4000, posAt = area + 0x8000, fracAt = posAt + 4, gains = posAt + 8;
    int failed = 0;
    for (const Routine& r : routines) {
        if (!b.cpu().nativeName(b.cpu().read32(r.address + 1) + r.address + 5) || b.cpu().read8(r.address) != 0xe9) {
            std::printf("%-24s not replaced (its code isn't the retail one)\n", r.name);
            ++failed;
            continue;
        }
        int mismatches = 0;
        for (int t = 0; t < trials && mismatches < 3; ++t) {
            // The inputs, the same in both.
            std::vector<uint32_t> source(0x1000), dest(0x1000);
            const double range = r.kind == 3 ? 40000.0 : t % 3 == 0 ? 1.0 : 32768.0;
            for (auto& v : source)
                v = std::bit_cast<uint32_t>(randomFloat(range));
            for (auto& v : dest)
                v = std::bit_cast<uint32_t>(randomFloat(32768.0));
            const int32_t count = int32_t(random() % 44) - 3;
            const uint32_t pos = uint32_t(random() % 100), frac = uint32_t(random()), stepInt = uint32_t(random() % 3),
                           stepFrac = uint32_t(random());
            const uint32_t high = t % 4 == 0 ? uint32_t(random() & 0xffff) : 0;
            const float gainLeft = randomFloat(1.5f), gainRight = randomFloat(1.5f);
            uint32_t regs[8];
            for (auto& v : regs)
                v = uint32_t(random());
            std::vector<uint32_t> args;
            switch (r.kind) {
            case 0:
            case 1: args = {uint32_t(count), src, dst, posAt, fracAt, stepInt, stepFrac}; break;
            case 2: args = {gains, uint32_t(count), src, dst}; break;
            default: args = {uint32_t(std::max(count, 0)), src, dst}; break;
            }
            std::vector<uint8_t> after[2];
            uint32_t regsAfter[2][8];
            std::string error[2];
            for (int i = 0; i < 2; ++i) {
                auto& c = hosts[i]->cpu();
                for (size_t k = 0; k < source.size(); ++k) {
                    c.write32(src + 4 * uint32_t(k), source[k]);
                    c.write32(dst + 4 * uint32_t(k), dest[k]);
                }
                c.write32(posAt, pos);
                c.write32(fracAt, frac);
                c.write32(gains, std::bit_cast<uint32_t>(gainLeft));
                c.write32(gains + 4, std::bit_cast<uint32_t>(gainRight));
                if (r.scratch)
                    c.write16(r.scratch + 4, uint16_t(high));
                for (int k = 0; k < 8; ++k)
                    c.reg[k] = regs[k];
                c.reg[reluva::x86::Cpu::ESP] = stack + (1 << 16) - 64;
                uint32_t result = 0;
                if (reluva::Error e = c.call(r.address, args, result))
                    error[i] = e.message;
                for (int k = 0; k < 8; ++k)
                    regsAfter[i][k] = c.reg[k];
                // Everything it may have written: the buffers, the state, its scratch.
                for (uint32_t o = 0; o < 0x8010; ++o)
                    after[i].push_back(c.read8(area + o));
                if (r.scratch)
                    for (uint32_t o = 0; o < 8; ++o)
                        after[i].push_back(c.read8(r.scratch + o));
            }
            std::string why;
            if (error[0] != error[1])
                why = "failed differently: '" + error[0] + "' and '" + error[1] + "'";
            else if (after[0] != after[1]) {
                size_t k = 0;
                while (after[0][k] == after[1][k])
                    ++k;
                why = "memory differs first at +" + std::to_string(k);
            } else
                for (int k = 0; k < 8 && why.empty(); ++k)
                    if (k != reluva::x86::Cpu::ESP && regsAfter[0][k] != regsAfter[1][k])
                        why = "register " + std::to_string(k) + " differs";
            if (!why.empty()) {
                ++mismatches;
                std::printf("%-24s trial %d (count %d): %s\n", r.name, t, count, why.c_str());
            }
        }
        std::printf("%-24s %s\n", r.name, mismatches ? "DIFFERS" : ("the same in " + std::to_string(trials) + " trials").c_str());
        failed += mismatches ? 1 : 0;
    }
    return failed ? 1 : 0;
}

// Draws one of the engine's viewer pages after the keys given, as the
// engine would, without a window: for checking pages and taking pictures
// of them.
int cmdNfsPage(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool nfs-page <game folder> <out.tga> <page> [key...]\n"
                             "  keys: left right up down pgup pgdn home end enter esc, or a letter or symbol;\n"
                             "  also +key and -key (held, let go), wait<ms>, click and move<dx>/<dy>\n"
                             "  (the Play page writes to nfs-page-save)\n");
        return 2;
    }
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameFiles files;
    files.scan(folders);
    auto pages = reluva::nfs::makePages("Need for Speed III: Hot Pursuit", folders, files, nullptr, "nfs-page-save");
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
    // The Play page's game starts at once, as it would when shown.
    if (auto* play = dynamic_cast<reluva::nfs::PlayPage*>(page))
        play->start();
    const std::pair<const char*, uint32_t> names[] = {
        {"left", reluva::key::Left},  {"right", reluva::key::Right},   {"up", reluva::key::Up},
        {"down", reluva::key::Down},  {"pgup", reluva::key::PageUp},   {"pgdn", reluva::key::PageDown},
        {"home", reluva::key::Home},  {"end", reluva::key::End},       {"enter", reluva::key::Return},
        {"esc", reluva::key::Escape}};
    int mouseX = 320, mouseY = 240;
    auto mouse = [&](reluva::EventType type) {
        reluva::Event ev;
        ev.type = type;
        ev.mouseX = mouseX;
        ev.mouseY = mouseY;
        ev.button = reluva::MouseButton::Left;
        page->handleEvent(ev);
    };
    mouse(reluva::EventType::MouseMove);
    for (int i = 5; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("wait", 0) == 0 && arg.size() > 4) { // wait<ms>: the page ticking meanwhile
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::atoi(argv[i] + 4));
            while (std::chrono::steady_clock::now() < until) {
                page->tick(0);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }
        if (arg == "click") {
            mouse(reluva::EventType::MouseDown);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            mouse(reluva::EventType::MouseUp);
            continue;
        }
        if (arg.rfind("move", 0) == 0 && arg.find('/') != std::string::npos) { // move<dx>/<dy>
            mouseX += std::atoi(arg.c_str() + 4);
            mouseY += std::atoi(arg.c_str() + arg.find('/') + 1);
            mouse(reluva::EventType::MouseMove);
            continue;
        }
        // +key and -key: pressed, let go; else both.
        const bool press = arg[0] != '-', release = arg[0] != '+';
        const std::string keyArg = arg.size() > 1 && (arg[0] == '+' || arg[0] == '-') ? arg.substr(1) : arg;
        uint32_t key = 0;
        for (const auto& [name, code] : names)
            if (keyArg == name)
                key = code;
        if (!key && keyArg.size() == 1)
            key = static_cast<unsigned char>(keyArg[0]);
        if (!key) {
            std::fprintf(stderr, "unknown key '%s'\n", argv[i]);
            return 2;
        }
        reluva::Event ev;
        ev.key = key;
        if (press) {
            ev.type = reluva::EventType::KeyDown;
            page->handleEvent(ev);
        }
        if (release) {
            ev.type = reluva::EventType::KeyUp;
            page->handleEvent(ev);
        }
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
    if (std::strcmp(cmd, "nfs-native-check") == 0)
        return cmdNfsNativeCheck(argc, argv);
    if (std::strcmp(cmd, "nfs-huff-check") == 0)
        return cmdNfsHuffCheck(argc, argv);
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
