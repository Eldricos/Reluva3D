// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Theme Park World commands for the game's text: settings
// files (.sam) and string tables (.str).

#include "common/fs.h"
#include "common/text.h"
#include "devtools/tool.h"
#include "common/md5.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/game_files.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::tpw::CharacterSet;
using reluva::tpw::SamFile;
using reluva::tpw::StringTable;

// A game's files mounted, or a message why not.
bool mount(const char *folder, reluva::SearchSet &files) {
    std::vector<std::string> warnings;
    if (reluva::Error err = reluva::tpw::mountGame(files, folder, warnings)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return false;
    }
    for (const std::string &w : warnings)
        std::printf("warning: %s\n", w.c_str());
    return true;
}

std::vector<std::string> namesEndingIn(const reluva::SearchSet &files, const char *extension) {
    std::vector<std::string> out;
    for (const std::string &name : files.list())
        if (reluva::fs::toLower(extensionOf(name)) == extension)
            out.push_back(name);
    return out;
}

void printSam(const SamFile &sam) {
    for (const SamFile::Entry &e : sam.entries()) {
        std::printf("%5d  %s", e.line, e.name.c_str());
        for (const SamFile::Value &v : e.values)
            std::printf(v.isText ? "  \"%s\"" : "  %s",
                        v.isText ? v.text.c_str() : reluva::text::format("%g", v.number).c_str());
        std::printf("\n");
        for (const std::string &row : e.grid)
            std::printf("         | %s\n", row.c_str());
    }
}

// Every settings file of a game read; with a name, that one's settings.
int cmdSamCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw sam-check <game folder> [name in Data]\n");
        return 2;
    }
    reluva::SearchSet files;
    if (!mount(argv[2], files))
        return 1;
    const std::vector<std::string> names = argc > 3 ? std::vector<std::string>{argv[3]} : namesEndingIn(files, ".sam");
    int failed = 0;
    size_t entries = 0;
    size_t grids = 0;
    for (const std::string &name : names) {
        std::vector<uint8_t> data;
        SamFile sam;
        reluva::Error err = files.read(name, data);
        if (!err)
            err = sam.parse(data);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        if (argc > 3)
            printSam(sam);
        entries += sam.entries().size();
        for (const SamFile::Entry &e : sam.entries())
            grids += !e.grid.empty();
    }
    std::printf("%zu settings files read, %d failed: %zu settings, %zu of them grids\n", names.size() - size_t(failed),
                failed, entries, grids);
    return failed ? 1 : 0;
}

// The language's folder (Data/Language/<language>/), by its character set.
std::string languageFolder(const reluva::SearchSet &files) {
    for (const std::string &name : files.list()) {
        const std::string lower = reluva::fs::toLower(name);
        const std::string_view file = "/mbtouni.dat";
        if (lower.rfind("language/", 0) == 0 && lower.size() > file.size() &&
            lower.compare(lower.size() - file.size(), file.size(), file) == 0)
            return name.substr(0, name.size() - file.size() + 1);
    }
    return {};
}

// Every string table of a game read; with a table's name, its strings.
int cmdStrCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool tpw str-check <game folder> [table, e.g. UITEXT]\n");
        return 2;
    }
    reluva::SearchSet files;
    if (!mount(argv[2], files))
        return 1;
    const std::string folder = languageFolder(files);
    std::vector<uint8_t> data;
    CharacterSet characters;
    reluva::Error err = folder.empty() ? reluva::Error::fail("no Language/<language>/MBToUni.dat")
                                       : files.read(folder + "MBToUni.dat", data);
    if (!err)
        err = characters.parse(data);
    if (err) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::vector<std::string> names;
    for (const std::string &name : namesEndingIn(files, ".str"))
        if (argc < 4 || reluva::fs::toLower(name) == reluva::fs::toLower(folder + argv[3] + ".str"))
            names.push_back(name);
    int failed = 0;
    size_t strings = 0;
    size_t parameters = 0;
    reluva::Md5 all;
    for (const std::string &name : names) {
        StringTable table;
        err = files.read(name, data);
        if (!err)
            err = table.parse(data, characters);
        if (err) {
            std::printf("failed: %s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        if (argc > 3)
            std::printf("%s: table %u, %zu strings\n", name.c_str(), table.id(), table.size());
        for (size_t i = 0; i < table.size(); ++i) {
            const std::string text = table.text(i);
            all.update(text.data(), text.size() + 1);
            for (const StringTable::Part &p : table.parts(i))
                parameters += p.parameter;
            if (argc > 3)
                std::printf("%4zu  %s\n", i, text.c_str());
        }
        strings += table.size();
    }
    std::printf("%s: %zu codes; %zu string tables read, %d failed: %zu strings, %zu parameters; md5 %s\n",
                (folder + "MBToUni.dat").c_str(), characters.size(), names.size() - size_t(failed), failed, strings,
                parameters, all.hexDigest().c_str());
    return failed || names.empty() ? 1 : 0;
}

const Commands kCommands("tpw", {
                                    {"sam-check", "<game folder> [name in Data]",
                                     "read every settings file (.sam), or list one's settings", cmdSamCheck},
                                    {"str-check", "<game folder> [table, e.g. UITEXT]",
                                     "read every string table (.str), or list one's strings", cmdStrCheck},
                                });

} // namespace
