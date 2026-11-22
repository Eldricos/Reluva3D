// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: the settings files in the game's C-like language,
// on texts written here in the forms Armada's take.

#include "engines/storm3d/formats/config_file.h"

#include "tests/support/test.h"

#include <map>
#include <string>

using namespace reluva;
using reluva::storm3d::ConfigFile;

namespace {

ConfigFile::Loader loaderFor(const std::map<std::string, std::string> &files) {
    return [&files](std::string_view name, std::string &text) {
        const auto it = files.find(std::string(name));
        if (it == files.end())
            return Error::fail(std::string(name) + " not found");
        text = it->second;
        return Error::success();
    };
}

// Typed variables, numbers with `f`, negative and worked out from others,
// strings without escapes, comments of both kinds, and an include.
void testVariables() {
    const std::map<std::string, std::string> files = {
        {"ART_CFG.h", "float FAR = 80000.0;\r\nint COUNT = 400;\r\n"},
        {"RTS_CFG.h", "// camera\r\n#include \"ART_CFG.h\"\r\n/* old:\r\nint X = 1;\r\n*/\r\n"
                      "float OVERVIEW_MAX_ZOOM = 4.0f;\r\nint PRIORITY = -2;\r\nfloat SCALE = FAR*0.02;\r\n"
                      "char DIR[256] = \".\\missions\";\r\nconst double HALF = (1 + 2) / 6;\r\n"
                      "DETAIL_1_show_grid = 0;\r\n"},
    };
    ConfigFile config;
    CHECK(!config.load("RTS_CFG.h", loaderFor(files)));
    CHECK(config.number("OVERVIEW_MAX_ZOOM") == 4.0);
    CHECK(config.number("COUNT") == 400);
    CHECK(config.number("PRIORITY") == -2);
    CHECK(config.number("SCALE") == 1600);
    CHECK(config.string("DIR") == ".\\missions");
    CHECK(config.number("HALF") == 0.5);
    CHECK(config.number("DETAIL_1_show_grid", 5) == 0);
    CHECK(config.find("X") == nullptr);
    CHECK(config.number("overview_max_zoom", -1) == -1);
    CHECK(config.problems().empty());
}

// Defines are settings too: numbers, strings, and expressions of others.
void testDefines() {
    const std::map<std::string, std::string> files = {
        {"c.cfg", "#define GRID 256.0\n#define NAME \"mbgstars.sod\" // the stars\n#define TWICE GRID*2\n"
                  "#define LOOP LOOP\n#define MAX 3\nint list[MAX] = {1, 2, 3,};\n"},
    };
    ConfigFile config;
    CHECK(!config.load("c.cfg", loaderFor(files)));
    CHECK(config.number("GRID") == 256);
    CHECK(config.string("NAME") == "mbgstars.sod");
    CHECK(config.number("TWICE") == 512);
    CHECK(config.find("LOOP") == nullptr);
    const ConfigFile::Value *list = config.find("list");
    CHECK(list && list->kind == ConfigFile::Value::Kind::kList && list->items.size() == 3);
}

// An array of structures written as one flat list is grouped by fields;
// a string may run over lines.
void testStructs() {
    const std::map<std::string, std::string> files = {
        {"a.aip", "struct Entry\r\n{\r\n\tchar key[256];\r\n\tchar text[256];\r\n};\r\n"
                  "Entry translations[10] =\r\n{\r\n\t\"GUI_A\", \"one\",\r\n\t\"GUI_B\", \"two\r\nlines\",\r\n};\r\n"},
    };
    ConfigFile config;
    CHECK(!config.load("a.aip", loaderFor(files)));
    CHECK(config.structs().count("Entry") && config.structs().at("Entry").fields.size() == 2);
    const ConfigFile::Value *t = config.find("translations");
    CHECK(t && t->items.size() == 2);
    CHECK(t && t->items.size() == 2 && t->items[1].items.size() == 2 && t->items[1].items[1].text == "two\nlines");
}

// What the game's files have but can't be read is skipped and reported: a
// name used before it's set, and a #DATA block.
void testSkipped() {
    const std::map<std::string, std::string> files = {
        {"s.h", "float SCALE = LATER*2;\nfloat LATER = 1;\n#DATA\n\"Slush\", 1, 40;\n#END_DATA\nint after = 1;\n"},
    };
    ConfigFile config;
    CHECK(!config.load("s.h", loaderFor(files)));
    CHECK(config.find("SCALE") == nullptr);
    CHECK(config.number("LATER") == 1);
    CHECK(config.number("after") == 1);
    CHECK(config.problems().size() == 2);
}

// Text that isn't the language fails, with where.
void testFailures() {
    ConfigFile config;
    CHECK(config.load("a.h", loaderFor({{"a.h", "widget w = 1;\n"}})));
    CHECK(config.load("a.h", loaderFor({{"a.h", "char s[4] = \"open;\n"}})));
    CHECK(config.load("a.h", loaderFor({{"a.h", "int x = 1\n"}})));
    CHECK(config.load("a.h", loaderFor({{"a.h", "#include \"a.h\"\n"}})));
    const Error err = config.load("a.h", loaderFor({{"a.h", "int x = 1;\nint y = ;\n"}}));
    CHECK(err && err.message.find("a.h:2") != std::string::npos);
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.config",
                                        {
                                            {"variables", [](const std::filesystem::path &) { testVariables(); }},
                                            {"defines", [](const std::filesystem::path &) { testDefines(); }},
                                            {"structs", [](const std::filesystem::path &) { testStructs(); }},
                                            {"skipped", [](const std::filesystem::path &) { testSkipped(); }},
                                            {"failures", [](const std::filesystem::path &) { testFailures(); }},
                                        });
