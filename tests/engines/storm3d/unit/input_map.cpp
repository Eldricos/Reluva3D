// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: Input.map, on texts written here in its forms.

#include "engines/storm3d/formats/input_map.h"

#include "tests/support/test.h"

using namespace reluva;
using reluva::storm3d::InputMap;

namespace {

// Blocks of conditions, with comments, tabs and a space after a brace.
void testParse() {
    const char *text = "####\r\n# Game Key\r\ncontrol {\r\n\t+ keyboard\tControl\r\n}\r\n"
                       "# Attack shortcut.\r\nattack {\r\n\t+ keyboard\tA\r\n\t- keyboard \tControl\r\n} \r\n"
                       "cursor_x {\r\n\t+ mouse\tHorizPos\r\n}\r\n";
    InputMap map;
    CHECK(!map.parse(text));
    CHECK(map.bindings.size() == 3);
    CHECK(map.problems.empty());
    const InputMap::Binding *attack = map.find("attack");
    CHECK(attack && attack->conditions.size() == 2);
    CHECK(attack && attack->conditions.size() == 2 && attack->conditions[0].down &&
          attack->conditions[0].control == "A" && !attack->conditions[1].down &&
          attack->conditions[1].control == "Control");
    CHECK(map.find("cursor_x") && map.find("cursor_x")->conditions[0].device == "mouse");
    CHECK(map.find("Attack") == nullptr);
}

// A line of no known form is reported; a block left open fails.
void testProblems() {
    InputMap map;
    CHECK(!map.parse("a {\n  * keyboard A\n}\n"));
    CHECK(map.problems.size() == 1);
    CHECK(map.parse("a {\n  + keyboard A\n"));
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.input_map",
                                        {
                                            {"parse", [](const std::filesystem::path &) { testParse(); }},
                                            {"problems", [](const std::filesystem::path &) { testProblems(); }},
                                        });
