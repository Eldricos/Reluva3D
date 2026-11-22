// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: object definition files (.odf), on texts written
// here in the forms Armada's take.

#include "engines/storm3d/formats/odf.h"

#include "tests/support/test.h"

#include <map>
#include <string>

using namespace reluva;
using reluva::storm3d::Odf;

namespace {

Odf::Loader loaderFor(const std::map<std::string, std::string> &files) {
    return [&files](std::string_view name, std::string &text) {
        const auto it = files.find(std::string(name));
        if (it == files.end())
            return Error::fail(std::string(name) + " not found");
        text = it->second;
        return Error::success();
    };
}

// A file's own lines override what it includes; lookups ignore case.
void testInclude() {
    const std::map<std::string, std::string> files = {
        {"craft.odf", "// all craft\r\nshieldDelay = 3.0f\r\nrangeScan = 500.0f\r\n"},
        {"destroy.odf", "#include \"craft.odf\"\r\nrangeScan = 600.0f\r\nclassLabel = \"craft\"\r\n"},
        {"fdestroy.odf", "#include \"destroy.odf\"\r\nunitName = \"Defiant Class\" // the ship\r\n"},
    };
    Odf odf;
    CHECK(!odf.load("fdestroy.odf", loaderFor(files)));
    CHECK(odf.entries().size() == 5);
    CHECK(odf.entries().front().file == "craft.odf");
    CHECK(odf.number("shielddelay") == 3.0);
    CHECK(odf.number("rangeScan") == 600.0);
    CHECK(odf.string("unitName") == "Defiant Class");
    CHECK(odf.string("CLASSLABEL") == "craft");
    CHECK(odf.find("missing") == nullptr);
    CHECK(odf.number("missing", -1) == -1);
    CHECK(odf.problems().empty());
}

// A list goes on over the lines after its key; sections are kept with
// their entries.
void testListsAndSections() {
    const std::map<std::string, std::string> files = {
        {"f.odf", "[Builder]\r\npossibleCraftNames =\r\n\t\"USS Odyssey\" \"USS Ulysses\"\r\n\t\"USS Argo\"\r\n"
                  "[GameObjectClass]\r\nhardpoints = \"hp20\" \"hp21\"\r\n"},
    };
    Odf odf;
    CHECK(!odf.load("f.odf", loaderFor(files)));
    CHECK(odf.entries().size() == 2);
    CHECK(odf.entries()[0].section == "Builder");
    CHECK(odf.entries()[1].section == "GameObjectClass");
    CHECK((odf.strings("possibleCraftNames") == std::vector<std::string>{"USS Odyssey", "USS Ulysses", "USS Argo"}));
    CHECK(odf.strings("hardpoints").size() == 2);
}

// Values as the game's files have them: `f` suffixes, a stray `;`, an
// unclosed quote; and a line that's no form at all is reported.
void testValues() {
    const std::map<std::string, std::string> files = {
        {"f.odf", "a = 0.75f\nb = 10;\nc = \"SpecialAttack\"t\"\nd = -2\nstray line\n"},
    };
    Odf odf;
    CHECK(!odf.load("f.odf", loaderFor(files)));
    CHECK(odf.number("a") == 0.75);
    CHECK(odf.number("b") == 10);
    CHECK(odf.string("c") == "SpecialAttack");
    CHECK(odf.number("d") == -2);
    CHECK(odf.number("c", 5) == 5);
    CHECK(odf.problems().size() == 1);
}

// A missing include, and includes that loop, fail.
void testFailures() {
    Odf odf;
    CHECK(odf.load("a.odf", loaderFor({{"a.odf", "#include \"b.odf\"\n"}})));
    CHECK(odf.load("a.odf", loaderFor({{"a.odf", "#include \"a.odf\"\n"}})));
    CHECK(odf.load("none.odf", loaderFor({})));
}

} // namespace

static const reluva::test::Suite
    kSuite("storm3d.unit.odf",
           {
               {"include", [](const std::filesystem::path &) { testInclude(); }},
               {"lists and sections", [](const std::filesystem::path &) { testListsAndSections(); }},
               {"values", [](const std::filesystem::path &) { testValues(); }},
               {"failures", [](const std::filesystem::path &) { testFailures(); }},
           });
