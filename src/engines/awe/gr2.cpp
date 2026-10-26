// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/gr2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::awe {

namespace {

// The magic of a little-endian file with 32-bit pointers.
constexpr uint8_t kMagic[16] = {0xb8, 0x67, 0xb0, 0xca, 0xf8, 0x6d, 0xb1, 0x0f,
                                0x84, 0x72, 0x8c, 0x7e, 0x5e, 0x19, 0x00, 0x1e};
constexpr uint32_t kSectionBytes = 44;
constexpr uint32_t kFixupBytes = 12;
constexpr uint32_t kMemberBytes = 32; // a granny_data_type_definition with 32-bit pointers

uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}

float halfToFloat(uint16_t h) {
    const int sign = h >> 15, exp = (h >> 10) & 31, man = h & 1023;
    float v;
    if (exp == 0)
        v = std::ldexp(float(man), -24);
    else if (exp == 31)
        v = man ? NAN : INFINITY;
    else
        v = std::ldexp(float(man | 1024), exp - 25);
    return sign ? -v : v;
}

} // namespace

const char* gr2TypeName(Gr2Type type) {
    static const char* const names[] = {
        "end",       "inline",    "reference", "reference to array", "array of references", "variant reference",
        "unsupported", "reference to variant array", "string", "transform", "real32", "int8", "uint8",
        "binormal int8", "normal uint8", "int16", "uint16", "binormal int16", "normal uint16", "int32", "uint32",
        "real16", "empty reference"};
    const auto i = size_t(type);
    return i < std::size(names) ? names[i] : "?";
}

uint32_t gr2ValueSize(Gr2Type type) {
    switch (type) {
    case Gr2Type::Reference:
    case Gr2Type::String:
    case Gr2Type::Real32:
    case Gr2Type::Int32:
    case Gr2Type::UInt32:
    case Gr2Type::EmptyReference:
        return 4;
    case Gr2Type::ReferenceToArray:
    case Gr2Type::ArrayOfReferences:
    case Gr2Type::VariantReference:
        return 8;
    case Gr2Type::ReferenceToVariantArray:
        return 12;
    case Gr2Type::Transform:
        return 4 + 12 + 16 + 36;
    case Gr2Type::Int8:
    case Gr2Type::UInt8:
    case Gr2Type::BinormalInt8:
    case Gr2Type::NormalUInt8:
        return 1;
    case Gr2Type::Int16:
    case Gr2Type::UInt16:
    case Gr2Type::BinormalInt16:
    case Gr2Type::NormalUInt16:
    case Gr2Type::Real16:
        return 2;
    default:
        return 0;
    }
}

