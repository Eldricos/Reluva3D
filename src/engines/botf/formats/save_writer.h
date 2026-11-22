// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/botf/formats/alt_res.h"
#include "engines/botf/formats/save_game.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Writing a saved game: its files, kept as they were read, with what Reluva
// knows of the game written back into them (docs/engines/botf/formats/sav.md).

namespace reluva::botf {

/**
 * Every file of a saved game, decoded, to be changed and written out again.
 * A file left as it was read is written back as it was stored, byte for
 * byte, so a save read and written unchanged comes out the same.
 */
class SaveFiles {
public:
    /** Reads every file of the saved game at `path`. */
    Error open(const std::filesystem::path &path);
    /** Writes them all as a saved game, its header named name(). */
    Error write(const std::filesystem::path &path) const;

    /** The header's name: the save's file name, "game0.sav", up to 15 characters. */
    ///@{
    const std::string &name() const { return _name; }
    void setName(std::string name) { _name = std::move(name); }
    ///@}

    /** Whether it has a file named so, ignoring case. */
    bool has(std::string_view name) const { return find(name) < _files.size(); }
    /**
     * A file's contents, decoded; null where there's no such file. result.lst
     * is kept as stored, in its chunks, and isn't one of these.
     */
    ///@{
    std::vector<uint8_t> *file(std::string_view name);
    const std::vector<uint8_t> *file(std::string_view name) const;
    ///@}
    /** Every file's name, in the archive's order. */
    std::vector<std::string> names() const;
    /** The file named so, added empty after the others if there's none. */
    std::vector<uint8_t> &add(std::string_view name);

private:
    struct File {
        AltResFile stored;         // as read, kept for writing it unchanged
        std::vector<uint8_t> data; // decoded
        std::vector<uint8_t> read; // decoded as read, to tell whether it changed
        bool chunked = false;      // result.lst: only ever written as stored
    };
    size_t find(std::string_view name) const; // _files.size() for none

    std::string _name;
    std::vector<File> _files;
};

/**
 * Writes `game` into a saved game's files: each value of it that differs
 * from what the files hold, where readSavedGame() read it. Ships and task
 * forces may be added or taken out, and the systems' buildings and the
 * empires' provinces are written whole; other lists' lengths must be the
 * files' (systems' planets, treaties, outposts ...). The last turn's
 * results aren't written. Text too long for its field is cut short.
 */
Error writeSavedGame(const SavedGame &game, SaveFiles &save);

} // namespace reluva::botf
