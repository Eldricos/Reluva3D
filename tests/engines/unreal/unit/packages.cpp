// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The package reader: compact indices, the name, import and export tables, tagged properties, finding packages and games, map extensions, dialogue tables.
//
//   reluva-tests unreal.unit.packages

#include "tests/engines/unreal/support.h"

using namespace reluva;
using namespace reluva::unreal;
using namespace reluva::unreal::test_data;

namespace {

void testCompactIndex() {
    for (int32_t v : {0, 1, -1, 63, 64, -64, 127, 8191, 8192, 1000000, -1000000, 0x7ffffff}) {
        Bytes b;
        b.index(v);
        Reader r(b.b);
        CHECK(r.index() == v && r.ok() && r.remaining() == 0);
    }
    const std::vector<uint8_t> truncated{0x40};
    Reader shortRead(truncated);
    shortRead.index();
    CHECK(!shortRead.ok());
}

void testPackageTables() {
    for (int version : {61, 69}) {
        PackageWriter w;
        w.version = version;
        const int32_t core = w.addImport("Core", "Package", 0, "Engine");
        const int32_t texClass = w.addImport("Core", "Class", core, "Texture");
        const int32_t group = w.addExport(0, 0, "Walls", {}); // a class stands in for a group
        Bytes d;
        endProps(d, w);
        w.addExport(texClass, group, "Brick", d.b);
        Package p;
        CHECK(!p.openMemory(w.build(), "Test"));
        CHECK(p.version() == version && p.exports().size() == 2 && p.imports().size() == 2);
        CHECK(p.path(2) == "Walls.Brick" && p.className(2) == "Texture" && p.className(1) == "Class");
        CHECK(p.importPackage(1) == "Engine" && p.path(texClass) == "Engine.Texture");
        CHECK(p.findExport("brick", "TEXTURE") == 1 && p.findExport("brick", "Sound") == -1);
        CHECK(p.exportsOfClass("texture") == std::vector<int>{1});
    }
    Package bad;
    CHECK(bad.openMemory({1, 2, 3, 4}, "x"));
    std::vector<uint8_t> cut = PackageWriter{}.build();
    cut.resize(cut.size() - 3);
    CHECK(bad.openMemory(cut, "x"));
}

void testProperties() {
    PackageWriter w;
    Bytes o;
    tag(o, w, "Count", PropertyType::Int, 4);
    o.i32(-7);
    tag(o, w, "Scale", PropertyType::Float, 4);
    o.f32(1.5f);
    tag(o, w, "bOn", PropertyType::Bool, 0);
    o.b.back() |= 0x80; // the value, in the info byte
    tag(o, w, "Kind", PropertyType::Byte, 1);
    o.u8(3);
    tag(o, w, "Tag", PropertyType::Name, 1);
    o.index(w.name("Door"));
    tag(o, w, "Location", PropertyType::Struct, 12, 0, "Vector");
    o.vec(1, 2, 3);
    tag(o, w, "Rotation", PropertyType::Struct, 12, 0, "Rotator");
    o.i32(0), o.i32(16384), o.i32(0);
    tag(o, w, "Slots", PropertyType::Int, 4, 2);
    o.i32(42);
    Bytes s;
    s.str("hello", 69);
    tag(o, w, "Message", PropertyType::Str, s.b.size());
    o.bytes(s.b);
    endProps(o, w);
    o.u8(0xAB); // the class's own data
    // With a state frame first.
    Bytes withStack;
    withStack.index(5), withStack.index(5), withStack.u64(0), withStack.i32(0), withStack.index(12);
    withStack.bytes(o.b);
    w.addExport(0, 0, "Plain", o.b);
    const int32_t cls = w.addImport("Engine", "Class", 0, "Actor");
    w.exports[0].classRef = cls;
    w.addExport(cls, 0, "Stacked", withStack.b, objflag::HasStack);
    Package p;
    CHECK(!p.openMemory(w.build(), "Props"));
    for (int i = 0; i < 2; ++i) {
        std::vector<uint8_t> data;
        CHECK(!p.readExport(i, data));
        Reader r(data);
        PropertyList props;
        CHECK(!beginObject(p, i, r, props));
        CHECK(props.items.size() == 9);
        CHECK(props.integer("count") == -7 && props.number("Scale") == 1.5f && props.boolean("bOn"));
        CHECK(props.byte("Kind") == 3 && p.name(props.nameIndex("Tag")) == "Door");
        std::array<float, 3> v{};
        CHECK(props.vector("Location", v) && v[0] == 1 && v[1] == 2 && v[2] == 3);
        Rotator rot;
        CHECK(props.rotator("Rotation", rot) && rot.yaw == 16384);
        CHECK(props.integer("Slots", 0, 2) == 42 && props.integer("Slots", 9, 0) == 9);
        CHECK(props.string("Message", 69) == "hello");
        CHECK(r.u8() == 0xAB && r.ok());
    }
}

// A package with a 4x2 P8 texture (masked) and its palette.

void testSearchPathsAndDetection() {
    const std::string ini = "[Engine.Engine]\nPaths=nope\n[Core.System]\nPurgeCacheDays=30\n"
                            "Paths=..\\System\\*.u\r\nPaths=../Maps/*.unr\n  paths = ..\\Textures\\*.utx \n[Other]\n";
    CHECK(parseSearchPaths(ini) ==
          (std::vector<std::string>{"..\\System\\*.u", "../Maps/*.unr", "..\\Textures\\*.utx"}));

    const auto tmp = std::filesystem::temp_directory_path() / "reluva_unreal_test";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "system");
    std::filesystem::create_directories(tmp / "TEXTURES");
    std::ofstream(tmp / "system" / "CORE.U") << "x";
    std::ofstream(tmp / "system" / "Engine.u") << "x";
    CHECK(detect("unreal", tmp).size() == 1 && detect("unreal", tmp)[0].gameId == "ue1");
    std::ofstream(tmp / "system" / "ds9.ini") << "[Core.System]\nPaths=*.u\nPaths=..\\Textures\\*.utx\n";
    const auto found = detect("unreal", tmp);
    CHECK(found.size() == 1 && found[0].gameId == "ds9fallen");
    {
        const std::vector<uint8_t> pkg = texturePackage();
        std::ofstream(tmp / "TEXTURES" / "Walls.utx", std::ios::binary)
            .write(reinterpret_cast<const char *>(pkg.data()), std::streamsize(pkg.size()));
    }
    PackageSet set;
    std::vector<std::string> warnings;
    CHECK(!set.open(tmp, "DS9.ini", &warnings));
    CHECK(set.names() == (std::vector<std::string>{"CORE", "Engine", "Walls"}));
    const Package *walls = set.get("walls");
    CHECK(walls && walls->findExport("Brick") >= 0);
    CHECK(set.get("core") == nullptr && !set.error("core").empty()); // not a real package
    if (walls) {
        image::Image img;
        CHECK(!decodeTexture(set, *walls, walls->findExport("Brick"), img));
    }
    std::filesystem::remove_all(tmp);
    CHECK(findEngine("unreal") != nullptr);
}

