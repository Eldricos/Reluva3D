// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Unreal packages built in the tests: the bytes, names, imports and
// exports, and tagged properties.

#include "audio/decode.h"
#include "audio/wav.h"
#include "tests/common/data/tone440_mp3.h"
#include "engines/registry.h"
#include "engines/unreal/formats/dialog_table.h"
#include "engines/unreal/formats/actors.h"
#include "engines/unreal/formats/class_defaults.h"
#include "engines/unreal/games.h"
#include "engines/unreal/formats/mesh.h"
#include "engines/unreal/render/mesh_render.h"
#include "engines/unreal/render/lighting.h"
#include "engines/unreal/game/zones.h"
#include "engines/unreal/render/procedural.h"
#include "engines/unreal/vm/script.h"
#include "engines/unreal/vm/script_text.h"
#include "engines/unreal/vm/vm.h"
#include "engines/unreal/game/collision.h"
#include "engines/unreal/formats/model.h"
#include "engines/unreal/formats/package.h"
#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/formats/properties.h"
#include "engines/unreal/formats/sound.h"
#include "engines/unreal/formats/texture.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "tests/support/test.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace reluva::unreal::test_data {


// Little-endian output, with the engine's compact integers.
struct Bytes {
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back(uint8_t(v)); }
    void u16(int v) { u8(v & 0xff), u8((v >> 8) & 0xff); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i)
            u8(int((v >> (8 * i)) & 0xff));
    }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void u64(uint64_t v) { u32(uint32_t(v)), u32(uint32_t(v >> 32)); }
    void f32(float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        u32(v);
    }
    void index(int32_t v) {
        uint32_t a = uint32_t(v < 0 ? -v : v);
        uint8_t first = uint8_t((v < 0 ? 0x80 : 0) | (a & 0x3f));
        a >>= 6;
        if (a)
            first |= 0x40;
        u8(first);
        while (a) {
            uint8_t next = uint8_t(a & 0x7f);
            a >>= 7;
            if (a)
                next |= 0x80;
            u8(next);
        }
    }
    void str(const std::string &s, int version) {
        if (version >= 64)
            index(int32_t(s.size() + 1));
        b.insert(b.end(), s.begin(), s.end());
        u8(0);
    }
    void bytes(const std::vector<uint8_t> &d) { b.insert(b.end(), d.begin(), d.end()); }
    void vec(float x, float y, float z) { f32(x), f32(y), f32(z); }
};

// Builds a package: names are added as used, exports' data written by the
// test with the names' indices.
struct PackageWriter {
    int version = 69;
    std::vector<std::string> names{"None"};
    struct Imp {
        int32_t classPackage, className, package, objectName;
    };
    struct Exp {
        int32_t classRef, superRef, package, objectName;
        uint32_t flags;
        std::vector<uint8_t> data;
    };
    std::vector<Imp> imports;
    std::vector<Exp> exports;

    int32_t name(const std::string &n) {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == n)
                return int32_t(i);
        names.push_back(n);
        return int32_t(names.size() - 1);
    }
    // Returns the object reference.
    int32_t addImport(const std::string &pkg, const std::string &cls, int32_t outer, const std::string &obj) {
        imports.push_back({name(pkg), name(cls), outer, name(obj)});
        return -int32_t(imports.size());
    }
    int32_t addExport(int32_t cls, int32_t outer, const std::string &obj, std::vector<uint8_t> data,
                      uint32_t flags = 0) {
        exports.push_back({cls, 0, outer, name(obj), flags, std::move(data)});
        return int32_t(exports.size());
    }
    std::vector<uint8_t> build() const {
        Bytes body;
        // Header written last, at a fixed size; the data follows it.
        const size_t headerSize = 36 + (version >= 68 ? 16 + 4 + 8 : 8);
        std::vector<int32_t> offsets;
        for (const Exp &e : exports) {
            offsets.push_back(int32_t(headerSize + body.b.size()));
            body.bytes(e.data);
        }
        const auto nameOffset = uint32_t(headerSize + body.b.size());
        for (const std::string &n : names) {
            body.str(n, version);
            body.u32(0x70010);
        }
        const auto importOffset = uint32_t(headerSize + body.b.size());
        for (const Imp &i : imports) {
            body.index(i.classPackage);
            body.index(i.className);
            body.i32(i.package);
            body.index(i.objectName);
        }
        const auto exportOffset = uint32_t(headerSize + body.b.size());
        for (size_t k = 0; k < exports.size(); ++k) {
            const Exp &e = exports[k];
            body.index(e.classRef);
            body.index(e.superRef);
            body.i32(e.package);
            body.index(e.objectName);
            body.u32(e.flags);
            body.index(int32_t(e.data.size()));
            if (!e.data.empty())
                body.index(offsets[k]);
        }
        Bytes h;
        h.u32(kPackageTag);
        h.u16(version);
        h.u16(0);
        h.u32(1);
        h.u32(uint32_t(names.size()));
        h.u32(nameOffset);
        h.u32(uint32_t(exports.size()));
        h.u32(exportOffset);
        h.u32(uint32_t(imports.size()));
        h.u32(importOffset);
        if (version >= 68) {
            for (int i = 0; i < 16; ++i)
                h.u8(i);
            h.u32(1);
            h.u32(uint32_t(exports.size()));
            h.u32(uint32_t(names.size()));
        } else {
            h.u32(0);
            h.u32(0);
        }
        h.bytes(body.b);
        return h.b;
    }
};

// A property tag for a value of `size` bytes (sizes the short codes cover).
inline void tag(Bytes &o, PackageWriter &w, const std::string &name, PropertyType type, size_t size, int arrayIndex = 0,
         const std::string &structName = {}) {
    o.index(w.name(name));
    int code = size <= 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : size == 12 ? 3 : size == 16 ? 4 : 5;
    o.u8(int(type) | code << 4 | (arrayIndex ? 0x80 : 0));
    if (type == PropertyType::kStruct)
        o.index(w.name(structName));
    if (code == 5)
        o.u8(int(size));
    if (arrayIndex)
        o.u8(arrayIndex);
}

inline void endProps(Bytes &o, PackageWriter &w) {
    o.index(w.name("None"));
}


inline std::vector<uint8_t> texturePackage() {
    PackageWriter w;
    const int32_t engine = w.addImport("Core", "Package", 0, "Engine");
    const int32_t texClass = w.addImport("Core", "Class", engine, "Texture");
    const int32_t palClass = w.addImport("Core", "Class", engine, "Palette");
    Bytes pal;
    endProps(pal, w);
    pal.index(256);
    for (int i = 0; i < 256; ++i)
        pal.u8(i), pal.u8(255 - i), pal.u8(7), pal.u8(0);
    const int32_t palRef = w.addExport(palClass, 0, "BrickPal", pal.b);
    Bytes tex;
    tag(tex, w, "Palette", PropertyType::kObject, 1);
    tex.index(palRef);
    tag(tex, w, "USize", PropertyType::kInt, 4);
    tex.i32(4);
    tag(tex, w, "VSize", PropertyType::kInt, 4);
    tex.i32(2);
    tag(tex, w, "bMasked", PropertyType::kBool, 0);
    tex.b.back() |= 0x80;
    endProps(tex, w);
    tex.index(1); // one mipmap
    tex.i32(0);   // where its data ends (the reader doesn't need it)
    tex.index(8);
    for (int i = 0; i < 8; ++i)
        tex.u8(i);
    tex.i32(4), tex.i32(2), tex.u8(2), tex.u8(1);
    w.addExport(texClass, 0, "Brick", tex.b);
    return w.build();
}

} // namespace reluva::unreal::test_data
