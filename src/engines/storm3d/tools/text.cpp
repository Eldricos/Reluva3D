// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for Star Trek: Armada's text files
// (docs/engines/storm3d/games/armada/formats/).
//
//   reluva-tool storm3d odf <game folder> <name.odf>
//   reluva-tool storm3d odf-check <game folder>

#include "common/archive.h"
#include "common/text.h"
#include "engines/storm3d/formats/odf.h"
#include "engines/storm3d/formats/zfs_archive.h"
#include "devtools/tool.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::SearchSet;
using reluva::storm3d::Odf;
using reluva::storm3d::ZfsArchive;

// The install's own files over assets.zfs's: which the game prefers isn't
// known yet, and the two hold no name in common.
Error openGameFiles(const std::string &folder, SearchSet &files) {
    auto loose = std::make_unique<reluva::DirectoryArchive>();
    if (Error err = loose->open(folder, 0))
        return err;
    auto assets = std::make_unique<ZfsArchive>();
    if (Error err = assets->open(std::filesystem::path(folder) / "assets.zfs"))
        return err;
    files.add(std::move(loose), 1);
    files.add(std::move(assets), 0);
    return Error::success();
}

Odf::Loader loaderFor(const SearchSet &files) {
    return [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> data;
        if (Error err = files.read(name, data))
            return err;
        text.assign(data.begin(), data.end());
        return Error::success();
    };
}

int cmdOdf(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d odf <game folder> <name.odf>\n");
        return 2;
    }
    SearchSet files;
    if (Error err = openGameFiles(argv[2], files)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    Odf odf;
    if (Error err = odf.load(argv[3], loaderFor(files))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const Odf::Entry &e : odf.entries()) {
        std::string value = e.value;
        for (char &c : value)
            if (c == '\n')
                c = ' ';
        std::printf("%-14s %-18s %-28s %s\n", e.file.c_str(), e.section.c_str(), e.key.c_str(),
                    reluva::text::windows1252ToUtf8(value).c_str());
    }
    for (const std::string &p : odf.problems())
        std::printf("not read: %s\n", p.c_str());
    return 0;
}

int cmdOdfCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d odf-check <game folder>\n");
        return 2;
    }
    SearchSet files;
    if (Error err = openGameFiles(argv[2], files)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int read = 0;
    int failed = 0;
    size_t entries = 0;
    size_t problems = 0;
    for (const std::string &name : files.list()) {
        if (!reluva::text::hasSuffixIgnoreCase(name, ".odf"))
            continue;
        // events.odf is the announcements' blocks, another format.
        std::vector<uint8_t> head;
        if (!files.read(name, head) && std::string(head.begin(), head.end()).rfind("#EVENT", 0) == 0) {
            std::printf("skipped: %s, an events file\n", name.c_str());
            continue;
        }
        Odf odf;
        if (Error err = odf.load(name, loaderFor(files))) {
            std::printf("%s\n", err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        entries += odf.entries().size();
        problems += odf.problems().size();
        for (const std::string &p : odf.problems())
            std::printf("not read: %s\n", p.c_str());
    }
    std::printf("%d files read, %d failed; %zu entries, includes resolved; %zu lines not read\n", read, failed, entries,
                problems);
    return failed ? 1 : 0;
}

const reluva::tool::Commands kCommands(
    "storm3d",
    {
        {"odf", "<game folder> <name.odf>",
         "an object definition with the files it includes: file, section, key and value of each entry", cmdOdf},
        {"odf-check", "<game folder>",
         "read every object definition, in the install's folder and assets.zfs, and report what fails", cmdOdfCheck},
    });

} // namespace
