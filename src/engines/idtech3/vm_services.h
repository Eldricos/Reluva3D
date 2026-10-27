// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

class Qvm;

// What every game module's system calls have in common, whatever their
// numbers in a given game: reading files by handle, and the memory and
// math helpers. See docs/formats/idtech3/qvm.md.

// The files one game module has open, by the handles it holds (1 and up),
// whatever its memory: read from the game's search path; what it writes
// kept only while open (nothing the menus or the game write is saved yet).
class GameFiles {
public:
    explicit GameFiles(const SearchSet& files) : files_(files) {}
    // FS_FOpenFile: `path` to read (mode 0) or write (others; refused
    // unless `writable`). The length to read (0 writing), or -1 if there's
    // no such file or it can't be opened; `handle` gets its handle (0 for
    // none), unless `wantHandle` is false (the length only).
    int32_t open(const std::string& path, int32_t mode, bool wantHandle, bool writable, uint32_t& handle);
    // FS_Read and FS_Write: the bytes moved.
    size_t read(uint32_t handle, uint8_t* out, size_t length);
    size_t write(uint32_t handle, const uint8_t* in, size_t length);
    void close(uint32_t handle) { open_.erase(handle); }
    // How many bytes a read of `length` would give now.
    size_t readable(uint32_t handle, size_t length) const;
    size_t openCount() const { return open_.size(); }

private:
    struct OpenFile {
        std::vector<uint8_t> data;
        size_t position = 0;
    };
    const SearchSet& files_;
    std::map<uint32_t, OpenFile> open_;
    uint32_t next_ = 1;
};

// The same for a QVM's memory: reading only.
class VmFiles {
public:
    explicit VmFiles(const SearchSet& files) : files_(files), open_(files) {}

    // FS_FOpenFile(path, &handle, mode). Mode 0 reads: returns the file's
    // length and stores a handle, or returns -1 and stores 0. With no
    // handle address it only returns the length. Other modes (writing)
    // fail.
    int32_t open(Qvm& vm, int32_t pathAddress, int32_t handleAddress, int32_t mode);
    // FS_Read(buffer, length, handle): the bytes read.
    int32_t read(Qvm& vm, int32_t buffer, int32_t length, int32_t handle);
    void close(int32_t handle) { open_.close(uint32_t(handle)); }
    // FS_GetFileList(folder, extension, buffer, size): the names (relative
    // to the folder) of the files directly in it that end with
    // `extension`, or its subfolders if the extension is "/", stored one
    // after another, each NUL-terminated, as many as fit. Returns how many
    // were stored.
    int32_t list(Qvm& vm, int32_t folderAddress, int32_t extensionAddress, int32_t buffer, int32_t size) const;
    static std::vector<std::string> listNames(const SearchSet& files, std::string_view folder,
                                              std::string_view extension);

    size_t openCount() const { return open_.openCount(); }

private:
    const SearchSet& files_;
    GameFiles open_;
};

// The memory and math calls numbered 100 and up, the same in each module:
// memset, memcpy, strncpy, sin, cos, atan2, sqrt (100 to 106). Returns
// false if `number` isn't one of them. Floats come and go as their bits.
bool commonSystemCall(Qvm& vm, int number, const int32_t* args, int32_t& result);

// How often each numbered call a game module made was made: counted
// cheaply, as calls run in their hundreds of thousands a frame.
class CallCounts {
public:
    void add(int number) {
        if (number >= 0 && number < kDirect)
            ++direct_[size_t(number)];
        else
            ++others_[number];
    }
    int count(int number) const;
    std::map<int, int> map() const; // those made, by number
private:
    static constexpr int kDirect = 1024;
    std::array<int, kDirect> direct_{};
    std::map<int, int> others_;
};

// Names the game code registers (shaders, models, sounds, skins), each
// given a handle from 1 up; the same name, ignoring case, gets the same
// handle. 0 is "none".
class HandleTable {
public:
    int32_t add(std::string_view name);
    const std::string& name(int32_t handle) const; // "" for none
    size_t size() const { return names_.size(); }

private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, int32_t> byName_;
};

} // namespace reluva::idtech3
