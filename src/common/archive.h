// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva {

/**
 * A collection of named files: a folder on disk, a .zip/.pk3, or an engine's
 * own container format. Names use '/' separators and are matched ignoring
 * case, as the games that look files up expect.
 */
class Archive {
public:
    virtual ~Archive() = default;

    /** Every file's name, in the archive's own spelling. */
    virtual std::vector<std::string> list() const = 0;
    /** Whether it has a file of that name, and the file's whole contents. */
    ///@{
    virtual bool has(std::string_view name) const = 0;
    virtual Error read(std::string_view name, std::vector<uint8_t> &out) const = 0;
    ///@}
    /** Where the files come from, for messages ("baseEF/pak0.pk3"). */
    virtual std::string describe() const = 0;
};

/**
 * The key archives look names up by: "Textures\\Base\\Floor.TGA" is
 * "textures/base/floor.tga".
 */
std::string normalizeArchiveName(std::string_view name);

/**
 * The loose files under a folder on disk, subfolders included. The folder
 * is listed once when opened; files added afterwards aren't seen. Names
 * from a CD with a ";1" version are found without it.
 */
class DirectoryArchive : public Archive {
public:
    /**
     * Lists the folder.
     * @param maxDepth How many levels of subfolders to list: below 0 all, 0 only
     *        the folder's own files.
     */
    Error open(const std::filesystem::path &dir, int maxDepth = -1);

    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override;
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return _dir.string(); }

private:
    std::filesystem::path _dir;
    std::unordered_map<std::string, std::filesystem::path> _files; // normalized name -> file
};

/**
 * Several archives searched as one, like a game's search path: a name is
 * read from the archive with the highest priority that has it; archives of
 * equal priority are searched newest first.
 */
class SearchSet {
public:
    /** Adds an archive, which the set then owns. */
    void add(std::unique_ptr<Archive> archive, int priority);
    /** Removes every archive. */
    void clear() { _members.clear(); }

    /** Whether any archive has the name. */
    bool has(std::string_view name) const { return find(name) != nullptr; }
    /** The archive a name would be read from, or nullptr. */
    const Archive *find(std::string_view name) const;
    /** A file from the archive find() gives; fails if none has it. */
    Error read(std::string_view name, std::vector<uint8_t> &out) const;
    /**
     * Every name in the set once, sorted by normalized name, in the spelling
     * of the archive that would supply it.
     */
    std::vector<std::string> list() const;

    /** How many archives it has. */
    size_t size() const { return _members.size(); }
    /** The archives in search order (highest priority first). */
    std::vector<const Archive *> archives() const;

private:
    struct Member {
        std::unique_ptr<Archive> archive;
        int priority;
    };
    std::vector<Member> _members; // kept in search order
};

} // namespace reluva