Error Gr2File::open(const std::vector<uint8_t>& file, const Gr2Decompressor& decompress) {
    *this = Gr2File();
    if (file.size() < 0x58 || std::memcmp(file.data(), kMagic, 16) != 0)
        return Error::fail("Not a little-endian 32-bit Granny 2 file");
    const uint32_t headerSize = rd32(file, 0x10);
    version_ = rd32(file, 0x20);
    const uint32_t totalSize = rd32(file, 0x24);
    const uint32_t sectionTable = 0x20 + rd32(file, 0x2c);
    const uint32_t sectionCount = rd32(file, 0x30);
    const uint32_t rootTypeSection = rd32(file, 0x34), rootTypeOffset = rd32(file, 0x38);
    const uint32_t rootSection = rd32(file, 0x3c), rootOffset = rd32(file, 0x40);
    typeTag_ = rd32(file, 0x44);
    if (version_ != 6)
        return Error::fail("Granny file format " + std::to_string(version_) + ", only 6 is known");
    if (totalSize > file.size() || headerSize > file.size() || sectionCount > 64 ||
        sectionTable + uint64_t(sectionCount) * kSectionBytes > file.size())
        return Error::fail("Granny file header out of range");

    // Lay the sections out, each at its alignment (16 at least).
    uint32_t end = 16;
    for (uint32_t i = 0; i < sectionCount; i++) {
        Gr2Section s;
        uint32_t* f = &s.compression;
        for (int k = 0; k < 11; k++)
            f[k] = rd32(file, sectionTable + i * kSectionBytes + uint32_t(k) * 4);
        if (s.dataOffset + uint64_t(s.compressedSize) > file.size() ||
            s.fixupOffset + uint64_t(s.fixupCount) * kFixupBytes > file.size() || s.size > (256u << 20))
            return Error::fail("Granny section " + std::to_string(i) + " out of range");
        const uint32_t align = std::max<uint32_t>(16, s.alignment);
        if (align & (align - 1))
            return Error::fail("Granny section " + std::to_string(i) + " has a bad alignment");
        end = (end + align - 1) & ~(align - 1);
        sectionBase_.push_back(end);
        end += s.size + 4;
        sections_.push_back(s);
    }
    memory_.assign(end, 0);

    for (size_t i = 0; i < sections_.size(); i++) {
        const Gr2Section& s = sections_[i];
        if (s.size == 0)
            continue;
        uint8_t* dst = memory_.data() + sectionBase_[i];
        if (s.compression == 0) {
            if (s.compressedSize < s.size)
                return Error::fail("Granny section " + std::to_string(i) + " is short");
            std::memcpy(dst, file.data() + s.dataOffset, s.size);
            continue;
        }
        if (!decompress)
            return Error::fail("Granny section " + std::to_string(i) + " is compressed (format " +
                               std::to_string(s.compression) + ") and no decompressor was given");
        const std::vector<uint8_t> in(file.begin() + s.dataOffset, file.begin() + s.dataOffset + s.compressedSize);
        std::vector<uint8_t> out(s.size);
        if (Error e = decompress(s.compression, in, s.stop0, s.stop1, out))
            return Error::fail("Granny section " + std::to_string(i) + ": " + e.message);
        std::memcpy(dst, out.data(), s.size);
    }

    // Fixups: each a pointer's place in its section, and the section and
    // offset it points at.
    for (size_t i = 0; i < sections_.size(); i++) {
        const Gr2Section& s = sections_[i];
        for (uint32_t k = 0; k < s.fixupCount; k++) {
            const uint32_t o = s.fixupOffset + k * kFixupBytes;
            const uint32_t at = rd32(file, o), target = rd32(file, o + 4), targetOffset = rd32(file, o + 8);
            if (target >= sections_.size() || uint64_t(at) + 4 > s.size || targetOffset > sections_[target].size)
                return Error::fail("Granny section " + std::to_string(i) + " has a bad fixup");
            const uint32_t value = sectionBase_[target] + targetOffset;
            uint8_t* p = memory_.data() + sectionBase_[i] + at;
            p[0] = uint8_t(value), p[1] = uint8_t(value >> 8), p[2] = uint8_t(value >> 16), p[3] = uint8_t(value >> 24);
        }
    }
    if (rootTypeSection >= sections_.size() || rootSection >= sections_.size())
        return Error::fail("Granny root out of range");
    rootType_ = sectionBase_[rootTypeSection] + rootTypeOffset;
    rootObject_ = sectionBase_[rootSection] + rootOffset;
    return Error::success();
}

uint16_t Gr2File::read16(uint32_t a) const {
    return valid(a, 2) ? uint16_t(memory_[a] | memory_[a + 1] << 8) : 0;
}

uint32_t Gr2File::read32(uint32_t a) const { return valid(a, 4) ? rd32(memory_, a) : 0; }

