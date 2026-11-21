// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/formats/package.h"

#include "common/fs.h"

#include <cstring>
#include <fstream>

namespace reluva::unreal {

void Reader::seek(size_t absolute) {
    if (absolute < _base || absolute > _base + _size) {
        _ok = false;
        _pos = _size;
        return;
    }
    _pos = absolute - _base;
}

void Reader::skip(size_t n) {
    if (n > remaining()) {
        _ok = false;
        _pos = _size;
        return;
    }
    _pos += n;
}

bool Reader::bytes(void *out, size_t n) {
    if (n > remaining()) {
        _ok = false;
        _pos = _size;
        std::memset(out, 0, n);
        return false;
    }
    std::memcpy(out, _data + _pos, n);
    _pos += n;
    return true;
}

uint8_t Reader::u8() {
    uint8_t v = 0;
    bytes(&v, 1);
    return v;
}

uint16_t Reader::u16() {
    uint8_t b[2];
    bytes(b, 2);
    return static_cast<uint16_t>(b[0] | b[1] << 8);
}

uint32_t Reader::u32() {
    uint8_t b[4];
    bytes(b, 4);
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}

uint64_t Reader::u64() {
    const uint64_t lo = u32();
    return lo | uint64_t(u32()) << 32;
}

float Reader::f32() {
    const uint32_t bits = u32();
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

int32_t Reader::index() {
    // First byte: sign (bit 7), more follows (bit 6), 6 bits of value;
    // then up to four bytes of 7 bits each, bit 7 saying more follows.
    const uint8_t first = u8();
    uint32_t value = first & 0x3f;
    if (first & 0x40) {
        int shift = 6;
        for (int i = 0; i < 4; ++i) {
            const uint8_t b = u8();
            value |= uint32_t(b & 0x7f) << shift;
            shift += 7;
            if (!(b & 0x80))
                break;
        }
    }
    const auto v = static_cast<int32_t>(value & 0x7fffffff);
    return (first & 0x80) ? -v : v;
}

std::string Reader::string(int version) {
    std::string s;
    if (version >= 64) {
        const int32_t len = index();
        if (len < 0 || size_t(len) > remaining()) {
            fail();
            return s;
        }
        s.resize(size_t(len));
        bytes(s.data(), s.size());
        // The length counts the terminating NUL.
        while (!s.empty() && s.back() == '\0')
            s.pop_back();
        return s;
    }
    while (_ok) {
        const char c = static_cast<char>(u8());
        if (c == '\0')
            break;
        s += c;
    }
    return s;
}

Error Package::open(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return Error::fail("Can't open " + file.string());
    // Read whole, at once (a byte at a time through stream iterators took
    // a fifth of a level's start).
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size > 0 ? size_t(size) : 0);
    if (!data.empty() && !in.read(reinterpret_cast<char *>(data.data()), std::streamsize(data.size())))
        return Error::fail("Can't read " + file.string());
    _file = file;
    _name = file.stem().string();
    _memory.clear();
    if (Error err = parse(data))
        return Error::fail(file.filename().string() + ": " + err.message);
    return Error::success();
}

Error Package::openMemory(std::vector<uint8_t> data, std::string name) {
    _file.clear();
    _name = std::move(name);
    if (Error err = parse(data))
        return Error::fail(_name + ": " + err.message);
    _memory = std::move(data);
    return Error::success();
}

Error Package::parse(const std::vector<uint8_t> &data) {
    _stream.reset();
    _names.clear();
    _imports.clear();
    _exports.clear();
    _fileSize = data.size();
    Reader r(data);
    if (r.u32() != kPackageTag || !r.ok())
        return Error::fail("not an Unreal package");
    _version = r.u16();
    _licensee = r.u16();
    _flags = r.u32();
    const uint32_t nameCount = r.u32();
    const uint32_t nameOffset = r.u32();
    const uint32_t exportCount = r.u32();
    const uint32_t exportOffset = r.u32();
    const uint32_t importCount = r.u32();
    const uint32_t importOffset = r.u32();
    if (!r.ok())
        return Error::fail("header cut short");
    // Every entry takes at least two bytes, which bounds the counts by the
    // file's size before anything is allocated for them.
    for (uint32_t n : {nameCount, exportCount, importCount})
        if (n > data.size() / 2)
            return Error::fail("table counts larger than the file");

    r.seek(nameOffset);
    _names.resize(nameCount);
    for (NameEntry &n : _names) {
        n.name = r.string(_version);
        n.flags = r.u32();
    }
    if (!r.ok())
        return Error::fail("name table runs past the end");

    r.seek(importOffset);
    _imports.resize(importCount);
    for (Import &i : _imports) {
        i.classPackage = r.index();
        i.className = r.index();
        i.package = r.i32();
        i.objectName = r.index();
    }
    if (!r.ok())
        return Error::fail("import table runs past the end");

    r.seek(exportOffset);
    _exports.resize(exportCount);
    for (Export &e : _exports) {
        e.classRef = r.index();
        e.superRef = r.index();
        e.package = r.i32();
        e.objectName = r.index();
        e.flags = r.u32();
        e.serialSize = r.index();
        if (e.serialSize > 0)
            e.serialOffset = r.index();
        if (e.serialSize < 0 || e.serialOffset < 0 || uint64_t(e.serialOffset) + uint64_t(e.serialSize) > data.size())
            r.fail();
    }
    if (!r.ok())
        return Error::fail("export table runs past the end or points outside the file");
    _byName.clear();
    for (size_t i = 0; i < _exports.size(); ++i)
        _byName[fs::toLower(name(_exports[i].objectName))].push_back(int(i));
    return Error::success();
}

const std::string &Package::name(int32_t index) const {
    static const std::string kNone;
    return index >= 0 && size_t(index) < _names.size() ? _names[size_t(index)].name : kNone;
}

std::string Package::objectName(int32_t ref) const {
    if (isExport(ref) && size_t(exportIndex(ref)) < _exports.size())
        return name(_exports[size_t(exportIndex(ref))].objectName);
    if (isImport(ref) && size_t(importIndex(ref)) < _imports.size())
        return name(_imports[size_t(importIndex(ref))].objectName);
    return {};
}

std::string Package::className(int32_t ref) const {
    if (isExport(ref) && size_t(exportIndex(ref)) < _exports.size()) {
        const int32_t cls = _exports[size_t(exportIndex(ref))].classRef;
        return cls == 0 ? "Class" : objectName(cls);
    }
    if (isImport(ref) && size_t(importIndex(ref)) < _imports.size())
        return name(_imports[size_t(importIndex(ref))].className);
    return {};
}

std::string Package::path(int32_t ref) const {
    std::string out;
    // Bounded, in case a damaged file makes the outers loop.
    for (int depth = 0; ref != 0 && depth < 64; ++depth) {
        const std::string n = objectName(ref);
        out = out.empty() ? n : n + "." + out;
        if (isExport(ref) && size_t(exportIndex(ref)) < _exports.size())
            ref = _exports[size_t(exportIndex(ref))].package;
        else if (isImport(ref) && size_t(importIndex(ref)) < _imports.size())
            ref = _imports[size_t(importIndex(ref))].package;
        else
            break;
    }
    return out;
}

std::string Package::importPackage(int index) const {
    int32_t ref = -index - 1;
    for (int depth = 0; depth < 64; ++depth) {
        if (!isImport(ref) || size_t(importIndex(ref)) >= _imports.size())
            return {};
        const Import &imp = _imports[size_t(importIndex(ref))];
        if (imp.package == 0)
            return name(imp.objectName);
        ref = imp.package;
    }
    return {};
}

const std::vector<int> &Package::exportsNamed(std::string_view objectName) const {
    static const std::vector<int> kNone;
    auto it = _byName.find(fs::toLower(objectName));
    return it == _byName.end() ? kNone : it->second;
}

int Package::findExport(std::string_view objectName, std::string_view className) const {
    const std::string wantedClass = fs::toLower(className);
    for (int i : exportsNamed(objectName))
        if (wantedClass.empty() || fs::toLower(this->className(i + 1)) == wantedClass)
            return i;
    return -1;
}

std::vector<int> Package::exportsOfClass(std::string_view className) const {
    const std::string wanted = fs::toLower(className);
    std::vector<int> out;
    for (size_t i = 0; i < _exports.size(); ++i)
        if (fs::toLower(this->className(int32_t(i) + 1)) == wanted)
            out.push_back(int(i));
    return out;
}

Error Package::readExport(int index, std::vector<uint8_t> &out) const {
    if (index < 0 || size_t(index) >= _exports.size())
        return Error::fail("no export " + std::to_string(index));
    const Export &e = _exports[size_t(index)];
    out.resize(size_t(e.serialSize));
    if (e.serialSize == 0)
        return Error::success();
    if (!_memory.empty()) {
        std::memcpy(out.data(), _memory.data() + e.serialOffset, out.size());
        return Error::success();
    }
    std::lock_guard<std::mutex> lock(_streamMutex);
    if (!_stream)
        _stream = std::make_unique<std::ifstream>(_file, std::ios::binary);
    _stream->clear();
    _stream->seekg(e.serialOffset);
    if (!_stream->read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size())))
        return Error::fail("Can't read " + name(e.objectName) + " from " + _file.string());
    return Error::success();
}

} // namespace reluva::unreal
