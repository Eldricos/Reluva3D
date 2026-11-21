// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Star Trek: Away Team commands for the objects saved in
// binary in DATA.dat: the sequences, models and waypoint maps
// (formats/binary_object.h).

#include "devtools/tool.h"

#include "common/fs.h"
#include "common/zip.h"
#include "engines/stawayteam/formats/binary_object.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::stawayteam;
using namespace reluva::tool;

// The class a kind of file holds; empty for the sequences, whose version
// says (a CSequence, or a CFont).
bool classForFile(const std::string &name, std::string &className) {
    const std::string lower = fs::toLower(name);
    if (lower.ends_with(".mdl16"))
        className = "CModel";
    else if (lower.ends_with(".way"))
        className = "CWayPointMap";
    else if (lower.ends_with(".seq16"))
        className.clear();
    else
        return false;
    return true;
}

bool openArchive(const char *path, ZipArchive &zip) {
    if (Error err = zip.open(path)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    return true;
}

// Every sequence, model and waypoint map in DATA.dat read.
int cmdObjectCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stawayteam object-check <DATA.dat>\n");
        return 2;
    }
    ZipArchive zip;
    if (!openArchive(argv[2], zip))
        return 1;
    const auto start = std::chrono::steady_clock::now();
    std::map<std::string, int> read;
    size_t frames = 0;
    size_t sequences = 0;
    size_t wayPoints = 0;
    int failed = 0;
    std::vector<uint8_t> data;
    for (const auto &entry : zip.entries()) {
        std::string className;
        if (!classForFile(entry.name, className))
            continue;
        if (Error err = zip.read(entry, data)) {
            std::fprintf(stderr, "%s: %s\n", entry.name.c_str(), err.message.c_str());
            return 1;
        }
        BinaryObject object;
        if (Error err = readBinaryObject(data.data(), data.size(), className, object)) {
            std::printf("FAILED  %s: %s\n", entry.name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++read[object.object.className];
        if (object.sequence)
            frames += object.sequence->frames.size();
        for (const NamedSequence &s : object.sequences)
            if (s.sequence && s.sequence->sequence) {
                ++sequences;
                frames += s.sequence->sequence->frames.size();
            }
        wayPoints += object.wayPoints.size();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    for (const auto &[cls, count] : read)
        std::printf("%6d  %s\n", count, cls.c_str());
    std::printf("%zu rotation sequences in the models, %zu frames, %zu waypoints; %d failed (%.1f s)\n", sequences,
                frames, wayPoints, failed, seconds);
    return failed ? 1 : 0;
}

void printObject(const TextObject &object, int depth) {
    for (const TextField &f : object.fields) {
        std::printf("%*s%s = \"%s\"\n", depth * 2, "", f.key.c_str(), f.value.c_str());
        if (f.object)
            printObject(*f.object, depth + 1);
    }
}

void printSequence(const SequenceData &seq, int depth) {
    int present = 0;
    for (const auto &f : seq.frames)
        present += f.has_value();
    std::printf("%*s(%zu frames, %d with pictures; bounds %d,%d to %d,%d)\n", depth * 2, "", seq.frames.size(), present,
                seq.box[0], seq.box[1], seq.box[2], seq.box[3]);
}

// One file's object: its properties, and what its class keeps of its own.
int cmdObjectShow(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool stawayteam object-show <DATA.dat> <file in it>\n");
        return 2;
    }
    ZipArchive zip;
    if (!openArchive(argv[2], zip))
        return 1;
    std::string className;
    if (!classForFile(argv[3], className)) {
        std::fprintf(stderr, "%s: not a .mdl16, .seq16 or .way\n", argv[3]);
        return 2;
    }
    std::vector<uint8_t> data;
    BinaryObject object;
    if (Error err = zip.read(argv[3], data);
        err || (err = readBinaryObject(data.data(), data.size(), className, object))) {
        std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
        return 1;
    }
    std::printf("%s\n", object.object.className.c_str());
    printObject(object.object, 1);
    if (object.sequence)
        printSequence(*object.sequence, 1);
    for (const NamedSequence &s : object.sequences) {
        if (!s.sequence) {
            std::printf("  sequence (none)\n");
            continue;
        }
        std::printf("  sequence \"%s\": %s\n", s.name.c_str(), s.sequence->object.className.c_str());
        printObject(s.sequence->object, 2);
        if (s.sequence->sequence)
            printSequence(*s.sequence->sequence, 2);
    }
    if (!object.wayPoints.empty() || !object.lines.empty())
        std::printf("  (%zu waypoints, %zu lines)\n", object.wayPoints.size(), object.lines.size());
    return 0;
}

const Commands kCommands("stawayteam",
                         {
                             {"object-check", "<DATA.dat>",
                              "read every sequence, model and waypoint map saved in binary in DATA.dat",
                              cmdObjectCheck},
                             {"object-show", "<DATA.dat> <file in it>",
                              "show a sequence's, model's or waypoint map's object as Reluva reads it", cmdObjectShow},
                         });

} // namespace