float Gr2File::readFloat(uint32_t a) const {
    const uint32_t v = read32(a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

std::string Gr2File::readString(uint32_t a) const {
    std::string s;
    while (valid(a, 1) && memory_[a] && s.size() < 4096)
        s += char(memory_[a++]);
    return s;
}

const std::vector<Gr2Member>& Gr2File::members(uint32_t type) const {
    auto it = typeCache_.find(type);
    if (it != typeCache_.end())
        return it->second;
    std::vector<Gr2Member>& list = typeCache_[type];
    uint32_t offset = 0;
    for (uint32_t a = type; valid(a, kMemberBytes) && list.size() < 1024; a += kMemberBytes) {
        Gr2Member m;
        m.type = Gr2Type(read32(a));
        if (m.type == Gr2Type::End)
            break;
        m.name = readString(read32(a + 4));
        m.refType = read32(a + 8);
        m.arrayWidth = read32(a + 12);
        m.offset = offset;
        const uint32_t one = m.type == Gr2Type::Inline ? (m.refType == type ? 0 : typeSize(m.refType))
                                                       : gr2ValueSize(m.type);
        m.size = one * std::max<uint32_t>(1, m.arrayWidth);
        offset += m.size;
        list.push_back(std::move(m));
    }
    return list;
}

uint32_t Gr2File::typeSize(uint32_t type) const {
    uint32_t size = 0;
    for (const Gr2Member& m : members(type))
        size += m.size;
    return size;
}

const std::vector<Gr2Member>& Gr2Object::members() const {
    static const std::vector<Gr2Member> none;
    return file_ && type_ ? file_->members(type_) : none;
}

const Gr2Member* Gr2Object::member(std::string_view name) const {
    for (const Gr2Member& m : members())
        if (m.name == name)
            return &m;
    return nullptr;
}

double Gr2Object::number(std::string_view name, uint32_t index) const {
    const Gr2Member* m = member(name);
    if (!m || !address_)
        return 0;
    const uint32_t one = gr2ValueSize(m->type);
    if (!one || index >= std::max<uint32_t>(1, m->arrayWidth))
        return 0;
    const uint32_t a = address_ + m->offset + index * one;
    switch (m->type) {
    case Gr2Type::Real32:
        return file_->readFloat(a);
    case Gr2Type::Real16:
        return halfToFloat(file_->read16(a));
    case Gr2Type::Int8:
        return int8_t(file_->read8(a));
    case Gr2Type::UInt8:
        return file_->read8(a);
    case Gr2Type::BinormalInt8:
        return int8_t(file_->read8(a)) / 127.0;
    case Gr2Type::NormalUInt8:
        return file_->read8(a) / 255.0;
    case Gr2Type::Int16:
        return int16_t(file_->read16(a));
    case Gr2Type::UInt16:
        return file_->read16(a);
    case Gr2Type::BinormalInt16:
        return int16_t(file_->read16(a)) / 32767.0;
    case Gr2Type::NormalUInt16:
        return file_->read16(a) / 65535.0;
    case Gr2Type::Int32:
        return int32_t(file_->read32(a));
    case Gr2Type::UInt32:
        return file_->read32(a);
    default:
        return 0;
    }
}

std::string Gr2Object::string(std::string_view name) const {
    const Gr2Member* m = member(name);
    if (!m || m->type != Gr2Type::String || !address_)
        return "";
    return file_->readString(file_->read32(address_ + m->offset));
}

Gr2Transform Gr2Object::transform(std::string_view name) const {
    Gr2Transform t;
    const Gr2Member* m = member(name);
    if (!m || m->type != Gr2Type::Transform || !address_)
        return t;
    uint32_t a = address_ + m->offset;
    t.flags = file_->read32(a);
    for (float& f : t.position)
        f = file_->readFloat(a += 4);
    for (float& f : t.orientation)
        f = file_->readFloat(a += 4);
    for (float& f : t.scaleShear)
        f = file_->readFloat(a += 4);
    return t;
}

Gr2Object Gr2Object::object(std::string_view name) const {
    const Gr2Member* m = member(name);
    if (!m || !address_)
        return {};
    const uint32_t a = address_ + m->offset;
    switch (m->type) {
    case Gr2Type::Inline:
        return Gr2Object(file_, m->refType, a);
    case Gr2Type::Reference:
        return Gr2Object(file_, m->refType, file_->read32(a));
    case Gr2Type::VariantReference:
        return Gr2Object(file_, file_->read32(a), file_->read32(a + 4));
    default:
        return {};
    }
}

uint32_t Gr2Object::count(std::string_view name) const {
    const Gr2Member* m = member(name);
    if (!m || !address_)
        return 0;
    const uint32_t a = address_ + m->offset;
    switch (m->type) {
    case Gr2Type::ReferenceToArray:
    case Gr2Type::ArrayOfReferences:
        return file_->read32(a);
    case Gr2Type::ReferenceToVariantArray:
        return file_->read32(a + 4);
    default:
        return 0;
    }
}

Gr2Object Gr2Object::element(std::string_view name, uint32_t index) const {
    const Gr2Member* m = member(name);
    if (!m || !address_ || index >= count(name))
        return {};
    const uint32_t a = address_ + m->offset;
    switch (m->type) {
    case Gr2Type::ReferenceToArray:
        return Gr2Object(file_, m->refType, file_->read32(a + 4) + index * file_->typeSize(m->refType));
    case Gr2Type::ArrayOfReferences:
        return Gr2Object(file_, m->refType, file_->read32(file_->read32(a + 4) + 4 * index));
    case Gr2Type::ReferenceToVariantArray: {
        const uint32_t type = file_->read32(a);
        return Gr2Object(file_, type, file_->read32(a + 8) + index * file_->typeSize(type));
    }
    default:
        return {};
    }
}

Gr2Object::Array Gr2Object::array(std::string_view name) const {
    Array r;
    const Gr2Member* m = member(name);
    if (!m || !address_)
        return r;
    const uint32_t a = address_ + m->offset;
    if (m->type == Gr2Type::ReferenceToArray) {
        r.type = m->refType;
        r.count = file_->read32(a);
        r.address = file_->read32(a + 4);
    } else if (m->type == Gr2Type::ReferenceToVariantArray) {
        r.type = file_->read32(a);
        r.count = file_->read32(a + 4);
        r.address = file_->read32(a + 8);
    } else {
        return r;
    }
    r.stride = file_->typeSize(r.type);
    if (!r.type || !file_->valid(r.address, r.count * r.stride))
        r = Array();
    return r;
}

namespace {

void dumpObject(const Gr2Object& o, std::string& out, int depth, uint32_t maxElements, int maxDepth) {
    const std::string pad(size_t(depth) * 2, ' ');
    char buf[64];
    for (const Gr2Member& m : o.members()) {
        out += pad + m.name + " (" + gr2TypeName(m.type) +
               (m.arrayWidth > 1 ? "[" + std::to_string(m.arrayWidth) + "]" : "") + ")";
        switch (m.type) {
        case Gr2Type::String:
            out += ": \"" + o.string(m.name) + "\"\n";
            break;
        case Gr2Type::Transform: {
            const Gr2Transform t = o.transform(m.name);
            std::snprintf(buf, sizeof buf, ": flags %u pos (%g %g %g)", t.flags, t.position[0], t.position[1],
                          t.position[2]);
            out += buf;
            std::snprintf(buf, sizeof buf, " rot (%g %g %g %g)\n", t.orientation[0], t.orientation[1],
                          t.orientation[2], t.orientation[3]);
            out += buf;
            break;
        }
        case Gr2Type::Inline:
        case Gr2Type::Reference:
        case Gr2Type::VariantReference: {
            const Gr2Object child = o.object(m.name);
            if (!child.valid()) {
                out += ": null\n";
            } else if (depth >= maxDepth) {
                out += ": ...\n";
            } else {
                out += ":\n";
                dumpObject(child, out, depth + 1, maxElements, maxDepth);
            }
            break;
        }
        case Gr2Type::ReferenceToArray:
        case Gr2Type::ArrayOfReferences:
        case Gr2Type::ReferenceToVariantArray: {
            const uint32_t n = o.count(m.name);
            out += ": " + std::to_string(n) + (n == 1 ? " element\n" : " elements\n");
            if (depth >= maxDepth)
                break;
            for (uint32_t i = 0; i < n && i < maxElements; i++) {
                out += pad + "  [" + std::to_string(i) + "]\n";
                dumpObject(o.element(m.name, i), out, depth + 2, maxElements, maxDepth);
            }
            if (n > maxElements)
                out += pad + "  ...\n";
            break;
        }
        default:
            if (gr2ValueSize(m.type)) {
                out += ":";
                const uint32_t n = std::max<uint32_t>(1, m.arrayWidth);
                for (uint32_t i = 0; i < n && i < 16; i++) {
                    std::snprintf(buf, sizeof buf, " %g", o.number(m.name, i));
                    out += buf;
                }
                out += n > 16 ? " ...\n" : "\n";
            } else {
                out += "\n";
            }
        }
    }
}

} // namespace

std::string Gr2File::dump(uint32_t maxElements, int maxDepth) const {
    std::string out;
    dumpObject(root(), out, 0, maxElements, maxDepth);
    return out;
}

} // namespace reluva::awe
