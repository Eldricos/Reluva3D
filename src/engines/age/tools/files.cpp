// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Angel Game Engine commands for Midtown Madness 2's files:
// its .ar archives listed and extracted, and what a texture, a model or a
// city holds (docs/engines/age/). Listed by reluva-tool help age.

#include "common/archive.h"
#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/age/filesystem.h"
#include "engines/age/formats/ar_archive.h"
#include "engines/age/formats/pathset.h"
#include "engines/age/formats/pvs.h"
#include "engines/age/formats/instances.h"
#include "engines/age/formats/bai.h"
#include "engines/age/formats/pkg.h"
#include "engines/age/formats/psdl.h"
#include "engines/age/formats/texture.h"
#include "engines/age/render/city_mesh.h"
#include "image/image.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;

int cmdArList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool age ar-list <file.ar> [name filter]\n");
        return 2;
    }
    reluva::age::ArArchive ar;
    if (reluva::Error err = ar.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::string filter = argc > 3 ? argv[3] : "";
    std::vector<uint8_t> data;
    unsigned listed = 0;
    unsigned deflated = 0;
    unsigned failed = 0;
    uint64_t total = 0;
    for (const auto &e : ar.entries()) {
        if (e.name.find(filter) == std::string::npos)
            continue;
        // Reading each one checks it: deflated data must come to its size.
        if (reluva::Error err = ar.read(e, data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            ++failed;
        }
        std::printf("%10u %10u %10u%s %s\n", e.size, e.storedSize, e.offset, e.deflated() ? " d" : "  ",
                    e.name.c_str());
        ++listed;
        deflated += e.deflated();
        total += e.size;
    }
    std::printf("%u files (%u deflated), %llu bytes; %u failed to read\n", listed, deflated,
                static_cast<unsigned long long>(total), failed);
    return failed ? 1 : 0;
}

int cmdArExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool age ar-extract <file.ar> <output dir> [name filter]\n");
        return 2;
    }
    reluva::age::ArArchive ar;
    if (reluva::Error err = ar.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::filesystem::path outDir = argv[3];
    const std::string filter = argc > 4 ? argv[4] : "";
    std::vector<uint8_t> data;
    unsigned written = 0;
    for (const auto &e : ar.entries()) {
        if (e.name.find(filter) == std::string::npos)
            continue;
        // Names come from the archive: refuse anything that could escape outDir.
        const std::filesystem::path rel(e.name);
        bool safe = !rel.is_absolute() && !rel.has_root_name();
        for (const auto &part : rel)
            safe &= part != "..";
        if (!safe) {
            std::fprintf(stderr, "skipping unsafe name %s\n", e.name.c_str());
            continue;
        }
        if (reluva::Error err = ar.read(e, data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        const std::filesystem::path out = outDir / rel;
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary);
        f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
            return 1;
        }
        ++written;
    }
    std::printf("%u files written to %s\n", written, outDir.string().c_str());
    return 0;
}
// A game folder's search path, for the commands taking one.
static bool mountAge(const char *dir, reluva::SearchSet &files) {
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::age::mountGame(files, dir, warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    for (const std::string &w : warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    return true;
}

static bool hasSuffix(const std::string &s, const char *suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

int cmdTexInfo(int argc, char **argv) {
    using namespace reluva::age;
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool age tex-info <file.tex | game folder> [out.tga]\n");
        return 2;
    }
    if (std::filesystem::is_directory(argv[2])) {
        reluva::SearchSet files;
        if (!mountAge(argv[2], files))
            return 1;
        std::map<std::string, unsigned> byKind;
        unsigned failed = 0;
        unsigned total = 0;
        std::vector<uint8_t> data;
        for (const std::string &n : files.list()) {
            const std::string key = reluva::normalizeArchiveName(n);
            if (key.rfind("texture/", 0) != 0 || !hasSuffix(key, ".tex"))
                continue;
            ++total;
            Tex tex;
            reluva::Error err = files.read(n, data);
            if (!err)
                err = decodeTex(data, tex);
            if (err) {
                std::printf("%s: %s\n", n.c_str(), err.message.c_str());
                ++failed;
                continue;
            }
            char flags[16];
            std::snprintf(flags, sizeof flags, "0x%x", tex.flags);
            ++byKind[std::string(formatName(tex.format)) + ", flags " + flags];
        }
        for (const auto &[kind, count] : byKind)
            std::printf("%6u  %s\n", count, kind.c_str());
        std::printf("%u textures, %u failed\n", total, failed);
        return failed ? 1 : 0;
    }
    std::vector<uint8_t> data;
    std::ifstream in(argv[2], std::ios::binary);
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    Tex tex;
    if (reluva::Error err = decodeTex(data, tex)) {
        std::fprintf(stderr, "%s: %s\n", argv[2], err.message.c_str());
        return 1;
    }
    std::printf("%d x %d, %s (format %u), %u levels, flags 0x%x\n", tex.image.w, tex.image.h, formatName(tex.format),
                tex.format, tex.levels, tex.flags);
    if (argc > 3) {
        const auto tga = reluva::image::encodeTga(tex.image);
        std::ofstream(argv[3], std::ios::binary)
            .write(reinterpret_cast<const char *>(tga.data()), std::streamsize(tga.size()));
    }
    return 0;
}

int cmdPkgInfo(int argc, char **argv) {
    using namespace reluva::age;
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool age pkg-info <file.pkg | game folder> [out.obj]\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (std::filesystem::is_directory(argv[2])) {
        reluva::SearchSet files;
        if (!mountAge(argv[2], files))
            return 1;
        unsigned total = 0;
        unsigned failed = 0;
        unsigned meshes = 0;
        unsigned shaders = 0;
        unsigned untextured = 0;
        std::map<std::string, unsigned> missing;
        for (const std::string &n : files.list()) {
            const std::string key = reluva::normalizeArchiveName(n);
            if (key.rfind("geometry/", 0) != 0 || !hasSuffix(key, ".pkg"))
                continue;
            ++total;
            Pkg pkg;
            reluva::Error err = files.read(n, data);
            if (!err)
                err = readPkg(data, pkg);
            if (err) {
                std::printf("%s: %s\n", n.c_str(), err.message.c_str());
                ++failed;
                continue;
            }
            meshes += unsigned(pkg.meshes.size());
            for (const PkgShader &s : pkg.shaders) {
                ++shaders;
                if (s.texture.empty()) {
                    ++untextured;
                    continue;
                }
                const std::string t = "texture/" + s.texture;
                if (!files.has(t + ".tex") && !files.has(t + "-0001.tex") && !files.has(t + ".tga"))
                    ++missing[s.texture];
            }
        }
        for (const auto &[name, count] : missing)
            std::printf("texture not found: %s (%u shaders)\n", name.c_str(), count);
        std::printf("%u models, %u failed; %u meshes; %u shaders, %u without a texture, %zu textures not found\n",
                    total, failed, meshes, shaders, untextured, missing.size());
        return failed ? 1 : 0;
    }
    std::ifstream in(argv[2], std::ios::binary);
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    Pkg pkg;
    if (reluva::Error err = readPkg(data, pkg)) {
        std::fprintf(stderr, "%s: %s\n", argv[2], err.message.c_str());
        return 1;
    }
    for (const PkgMesh &m : pkg.meshes) {
        size_t verts = 0;
        size_t tris = 0;
        for (const PkgGroup &g : m.groups) {
            verts += g.vertices.size();
            tris += g.indices.size() / 3;
        }
        std::printf("%-20s fvf 0x%03x, %2zu groups, %5zu vertices, %5zu triangles\n", m.name.c_str(), m.fvf,
                    m.groups.size(), verts, tris);
    }
    std::printf("%u paint jobs of %u shaders\n", pkg.paintJobs, pkg.shadersPerJob);
    for (size_t i = 0; i < pkg.shaders.size(); ++i) {
        const PkgShader &s = pkg.shaders[i];
        std::printf("  %2zu.%-2zu %-28s diffuse %.2f %.2f %.2f %.2f, ambient %.2f %.2f %.2f, specular %.2f %.2f "
                    "%.2f, emissive %.2f %.2f %.2f, shininess %.2f\n",
                    i / std::max(pkg.shadersPerJob, 1u), i % std::max(pkg.shadersPerJob, 1u),
                    s.texture.empty() ? "(none)" : s.texture.c_str(), s.diffuse[0], s.diffuse[1], s.diffuse[2],
                    s.diffuse[3], s.ambient[0], s.ambient[1], s.ambient[2], s.specular[0], s.specular[1], s.specular[2],
                    s.emissive[0], s.emissive[1], s.emissive[2], s.shininess);
    }
    if (pkg.hasOffset)
        std::printf("offset %.3f %.3f %.3f\n", pkg.offset[0], pkg.offset[1], pkg.offset[2]);
    for (const PkgXref &x : pkg.xrefs)
        std::printf("placed %s at %.3f %.3f %.3f\n", x.name.c_str(), x.matrix[9], x.matrix[10], x.matrix[11]);
    if (argc > 3) {
        // Each mesh as an object, positions as stored (parts not placed).
        std::ofstream obj(argv[3]);
        size_t base = 1;
        for (const PkgMesh &m : pkg.meshes) {
            obj << "o " << m.name << "\n";
            for (const PkgGroup &g : m.groups) {
                for (const PkgVertex &v : g.vertices)
                    obj << "v " << v.pos[0] << " " << v.pos[1] << " " << v.pos[2] << "\nvt " << v.uv[0] << " "
                        << 1 - v.uv[1] << "\n";
                for (size_t i = 0; i + 2 < g.indices.size(); i += 3) {
                    obj << "f";
                    for (size_t k = 0; k < 3; ++k)
                        obj << " " << base + g.indices[i + k] << "/" << base + g.indices[i + k];
                    obj << "\n";
                }
                base += g.vertices.size();
            }
        }
    }
    return 0;
}
int cmdPsdlInfo(int argc, char **argv) {
    using namespace reluva::age;
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool age psdl-info <game folder> <city, e.g. sf> [out.obj]\n");
        return 2;
    }
    reluva::SearchSet files;
    if (!mountAge(argv[2], files))
        return 1;
    const std::string city = argv[3];
    std::vector<uint8_t> data;
    Psdl psdl;
    reluva::Error err = files.read("city/" + city + ".psdl", data);
    if (!err)
        err = readPsdl(data, psdl);
    if (err) {
        std::fprintf(stderr, "city/%s.psdl: %s\n", city.c_str(), err.message.c_str());
        return 1;
    }
    static const char *const kNames[] = {
        "road strip",   "sidewalk strip", "rectangle strip", "sliver",  "crosswalk", "road fan", "fan",
        "facade bound", "divided road",   "tunnel",          "texture", "facade",    "roof"};
    size_t counts[13] = {};
    size_t attributes = 0;
    for (const PsdlBlock &b : psdl.blocks)
        for (const PsdlAttribute &a : b.attributes) {
            ++counts[a.type];
            ++attributes;
        }
    std::printf("%zu vertices, %zu heights, %zu textures, %zu blocks (roads from block %u), %zu attributes, "
                "%zu roads\n",
                psdl.vertices.size(), psdl.heights.size(), psdl.textures.size(), psdl.blocks.size() - 1,
                psdl.firstRoadBlock, attributes, psdl.roads.size());
    for (size_t t = 0; t < 13; ++t)
        std::printf("  %6zu %s\n", counts[t], kNames[t]);
    std::printf("bounds %.1f %.1f %.1f to %.1f %.1f %.1f, centre %.1f %.1f %.1f, radius %.1f\n", psdl.min[0],
                psdl.min[1], psdl.min[2], psdl.max[0], psdl.max[1], psdl.max[2], psdl.center[0], psdl.center[1],
                psdl.center[2], psdl.radius);
    for (const std::string &t : psdl.textures) {
        const std::string base = "texture/" + t;
        if (!t.empty() && !files.has(base + ".tex") && !files.has(base + "-0001.tex") && !files.has(base + ".tga"))
            std::printf("texture not found: %s\n", t.c_str());
    }
    std::vector<Instance> instances;
    if (!files.read("city/" + city + ".inst", data)) {
        if (reluva::Error e = readInstances(data, instances))
            std::printf("city/%s.inst: %s\n", city.c_str(), e.message.c_str());
        size_t found = 0;
        for (const Instance &i : instances)
            found += files.has("geometry/" + i.model + ".pkg");
        std::printf("%zu placed models, %zu of them found\n", instances.size(), found);
    }
    CityMesh mesh;
    buildCityMesh(psdl, mesh);
    size_t triangles = 0;
    for (const CityMesh::Batch &b : mesh.batches)
        triangles += b.indices.size() / 3;
    std::printf("generated: %zu triangles in %zu textures\n", triangles, mesh.batches.size());
    if (argc > 4) {
        std::ofstream obj(argv[4]);
        for (const reluva::gfx::Vertex &v : mesh.vertices)
            obj << "v " << v.pos[0] << " " << v.pos[1] << " " << v.pos[2] << "\n";
        for (const CityMesh::Batch &b : mesh.batches) {
            obj << "g " << (b.texture.empty() ? "untextured" : b.texture) << "\n";
            for (size_t i = 0; i + 2 < b.indices.size(); i += 3)
                obj << "f " << b.indices[i] + 1 << " " << b.indices[i + 1] + 1 << " " << b.indices[i + 2] + 1 << "\n";
        }
    }
    return 0;
}

// Every texture, model, city, road map, placement list, path set and
// visibility file of an install read, by kind: how many there are and how
// many failed (each failure named). A file it can't read isn't an error
// here: the release's own odd files (an older model version, stray names)
// are kept in the game-files test's record, which a change shows up in.
int cmdCheck(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool age check <game folder>\n");
        return 2;
    }
    reluva::SearchSet files;
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::age::mountGame(files, argv[2], warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const std::string &w : warnings)
        std::printf("warning: %s\n", w.c_str());
    struct Kind {
        int read = 0;
        int failed = 0;
    };
    std::map<std::string, Kind> kinds;
    auto ends = [](const std::string &n, const char *ext) {
        const size_t e = std::strlen(ext);
        return n.size() > e && n.compare(n.size() - e, e, ext) == 0;
    };
    std::vector<std::string> names = files.list();
    std::sort(names.begin(), names.end());
    for (const std::string &name : names) {
        const std::string key = reluva::normalizeArchiveName(name);
        std::string kind;
        if (ends(key, ".tex"))
            kind = "textures";
        else if (ends(key, ".pkg"))
            kind = "models";
        else if (ends(key, ".psdl"))
            kind = "cities";
        else if (ends(key, ".bai"))
            kind = "road maps";
        else if (ends(key, ".inst"))
            kind = "placements";
        else if (ends(key, ".pathset"))
            kind = "path sets";
        else if (ends(key, ".cpvs"))
            kind = "visibility";
        else
            continue;
        std::vector<uint8_t> data;
        reluva::Error err = files.read(name, data);
        if (!err) {
            if (kind == "textures") {
                reluva::age::Tex tex;
                err = reluva::age::decodeTex(data, tex);
            } else if (kind == "models") {
                reluva::age::Pkg pkg;
                err = reluva::age::readPkg(data, pkg);
            } else if (kind == "cities") {
                reluva::age::Psdl psdl;
                err = reluva::age::readPsdl(data, psdl);
            } else if (kind == "road maps") {
                reluva::age::AiRoadMap bai;
                err = reluva::age::readBai(data, bai);
            } else if (kind == "placements") {
                std::vector<reluva::age::Instance> inst;
                err = reluva::age::readInstances(data, inst);
            } else if (kind == "path sets") {
                std::vector<reluva::age::PathsetPath> paths;
                err = reluva::age::readPathset(data, paths);
            } else {
                reluva::age::Pvs pvs;
                err = reluva::age::readCpvs(data, pvs);
            }
        }
        Kind &k = kinds[kind];
        ++k.read;
        if (err) {
            ++k.failed;
            std::printf("%s: %s\n", key.c_str(), err.message.c_str());
        }
    }
    int read = 0;
    int failed = 0;
    for (const auto &[kind, k] : kinds) {
        std::printf("%s: %d, %d failed\n", kind.c_str(), k.read, k.failed);
        read += k.read;
        failed += k.failed;
    }
    std::printf("files read: %d, %d failed\n", read, failed);
    return 0;
}

const Commands kCommands(
    "age",
    {
        {"ar-list", "<file.ar> [name filter]", "list a Midtown Madness 2 .ar archive's files", cmdArList},
        {"ar-extract", "<file.ar> <output dir> [name filter]", "extract a Midtown Madness 2 .ar archive's files",
         cmdArExtract},
        {"tex-info", "<file.tex | game folder> [out.tga]",
         "a Midtown Madness 2 texture's header, and the image as a TGA", cmdTexInfo},
        {"pkg-info", "<file.pkg | game folder> [out.obj]", "what a Midtown Madness 2 model holds, as an OBJ",
         cmdPkgInfo},
        {"check", "<game folder>",
         "read every texture, model, city, road map, placement list, path set and visibility "
         "file of a Midtown Madness 2 install",
         cmdCheck},
        {"psdl-info", "<game folder> <city> [out.obj]", "what a Midtown Madness 2 city's .psdl holds", cmdPsdlInfo},
    });

} // namespace
