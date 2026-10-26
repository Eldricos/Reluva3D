// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Granny 2 files (.gr2), RAD Game Tools' format for models and animations,
// as Murder on the Orient Express ships them (file format 6, 32-bit,
// little-endian). See docs/formats/awe/gr2.md.
//
// A file is a header, a table of sections (each possibly compressed), and
// for each section a list of pointer fixups. Its data describes itself:
// the root object comes with a type, a list of members each naming its
// type, so the whole tree can be walked without knowing it in advance.

namespace reluva::awe {

struct Gr2Section {
    uint32_t compression = 0; // 0 none, 1 Oodle0, 2 Oodle1
    uint32_t dataOffset = 0, compressedSize = 0, size = 0;
    uint32_t alignment = 0;
    uint32_t stop0 = 0, stop1 = 0; // where the compressor's three parts end
    uint32_t fixupOffset = 0, fixupCount = 0;
    uint32_t marshallingOffset = 0, marshallingCount = 0;
};

// Decompresses one section's data: `format` is Gr2Section::compression,
// `out` already sized to the section's size.
using Gr2Decompressor = std::function<Error(uint32_t format, const std::vector<uint8_t>& in, uint32_t stop0,
                                            uint32_t stop1, std::vector<uint8_t>& out)>;

// A member's type (Granny's granny_member_type).
enum class Gr2Type : uint32_t {
    End = 0,
    Inline = 1,                 // the referenced type's members, in place
    Reference = 2,              // pointer to one object
    ReferenceToArray = 3,       // count, pointer to that many objects
    ArrayOfReferences = 4,      // count, pointer to that many pointers
    VariantReference = 5,       // type pointer, object pointer
    Unsupported = 6,
    ReferenceToVariantArray = 7, // type pointer, count, pointer
    String = 8,                 // pointer to a NUL-terminated string
    Transform = 9,              // flags, position, orientation, scale/shear
    Real32 = 10,
    Int8 = 11,
    UInt8 = 12,
    BinormalInt8 = 13,
    NormalUInt8 = 14,
    Int16 = 15,
    UInt16 = 16,
    BinormalInt16 = 17,
    NormalUInt16 = 18,
    Int32 = 19,
    UInt32 = 20,
    Real16 = 21,
    EmptyReference = 22,
};
const char* gr2TypeName(Gr2Type type);

struct Gr2Member {
    Gr2Type type = Gr2Type::End;
    std::string name;
    uint32_t refType = 0; // address of the referenced type's members, 0 if none
    uint32_t arrayWidth = 0; // 0 or 1: one value
    uint32_t offset = 0; // from the start of the object
    uint32_t size = 0;   // in bytes, the array's whole width
};

// A Granny 2.x transform: which parts are set, then position, orientation
// (a quaternion x, y, z, w) and a 3x3 scale/shear matrix (rows).
struct Gr2Transform {
    uint32_t flags = 0;
    float position[3] = {0, 0, 0};
    float orientation[4] = {0, 0, 0, 1};
    float scaleShear[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

class Gr2File;

// An object in a file: its type and where it is. Members are found by
// name; a missing one reads as 0, "" or an empty reference.
class Gr2Object {
public:
    Gr2Object() = default;
    Gr2Object(const Gr2File* file, uint32_t type, uint32_t address) : file_(file), type_(type), address_(address) {}

    bool valid() const { return file_ && type_ && address_; }
    uint32_t address() const { return address_; }
    uint32_t type() const { return type_; }
    const std::vector<Gr2Member>& members() const;
    const Gr2Member* member(std::string_view name) const;

    // Numbers: the i'th value of an array member, converted.
    double number(std::string_view name, uint32_t index = 0) const;
    std::string string(std::string_view name) const;
    Gr2Transform transform(std::string_view name) const;
    // Reference, VariantReference or Inline member: the object.
    Gr2Object object(std::string_view name) const;
    // ReferenceToArray, ArrayOfReferences or ReferenceToVariantArray: the
    // elements.
    uint32_t count(std::string_view name) const;
    Gr2Object element(std::string_view name, uint32_t index) const;

    // The raw data of a ReferenceToArray or ReferenceToVariantArray (its
    // elements' type, their address, how many, the size of one), for
    // reading vertex arrays in bulk.
    struct Array {
        uint32_t type = 0, address = 0, count = 0, stride = 0;
    };
    Array array(std::string_view name) const;

private:
    const Gr2File* file_ = nullptr;
    uint32_t type_ = 0, address_ = 0;
};

class Gr2File {
public:
    // Reads, decompresses and fixes up every section. Uncompressed files
    // need no decompressor.
    Error open(const std::vector<uint8_t>& file, const Gr2Decompressor& decompress = {});

    uint32_t version() const { return version_; }
    uint32_t typeTag() const { return typeTag_; }
    const std::vector<Gr2Section>& sections() const { return sections_; }
    Gr2Object root() const { return Gr2Object(this, rootType_, rootObject_); }

    // The sections are laid out one after another in one block of memory,
    // from address 16 on so that 0 is a null pointer; fixed-up pointers
    // are addresses in it.
    const std::vector<uint8_t>& memory() const { return memory_; }
    uint32_t sectionAddress(size_t section) const { return sectionBase_[section]; }
    bool valid(uint32_t address, uint32_t size) const {
        return address != 0 && address <= memory_.size() && size <= memory_.size() - address;
    }
    uint8_t read8(uint32_t a) const { return valid(a, 1) ? memory_[a] : 0; }
    uint16_t read16(uint32_t a) const;
    uint32_t read32(uint32_t a) const;
    float readFloat(uint32_t a) const;
    std::string readString(uint32_t a) const;

    // A type's members (cached), each with its offset and size.
    const std::vector<Gr2Member>& members(uint32_t type) const;
    // The size of an object of the type.
    uint32_t typeSize(uint32_t type) const;

    // Writes the tree under the root as indented text, `maxElements` of
    // each array at most, for tools.
    std::string dump(uint32_t maxElements = 4, int maxDepth = 12) const;

private:
    uint32_t version_ = 0, typeTag_ = 0;
    uint32_t rootType_ = 0, rootObject_ = 0;
    std::vector<Gr2Section> sections_;
    std::vector<uint32_t> sectionBase_;
    std::vector<uint8_t> memory_;
    mutable std::map<uint32_t, std::vector<Gr2Member>> typeCache_;
};

// The size of one value of a member type (with 32-bit pointers); 0 for
// Inline, whose size is its referenced type's.
uint32_t gr2ValueSize(Gr2Type type);

} // namespace reluva::awe
