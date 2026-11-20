// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

class Qvm;

// What every game module's system calls have in common, whatever their
// numbers in a given game: reading files by handle, and the memory and
// math helpers. See docs/engines/idtech3/formats/qvm.md.

/**
 * The files one game module has open, by the handles it holds (1 and up),
 * whatever its memory: read from the game's search path; what it writes
 * kept only while open (nothing the menus or the game write is saved yet).
 */
class GameFiles {
public:
    /** Reading from `files`, which must outlive it. */
    explicit GameFiles(const SearchSet &files) : _files(files) {}
    /**
     * FS_FOpenFile: `path` to read (mode 0) or write (others; refused
     * unless `writable`). The length to read (0 writing), or -1 if there's
     * no such file or it can't be opened; `handle` gets its handle (0 for
     * none), unless `wantHandle` is false (the length only).
     */
    int32_t open(const std::string &path, int32_t mode, bool wantHandle, bool writable, uint32_t &handle);
    /** FS_Read and FS_Write: the bytes moved (0 for a handle not open). */
    ///@{
    size_t read(uint32_t handle, uint8_t *out, size_t length);
    size_t write(uint32_t handle, const uint8_t *in, size_t length);
    ///@}
    /** Closes a file; what was written to it is gone. */
    void close(uint32_t handle) { _open.erase(handle); }
    /** How many bytes a read of `length` would give now. */
    size_t readable(uint32_t handle, size_t length) const;
    /** How many files are open. */
    size_t openCount() const { return _open.size(); }

    // Told each file opened for reading, by its name.
    std::function<void(const std::string &)> onRead;

private:
    struct OpenFile {
        std::vector<uint8_t> data;
        size_t position = 0;
    };
    const SearchSet &_files;
    std::map<uint32_t, OpenFile> _open;
    uint32_t _next = 1;
};

/** The same for a QVM's memory: reading only. */
class VmFiles {
public:
    /** Reading from `files`, which must outlive it. */
    explicit VmFiles(const SearchSet &files) : _files(files), _open(files) {}

    /**
     * FS_FOpenFile(path, &handle, mode). Mode 0 reads: returns the file's
     * length and stores a handle, or returns -1 and stores 0. With no
     * handle address it only returns the length. Other modes (writing)
     * fail.
     */
    int32_t open(Qvm &vm, int32_t pathAddress, int32_t handleAddress, int32_t mode);
    /** FS_Read(buffer, length, handle): the bytes read. */
    int32_t read(Qvm &vm, int32_t buffer, int32_t length, int32_t handle);
    /** Closes a file. */
    void close(int32_t handle) { _open.close(uint32_t(handle)); }
    /**
     * FS_GetFileList(folder, extension, buffer, size): the names (relative
     * to the folder) of the files directly in it that end with
     * `extension`, or its subfolders if the extension is "/", stored one
     * after another, each NUL-terminated, as many as fit. Returns how many
     * were stored. The folder "$modlist" lists the mods instead (listMods).
     */
    int32_t list(Qvm &vm, int32_t folderAddress, int32_t extensionAddress, int32_t buffer, int32_t size) const;
    /** The names list() stores, all of them. */
    static std::vector<std::string> listNames(const SearchSet &files, std::string_view folder,
                                              std::string_view extension);

    /**
     * Where "$modlist" looks for mods (listMods): the install and its base
     * game's folder. Unset, it lists none.
     */
    void setModRoot(std::filesystem::path install, std::string baseDir) {
        _modInstall = std::move(install);
        _modBaseDir = std::move(baseDir);
    }

    /** How many files are open. */
    size_t openCount() const { return _open.openCount(); }
    /** Told each file opened for reading, by its name. */
    void setOnRead(std::function<void(const std::string &)> f) { _open.onRead = std::move(f); }

private:
    const SearchSet &_files;
    GameFiles _open;
    std::filesystem::path _modInstall;
    std::string _modBaseDir;
};

/**
 * The memory and math calls numbered 100 and up, the same in each module:
 * memset, memcpy, strncpy, sin, cos, atan2, sqrt (100 to 106). Returns
 * false if `number` isn't one of them. Floats come and go as their bits.
 */
bool commonSystemCall(Qvm &vm, int number, const int32_t *args, int32_t &result);

/**
 * How often each numbered call a game module made was made: counted
 * cheaply, as calls run in their hundreds of thousands a frame.
 */
class CallCounts {
public:
    /** Counts a call. */
    void add(int number) {
        if (number >= 0 && number < kDirect)
            ++_direct[size_t(number)];
        else
            ++_others[number];
    }
    /** How many times a call was made. */
    int count(int number) const;
    /** The calls made, by number, with their counts. */
    std::map<int, int> map() const;

private:
    static constexpr int kDirect = 1024;
    std::array<int, kDirect> _direct{};
    std::map<int, int> _others;
};

/**
 * Names the game code registers (shaders, models, sounds, skins), each
 * given a handle from 1 up; the same name, ignoring case and slash
 * direction, gets the same handle. 0 is "none".
 */
class HandleTable {
public:
    /** The name's handle, given it if it's new; 0 for an empty name. */
    int32_t add(std::string_view name);
    /** The name a handle was given, as first registered; "" for none. */
    const std::string &name(int32_t handle) const;
    /** How many names it has (the highest handle). */
    size_t size() const { return _names.size(); }

private:
    std::vector<std::string> _names;
    std::unordered_map<std::string, int32_t> _byName;
};

} // namespace reluva::idtech3
