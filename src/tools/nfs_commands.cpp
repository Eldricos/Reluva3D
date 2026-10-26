// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tools/nfs_commands.h"

#include "common/image.h"
#include "engines/nfs/fsh.h"
#include "engines/nfs/viv.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    "  reluva-tool fsh-dump <file.fsh | file.qfs | file.viv name> <output dir>\n";

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
    return -1;
}
