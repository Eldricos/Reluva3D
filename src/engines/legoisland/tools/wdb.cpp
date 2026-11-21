// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Island commands for WORLD.WDB
// (docs/engines/legoisland/formats/wdb.md).

#include "common/fs.h"
#include "common/stream.h"
#include "devtools/tool.h"
#include "engines/legoisland/formats/wdb.h"
#include "image/image.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::legoisland;
using namespace reluva::tool;

bool openWdb(const char *path, WdbFile &wdb) {
    if (Error e = wdb.open(path)) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return false;
    }
    return true;
}

struct Counts {
    int rois = 0;
    int lods = 0;
    int meshes = 0;
    int triangles = 0;
};

void count(const WdbRoi &roi, Counts &c) {
    c.rois++;
    for (const WdbLod &lod : roi.lods) {
        c.lods++;
        for (const WdbMesh &m : lod.meshes) {
            c.meshes++;
            c.triangles += int(m.triangles.size() / 3);
        }
    }
    for (const WdbRoi &child : roi.children)
        count(child, c);
}

// The worlds, and every part and model read, as a check of the whole file.
int cmdWdbInfo(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland wdb-info <WORLD.WDB>\n");
        return 2;
    }
    WdbFile wdb;
    if (!openWdb(argv[2], wdb))
        return 1;
    for (const WdbWorld &w : wdb.worlds())
        std::printf("%-10s %3zu parts %4zu models\n", w.name.c_str(), w.parts.size(), w.models.size());
    std::printf("shared: %zu textures, a part of %zu ROIs and %zu textures\n", wdb.sharedTextures().size(),
                wdb.sharedPart().rois.size(), wdb.sharedPart().textures.size());

    Counts c;
    int textures = int(wdb.sharedTextures().size() + wdb.sharedPart().textures.size());
    int failed = 0;
    for (const WdbRoi &roi : wdb.sharedPart().rois)
        count(roi, c);
    const std::vector<WdbPartRef> parts = wdb.allParts();
    for (const WdbPartRef &ref : parts) {
        WdbPart part;
        if (Error e = wdb.readPart(ref, part)) {
            std::fprintf(stderr, "%s\n", e.message.c_str());
            failed++;
            continue;
        }
        for (const WdbRoi &roi : part.rois)
            count(roi, c);
        textures += int(part.textures.size());
    }
    const std::vector<WdbModelRef> models = wdb.allModels();
    for (const WdbModelRef &ref : models) {
        WdbModel model;
        if (Error e = wdb.readModel(ref, model)) {
            std::fprintf(stderr, "%s\n", e.message.c_str());
            failed++;
            continue;
        }
        count(model.roi, c);
        textures += int(model.textures.size());
    }
    std::printf("%zu parts and %zu models read: %d ROIs, %d LODs, %d meshes, %d triangles, %d textures", parts.size(),
                models.size(), c.rois, c.lods, c.meshes, c.triangles, textures);
    if (failed)
        std::printf(", %d failed", failed);
    std::printf("\n");
    return failed ? 1 : 0;
}

// Each world's parts and models, and where it puts each model.
int cmdWdbList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland wdb-list <WORLD.WDB>\n");
        return 2;
    }
    WdbFile wdb;
    if (!openWdb(argv[2], wdb))
        return 1;
    for (const WdbWorld &w : wdb.worlds()) {
        std::printf("%s\n", w.name.c_str());
        for (const WdbPartRef &p : w.parts)
            std::printf("  part  %-10s %8u bytes at %#x\n", p.name.c_str(), p.size, p.offset);
        for (const WdbModelRef &m : w.models)
            std::printf("  model %-10s %8u bytes at %#x  %s  at (%g, %g, %g) facing (%g, %g, %g) up (%g, %g, %g) %u\n",
                        m.name.c_str(), m.size, m.offset, m.presenter.empty() ? "-" : m.presenter.c_str(),
                        m.location[0], m.location[1], m.location[2], m.direction[0], m.direction[1], m.direction[2],
                        m.up[0], m.up[1], m.up[2], m.flag);
    }
    return 0;
}

void printLods(const std::vector<WdbLod> &lods, int depth) {
    for (size_t i = 0; i < lods.size(); i++) {
        const WdbLod &lod = lods[i];
        std::printf("%*sLOD %zu: %zu vertices, %zu normals, %zu texture coordinates%s\n", depth * 2, "", i,
                    lod.vertices.size(), lod.normals.size(), lod.texCoords.size(),
                    lod.flags ? (" flags " + std::to_string(lod.flags)).c_str() : "");
        for (const WdbMesh &m : lod.meshes)
            std::printf("%*s  %4zu triangles, %4zu corners  #%02x%02x%02x  %-6s %.2f %u %u %u  \"%s\" %s\n", depth * 2,
                        "", m.triangles.size() / 3, m.corners.size(), m.color[0], m.color[1], m.color[2],
                        m.shading ? "smooth" : "flat", m.transparency, m.unknown[0], m.unknown[1], m.unknown[2],
                        m.material.c_str(), m.texture.c_str());
    }
}

void printRoi(const WdbRoi &roi, int depth) {
    std::printf("%*sROI %s", depth * 2, "", roi.name.c_str());
    if (roi.usesPart)
        std::printf("  (part %s)", roi.partName().c_str());
    if (!roi.indirectName.empty())
        std::printf("  \"%s\"", roi.indirectName.c_str());
    if (roi.sphereRadius > 0)
        std::printf("  sphere (%g, %g, %g) r %g", roi.sphereCenter[0], roi.sphereCenter[1], roi.sphereCenter[2],
                    roi.sphereRadius);
    std::printf("\n");
    printLods(roi.lods, depth + 1);
    for (const WdbRoi &c : roi.children)
        printRoi(c, depth + 1);
}

