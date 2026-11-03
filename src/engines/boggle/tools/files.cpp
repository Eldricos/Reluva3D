// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/archive.h"
#include "tools/tool.h"
#include <cstdio>
#include <filesystem>

namespace {
int cmdFiles(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reluva-tool boggle files <game folder>\n");
        return 2;
    }
    reluva::DirectoryArchive archive;
    if (auto err = archive.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const auto& name : archive.list()) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(std::filesystem::path(argv[2]) / name, ec);
        if (ec) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(), ec.message().c_str());
            return 1;
        }
        std::printf("%12llu  %s\n", static_cast<unsigned long long>(size), name.c_str());
    }
    return 0;
}
const reluva::tool::Commands commands("boggle", {
    {"files", "<game folder>", "list every file in a game's folder, with its size", cmdFiles},
});
} // namespace
