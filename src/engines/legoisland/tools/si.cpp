// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Island commands for SI files
// (docs/engines/legoisland/formats/si.md).

#include "common/stream.h"
#include "devtools/tool.h"
#include "engines/legoisland/formats/si.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

using namespace reluva;
using namespace reluva::legoisland;
using namespace reluva::tool;

bool openSi(const char *path, SiFile &si) {
    if (Error e = si.open(path)) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return false;
    }
    return true;
}

const char *typeName(uint16_t type) {
    switch (SiType(type)) {
    case SiType::kAnimation:
        return "anim";
    case SiType::kSound:
        return "sound";
    case SiType::kSerial:
        return "serial";
    case SiType::kParallel:
        return "parallel";
    case SiType::kEvent:
        return "event";
    case SiType::kSelect:
        return "select";
    case SiType::kStill:
        return "still";
    case SiType::kObject:
        return "object";
    }
    return "?";
}

void printObject(const SiObject &o, int depth) {
    std::printf("%*s%5u %-8s %s", depth * 2, "", o.id, typeName(o.type), o.name.c_str());
    if (!o.presenter.empty())
        std::printf("  [%s]", o.presenter.c_str());
    if (o.isMedia())
        std::printf("  %s %s", o.format.c_str(), o.fileName.c_str());
    if (o.location != std::array<double, 3>{})
        std::printf("  at (%g, %g, %g)", o.location[0], o.location[1], o.location[2]);
    if (o.type == uint16_t(SiType::kSelect)) {
        std::printf("  by %s:", o.variable.c_str());
        for (const std::string &c : o.choices)
            std::printf(" %s", c.c_str());
    }
    if (!o.extra.empty())
        std::printf("  \"%s\"", o.extra.c_str());
    std::printf("\n");
    for (const SiObject &c : o.children)
        printObject(c, depth + 1);
}

// The header, and how many objects and chunks of each kind.
int cmdSiInfo(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland si-info <file.si>\n");
        return 2;
    }
    SiFile si;
    if (!openSi(argv[2], si))
        return 1;
    std::printf("version %u.%u, buffers of %u bytes, %u of them\n", si.majorVersion(), si.minorVersion(),
                si.bufferSize(), si.bufferCount());
    int streams = 0;
    for (uint32_t offset : si.streamOffsets())
        streams += offset != 0;
    const std::vector<const SiObject *> all = si.all();
    int media = 0;
    for (const SiObject *o : all)
        media += o->isMedia();
    std::printf("%d streams, %zu objects (%d media), %zu chunks\n", streams, all.size(), media, si.chunks().size());
    return 0;
}

// Every object, as a tree.
int cmdSiList(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland si-list <file.si>\n");
        return 2;
    }
    SiFile si;
    if (!openSi(argv[2], si))
        return 1;
    for (const SiObject &o : si.objects())
        printObject(o, 0);
    return 0;
}

// One object's chunks: when each plays, and how big it is.
int cmdSiChunks(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool legoisland si-chunks <file.si> <object id>\n");
        return 2;
    }
    SiFile si;
    if (!openSi(argv[2], si))
        return 1;
    const uint32_t id = uint32_t(std::strtoul(argv[3], nullptr, 0));
    for (const SiChunk &c : si.chunks())
        if (c.objectId == id)
            std::printf("%10llu  flags %#06x  time %7d  %7u bytes (%u of the piece left)\n",
                        static_cast<unsigned long long>(c.dataOffset), c.flags, c.time, c.dataSize, c.remaining);
    return 0;
}

// Each medium as the file it was made from, named after the object.
int cmdSiExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool legoisland si-extract <file.si> <out folder> [object id]\n");
        return 2;
    }
    SiFile si;
    if (!openSi(argv[2], si))
        return 1;
    const std::filesystem::path outDir = argv[3];
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    const bool one = argc > 4;
    const uint32_t only = one ? uint32_t(std::strtoul(argv[4], nullptr, 0)) : 0;
    int written = 0;
    int failed = 0;
    for (const SiObject *o : si.all()) {
        if (!o->isMedia() || (one && o->id != only))
            continue;
        std::vector<SiPiece> pieces;
        std::vector<uint8_t> file;
        std::string ext = mediaExtension(*o);
        Error e = si.readPieces(o->id, pieces);
        if (!e && !ext.empty())
            e = rebuildMediaFile(*o, pieces, file);
        if (!e && ext.empty() && !objectKind(*o).empty()) {
            ext = objectKind(*o);
            file = objectData(pieces);
        } else if (!e && ext.empty()) {
            // Not a file of its own (an event, an inline 3D object): its
            // pieces as they are, one after another.
            ext = "bin";
            for (const SiPiece &p : pieces)
                file.insert(file.end(), p.data.begin(), p.data.end());
        }
        const std::filesystem::path out = outDir / (std::to_string(o->id) + "_" + o->name + "." + ext);
        if (e || !writeFile(out, file)) {
            std::fprintf(stderr, "%u %s: %s\n", o->id, o->name.c_str(), e ? e.message.c_str() : "can't write");
            failed++;
            continue;
        }
        written++;
    }
    std::printf("%d files written to %s", written, outDir.string().c_str());
    if (failed)
        std::printf(", %d failed", failed);
    std::printf("\n");
    return failed ? 1 : 0;
}

// Each event: its setting's name and value.
int cmdSiEvents(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoisland si-events <file.si>\n");
        return 2;
    }
    SiFile si;
    if (!openSi(argv[2], si))
        return 1;
    int failed = 0;
    for (const SiObject *o : si.all()) {
        if (o->format != "EVT")
            continue;
        std::vector<SiPiece> pieces;
        SiEvent ev;
        Error e = si.readPieces(o->id, pieces);
        if (!e)
            e = parseEvent(pieces, ev);
        if (e) {
            std::printf("%5u %-20s FAILED: %s\n", o->id, o->name.c_str(), e.message.c_str());
            failed++;
            continue;
        }
        std::printf("%5u %-20s %s = \"%s\"\n", o->id, o->name.c_str(), ev.name.c_str(), ev.value.c_str());
    }
    return failed ? 1 : 0;
}

const Commands kCommands(
    "legoisland",
    {
        {"si-info", "<file.si>", "an SI file's header, and how many objects and chunks it has", cmdSiInfo},
        {"si-list", "<file.si>", "an SI file's objects, as a tree", cmdSiList},
        {"si-chunks", "<file.si> <object id>", "one object's chunks", cmdSiChunks},
        {"si-events", "<file.si>", "an SI file's events: each one's setting and value", cmdSiEvents},
        {"si-extract", "<file.si> <out folder> [object id]",
         "an SI file's media as the files they were made from (.bmp, .wav, .flc, .smk, .ani, .mod, .tex, .gph)",
         cmdSiExtract},
    });

} // namespace