void printKeys(const char *kind, const std::vector<WdbKey> &keys, int depth) {
    for (const WdbKey &k : keys)
        std::printf("%*s%s %6u ms flags %u: %g %g %g %g\n", depth * 2, "", kind, k.time, k.flags, k.value[0],
                    k.value[1], k.value[2], k.value[3]);
}

void printAnim(const WdbAnimNode &n, int depth) {
    std::printf("%*s%s\n", depth * 2, "", n.name.c_str());
    printKeys("move  ", n.translation, depth + 1);
    printKeys("turn  ", n.rotation, depth + 1);
    printKeys("scale ", n.scale, depth + 1);
    printKeys("show  ", n.morph, depth + 1);
    for (const WdbAnimNode &c : n.children)
        printAnim(c, depth + 1);
}

void printTextures(const std::vector<WdbTexture> &textures) {
    for (const WdbTexture &t : textures)
        for (const WdbImage &img : t.images)
            std::printf("texture %s %ux%u, %zu colours\n", t.name.c_str(), img.width, img.height,
                        img.palette.size() / 3);
}

// Finds a part or a model by name, in any case; "shared" is the shared part.
bool findByName(const WdbFile &wdb, const std::string &name, WdbPart &part, WdbModel &model, bool &isModel) {
    if (fs::toLower(name) == "shared") {
        part = wdb.sharedPart();
        isModel = false;
        return true;
    }
    for (const WdbPartRef &ref : wdb.allParts())
        if (fs::toLower(ref.name) == fs::toLower(name)) {
            isModel = false;
            if (Error e = wdb.readPart(ref, part)) {
                std::fprintf(stderr, "%s\n", e.message.c_str());
                return false;
            }
            return true;
        }
    for (const WdbModelRef &ref : wdb.allModels())
        if (fs::toLower(ref.name) == fs::toLower(name)) {
            isModel = true;
            if (Error e = wdb.readModel(ref, model)) {
                std::fprintf(stderr, "%s\n", e.message.c_str());
                return false;
            }
            return true;
        }
    std::fprintf(stderr, "No part or model named %s\n", name.c_str());
    return false;
}

// A part's or a model's ROIs, LODs and meshes, a model's animation, and their textures.
int cmdWdbShow(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool legoisland wdb-show <WORLD.WDB> <part or model|shared>\n");
        return 2;
    }
    WdbFile wdb;
    if (!openWdb(argv[2], wdb))
        return 1;
    WdbPart part;
    WdbModel model;
    bool isModel = false;
    if (!findByName(wdb, argv[3], part, model, isModel))
        return 1;
    if (isModel) {
        std::printf("version %u, animation header %u %u %u\n", model.version, model.animHeader[0], model.animHeader[1],
                    model.animHeader[2]);
        printAnim(model.animation, 0);
        printRoi(model.roi, 0);
        printTextures(model.textures);
    } else {
        for (const WdbRoi &roi : part.rois)
            printRoi(roi, 0);
        printTextures(part.textures);
    }
    return 0;
}

// Named after the part or model holding each, as several hold textures of the same name.
void writeTextures(const std::string &owner, const std::vector<WdbTexture> &textures, const std::filesystem::path &dir,
                   int &written, int &failed) {
    for (const WdbTexture &t : textures)
        for (size_t i = 0; i < t.images.size(); i++) {
            image::Image img;
            img.w = int(t.images[i].width);
            img.h = int(t.images[i].height);
            t.images[i].toRGBA(img.rgba);
            std::string name = t.name.starts_with('^') ? t.name.substr(1) : t.name;
            name = std::filesystem::path(name).stem().string();
            if (i > 0)
                name += "_" + std::to_string(i + 1);
            if (writeFile(dir / (owner + "_" + name + ".png"), image::encodePng(img)))
                written++;
            else
                failed++;
        }
}

// Every texture, as PNG files: <owner>_<name>.png, a `^` texture's second picture <owner>_<name>_2.png.
int cmdWdbTextures(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool legoisland wdb-textures <WORLD.WDB> <out folder>\n");
        return 2;
    }
    WdbFile wdb;
    if (!openWdb(argv[2], wdb))
        return 1;
    const std::filesystem::path out = argv[3];
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    int written = 0;
    int failed = 0;
    writeTextures("shared", wdb.sharedTextures(), out, written, failed);
    writeTextures("sharedpart", wdb.sharedPart().textures, out, written, failed);
    for (const WdbPartRef &ref : wdb.allParts()) {
        WdbPart part;
        if (!wdb.readPart(ref, part))
            writeTextures(ref.name, part.textures, out, written, failed);
    }
    for (const WdbModelRef &ref : wdb.allModels()) {
        WdbModel model;
        if (!wdb.readModel(ref, model))
            writeTextures(ref.name, model.textures, out, written, failed);
    }
    std::printf("%d textures written to %s", written, out.string().c_str());
    if (failed)
        std::printf(", %d failed", failed);
    std::printf("\n");
    return failed ? 1 : 0;
}

const Commands kCommands(
    "legoisland",
    {
        {"wdb-info", "<WORLD.WDB>", "the world database's worlds, and every part and model read, with counts",
         cmdWdbInfo},
        {"wdb-list", "<WORLD.WDB>", "each world's parts and models, and where it puts the models", cmdWdbList},
        {"wdb-show", "<WORLD.WDB> <part or model|shared>",
         "a part's or model's ROIs, LODs, meshes, animation and textures", cmdWdbShow},
        {"wdb-textures", "<WORLD.WDB> <out folder>", "every texture in it, as PNG files", cmdWdbTextures},
    });

} // namespace
