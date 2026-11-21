// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Star Trek: Away Team commands for the game's text files in
// DATA.dat: its objects (formats/text_object.h) and scripts (formats/rds_script.h).

#include "devtools/tool.h"

#include "common/fs.h"
#include "common/zip.h"
#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/formats/text_object.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::stawayteam;
using namespace reluva::tool;

std::string extension(const std::string &name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? "" : fs::toLower(name.substr(dot + 1));
}

bool isScript(const std::string &name) {
    return extension(name) == "rds";
}

// The files that may hold objects: by their names, as the game has no
// mark in them; some .txt are other text (the credits, voice scripts).
bool mayBeObject(const std::string &name) {
    const std::string ext = extension(name);
    return ext == "txt" || ext == "zax" || ext == "mds" || ext == "sty" || ext == "bak";
}

int depthOf(const TextObject &object) {
    int depth = 0;
    for (const TextField &f : object.fields)
        if (f.object)
            depth = std::max(depth, 1 + depthOf(*f.object));
    return depth;
}

bool openArchive(const char *path, ZipArchive &zip) {
    if (Error err = zip.open(path)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    return true;
}

// Every object and script in DATA.dat read, and each object written back
// and compared with its file.
int cmdDataCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stawayteam data-check <DATA.dat>\n");
        return 2;
    }
    ZipArchive zip;
    if (!openArchive(argv[2], zip))
        return 1;
    int objects = 0;
    int same = 0;
    int noLineEnd = 0;
    int deepest = 0;
    int scripts = 0;
    size_t entries = 0;
    std::vector<std::string> notObjects;
    int failed = 0;
    std::vector<uint8_t> data;
    for (const auto &entry : zip.entries()) {
        if (!isScript(entry.name) && !mayBeObject(entry.name))
            continue;
        if (Error err = zip.read(entry, data)) {
            std::fprintf(stderr, "%s: %s\n", entry.name.c_str(), err.message.c_str());
            return 1;
        }
        const std::string text(data.begin(), data.end());
        if (isScript(entry.name)) {
            const RdsScript script = readRdsScript(text);
            ++scripts;
            entries += script.entries.size();
            continue;
        }
        TextObject object;
        if (Error err = readTextObject(text, object)) {
            notObjects.push_back(entry.name + ": " + err.message);
            continue;
        }
        ++objects;
        deepest = std::max(deepest, depthOf(object));
        const std::string written = writeTextObject(object);
        if (written == text) {
            ++same;
        } else if (written == text + "\r\n") {
            ++noLineEnd;
        } else {
            std::printf("DIFFERS %s\n", entry.name.c_str());
            ++failed;
        }
    }
    for (const std::string &n : notObjects)
        std::printf("not an object: %s\n", n.c_str());
    std::printf("%d objects: %d written back the same, %d the same but for a line end at the end; "
                "nested %d deep at most\n",
                objects, same, noLineEnd, deepest);
    std::printf("%d scripts, %zu entries\n", scripts, entries);
    std::printf("%zu text files not objects, %d problems\n", notObjects.size(), failed);
    return failed ? 1 : 0;
}

void printObject(const TextObject &object, int depth) {
    for (const TextField &f : object.fields) {
        std::printf("%*s%s = \"%s\"\n", depth * 2, "", f.key.c_str(), f.value.c_str());
        if (f.object)
            printObject(*f.object, depth + 1);
    }
}

// One file as Reluva reads it: an object's fields, a script's entries.
int cmdTextShow(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool stawayteam text-show <DATA.dat> <file in it>\n");
        return 2;
    }
    ZipArchive zip;
    if (!openArchive(argv[2], zip))
        return 1;
    std::vector<uint8_t> data;
    if (Error err = zip.read(argv[3], data)) {
        std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
        return 1;
    }
    const std::string text(data.begin(), data.end());
    if (isScript(argv[3])) {
        for (const auto &e : readRdsScript(text).entries)
            std::printf("[%s]\n%s\n\n", e.label.c_str(), e.text.c_str());
        return 0;
    }
    TextObject object;
    if (Error err = readTextObject(text, object)) {
        std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
        return 1;
    }
    std::printf("%s\n", object.className.c_str());
    printObject(object, 1);
    return 0;
}

const Commands kCommands("stawayteam",
                         {
                             {"data-check", "<DATA.dat>",
                              "read every object and script in DATA.dat, writing each object back to compare",
                              cmdDataCheck},
                             {"text-show", "<DATA.dat> <file in it>",
                              "show an object's fields or a script's entries as Reluva reads them", cmdTextShow},
                         });

} // namespace
