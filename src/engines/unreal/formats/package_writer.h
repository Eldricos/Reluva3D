// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Writes an Unreal package (version 73, as The Fallen's), starting from
// another's tables: its names, imports and exports keep their numbers, so
// an export's data copied as it is still refers to the right objects. More
// names, imports and exports are added after them. See
// docs/engines/unreal/games/ds9fallen/formats/package.md.

#include "common/types.h"
#include "engines/unreal/formats/package.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::unreal {

/** Little-endian values and Unreal's compact indexes, into a buffer. */
class ByteWriter {
public:
    /** A value added to the end, little-endian. */
    ///@{
    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void i32(int32_t v) { u32(uint32_t(v)); }
    void f32(float v);
    ///@}
    /** A compact index: 1 to 5 bytes, by its size. */
    void index(int32_t v);
    /**
     * A string as version 64 and later keep them: a compact length,
     * counting the NUL, then the letters and the NUL.
     */
    void string(const std::string &s);
    /** Bytes added to the end as they are. */
    ///@{
    void bytes(const void *p, size_t n);
    void append(const std::vector<uint8_t> &v) { data.insert(data.end(), v.begin(), v.end()); }
    ///@}
    std::vector<uint8_t> data;
};

/** A package to write, made from another's tables (see the top of this file). */
class PackageWriter {
public:
    /**
     * Starts from `base`'s tables, each export's data its own (copied when
     * written unless replaced). `base` must outlive it.
     */
    explicit PackageWriter(const Package &base);

    /** A name's index (matched ignoring case), added if it's new. */
    int32_t name(const std::string &text);
    /**
     * A reference to an import, added (with the packages and groups it's
     * in) if it's new: `path` "Package.Group.Object", `classPackage` and
     * `className` its class's ("Engine", "Texture").
     */
    int32_t import(const std::string &path, const std::string &classPackage, const std::string &className);
    /** An import of `name` in another import (a state in its class). */
    int32_t importIn(int32_t outer, const std::string &name, const std::string &classPackage,
                     const std::string &className);
    /** A new export, with no data until setData; returns its reference (index + 1). */
    int32_t addExport(int32_t classRef, int32_t outerRef, const std::string &objectName, uint32_t flags);
    /** Replaces an export's object flags (`ref` its reference, index + 1). */
    void setFlags(int32_t ref, uint32_t flags) { _exports[size_t(ref - 1)].e.flags = flags; }
    /** An export's data, in place of what the base has. */
    void setData(int32_t ref, std::vector<uint8_t> data);
    /** Left out: written with no data (an object the level no longer has). */
    void drop(int32_t ref);
    /** How many exports it has, the base's and the added. */
    size_t exportCount() const { return _exports.size(); }

    /** Writes the package to `path`, replacing any file there. */
    Error write(const std::string &path) const;

private:
    const Package &_base;
    std::vector<NameEntry> _names;
    std::map<std::string, int32_t> _nameIndex; // lower case
    std::vector<Import> _imports;
    std::map<std::string, int32_t> _importIndex; // "classpackage.classname path", lower case
    struct Out {
        Export e;
        bool replaced = false;
        bool dropped = false;
        std::vector<uint8_t> data;
    };
    std::vector<Out> _exports;
};

} // namespace reluva::unreal
