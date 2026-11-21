// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Island commands for the SI files' 3D objects
// (docs/engines/legoisland/formats/obj.md).

#include "devtools/tool.h"
#include "engines/legoisland/formats/gph.h"
#include "engines/legoisland/formats/si.h"
#include "engines/legoisland/formats/wdb.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::legoisland;
using namespace reluva::tool;

int countNodes(const WdbAnimNode &n) {
    int c = 1;
    for (const WdbAnimNode &child : n.children)
        c += countNodes(child);
    return c;
}

int countRois(const WdbRoi &r) {
    int c = 1;
    for (const WdbRoi &child : r.children)
        c += countRois(child);
    return c;
}

// One 3D object read, and a line saying what it holds; false if it can't be read.
bool describe(const SiFile &si, const SiObject &o, std::string &line) {
    const std::string kind = objectKind(o);
    std::vector<SiPiece> pieces;
    if (Error e = si.readPieces(o.id, pieces)) {
        line = e.message;
        return false;
    }
    const std::vector<uint8_t> data = objectData(pieces);
    char buf[300];
    if (kind == "ani") {
        WdbAnimation a;
        if (Error e = parseAnimation(data, a)) {
            line = e.message;
            return false;
        }
        std::string actors;
        for (const WdbActor &actor : a.actors)
            actors += " " + actor.name + "(" + std::to_string(actor.kind) + ")";
        std::snprintf(buf, sizeof buf, "animation, %u ms, %d nodes, round (%g, %g, %g) r %g%s; actors:", a.duration,
                      countNodes(a.root), a.center[0], a.center[1], a.center[2], a.radius,
                      a.hasCamera ? ", moves the camera" : "");
        line = buf + actors;
    } else if (kind == "mod") {
        WdbModel m;
        if (Error e = parseModel(data, m)) {
            line = e.message;
            return false;
        }
        std::snprintf(buf, sizeof buf, "model %s, %d ROIs, %zu textures", m.roi.name.c_str(), countRois(m.roi),
                      m.textures.size());
        line = buf;
    } else if (kind == "tex") {
        std::vector<WdbTexture> textures;
        if (Error e = parseTextures(data, textures)) {
            line = e.message;
            return false;
        }
        line = "textures:";
        for (const WdbTexture &t : textures)
            for (const WdbImage &img : t.images)
                line += " " + t.name + " " + std::to_string(img.width) + "x" + std::to_string(img.height);
    } else if (kind == "gph") {
        GphFile g;
        if (Error e = g.parse(data)) {
            line = e.message;
            return false;
        }
        size_t triggers = 0;
        for (const GphFace &f : g.faces)
            triggers += f.triggers.size();
        std::snprintf(buf, sizeof buf, "path, %zu points, %zu edges, %zu faces, %zu triggers of %zu kinds",
                      g.points.size(), g.edges.size(), g.faces.size(), triggers, g.kinds.size());
        line = buf;
    } else {
        line = data.empty() ? "no data of its own: \"" + o.extra + "\"" : std::to_string(data.size()) + " bytes";
    }
    return true;
}

// Each 3D object in an SI file, read, with what it holds.
int cmdSiObjects(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland si-objects <file.si> [object id]\n");
        return 2;
    }
    SiFile si;
    if (Error e = si.open(argv[2])) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    const bool one = argc > 3;
    const uint32_t only = one ? uint32_t(std::strtoul(argv[3], nullptr, 0)) : 0;
    std::map<std::string, int> read;
    int failed = 0;
    for (const SiObject *o : si.all()) {
        if (o->format != "OBJ" || (one && o->id != only))
            continue;
        std::string line;
        const bool ok = describe(si, *o, line);
        std::printf("%5u %-24s %s%s\n", o->id, o->name.c_str(), ok ? "" : "FAILED: ", line.c_str());
        const std::string kind = objectKind(*o);
        read[kind.empty() ? "inline" : kind] += ok;
        failed += !ok;
    }
    std::printf("read:");
    for (const auto &[kind, n] : read)
        std::printf(" %d %s", n, kind.c_str());
    if (failed)
        std::printf(", %d failed", failed);
    std::printf("\n");
    return failed ? 1 : 0;
}

const Commands kCommands("legoisland",
                         {
                             {"si-objects", "<file.si> [object id]",
                              "an SI file's 3D objects (animations, models, textures, paths), read", cmdSiObjects},
                         });

} // namespace
