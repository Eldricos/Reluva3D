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
// little-endian). See docs/engines/awe/formats/gr2.md.
//
// A file is a header, a table of sections (each possibly compressed), and
// for each section a list of pointer fixups. Its data describes itself:
// the root object comes with a type, a list of members each naming its
// type, so the whole tree can be walked without knowing it in advance.

namespace reluva::awe {

struct Gr2Section {
    uint32_t compression = 0; // 0 none, 1 Oodle0, 2 Oodle1
    uint32_t dataOffset = 0;
    uint32_t compressedSize = 0;
    uint32_t size = 0;
    uint32_t alignment = 0;
    // where the compressor's three parts end
    uint32_t stop0 = 0;
    uint32_t stop1 = 0;
    uint32_t fixupOffset = 0;
    uint32_t fixupCount = 0;
    uint32_t marshallingOffset = 0;
    uint32_t marshallingCount = 0;
};

// Decompresses one section's data: `format` is Gr2Section::compression,
// `out` already sized to the section's size.
using Gr2Decompressor = std::function<Error(uint32_t format, const std::vector<uint8_t> &in, uint32_t stop0,
                                            uint32_t stop1, std::vector<uint8_t> &out)>;

// A member's type (Granny's granny_member_type).
enum class Gr2Type : uint32_t {
    kEnd = 0,
    kInline = 1,            // the referenced type's members, in place
    kReference = 2,         // pointer to one object
    kReferenceToArray = 3,  // count, pointer to that many objects
    kArrayOfReferences = 4, // count, pointer to that many pointers
    kVariantReference = 5,  // type pointer, object pointer
    kUnsupported = 6,
    kReferenceToVariantArray = 7, // type pointer, count, pointer
    kString = 8,                  // pointer to a NUL-terminated string
    kTransform = 9,               // flags, position, orientation, scale/shear
    kReal32 = 10,
    kInt8 = 11,
    kUInt8 = 12,
    kBinormalInt8 = 13,
    kNormalUInt8 = 14,
    kInt16 = 15,
    kUInt16 = 16,
    kBinormalInt16 = 17,
    kNormalUInt16 = 18,
    kInt32 = 19,
    kUInt32 = 20,
    kReal16 = 21,
    kEmptyReference = 22,
};
const char *gr2TypeName(Gr2Type type);

struct Gr2Member {
    Gr2Type type = Gr2Type::kEnd;
    std::string name;
    uint32_t refType = 0;    // address of the referenced type's members, 0 if none
    uint32_t arrayWidth = 0; // 0 or 1: one value
    uint32_t offset = 0;     // from the start of the object
    uint32_t size = 0;       // in bytes, the array's whole width
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
    Gr2Object(const Gr2File *file, uint32_t type, uint32_t address) : _file(file), _type(type), _address(address) {}

    bool valid() const { return _file && _type && _address; }
    uint32_t address() const { return _address; }
    uint32_t type() const { return _type; }
    const std::vector<Gr2Member> &members() const;
    const Gr2Member *member(std::string_view name) const;

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
        uint32_t type = 0;
        uint32_t address = 0;
        uint32_t count = 0;
        uint32_t stride = 0;
    };
    Array array(std::string_view name) const;

private:
    const Gr2File *_file = nullptr;
    uint32_t _type = 0;
    uint32_t _address = 0;
};

class Gr2File {
public:
    // Reads, decompresses and fixes up every section. Without a
    // decompressor, sections are decompressed natively (gr2Decompress).
    Error open(const std::vector<uint8_t> &file, const Gr2Decompressor &decompress = {});

    uint32_t version() const { return _version; }
    // The file as it was given to open().
    const std::vector<uint8_t> &bytes() const { return _bytes; }
    uint32_t typeTag() const { return _typeTag; }
    const std::vector<Gr2Section> &sections() const { return _sections; }
    Gr2Object root() const { return Gr2Object(this, _rootType, _rootObject); }

    // The sections are laid out one after another in one block of memory,
    // from address 16 on so that 0 is a null pointer; fixed-up pointers
    // are addresses in it.
    const std::vector<uint8_t> &memory() const { return _memory; }
    uint32_t sectionAddress(size_t section) const { return _sectionBase[section]; }
    bool valid(uint32_t address, uint32_t size) const {
        return address != 0 && address <= _memory.size() && size <= _memory.size() - address;
    }
    uint8_t read8(uint32_t a) const { return valid(a, 1) ? _memory[a] : 0; }
    uint16_t read16(uint32_t a) const;
    uint32_t read32(uint32_t a) const;
    float readFloat(uint32_t a) const;
    std::string readString(uint32_t a) const;

    // A type's members (cached), each with its offset and size.
    const std::vector<Gr2Member> &members(uint32_t type) const;
    // The size of an object of the type.
    uint32_t typeSize(uint32_t type) const;

    // Writes the tree under the root as indented text, `maxElements` of
    // each array at most, for tools.
    std::string dump(uint32_t maxElements = 4, int maxDepth = 12) const;

private:
    uint32_t _version = 0;
    uint32_t _typeTag = 0;
    uint32_t _rootType = 0;
    uint32_t _rootObject = 0;
    std::vector<Gr2Section> _sections;
    std::vector<uint32_t> _sectionBase;
    std::vector<uint8_t> _memory;
    std::vector<uint8_t> _bytes;
    mutable std::map<uint32_t, std::vector<Gr2Member>> _typeCache;
};

// The size of one value of a member type (with 32-bit pointers); 0 for
// Inline, whose size is its referenced type's.
uint32_t gr2ValueSize(Gr2Type type);

} // namespace reluva::awe
