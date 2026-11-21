// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The packages a game can load, found as the engine finds them: by name,
// in the folders its .ini's [Core.System] Paths lines list
// ("../Textures/*.utx"), opened when first asked for. Objects one package
// imports are looked up in the package they come from.

#include "engines/unreal/formats/package.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::unreal {

/**
 * The Paths a game's .ini gives, relative to its System folder; these when
 * it gives none.
 */
std::vector<std::string> defaultSearchPaths();
/** The Paths= lines of an .ini's [Core.System] section, in order. */
std::vector<std::string> parseSearchPaths(std::string_view ini);
/** A key's value in an .ini's section (both in any case); empty if it has none. */
std::string iniValue(std::string_view ini, std::string_view section, std::string_view key);

/** An export found through a package set. */
struct ObjectRef {
    const Package *package = nullptr;
    int exportIndex = -1;
    /** Whether it found one. */
    explicit operator bool() const { return package && exportIndex >= 0; }
};

/**
 * A game's packages by name (see the top of this file). Packages open
 * lazily, so even its const functions change it: one thread at a time.
 */
class PackageSet {
public:
    /**
     * Indexes the packages under a game folder: its System folder's .ini
     * (`iniName` first, then Default.ini, else any with Paths lines; else
     * defaultSearchPaths()) says where to look. Fails with no System folder
     * or no packages; problems that don't stop the game are added to
     * `warnings`.
     */
    Error open(const std::filesystem::path &gameDir, std::string_view iniName,
               std::vector<std::string> *warnings = nullptr);
    /**
     * Adds one package, from disk or from memory (tests); the first of a
     * name wins, as with the search path.
     */
    ///@{
    void addFile(const std::filesystem::path &file);
    void addMemory(const std::string &name, const std::string &extension, std::vector<uint8_t> data);
    ///@}
    /**
     * A package found later (a saved game of the original's, opened as the
     * level it is): added under its file's name, which it returns, in place
     * of another file of that name.
     */
    std::string addLater(const std::filesystem::path &file) const;

    /**
     * The package of that name (no extension, any case), opened on first
     * use; nullptr if there's none or it won't open (see error()).
     */
    const Package *get(std::string_view name) const;
    /** Why the package of that name didn't open (empty if it did, or wasn't tried). */
    std::string error(std::string_view name) const;

    /**
     * Where an object reference made in `from` leads: an export of `from`,
     * or for an import the export it names in the package it comes from
     * (one of its name and class in other groups if none is in the same);
     * empty if it leads nowhere.
     */
    ObjectRef resolve(const Package &from, int32_t ref) const;

    /**
     * Every package's name, sorted, optionally only those with the
     * extension (".utx").
     */
    std::vector<std::string> names(std::string_view extension = {}) const;
    /** A package's extension, lower case with the dot; empty if there's none of that name. */
    std::string extension(std::string_view name) const;
    /**
     * Maps: packages with the extension the search path gives its Maps
     * folder (The Fallen's are .dsm), or .unr.
     */
    bool isMap(std::string_view name) const;
    /** The maps' names, sorted. */
    std::vector<std::string> mapNames() const;
    /** The search paths open() used, as the .ini's Paths lines give them. */
    const std::vector<std::string> &searchPaths() const { return _paths; }
    /** The .ini they came from; empty if from none. */
    std::filesystem::path iniFile() const { return _ini; }
    /** The game folder open() was given (empty for sets built by hand). */
    const std::filesystem::path &gameDir() const { return _gameDir; }

private:
    struct Entry {
        std::string name;      // as spelled on disk
        std::string extension; // lower case, with the dot
        std::filesystem::path file;
        std::vector<uint8_t> memory;
        mutable std::unique_ptr<Package> package;
        mutable bool tried = false;
        mutable std::string error;
    };
    mutable std::map<std::string, Entry> _entries; // by lower-case name (nodes stay put: packages keep their places)
    std::vector<std::string> _paths;
    std::vector<std::string> _mapExtensions{".unr"};
    std::filesystem::path _ini;
    std::filesystem::path _gameDir;
};

} // namespace reluva::unreal
