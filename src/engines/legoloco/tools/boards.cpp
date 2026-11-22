// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's LEGO Loco commands for boards (docs/engines/legoloco/formats/sav.md).

#include "common/archive.h"
#include "common/fs.h"
#include "devtools/tool.h"
#include "engines/legoloco/formats/board.h"
#include "engines/legoloco/formats/resource_file.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::tool;

void print(const legoloco::Board &board) {
    std::printf("version %u, %u by %u squares, backdrop \"%s\", %zu of %u pieces, %zu trains\n", board.version,
                board.columns, board.rows, board.backdrop.c_str(), board.pieces.size(), board.declaredPieces,
                board.trains.size());
    for (int row = 0; row < board.rows; ++row) {
        std::string line;
        for (int column = 0; column < board.columns; ++column) {
            const uint8_t v = board.square(column, row);
            line += v == 0 ? '.' : (v < 10 ? char('0' + v) : '+');
        }
        std::printf("  %s\n", line.c_str());
    }
    std::printf("  %8s %6s %4s %8s %8s  %s\n", "resource", "column", "row", "+8", "+12", "name, minifigs");
    for (const auto &p : board.pieces) {
        std::printf("  %8u %6u %4u %8u %8u  \"%s\"", p.resource, p.column, p.row, p.value8, p.value12, p.name.c_str());
        for (const auto &m : p.minifigs)
            if (m.resource)
                std::printf(" %u:\"%s\"", m.resource, m.name.c_str());
        std::printf("\n");
    }
    for (const auto &t : board.trains)
        std::printf("  train \"%s\"\n", t.name.c_str());
}

bool readFile(const std::filesystem::path &path, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// A board file on disk, a board in the resource file, or every board of a copy.
int cmdSav(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool legoloco sav <file.sav> | <game or art-res folder> [name.sav]\n");
        return 2;
    }
    const std::filesystem::path arg = argv[2];
    std::vector<uint8_t> data;
    legoloco::Board board;
    if (std::filesystem::is_regular_file(arg)) {
        if (!readFile(arg, data)) {
            std::fprintf(stderr, "can't read %s\n", argv[2]);
            return 1;
        }
        if (Error err = legoloco::parseBoard(data, board)) {
            std::fprintf(stderr, "%s: %s\n", argv[2], err.message.c_str());
            return 1;
        }
        print(board);
        return 0;
    }
    std::filesystem::path folder = arg;
    if (const auto artRes = fs::findDirectory(folder, "art-res"))
        folder = *artRes;
    legoloco::ResourceFile rf;
    if (Error err = rf.open(folder)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    if (argc > 3) {
        if (Error err = rf.read(argv[3], data)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 1;
        }
        if (Error err = legoloco::parseBoard(data, board)) {
            std::fprintf(stderr, "%s: %s\n", argv[3], err.message.c_str());
            return 1;
        }
        print(board);
        return 0;
    }
    int read = 0;
    int failed = 0;
    auto check = [&](const std::string &name, const std::vector<uint8_t> &bytes) {
        if (Error err = legoloco::parseBoard(bytes, board)) {
            std::printf("%s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            return;
        }
        ++read;
        std::printf("%-28s %3ux%-3u %5zu of %5u pieces, %u trains, backdrop \"%s\"\n", name.c_str(), board.columns,
                    board.rows, board.pieces.size(), board.declaredPieces, unsigned(board.trains.size()),
                    board.backdrop.c_str());
    };
    for (size_t i = 0; i < rf.entries().size(); ++i)
        if (normalizeArchiveName(rf.entries()[i].name).ends_with(".sav") && !rf.readEntry(i, data))
            check(rf.entries()[i].name, data);
    for (const char *sub : {"SaveGame", "ScrSaver"}) {
        const auto dir = fs::findDirectory(folder, sub);
        if (!dir)
            continue;
        for (const auto &e : std::filesystem::directory_iterator(*dir))
            if (normalizeArchiveName(e.path().filename().string()).ends_with(".sav") && readFile(e.path(), data))
                check(std::string(sub) + "/" + e.path().filename().string(), data);
    }
    std::printf("%d boards read, %d failed\n", read, failed);
    return failed ? 1 : 0;
}

const Commands kCommands("legoloco", {
                                         {"sav", "<file.sav> | <game or art-res folder> [name.sav]",
                                          "print a board, from a file or the resource file; with only a folder, read "
                                          "every board of the copy",
                                          cmdSav},
                                     });

} // namespace
