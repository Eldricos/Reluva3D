// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Unreal Engine 1 packages (.u, .unr, .utx, .uax, .umx): every file the
// engine loads is one. A package holds a table of names, the objects it
// takes from other packages (imports) and the objects it defines
// (exports), each export's data stored at its own offset. See
// docs/engines/unreal/games/ds9fallen/formats/package.md.

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::unreal {

/** The first four bytes of every package, read little-endian. */
constexpr uint32_t kPackageTag = 0x9E2A83C1;

/** Object flags that change how an export's data starts, and where it's loaded. */
namespace objflag {
constexpr uint32_t kLoadForClient = 0x00010000;
constexpr uint32_t kLoadForServer = 0x00020000;
constexpr uint32_t kHasStack = 0x02000000; // a state frame comes before the properties
} // namespace objflag

/**
 * Reads little-endian values from a buffer, bounds-checked: reading past
 * the end sets a failure flag and returns zeros, so callers check ok()
 * once after a run of reads. Positions are absolute: `base` is the file
 * offset of data[0] (an export's data read on its own still sees the
 * offsets the file stores).
 */
class Reader {
public:
    /** Over `size` bytes at `data`, which it doesn't copy: they must outlive it. */
    ///@{
    Reader(const uint8_t *data, size_t size, size_t base = 0) : _data(data), _size(size), _base(base) {}
    explicit Reader(const std::vector<uint8_t> &data, size_t base = 0) : Reader(data.data(), data.size(), base) {}
    ///@}

    /** Whether every read so far was in bounds, and marking that one wasn't. */
    ///@{
    bool ok() const { return _ok; }
    void fail() { _ok = false; }
    ///@}
    /** Where it is and where the data ends, as file offsets. */
    ///@{
    size_t pos() const { return _base + _pos; }
    size_t end() const { return _base + _size; }
    ///@}
    /** How many bytes are left. */
    size_t remaining() const { return _size - _pos; }
    /** Moves to a file offset, or past the end and failing if it's outside the data. */
    void seek(size_t absolute);
    /** Skips `n` bytes, or to the end and failing if there aren't as many. */
    void skip(size_t n);

    /** The next value, little-endian; 0 past the end. */
    ///@{
    uint8_t u8();
    uint16_t u16();
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32();
    int32_t i32() { return static_cast<int32_t>(u32()); }
    uint64_t u64();
    float f32();
    ///@}
    /** The engine's compact integer: 1 to 5 bytes, see package.md. */
    int32_t index();
    /** The next `n` bytes; false, with `out` zeroed, if there aren't as many. */
    bool bytes(void *out, size_t n);
    /**
     * A string as packages of `version` store them: before 64 NUL
     * terminated, from 64 a compact length (counting the NUL) first.
     */
    std::string string(int version);

private:
    const uint8_t *_data;
    size_t _size;
    size_t _base;
    size_t _pos = 0;
    bool _ok = true;
};

/** An entry of the name table. */
struct NameEntry {
    std::string name;
    uint32_t flags = 0;
};

/** An object another package defines. Names are indices into the name table. */
struct Import {
    int32_t classPackage = 0;
    int32_t className = 0;
    int32_t package = 0; // object reference: the import's outer (its group or package)
    int32_t objectName = 0;
};

/** An object this package defines. */
struct Export {
    int32_t classRef = 0; // object reference; 0: the object is a class
    int32_t superRef = 0; // object reference: what a class or struct extends
    int32_t package = 0;  // object reference: the outer (a group), 0 for the package itself
    int32_t objectName = 0;
    uint32_t flags = 0;
    int32_t serialSize = 0;
    int32_t serialOffset = 0;
};

/**
 * Object references, as packages store them: > 0 the export at ref - 1,
 * < 0 the import at -ref - 1, 0 none.
 */
///@{
inline bool isExport(int32_t ref) {
    return ref > 0;
}
inline bool isImport(int32_t ref) {
    return ref < 0;
}
inline int exportIndex(int32_t ref) {
    return ref - 1;
}
inline int importIndex(int32_t ref) {
    return -ref - 1;
}
///@}

/** One package: its tables, and its exports' data read when asked for. */
class Package {
public:
    /**
     * Reads the header and tables from a file, which it keeps for reading
     * export data when asked for. Its name is the file's stem.
     */
    Error open(const std::filesystem::path &file);
    /** The same from a whole file in memory (kept). */
    Error openMemory(std::vector<uint8_t> data, std::string name);

    /** Its name (no extension), and the file it came from (empty from memory). */
    ///@{
    const std::string &name() const { return _name; }
    const std::filesystem::path &file() const { return _file; }
    ///@}
    /** The header's fields. */
    ///@{
    int version() const { return _version; }
    int licensee() const { return _licensee; }
    uint32_t flags() const { return _flags; }
    ///@}
    /** The file's size in bytes. */
    uint64_t fileSize() const { return _fileSize; }

    /** The name, import and export tables. */
    ///@{
    const std::vector<NameEntry> &names() const { return _names; }
    const std::vector<Import> &imports() const { return _imports; }
    const std::vector<Export> &exports() const { return _exports; }
    ///@}

    /** A name table entry; "" when out of range. */
    const std::string &name(int32_t index) const;
    /** An object's own name and its class's name ("Class" for classes); empty for a bad reference. */
    ///@{
    std::string objectName(int32_t ref) const;
    std::string className(int32_t ref) const;
    ///@}
    /**
     * Its outers' names and its own, joined by dots ("Group.Name"); for an
     * import, starting with the package it comes from.
     */
    std::string path(int32_t ref) const;
    /** The package an import comes from: its outermost outer's name. */
    std::string importPackage(int index) const;

    /**
     * The first export of that name (and class, unless empty), ignoring
     * case; -1 if none.
     */
    int findExport(std::string_view objectName, std::string_view className = {}) const;
    /** Every export of that name, ignoring case, in table order. */
    const std::vector<int> &exportsNamed(std::string_view objectName) const;
    /** Every export whose class has that name, ignoring case. */
    std::vector<int> exportsOfClass(std::string_view className) const;

    /**
     * An export's data (empty if it has none); a Reader over it with its
     * serialOffset as `base` sees the offsets the file stores. Safe to call
     * from several threads.
     */
    Error readExport(int index, std::vector<uint8_t> &out) const;

private:
    Error parse(const std::vector<uint8_t> &data);

    std::string _name;
    std::filesystem::path _file;
    std::vector<uint8_t> _memory; // the whole file, for packages opened from memory
    int _version = 0;
    int _licensee = 0;
    uint32_t _flags = 0;
    uint64_t _fileSize = 0;
    std::vector<NameEntry> _names;
    std::vector<Import> _imports;
    std::vector<Export> _exports;
    std::unordered_map<std::string, std::vector<int>> _byName; // lower-case name -> exports
    // The file, kept open for reading exports (opened on first use).
    mutable std::unique_ptr<std::ifstream> _stream;
    mutable std::mutex _streamMutex;
};

} // namespace reluva::unreal
