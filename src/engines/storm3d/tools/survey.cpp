// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool storm3d exe-survey <game folder>: what Armada.exe offers its
// missions and needs from Windows, for the research notes
// (docs/engines/storm3d/games/armada/research/): each export with the
// number of mission and AI DLLs that import it, each import by DLL, and
// each class its run-time type information names. Names are printed as
// the compiler decorated them.

#include "common/fs.h"
#include "common/pe.h"
#include "common/text.h"
#include "devtools/tool.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::PeImage;

Error readPe(const std::filesystem::path &path, std::vector<uint8_t> &file, PeImage &pe) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Cannot open " + path.string());
    file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (Error err = reluva::parsePe(file, pe))
        return Error::fail(path.string() + ": " + err.message);
    return Error::success();
}

int cmdExeSurvey(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d exe-survey <game folder>\n");
        return 2;
    }
    const std::filesystem::path dir = argv[2];
    const auto exePath = reluva::fs::findFile(dir, "Armada.exe");
    if (!exePath) {
        std::fprintf(stderr, "Armada.exe isn't in %s\n", argv[2]);
        return 1;
    }
    std::vector<uint8_t> exe;
    PeImage pe;
    if (Error err = readPe(*exePath, exe, pe)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }

    // Who imports each export: the missions' and the computer player's DLLs.
    std::map<std::string, std::set<std::string>> users;
    int dlls = 0;
    for (const char *folder : {"missions", "AI"}) {
        const auto found = reluva::fs::findDirectory(dir, folder);
        std::error_code ec;
        if (!found)
            continue;
        for (const auto &entry : std::filesystem::directory_iterator(*found, ec)) {
            const std::string name = entry.path().filename().string();
            if (!reluva::text::hasSuffixIgnoreCase(name, ".dsl") && !reluva::text::hasSuffixIgnoreCase(name, ".drl"))
                continue;
            std::vector<uint8_t> file;
            PeImage dll;
            if (readPe(entry.path(), file, dll))
                continue;
            ++dlls;
            for (const PeImage::ImportedDll &from : dll.imports)
                if (reluva::text::equalsIgnoreCase(from.name, "Armada.exe"))
                    for (const PeImage::Import &f : from.functions)
                        users[f.name].insert(std::string(folder) + "/" + name);
        }
    }
    for (const PeImage::Export &e : pe.exports)
        std::printf("export %3zu %08x %s\n", users.count(e.name) ? users[e.name].size() : size_t(0), e.rva,
                    e.name.c_str());
    for (const PeImage::ImportedDll &from : pe.imports)
        for (const PeImage::Import &f : from.functions)
            std::printf("import %-16s %s\n", from.name.c_str(),
                        f.name.empty() ? ("#" + std::to_string(f.ordinal)).c_str() : f.name.c_str());
    // Run-time type information names each polymorphic class as ".?AV",
    // its name, "@@".
    std::set<std::string> classes;
    for (size_t i = 0; i + 4 < exe.size(); ++i) {
        if (std::memcmp(&exe[i], ".?AV", 4) != 0)
            continue;
        size_t end = i + 4;
        while (end < exe.size() && exe[end] >= 0x20 && exe[end] < 0x7f)
            ++end;
        classes.insert(std::string(reinterpret_cast<const char *>(&exe[i]), end - i));
    }
    for (const std::string &c : classes)
        std::printf("class %s\n", c.c_str());
    std::printf("%zu exports, %zu used by %d mission and AI DLLs; %zu classes\n", pe.exports.size(), users.size(), dlls,
                classes.size());
    return 0;
}

const reluva::tool::Commands kCommands("storm3d", {
                                                      {"exe-survey", "<game folder>",
                                                       "Armada.exe's exports and who imports them, its imports, and "
                                                       "the classes its type information names",
                                                       cmdExeSurvey},
                                                  });

} // namespace