void testMapExtension() {
    const auto tmp = std::filesystem::temp_directory_path() / "reluva_unreal_maps";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "System");
    std::filesystem::create_directories(tmp / "Maps");
    // No DS9.ini yet: the game makes it from Default.ini.
    std::ofstream(tmp / "System" / "Default.ini") << "[Core.System]\nPaths=..\\Maps\\*.dsm\n";
    std::ofstream(tmp / "Maps" / "Ops.dsm") << "x";
    std::ofstream(tmp / "Maps" / "Other.unr") << "x";
    PackageSet set;
    CHECK(!set.open(tmp, "DS9.ini"));
    CHECK(set.iniFile().filename() == "Default.ini");
    CHECK(set.mapNames() == std::vector<std::string>{"Ops"} && set.isMap("ops") && !set.isMap("nothing"));
    std::filesystem::remove_all(tmp);
}

void testDialogTable() {
    const std::string text = "Missions\t\tK01 M01\r\n"
                             "REC ID\tREC ID\tMISSION\tTYPE\tSPEAKER\tLISTENER\tLINE ID\tLINE\r\n"
                             "\t\t\t\t\t\t\t\t\r\n"
                             "KRA\t001\tK01\tHAL\tKRA\tOBN\t000\tChief \x97 quickly!\t\t2\tGENHALOBN0?0\t\tTAG1\tEvt\t"
                             "! CommunicationBlocked\t\"CAM(V:1 \"\"x\"\")\"\r\n";
    std::vector<DialogLine> lines;
    parseDialogTable(text, "M01_Dialogue", lines);
    CHECK(lines.size() == 1);
    if (lines.size() == 1) {
        const DialogLine &l = lines[0];
        CHECK(l.record == "KRA001" && l.mission == "K01" && l.type == "HAL" && l.listener == "OBN");
        CHECK(l.text == "Chief - quickly!" && l.pause == 2 && l.next == "GENHALOBN0?0");
        CHECK(l.tag == "TAG1" && l.event == "Evt" && l.condition == "! CommunicationBlocked");
        CHECK(l.camera == "CAM(V:1 \"x\")");
        CHECK(l.key() == "K01HALKRA000");
    }
    CHECK(dialogKeyMatches("GENHALOBN0?0", "GENHALOBN010"));
    CHECK(!dialogKeyMatches("GENHALOBN0?0", "GENHALOBN011"));
    CHECK(!dialogKeyMatches("GEN", "GENHAL"));
}

} // namespace

static const reluva::test::Suite kSuite("unreal.unit.packages", {
    {"compact index", [](const std::filesystem::path &) { testCompactIndex(); }},
    {"package tables", [](const std::filesystem::path &) { testPackageTables(); }},
    {"properties", [](const std::filesystem::path &) { testProperties(); }},
    {"search paths and detection", [](const std::filesystem::path &) { testSearchPathsAndDetection(); }},
    {"map extension", [](const std::filesystem::path &) { testMapExtension(); }},
    {"dialog table", [](const std::filesystem::path &) { testDialogTable(); }},
});
