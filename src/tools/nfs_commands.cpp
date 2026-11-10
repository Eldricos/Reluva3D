// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tools/nfs_commands.h"

#include "backends/sockets/socket_net.h"
#include "common/audio/wav.h"
#include "common/events.h"
#include "common/fs.h"
#include "common/image.h"
#include "common/x86/pokes.h"
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
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
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
    "  reluva-tool mad-frames <file.mad> <output dir> [first frame] [count] [-game] [-display 1-3]\n"
    "  reluva-tool mad-wav <file.mad> <out.wav>\n"
    "  reluva-tool nfs-host <game folder> [-args \"nfs3.exe\"] [-renderer softtria.dll] [-limit N] [-log N] [-save dir]\n"
    "                       [-frames dir] [-every N] [-framesfrom N] [-watch address]... [-watchlimit 50] [-watchmem address size]\n"
    "                       [-watchread address | reg+offset]... (dwords printed at each -watch stop)\n"
    "                       [-slice instructions] [-dump memory.bin] [-keys ms:key,...]\n"
    "                       [-poke WHEN:ADDR:VALUE[:SIZE]]... [-pokeevery ...]... [-dumpmem WHEN:ADDR:SIZE:file]...\n"
    "                       (WHEN ms=N or at=ADDR[#N]; VALUE a number, size 1/2/4, or bytes=0a0b...)\n"
    "                       (keys: enter, up, a..., kp0-kp9, click, moveDX/DY) [-framelog frames.csv]\n"
    "                       [-timescale 0.1] [-thrashlog fromframe toframe] [-native] [-wav sound.wav] [-voices voices.csv]\n"
    "                       [-joystick name] (keys also joy:A=V...: axes 0-5 = V, button bB; +key/-key held)\n"
    "                       [-wheel] [-ff] (the joystick a wheel; with force feedback) [-forcelog forces.csv]\n"
    "                       [-net] (the network) [-netaddr a.b.c.d] [-broadcast a.b.c.d,...]\n"
    "                       [-netlog messages.txt] (each message sent and received, in hex)\n"
    "                       [-netdrop N[:fromMs[-toMs]]] (every Nth packet sent lost, over those game ms)\n"
    "                       [-serial listen:port | host:port] (COM ports: a link over TCP, with -net)\n"
    "                       [-modem listen:port | host:port] (a TAPI modem whose calls are such links)\n"
    "                       [-deterministic instructionsPerMs] (a clock of instructions; the end state hashed)\n"
    "                       [-mhz N] (the processor's clock the time stamp counter shows; 300 by default)\n"
    "                       [-mmx] (CPUID reports MMX: the game mixes its sound with its MMX routines)\n"
    "                       [-count ADDR] (how many times the instruction at ADDR ran; repeatable)\n"
    "                       [-drive CAR:TARGET:FROMMS[:TOMS[:GAP]]] (CAR, a human's, steered at TARGET from\n"
    "                       the controls; full gas, or GAP metres behind at its speed) [-drivesign -1]\n"
    "                       [-hotspots from to] (instructions counted by where they run, 16-byte blocks)\n"
    "                       [-gpulog] (THRASH recorded as for the GPU; each frame's list summed up)\n"
    "                       [-gpudump list dir] (that list in full: list.csv and its textures)\n"
    "                       [-present voodoo2a.dll] (the renderer the game is told of, -renderer's loaded in\n"
    "                       its place; the default; none: as -renderer)\n"
    "                       [-direct3d] (the game told of d3da.dll, the Direct3D renderer: its Direct3D look,\n"
    "                       drawn natively as d3da's device draws; -present d3da.dll -native)\n"
    "                       [-glide] (the game's own voodoo2a.dll, unchanged, on Reluva's Glide stand-in)\n"
    "                       [-glidecompare out.csv] (-glide, with each flip also drawn natively and compared)\n"
    "                       [-glidefilter point | bilinear] (with -glide: the texture filter held, to compare)\n"
    "                       [-glidegpu] (with -glide: Glide's pixels drawn on the GPU, if there is one)\n"
    "                       [-glidegpucompare out.csv] (-glide on the GPU, each call also drawn in software and\n"
    "                       each frame compared: its mean difference, of 255, per flip)\n"
    "                       [-glidedll file] (-glide with this THRASH DLL in voodoo2a.dll's place, from outside\n"
    "                       the install, e.g. the CD's Patch/Voodoo3/voodoo2a.dll: Glide 3 on a Voodoo3)\n"
    "                       [-glideboard voodoo1 | rush | voodoo2 | banshee | voodoo3] (-glide on this board,\n"
    "                       e.g. -glidedll <install>/voodooa.dll -glideboard voodoo1)\n"
    "  reluva-tool nfs-native-check <game folder> [trials]\n"
    "  reluva-tool nfs-mmx-check <game folder> [trials]\n"
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
    std::printf("line height %d, gap %d, pen %d,%d, flags %d, atlas %dx%d\n", font.lineHeight, font.lineGap,
                font.penX, font.penY, font.flags, font.atlasWidth, font.atlasHeight);
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
    // Each polygon's corners in the order the game queues them (frd.md,
    // Textures: a block's as stored, a placed object's 1, 0, 3, 2), corner
    // k with the texture's u, v k; written last to first, since with z
    // mirrored the game's front faces turn the other way (as the Tracks
    // page winds them), so OBJ's counter-clockwise front is the game's.
    auto emit = [&](const std::vector<reluva::nfs::Track::Vec3>& verts, const reluva::nfs::Track::Vec3& offset,
                    const std::vector<reluva::nfs::Track::Polygon>& polys, const char* group, bool placed) {
        out << "g " << group << "\n";
        for (const auto& v : verts)
            out << "v " << v[0] + offset[0] << ' ' << v[1] + offset[1] << ' ' << -(v[2] + offset[2]) << "\n";
        for (const auto& p : polys) {
            const auto& t = track.textures[p.texture];
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
        const auto& b = track.blocks[i];
        std::vector<reluva::nfs::Track::Polygon> polys = b.polygons[2 * detail];
        for (const auto& layer : b.objectLayers)
            for (const auto& o : layer)
                polys.insert(polys.end(), o.polygons.begin(), o.polygons.end());
        emit(b.vertices, {0, 0, 0}, polys, ("block" + std::to_string(i)).c_str(), false);
    }
    for (const auto& g : track.extraObjects)
        for (const auto& o : g)
            if (o.type == 4)
                emit(o.vertices, o.reference, o.polygons, "object", true);
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
        if (h.loopEnd)
            std::printf(" loop %u-%u", h.loopStart, h.loopEnd);
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
            std::printf("  part %d", sec.part);
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
        std::fprintf(stderr, "usage: reluva-tool mad-frames <file.mad> <output dir> [first frame] [count] [-game] [-display 1-3]\n");
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
    // As the Play page: the game told of a Voodoo 2, so that it draws its sky
    // and fog (docs/engines/nfs.md, Sky and fog).
    options.presentedRenderer = "voodoo2a.dll";
    int traceCount = 0, every = 50; // traceCount: the last instructions kept, for a failure (0: none)
    std::vector<uint32_t> watch; // addresses at which to print the registers
    std::vector<std::pair<uint32_t, uint64_t>> counted; // addresses whose runs are counted (-count)
    int watchLimit = 50;         // how many times, at most
    std::vector<std::pair<int, uint32_t>> watchReads; // dwords printed at each watch: register (-1 none) + offset
    std::string dumpFile;        // the emulated memory, written at the end
    uint32_t slice = 0;          // instructions between thread changes (0: the default)
    uint32_t watchMem = 0, watchMemSize = 0; // memory whose stores are printed
    std::string keyScript;                   // "ms:key,ms:key...": each pressed for 100 ms
    std::string frameLogFile;                // each frame's time and instructions, as CSV
    std::string wavFile;                     // what the game's sound buffers played
    std::string voicesFile; // what the game's mixer adds, front and behind, each 100 ms (CSV)
    uint64_t hotFrom = 0, hotTo = 0;         // instructions counted by where they run, over this range
    bool gpuLog = false;
    uint64_t gpuDumpList = 0; // a list (1 the first) written out in full
    std::string gpuDumpDir;
    std::string framesDir;
    std::string glideCompareFile; // each flip's Glide frame against the native one, as CSV
    std::string glideGpuCompareFile; // each Glide frame on the GPU against the software reference, as CSV
    uint64_t framesFrom = 0; // the first frame shown that -frames writes
    std::string forceLogFile; // the forces the game asks for, each time they change, as CSV
    reluva::x86::Pokes pokes; // memory written, or saved, at moments of the run
    // -drive: a human's car steered at another car, its controls written as
    // the game unpacks them (0x4314c9, after the steering and pedals): a
    // steady pair of hands for scripted chases.
    struct Driver {
        uint32_t car = 0, target = 0;
        uint64_t from = 0, to = ~0ull;
        float gap = 0; // 0: full gas; else metres behind the target, at its speed
        uint64_t logged = 0;
    };
    std::vector<Driver> drivers;
    float driveSign = 1; // the steering's sign (the car's right row against the keys)
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
        else if (arg == "-watchlimit" && a + 1 < argc)
            watchLimit = std::atoi(argv[++a]);
        else if (arg == "-watch" && a + 1 < argc)
            watch.push_back(uint32_t(std::strtoul(argv[++a], nullptr, 0)));
        else if (arg == "-drive" && a + 1 < argc) {
            Driver d;
            char* at = argv[++a];
            d.car = uint32_t(std::strtoul(at, &at, 0));
            if (*at == ':')
                d.target = uint32_t(std::strtoul(at + 1, &at, 0));
            if (*at == ':')
                d.from = std::strtoull(at + 1, &at, 10);
            if (*at == ':')
                d.to = std::strtoull(at + 1, &at, 10);
            if (*at == ':')
                d.gap = std::strtof(at + 1, &at);
            if (!d.car || !d.target)
                return fail(Error::fail("-drive: CAR:TARGET:FROMMS[:TOMS[:GAP]]"));
            drivers.push_back(d);
        } else if (arg == "-drivesign" && a + 1 < argc)
            driveSign = std::strtof(argv[++a], nullptr) < 0 ? -1.0f : 1.0f;
        else if (arg == "-count" && a + 1 < argc)
            counted.push_back({uint32_t(std::strtoul(argv[++a], nullptr, 0)), 0});
        else if (arg == "-watchread" && a + 1 < argc) { // ADDR, or reg+OFF / reg-OFF (eax ... edi)
            const std::string spec = lowerAscii(argv[++a]);
            static const char* const kRegs[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
            int r = -1;
            for (int i = 0; i < 8; ++i)
                if (spec.rfind(kRegs[i], 0) == 0)
                    r = i;
            uint32_t off = 0;
            if (r < 0)
                off = uint32_t(std::strtoul(spec.c_str(), nullptr, 0));
            else if (spec.size() > 4)
                off = spec[3] == '-' ? 0u - uint32_t(std::strtoul(spec.c_str() + 4, nullptr, 0))
                                     : uint32_t(std::strtoul(spec.c_str() + 4, nullptr, 0));
            watchReads.push_back({r, off});
        }
        else if (arg == "-thrashlog" && a + 2 < argc) {
            options.thrashLogFrom = std::strtoull(argv[++a], nullptr, 0);
            options.thrashLogTo = std::strtoull(argv[++a], nullptr, 0);
        } else if (arg == "-present" && a + 1 < argc) {
            options.presentedRenderer = argv[++a];
            if (options.presentedRenderer == "none")
                options.presentedRenderer.clear();
        } else if (arg == "-renderer" && a + 1 < argc) { // the game told of this one, and given it
            options.renderer = argv[++a];
            options.presentedRenderer.clear();
        } else if (arg == "-native") {
            options.nativeThrash = true;
        } else if (arg == "-direct3d") { // the game told of d3da.dll, drawn natively as d3da's device draws
            options.presentedRenderer = "d3da.dll";
            options.renderer = "softtria.dll";
            options.nativeThrash = true;
        } else if (arg == "-glide") { // the retail voodoo2a.dll on Reluva's glide2x.dll
            options.glide = true;
        } else if (arg == "-glidegpu") { // ... Glide's pixels drawn on the GPU
            options.glide = true;
            options.glideGpu = 1;
        } else if (arg == "-glidegpucompare" && a + 1 < argc) { // ... and in software beside it, compared
            options.glide = true;
            options.glideGpu = 2;
            glideGpuCompareFile = argv[++a];
        } else if (arg == "-glidedll" && a + 1 < argc) { // ... with this THRASH DLL file in voodoo2a.dll's place
            options.glide = true;
            options.glideDll = argv[++a];
        } else if (arg == "-glideboard" && a + 1 < argc) { // ... on this board (voodoo1, rush, voodoo2...)
            options.glide = true;
            options.glideBoard = argv[++a];
        } else if (arg == "-glidefilter" && a + 1 < argc) { // ... its texture filter held: point or bilinear
            options.glideFilter = std::string(argv[++a]) == "point" ? 0 : 1;
        } else if (arg == "-glidecompare" && a + 1 < argc) { // ... and the native drawing beside it
            options.glide = true;
            options.glideCompare = true;
            glideCompareFile = argv[++a];
        } else if (arg == "-deterministic" && a + 1 < argc) {
            options.instructionsPerMs = uint32_t(std::strtoul(argv[++a], nullptr, 0));
        } else if (arg == "-mmx") {
            options.mmx = true;
        } else if (arg == "-mhz" && a + 1 < argc) {
            options.cpuMhz = std::max(1u, uint32_t(std::strtoul(argv[++a], nullptr, 0)));
        } else if (arg == "-timescale" && a + 1 < argc)
            options.timeScale = std::atof(argv[++a]);
        else if (arg == "-framelog" && a + 1 < argc)
            frameLogFile = argv[++a];
        else if (arg == "-wav" && a + 1 < argc)
            wavFile = argv[++a];
        else if (arg == "-voices" && a + 1 < argc)
            voicesFile = argv[++a];
        else if (arg == "-hotspots" && a + 2 < argc) {
            hotFrom = std::strtoull(argv[++a], nullptr, 10);
            hotTo = std::strtoull(argv[++a], nullptr, 10);
        } else if (arg == "-gpulog") { // THRASH recorded as for the GPU, each frame's list summed up
            options.nativeThrash = true;
            options.gpuThrash = true;
            gpuLog = true;
        } else if (arg == "-gpudump" && a + 2 < argc) { // a list in full: its vertices, by batch, and textures
            options.nativeThrash = true;
            options.gpuThrash = true;
            gpuDumpList = std::strtoull(argv[++a], nullptr, 10);
            gpuDumpDir = argv[++a];
        } else if (arg == "-joystick" && a + 1 < argc)
            options.joystick = argv[++a];
        else if (arg == "-net")
            options.sockets = reluva::net::systemSockets();
        else if (arg == "-netlog" && a + 1 < argc)
            options.netLog = argv[++a];
        else if (arg == "-netdrop" && a + 1 < argc) { // N[:fromMs[-toMs]]
            char* end = nullptr;
            options.netDrop = uint32_t(std::strtoul(argv[++a], &end, 10));
            if (end && *end == ':')
                options.netDropFrom = uint32_t(std::strtoul(end + 1, &end, 10));
            if (end && *end == '-')
                options.netDropTo = uint32_t(std::strtoul(end + 1, nullptr, 10));
        } else if (arg == "-serial" && a + 1 < argc)
            options.serial = argv[++a];
        else if (arg == "-modem" && a + 1 < argc)
            options.modem = argv[++a];
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
        else if ((arg == "-poke" || arg == "-pokeevery" || arg == "-dumpmem") && a + 1 < argc) {
            reluva::x86::Poke p;
            std::string error;
            const bool ok = arg == "-dumpmem" ? reluva::x86::parseDump(argv[++a], p, error)
                                              : reluva::x86::parsePoke(argv[++a], arg == "-pokeevery", p, error);
            if (!ok)
                return fail(Error::fail(arg + ": " + error));
            pokes.list.push_back(std::move(p));
        }
        else if (arg == "-every" && a + 1 < argc)
            every = std::max(1, std::atoi(argv[++a]));
        else if (arg == "-framesfrom" && a + 1 < argc)
            framesFrom = std::strtoull(argv[++a], nullptr, 10);
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
    reluva::Surface lastShown; // -glidecompare: the Glide frame of the flip under way
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
        if (!glideCompareFile.empty())
            lastShown = frame;
        if (framesDir.empty() || shown++ % every || uint64_t(shown - 1) < framesFrom)
            return;
        reluva::image::Image img;
        img.w = frame.w;
        img.h = frame.h;
        img.rgba = frame.pixels;
        char name[32];
        std::snprintf(name, sizeof name, "frame%05d.tga", shown - 1);
        writeFile(stdfs::path(framesDir) / name, reluva::image::encodeTga(img));
    };
    // -glidegpucompare: per buffer swap, the GPU frame against the software
    // reference's.
    std::string gpuCompareCsv = "swap,mean,differing,over24,max\n";
    double gpuCompareSum = 0;
    uint64_t gpuCompared = 0;
    if (!glideGpuCompareFile.empty())
        host.onGlideComparison = [&](const reluva::glide::FrameComparison& f) {
            char row[160];
            std::snprintf(row, sizeof row, "%llu,%.4f,%llu,%llu,%d\n", (unsigned long long)f.swap, f.meanDifference,
                          (unsigned long long)f.pixelsDiffering, (unsigned long long)f.pixelsOver24, f.maxDifference);
            gpuCompareCsv += row;
            gpuCompareSum += f.meanDifference;
            ++gpuCompared;
        };
    // -glidecompare: per flip, the mean difference of each channel and the
    // pixels off by more than 24 in any; the native frame written beside
    // the Glide one when that one is.
    std::string compareCsv = "frame,dr,dg,db,over24\n";
    int compared = 0;
    if (!glideCompareFile.empty())
        host.onCompareFrame = [&](const reluva::Surface& native) {
            if (lastShown.w != native.w || lastShown.h != native.h)
                return;
            double d[3] = {0, 0, 0};
            int over = 0;
            for (int y = 0; y < native.h; ++y) {
                const uint8_t* a = lastShown.row(y);
                const uint8_t* b = native.row(y);
                for (int x = 0; x < native.w; ++x) {
                    int worst = 0;
                    for (int ch = 0; ch < 3; ++ch) {
                        const int e = std::abs(int(a[4 * x + ch]) - int(b[4 * x + ch]));
                        d[ch] += e;
                        worst = std::max(worst, e);
                    }
                    over += worst > 24;
                }
            }
            const double n = double(native.w) * native.h;
            char line[128];
            std::snprintf(line, sizeof line, "%d,%.3f,%.3f,%.3f,%d\n", compared, d[0] / n, d[1] / n, d[2] / n, over);
            compareCsv += line;
            ++compared;
            if (!framesDir.empty() && (shown - 1) % every == 0 && uint64_t(shown - 1) >= framesFrom) {
                reluva::image::Image img;
                img.w = native.w;
                img.h = native.h;
                img.rgba = native.pixels;
                char name[32];
                std::snprintf(name, sizeof name, "native%05d.tga", shown - 1);
                writeFile(stdfs::path(framesDir) / name, reluva::image::encodeTga(img));
            }
        };
    std::vector<uint32_t> recent(size_t(std::max(traceCount, 1)));
    size_t recentAt = 0;
    int watched = 0;
    // Every texture as it now is, for -gpudump.
    std::map<uint32_t, reluva::nfs::ThrashFrame::Texture> dumpTextures;
    uint64_t lists = 0;
    if (gpuLog || gpuDumpList)
        host.onThrashFrame = [&](reluva::nfs::ThrashFrame&& f) {
            if (f.resetTextures)
                dumpTextures.clear();
            for (auto& t : f.textures)
                dumpTextures[t.handle] = t;
            if (++lists == gpuDumpList) {
                // batch,texture,width,height,blend,mode,cull,fog,fogcolour then x,y,z,rhw,diffuse,u,v per vertex
                std::string csv = "batch,texture,width,height,blend,mode,cull,fog,fogcolour,x,y,z,rhw,diffuse,u,v\n";
                char line[256];
                std::set<uint32_t> used;
                for (size_t i = 0; i < f.batches.size(); ++i) {
                    const auto& b = f.batches[i];
                    auto t = dumpTextures.find(b.texture);
                    const int w = t == dumpTextures.end() ? 0 : t->second.width;
                    const int h = t == dumpTextures.end() ? 0 : t->second.height;
                    used.insert(b.texture);
                    for (uint32_t v = b.first; v < b.first + b.count; ++v) {
                        const auto& x = f.vertices[v];
                        std::snprintf(line, sizeof line, "%zu,%u,%d,%d,%d,%d,%d,%g,%06x,%g,%g,%g,%g,%08x,%g,%g\n", i,
                                      b.texture, w, h, int(b.blend), b.blendMode, b.cull, double(b.fog), b.fogColour,
                                      double(x.x), double(x.y), double(x.z), double(x.rhw), x.diffuse, double(x.u),
                                      double(x.v));
                        csv += line;
                    }
                }
                stdfs::create_directories(gpuDumpDir);
                writeFile(stdfs::path(gpuDumpDir) / "list.csv", std::vector<uint8_t>(csv.begin(), csv.end()));
                for (uint32_t handle : used) {
                    auto t = dumpTextures.find(handle);
                    if (t == dumpTextures.end() || t->second.rgba.empty())
                        continue;
                    reluva::image::Image img;
                    img.w = t->second.width;
                    img.h = t->second.height;
                    img.rgba.resize(t->second.rgba.size() * 4);
                    for (size_t i = 0; i < t->second.rgba.size(); ++i) {
                        const uint32_t c = t->second.rgba[i];
                        img.rgba[4 * i] = uint8_t(c >> 16);
                        img.rgba[4 * i + 1] = uint8_t(c >> 8);
                        img.rgba[4 * i + 2] = uint8_t(c);
                        img.rgba[4 * i + 3] = uint8_t(c >> 24);
                    }
                    writeFile(stdfs::path(gpuDumpDir) / ("texture" + std::to_string(handle) + ".tga"),
                              reluva::image::encodeTga(img));
                }
                std::printf("  list %llu written to %s\n", (unsigned long long)lists, gpuDumpDir.c_str());
            }
            if (!gpuLog)
                return;
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
    // Tracing every instruction costs: only when asked for.
    const bool hotspots = hotTo > hotFrom, tracePokes = pokes.needsTrace();
    if (hotspots || traceCount > 0 || !watch.empty() || tracePokes || !counted.empty() || !drivers.empty())
        host.cpu().trace = [&](reluva::x86::Cpu& c) {
            if (c.eip == 0x4314c9)
                for (auto& d : drivers) {
                    const uint64_t now = host.process().now();
                    if (c.reg[reluva::x86::Cpu::ECX] != d.car || now < d.from || now >= d.to)
                        continue;
                    auto f = [&](uint32_t a) { return std::bit_cast<float>(c.read32(a)); };
                    const float dx = f(d.target + 0x98) - f(d.car + 0x98), dz = f(d.target + 0xa0) - f(d.car + 0xa0);
                    const float across = dx * f(d.car + 0x190) + dz * f(d.car + 0x198);
                    const float along = dx * f(d.car + 0x1a8) + dz * f(d.car + 0x1b0);
                    const float angle = std::atan2(across, along), dist = std::sqrt(dx * dx + dz * dz);
                    auto speed = [&](uint32_t car) {
                        const float x = f(car + 0xa4), y = f(car + 0xa8), z = f(car + 0xac);
                        return std::sqrt(x * x + y * y + z * z);
                    };
                    const float v = speed(d.car), vt = speed(d.target);
                    const int steer = int(std::clamp(driveSign * angle / 0.35f * 127.0f, -128.0f, 127.0f));
                    int gas = 255, brake = 0;
                    if (d.gap > 0) {
                        const float wanted = vt + 0.5f * (dist - d.gap);
                        gas = v < wanted ? 255 : 0;
                        brake = v > wanted + 2 ? 255 : 0;
                    }
                    if (c.read8(d.car + 0x50b) == 1) // in neutral (a cop starts so): ask for first gear
                        c.reg[reluva::x86::Cpu::EAX] = (c.reg[reluva::x86::Cpu::EAX] & ~0xffu) | 2;
                    c.write32(d.car + 0x518, uint32_t(steer));
                    c.write8(d.car + 0x504, uint8_t(gas));
                    c.write8(d.car + 0x505, uint8_t(brake));
                    if (now >= d.logged + 500) {
                        d.logged = now;
                        std::printf("  drive %08x at %08x ms %llu: %.1f m, %.2f rad, %.1f against %.1f m/s, steer %d gas %d"
                                    " brake %d\n",
                                    d.car, d.target, (unsigned long long)now, dist, angle, v, vt, steer, gas, brake);
                    }
                }
            for (auto& [address, n] : counted)
                n += c.eip == address;
            if (hotspots && c.executed() >= hotFrom && c.executed() < hotTo)
                ++hot[c.eip & ~0xfu];
            if (tracePokes)
                pokes.instruction(c, host.process().now());
            recent[recentAt++ % recent.size()] = c.eip;
            if (!watch.empty() && watched < watchLimit && std::find(watch.begin(), watch.end(), c.eip) != watch.end()) {
                ++watched;
                std::printf("  at %08x: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x [esp] %08x"
                            " ms %llu\n",
                            c.eip, c.reg[0], c.reg[1], c.reg[2], c.reg[3], c.reg[4], c.reg[5], c.reg[6], c.reg[7],
                            c.valid(c.reg[4], 4) ? c.read32(c.reg[4]) : 0u, (unsigned long long)host.process().now());
                if (!watchReads.empty()) {
                    std::string line = "   read";
                    for (const auto& [r, off] : watchReads) {
                        const uint32_t at = (r < 0 ? 0u : c.reg[r]) + off;
                        char item[32];
                        std::snprintf(item, sizeof item, " %08x", c.valid(at, 4) ? c.read32(at) : 0u);
                        line += item;
                    }
                    std::printf("%s\n", line.c_str());
                }
            }
        };
    if (!pokes.empty()) {
        pokes.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
        pokes.save = [](const std::string& file, const std::vector<uint8_t>& bytes) { writeFile(file, bytes); };
        host.process().onSlice = [&](reluva::x86::Cpu& c) { pokes.time(c, host.process().now()); };
    }
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
    // The sound, by the game's clock (GameHost::recordSound).
    reluva::audio::Sound sound;
    sound.rate = 22050;
    sound.channels = 2;
    if (!wavFile.empty())
        host.recordSound(&sound.samples, sound.rate);
    std::vector<reluva::nfs::GameHost::VoiceEnergy> voices;
    if (!voicesFile.empty())
        host.logVoices(&voices, 100);
    const auto start = std::chrono::steady_clock::now();
    const Error err = host.run(exitCode);
    if (!glideCompareFile.empty())
        writeFile(glideCompareFile, std::vector<uint8_t>(compareCsv.begin(), compareCsv.end()));
    if (!glideGpuCompareFile.empty()) {
        writeFile(glideGpuCompareFile, std::vector<uint8_t>(gpuCompareCsv.begin(), gpuCompareCsv.end()));
        std::printf("glide on the GPU against the software reference: %llu frames, mean difference %.4f of 255\n",
                    (unsigned long long)gpuCompared, gpuCompared ? gpuCompareSum / double(gpuCompared) : 0.0);
    }
    if (auto* g = host.glide()) {
        std::string list;
        for (const auto& [name, count] : g->calls())
            list += (list.empty() ? "" : ", ") + name + " " + std::to_string(count);
        std::printf("%s: %s\n", g->dllName().c_str(), list.c_str());
        for (const auto& [name, counts] : g->modes()) {
            std::string line;
            for (const auto& [value, count] : counts)
                line += " " + std::to_string(value) + " x" + std::to_string(count);
            std::printf("  %s:%s\n", name.c_str(), line.c_str());
        }
        std::printf("  drawn by %s\n", host.glideRenderer().c_str());
    }
    if (!wavFile.empty()) {
        host.recordSound(nullptr, 0);
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
    if (!voicesFile.empty()) {
        host.logVoices(nullptr, 0);
        std::string csv = "ms,calls,rear calls,front L+R,front L-R,rear L+R,rear L-R\n";
        char line[200];
        for (const auto& v : voices) {
            std::snprintf(line, sizeof line, "%llu,%u,%u,%.4g,%.4g,%.4g,%.4g\n", (unsigned long long)v.ms, v.calls,
                          v.rearCalls, v.frontC, v.frontS, v.rearC, v.rearS);
            csv += line;
        }
        writeFile(voicesFile, std::vector<uint8_t>(csv.begin(), csv.end()));
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
    for (const auto& [address, n] : counted)
        std::printf("%08x ran %llu times\n", address, (unsigned long long)n);
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

// Runs the game's own sound mixer (iSND's, bnk.md) twice on the same
// voices: with its float routines and with its MMX ones, both in the
// interpreter, the game's code as it is. Starts the voices with
// iSNDmixstart (0x5000b8) and mixes with 0x500864, as the game does, after
// setting the mixer up (0x4fff18) with the MMX flag (0x5643b4) clear or
// set; then compares the outputs sample by sample.
int cmdNfsMmxCheck(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs-mmx-check <game folder> [trials]\n");
        return 2;
    }
    using reluva::x86::Cpu;
    const int trials = argc > 3 ? std::max(1, std::atoi(argv[3])) : 40;
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameHost::Options options;
    options.nativeRoutines = false;
    options.saveFolder = stdfs::temp_directory_path() / "reluva-mmx-check";
    reluva::nfs::GameHost floatHost(folders, options), mmxHost(folders, options);
    reluva::nfs::GameHost* hosts[2] = {&floatHost, &mmxHost};
    constexpr uint32_t kDataSize = 1 << 22, kOutSize = 1 << 16;
    uint32_t stack[2], data[2], out[2];
    for (int i = 0; i < 2; ++i) {
        if (reluva::Error e = hosts[i]->load())
            return fail(e);
        stack[i] = hosts[i]->process().memory().alloc(1 << 16);
        data[i] = hosts[i]->process().memory().alloc(kDataSize);
        out[i] = hosts[i]->process().memory().alloc(kOutSize);
        if (!stack[i] || !data[i] || !out[i])
            return fail(reluva::Error::fail("no memory"));
    }
    // The MMX routines' entries, counted as they run.
    const std::pair<uint32_t, const char*> watched[] = {
        {0x51f5dd, "mix 8-bit mono"},       {0x51f6c7, "mix 8-bit stereo"},       {0x51f7ad, "mix 16-bit mono"},
        {0x51f897, "mix 16-bit stereo"},    {0x5270f0, "resample 8-bit mono"},    {0x527170, "resample 8-bit stereo"},
        {0x51f9de, "8-bit resample edge"},  {0x527210, "resample 16-bit mono"},   {0x527290, "resample 16-bit stereo"},
        {0x52d1a0, "EA-XA mono"},           {0x520830, "mix down 8-bit mono"},    {0x520870, "mix down 8-bit stereo"},
        {0x520a10, "mix down 16-bit mono"}, {0x520920, "mix down 16-bit stereo"}, {0x5209a0, "bus clear"},
    };
    constexpr size_t kWatched = std::size(watched);
    uint64_t runs[kWatched] = {};
    mmxHost.cpu().trace = [&](Cpu& c) {
        if (c.eip < 0x51f5dd || c.eip > 0x52d1a0)
            return;
        for (size_t k = 0; k < kWatched; ++k)
            if (c.eip == watched[k].first)
                ++runs[k];
    };
    // A voice as iSNDplatformplay hands it over: the data as the game holds
    // it, its format (1 8-bit mono, 2 8-bit stereo, 4 16-bit mono, 8 16-bit
    // stereo), compression (0 PCM, 7 EA-XA), levels and 16.16 step.
    struct Voice {
        std::vector<uint8_t> bytes;
        uint32_t samples = 0;
        int format = 4, compression = 0, left = 0, right = 0;
        uint32_t step = 0x10000;
        int quality = 50;
    };
    // The mix of `voices` in `frames` frames of output format `outFormat`
    // (the same flags), by the float (0) or MMX (1) routines, as signed
    // values (8-bit outputs centred).
    auto render = [&](int i, const std::vector<Voice>& voices, int frames, int outFormat, std::vector<int>& result) {
        auto& c = hosts[i]->cpu();
        const uint32_t top = stack[i] + (1 << 16) - 64;
        uint32_t ignored = 0;
        c.write32(0x5643b4, uint32_t(i));
        c.reg[Cpu::ESP] = top;
        c.reg[Cpu::EAX] = uint32_t(outFormat);
        c.reg[Cpu::EDX] = 0x500036; // the voice-ended callback: a ret
        if (reluva::Error e = c.call(0x4fff18, {}, ignored))
            return e;
        uint32_t at = data[i];
        for (size_t k = 0; k < voices.size(); ++k) {
            const Voice& v = voices[k];
            if (at + v.bytes.size() > data[i] + kDataSize)
                return reluva::Error::fail("voices too long");
            for (size_t n = 0; n < v.bytes.size(); ++n)
                c.write8(at + uint32_t(n), v.bytes[n]);
            c.reg[Cpu::ESP] = top;
            c.reg[Cpu::EAX] = uint32_t(k);
            c.reg[Cpu::EDX] = uint32_t(v.format);
            c.reg[Cpu::EBX] = uint32_t(v.compression);
            c.reg[Cpu::ECX] = 1; // played once
            if (reluva::Error e = c.call(0x5000b8,
                                         {at, v.samples, 0xffffffffu, 0xffffffffu, uint32_t(v.left), uint32_t(v.right),
                                          v.step, uint32_t(v.quality), 0},
                                         ignored))
                return e;
            at += (uint32_t(v.bytes.size()) + 15) & ~15u;
        }
        const int perFrame = outFormat == 8 ? 4 : outFormat == 1 ? 1 : 2;
        if (uint32_t(frames * perFrame) > kOutSize)
            return reluva::Error::fail("too many frames");
        c.reg[Cpu::ESP] = top;
        c.reg[Cpu::EAX] = out[i];
        c.reg[Cpu::EDX] = uint32_t(frames);
        if (reluva::Error e = c.call(0x500864, {}, ignored))
            return e;
        result.clear();
        if (outFormat == 8 || outFormat == 4)
            for (int n = 0; n < frames * perFrame / 2; ++n)
                result.push_back(int16_t(c.read16(out[i] + 2 * uint32_t(n))));
        else
            for (int n = 0; n < frames * perFrame; ++n)
                result.push_back(int(c.read8(out[i] + uint32_t(n))) - 128);
        return reluva::Error::success();
    };
    // The differences of MMX (m) from float (f), over many samples.
    struct Diff {
        uint64_t n = 0, nonzero = 0, even = 0, nonzeroF = 0, evenF = 0, clipM = 0, clipF = 0;
        double sum = 0, sum2 = 0, sumG = 0, sumG2 = 0, mf = 0, ff = 0;
        int maxAbs = 0, limit = 32767;
        std::map<int, uint64_t> hist;
        void add(int m, int f) {
            ++n;
            const int d = m - f;
            const double g = m - f * 127.0 / 128.0; // with the gain taken out
            sum += d, sum2 += double(d) * d, sumG += g, sumG2 += g * g;
            maxAbs = std::max(maxAbs, std::abs(d));
            ++hist[std::clamp(int(std::lround(g)), -5, 5)];
            if (m && m != limit) // the top, 32767, is odd
                ++nonzero, even += (m & 1) == 0;
            if (f && f != limit)
                ++nonzeroF, evenF += (f & 1) == 0;
            clipM += m >= limit || m <= -limit - 1;
            clipF += f >= limit || f <= -limit - 1;
            if (std::abs(f) < limit - 1000 && std::abs(m) < limit - 1000)
                mf += double(m) * f, ff += double(f) * f;
        }
        void print(const char* name) const {
            if (!n) {
                std::printf("%-34s no samples\n", name);
                return;
            }
            std::printf("%-34s %7llu: m-f mean %+7.2f rms %7.2f max %5d; gain out: mean "
                        "%+6.2f rms %6.2f; "
                        "gain %.5f; even %5.1f%% (float %4.1f%%); clip %.2f%%/%.2f%%\n",
                        name, (unsigned long long)n, sum / n, std::sqrt(sum2 / n), maxAbs, sumG / n,
                        std::sqrt(sumG2 / n), ff > 0 ? mf / ff : 0.0, nonzero ? 100.0 * even / nonzero : 0.0,
                        nonzeroF ? 100.0 * evenF / nonzeroF : 0.0, 100.0 * clipM / n, 100.0 * clipF / n);
        }
        void printHist() const {
            std::printf("%36s", "m - f x 127/128, rounded:");
            for (const auto& [k, c] : hist)
                std::printf(" %s%d %.1f%%", k == -5 ? "<=" : k == 5 ? ">=" : "", k, 100.0 * c / n);
            std::printf("\n");
        }
    };
    int failures = 0;
    auto compare = [&](const std::vector<Voice>& voices, int frames, int outFormat, Diff& diff,
                       std::vector<int>* floatOut = nullptr, std::vector<int>* mmxOut = nullptr) {
        std::vector<int> r[2];
        for (int i = 0; i < 2; ++i)
            if (reluva::Error e = render(i, voices, frames, outFormat, r[i])) {
                if (failures++ < 5)
                    std::printf("%s mixer failed: %s\n", i ? "MMX" : "float", e.message.c_str());
                return;
            }
        diff.limit = outFormat & 3 ? 127 : 32767;
        for (size_t k = 0; k < r[0].size(); ++k)
            diff.add(r[1][k], r[0][k]);
        if (floatOut)
            *floatOut = r[0];
        if (mmxOut)
            *mmxOut = r[1];
    };

    uint64_t seed = 0x9e3779b97f4a7c15ull;
    auto random = [&seed] {
        seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17;
        return seed;
    };
    // The data of a voice from 16-bit samples (interleaved when stereo):
    // as 16-bit or as signed 8-bit (the high byte).
    auto pcm = [](const std::vector<int16_t>& s, int channels, bool eightBit) {
        Voice v;
        v.format = eightBit ? (channels == 2 ? 2 : 1) : (channels == 2 ? 8 : 4);
        v.samples = uint32_t(s.size() / size_t(channels));
        for (int16_t x : s) {
            if (eightBit)
                v.bytes.push_back(uint8_t(x >> 8));
            else
                v.bytes.push_back(uint8_t(x)), v.bytes.push_back(uint8_t(uint16_t(x) >> 8));
        }
        return v;
    };
    const int frames = 4096;

    // The real sounds: Gen.bnk 29, 32, 41 (PCM), 33, 35 (EA-XA) and the
    // Corvette's car.bnk 0, 1 (stereo PCM, their left channel).
    struct Real {
        std::string name;
        std::vector<int16_t> samples;
        std::vector<uint8_t> eaxa; // the data as in the bank, when EA-XA
        uint32_t count = 0;
    };
    std::vector<Real> real;
    auto addBank = [&](const std::vector<uint8_t>& bankData, const std::string& name,
                       std::initializer_list<int> slots) {
        reluva::nfs::SoundBank bank;
        if (reluva::nfs::parseSoundBank(bankData, bank))
            return;
        for (int s : slots) {
            if (s >= int(bank.slots.size()) || !bank.slots[size_t(s)])
                continue;
            const auto& h = *bank.slots[size_t(s)];
            reluva::audio::Sound sound;
            if (reluva::nfs::decodeBankSound(bankData, h, sound) || sound.samples.empty())
                continue;
            Real r{name + " " + std::to_string(s), {}, {}, h.samples};
            for (size_t k = 0; k < sound.samples.size(); k += size_t(sound.channels)) // the left
                r.samples.push_back(sound.samples[k]);
            if (h.compression == reluva::nfs::SoundHeader::kEaXa)
                r.eaxa.assign(bankData.begin() + h.dataOffset, bankData.end());
            real.push_back(std::move(r));
        }
    };
    std::vector<uint8_t> bankData;
    if (auto p = reluva::fs::findFile(folders.install, "GameData/Audio/SFX/gen.bnk"); p && readFile(*p, bankData))
        addBank(bankData, "Gen.bnk", {29, 32, 41, 33, 35});
    if (auto p = reluva::fs::findFile(folders.install, "GameData/CarModel/Corv/car.viv")) {
        VivArchive viv;
        if (!viv.open(p->string()) && !viv.read("car.bnk", bankData))
            addBank(bankData, "Corv car.bnk", {0, 1});
    }
    if (real.empty())
        return fail(reluva::Error::fail("no Gen.bnk or Corvette car.bnk in the install"));
    std::printf("sounds:");
    for (const Real& r : real)
        std::printf(" %s (%zu samples%s)", r.name.c_str(), r.samples.size(), r.eaxa.empty() ? "" : ", EA-XA");
    std::printf("\n");
    // `count` samples of a real sound from `offset`, repeated as needed,
    // scaled by `scale` (saturated).
    auto realSamples = [&](size_t which, size_t offset, size_t count, double scale = 1.0) {
        const auto& s = real[which % real.size()].samples;
        std::vector<int16_t> v(count);
        for (size_t k = 0; k < count; ++k)
            v[k] = int16_t(std::clamp(std::lround(s[(offset + k) % s.size()] * scale), -32768l, 32767l));
        return v;
    };
    auto need = [&](uint32_t step) { return size_t(uint64_t(frames + 64) * step >> 16) + 64; };

    // The float path's 8-bit samples (its table at 0xa0c2a0, after the set-up).
    {
        std::vector<int> ignored;
        if (reluva::Error e = render(0, {}, 256, 8, ignored))
            return fail(e);
        auto& c = floatHost.cpu();
        auto f = [&](int k) { return std::bit_cast<float>(c.read32(0xa0c2a0 + 4 * uint32_t(k))); };
        std::printf("float 8-bit table: 0x00 %g, 0x7f %g, 0x80 %g, 0xff %g\n", f(0), f(0x7f), f(0x80), f(0xff));
    }

    // 1. One real voice at the output's rate, by level.
    std::printf("\none real voice, step 1, 16-bit mono PCM, by level (L = R):\n");
    for (int level : {127, 100, 64, 32, 8, -19}) {
        Diff d;
        for (size_t s = 0; s < real.size(); ++s) {
            Voice v = pcm(realSamples(s, 0, need(0x10000)), 1, false);
            v.left = v.right = level;
            compare({v}, frames, 8, d);
        }
        d.print(("  L " + std::to_string(level)).c_str());
    }
    // 2. Several real voices into one bus, at L = 32, at the output's rate
    // and pitched.
    for (uint32_t step : {0x10000u, 0x9a00u, 0x16000u}) {
        std::printf("\nreal voices at L = 32, step %.3f:\n", step / 65536.0);
        for (int count : {1, 2, 3, 5}) {
            Diff d;
            for (int t = 0; t < std::max(1, trials / 8); ++t) {
                std::vector<Voice> voices;
                for (int k = 0; k < count; ++k) {
                    Voice v = pcm(realSamples(size_t(k + t), size_t(random() % 20000), need(step)), 1, false);
                    v.left = v.right = 32;
                    v.step = step;
                    voices.push_back(v);
                }
                compare(voices, frames, 8, d);
            }
            d.print(("  " + std::to_string(count) + " voices").c_str());
            if (count == 5 && step == 0x10000)
                d.printHist();
        }
    }
    // 3. Pans and pitches at random, real voices, 1-6 of them, mono and
    // stereo sources.
    {
        Diff d;
        for (int t = 0; t < trials; ++t) {
            std::vector<Voice> voices;
            const int count = 1 + int(random() % 6);
            for (int k = 0; k < count; ++k) {
                const uint32_t step = random() % 4 == 0 ? 0x10000 : 0x4000 + uint32_t(random() % 0x1c000);
                const bool stereo = random() % 3 == 0;
                std::vector<int16_t> s = realSamples(random(), random() % 20000, need(step) * (stereo ? 2 : 1));
                Voice v = pcm(s, stereo ? 2 : 1, false);
                const int volume = int(random() % 128), pan = int(random() % 128);
                v.left = volume * (127 - pan) / 127, v.right = volume * pan / 127;
                v.step = step;
                voices.push_back(v);
            }
            compare(voices, frames, 8, d);
        }
        std::printf("\n");
        d.print("random pans and pitches, real");
        d.printHist();
    }
    // 4. Noise at full scale, random levels (some negative), formats and pitches.
    {
        Diff d;
        for (int t = 0; t < trials; ++t) {
            std::vector<Voice> voices;
            const int count = 1 + int(random() % 4);
            for (int k = 0; k < count; ++k) {
                const uint32_t step = random() % 4 == 0 ? 0x10000 : 0x2000 + uint32_t(random() % 0x7e000);
                const bool stereo = random() % 2 == 0;
                std::vector<int16_t> s(need(step) * (stereo ? 2 : 1));
                for (auto& x : s)
                    x = int16_t(random());
                Voice v = pcm(s, stereo ? 2 : 1, false);
                v.left = int(random() % 256) - 128, v.right = int(random() % 256) - 128;
                v.step = step;
                voices.push_back(v);
            }
            compare(voices, frames, 8, d);
        }
        d.print("random noise, levels, pitches");
    }
    // 5. EA-XA, as in the bank: the decoders too (0x52d1a0 with MMX). Each
    // path's EA-XA mix is also set against its mix of the samples decoded
    // by Reluva (audio::decodeEaXa), as 16-bit PCM.
    {
        Diff d, ownFloat, ownMmx;
        for (size_t s = 0; s < real.size(); ++s) {
            if (real[s].eaxa.empty())
                continue;
            Voice v;
            v.bytes = real[s].eaxa;
            if (v.bytes.size() > kDataSize / 2)
                v.bytes.resize(kDataSize / 2);
            v.samples = real[s].count;
            v.compression = 7;
            v.left = v.right = 127;
            const int n = std::min<int>(frames, int(real[s].count) - 64) & ~255;
            if (n <= 0)
                continue;
            std::vector<int> f, m, pf, pm;
            compare({v}, n, 8, d, &f, &m);
            Voice p = pcm(realSamples(s, 0, size_t(n) + 64), 1, false);
            p.left = p.right = 127;
            Diff ignored;
            compare({p}, n, 8, ignored, &pf, &pm);
            for (size_t k = 0; k < f.size() && k < pf.size(); ++k)
                ownFloat.add(f[k], pf[k]), ownMmx.add(m[k], pm[k]);
        }
        std::printf("\n");
        d.print("EA-XA, L 127, step 1");
        ownFloat.print("  float: EA-XA less PCM");
        ownMmx.print("  MMX: EA-XA less PCM");
    }
    // 6. 8-bit sources (no retail sound is 8-bit): the 8-bit MMX mixers and
    // resamplers.
    std::printf("\n8-bit sources (the high bytes of the real sounds):\n");
    for (int stereo = 0; stereo < 2; ++stereo)
        for (uint32_t step : {0x10000u, 0x9a00u, 0x16000u}) {
            Diff d;
            for (int t = 0; t < std::max(1, trials / 8); ++t) {
                std::vector<Voice> voices;
                for (int k = 0; k < 1 + t % 3; ++k) {
                    Voice v = pcm(realSamples(size_t(k + t), random() % 20000, need(step) * (stereo ? 2 : 1)),
                                  stereo ? 2 : 1, true);
                    v.left = 32 + 30 * k, v.right = 90 - 20 * k;
                    v.step = step;
                    voices.push_back(v);
                }
                compare(voices, frames, 8, d);
            }
            char name[64];
            std::snprintf(name, sizeof name, "  %s, step %.3f", stereo ? "stereo" : "mono", step / 65536.0);
            d.print(name);
        }
    for (int level : {127, 64, 32}) {
        Diff d;
        for (size_t s = 0; s < real.size(); ++s) {
            Voice v = pcm(realSamples(s, 0, need(0x10000)), 1, true);
            v.left = v.right = level;
            compare({v}, frames, 8, d);
        }
        d.print(("  one mono voice, step 1, L " + std::to_string(level)).c_str());
    }
    // 7. The other output formats (the game asks for 16-bit stereo).
    std::printf("\noutput formats, real voices at random levels and pitches:\n");
    for (int outFormat : {1, 2, 4, 8}) {
        Diff d;
        for (int t = 0; t < std::max(1, trials / 4); ++t) {
            std::vector<Voice> voices;
            for (int k = 0; k < 1 + t % 3; ++k) {
                const uint32_t step = 0x8000 + uint32_t(random() % 0x10000);
                Voice v = pcm(realSamples(size_t(k + t), random() % 20000, need(step)), 1, false);
                v.left = int(random() % 128), v.right = int(random() % 128);
                v.step = step;
                voices.push_back(v);
            }
            compare(voices, frames, outFormat, d);
        }
        static const char* const names[9] = {"", "  8-bit mono",   "  8-bit stereo", "", "  16-bit mono", "", "",
                                             "", "  16-bit stereo"};
        d.print(names[outFormat]);
    }
    // 8. The bus saturating: a voice made loud (x 4), three copies of it,
    // then two negated; and the other way round.
    std::printf("\nloud voices (a real one x 4, L = 127, step 1):\n");
    {
        size_t loudest = 0;
        for (size_t s = 0; s < real.size(); ++s)
            if (real[s].samples.size() > real[loudest].samples.size())
                loudest = s;
        const std::vector<int16_t> loud = realSamples(loudest, 0, need(0x10000), 4.0);
        std::vector<int16_t> negated(loud.size());
        for (size_t k = 0; k < loud.size(); ++k)
            negated[k] = int16_t(std::min(32767, -int(loud[k])));
        for (int order = 0; order < 2; ++order) {
            std::vector<Voice> voices;
            for (int k = 0; k < 5; ++k) {
                const bool neg = order == 0 ? k >= 3 : k < 2;
                Voice v = pcm(neg ? negated : loud, 1, false);
                v.left = v.right = 127;
                voices.push_back(v);
            }
            Diff d;
            std::vector<int> f, m;
            compare(voices, frames, 8, d, &f, &m);
            d.print(order == 0 ? "  3 loud, then 2 negated" : "  2 negated, then 3 loud");
            double rms[2] = {};
            for (size_t k = 0; k < f.size() && k < m.size(); ++k)
                rms[0] += double(f[k]) * f[k], rms[1] += double(m[k]) * m[k];
            if (!f.empty())
                std::printf("%36s float %.0f, MMX %.0f\n", "output rms:", std::sqrt(rms[0] / double(f.size())),
                            std::sqrt(rms[1] / double(f.size())));
        }
    }
    // 9. The MMX mix down by itself (0x520920) on random bus words.
    {
        auto& c = mmxHost.cpu();
        c.write32(0x5643b4, 1);
        int bad = 0;
        for (int t = 0; t < 8; ++t) {
            std::vector<int16_t> words(512);
            for (size_t k = 0; k < words.size(); ++k) {
                words[k] = int16_t(random());
                c.write16(data[1] + 2 * uint32_t(k), uint16_t(words[k]));
            }
            c.reg[Cpu::ESP] = stack[1] + (1 << 16) - 64;
            uint32_t ignored = 0;
            if (reluva::Error e = c.call(0x520920, {256, data[1], out[1]}, ignored))
                return fail(e);
            for (size_t k = 0; k < words.size(); ++k)
                bad += int16_t(c.read16(out[1] + 2 * uint32_t(k))) != std::clamp(2 * int(words[k]), -32768, 32767);
        }
        std::printf("\nMMX mix down 0x520920 on 4096 random words: %s\n",
                    bad ? (std::to_string(bad) + " not 2 x the word, saturated").c_str()
                        : "each 2 x the word, saturated");
    }
    std::printf("\nMMX routines run:");
    for (size_t k = 0; k < kWatched; ++k)
        std::printf("%s %s (%06x) %llu", k ? "," : "", watched[k].second, watched[k].first,
                    (unsigned long long)runs[k]);
    std::printf("\n");
    return failures ? 1 : 0;
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
    if (std::strcmp(cmd, "nfs-mmx-check") == 0)
        return cmdNfsMmxCheck(argc, argv);
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
