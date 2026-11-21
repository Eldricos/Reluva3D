// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The menus' parts: the game's fonts as its text is drawn with them,
// buttons, their labels, shortcuts and clicks, and the credits' lines.

#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/credits.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/widgets.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <map>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::stawayteam;

namespace {

// Files in memory, by name as given.
class MemoryArchive : public Archive {
public:
    std::map<std::string, std::vector<uint8_t>, std::less<>> files;

    std::vector<std::string> list() const override {
        std::vector<std::string> names;
        for (const auto &[name, data] : files)
            names.push_back(name);
        return names;
    }
    bool has(std::string_view name) const override { return files.find(name) != files.end(); }
    Error read(std::string_view name, std::vector<uint8_t> &out) const override {
        const auto it = files.find(name);
        if (it == files.end())
            return Error::fail(std::string(name) + " isn't there");
        out = it->second;
        return Error::success();
    }
    std::string describe() const override { return "memory"; }
};

void testFont() {
    MemoryArchive data;
    // A and B, 3 and 4 pixels wide: a font without lower case.
    data.files["Cache/Fonts/Test.seq16"] = fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65);
    Font font;
    CHECK(font.load(data, "Missing"));
    CHECK(!font.load(data, "Test") && font.loaded());
    CHECK(font.width("AB") == 7);
    CHECK(font.width("ab") == 7); // drawn as capitals
    CHECK(font.width("A\x01"
                     "B") == 7);  // control characters left out
    CHECK(font.width("Z?") == 6); // characters it hasn't got drawn as its first
    CHECK(font.baseline() == 11);
}

void testButtons() {
    MemoryArchive data;
    data.files["Cache/Interface/Screens/Menu/Button Default.frm16"] = solidFrame(40, 10, 0x001f);
    ButtonGroup group;
    group.setOrigin(100, 50);
    for (int i = 0; i < 2; ++i) {
        Button button(data, "Menu", "Button", 0, i * 20);
        button.setLabel(i == 0 ? "PLAY &NETWORK GAME" : "&QUIT", nullptr, 0, 0);
        group.add(std::move(button));
    }
    CHECK(group.buttons()[0].label() == "PLAY NETWORK GAME" && group.buttons()[0].shortcut() == 'n');
    CHECK(group.buttons()[1].width() == 40 && group.buttons()[1].shortcut() == 'q');

    Event ev;
    ev.type = EventType::kKeyDown;
    ev.key = 'q';
    CHECK(group.handleEvent(ev, nullptr) == 1);
    ev.mods = keymod::kCtrl;
    CHECK(group.handleEvent(ev, nullptr) == -1);
    // A click: down and up on the same button chooses it; let go off it, nothing.
    Event mouse;
    mouse.button = MouseButton::kLeft;
    mouse.type = EventType::kMouseDown;
    mouse.mouseX = 110;
    mouse.mouseY = 55;
    CHECK(group.handleEvent(mouse, nullptr) == -1);
    mouse.type = EventType::kMouseUp;
    CHECK(group.handleEvent(mouse, nullptr) == 0);
    mouse.type = EventType::kMouseDown;
    CHECK(group.handleEvent(mouse, nullptr) == -1);
    mouse.type = EventType::kMouseUp;
    mouse.mouseY = 75;
    CHECK(group.handleEvent(mouse, nullptr) == -1);
}

void testCredits() {
    // Comments left out, blank lines kept, the line ends' CR dropped, and
    // a line cut at 255 characters.
    const Font font;
    const CreditsRoll roll("// comment\r\n_Heading\r\n\r\nRole: Name\n/also a comment\n" + std::string(300, 'x'), font);
    CHECK(roll.lines().size() == 4);
    CHECK(roll.lines()[0] == "_Heading" && roll.lines()[1].empty() && roll.lines()[2] == "Role: Name");
    CHECK(roll.lines()[3].size() == 255);
}

void testWidgets() {
    MemoryArchive data;
    data.files["Cache/Fonts/Test.seq16"] = fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65);
    Font font;
    CHECK(!font.load(data, "Test"));
    image::Image picture;
    picture.w = 40;
    picture.h = 10;
    picture.rgba.assign(40 * 10 * 4, 200);
    // Dimmed, its colours scaled and its alpha kept.
    const image::Image dim = dimmed(picture, 0.5f);
    CHECK(dim.rgba[0] == 100 && dim.rgba[3] == 200);

    PictureButton button(picture, "CA&NCEL", &font);
    button.setPosition(10, 20);
    CHECK(button.label() == "CANCEL");
    Event ev;
    ev.type = EventType::kKeyDown;
    ev.key = 'n';
    CHECK(button.handleEvent(ev, 100, 100, nullptr));
    Event mouse;
    mouse.button = MouseButton::kLeft;
    mouse.mouseX = 115;
    mouse.mouseY = 125;
    mouse.type = EventType::kMouseDown;
    CHECK(!button.handleEvent(mouse, 100, 100, nullptr));
    mouse.type = EventType::kMouseUp;
    CHECK(button.handleEvent(mouse, 100, 100, nullptr));

    // A toggle: its box, the label, and 1 between them.
    image::Image box;
    box.w = box.h = 5;
    box.rgba.assign(5 * 5 * 4, 255);
    Toggle toggle(box, box, "AB", &font);
    CHECK(toggle.width() == 5 + 7 + 1 && toggle.height() == 5);
    mouse.mouseX = 2;
    mouse.mouseY = 2;
    mouse.type = EventType::kMouseDown;
    CHECK(!toggle.handleEvent(mouse, 0, 0));
    mouse.type = EventType::kMouseUp;
    CHECK(toggle.handleEvent(mouse, 0, 0));
}

} // namespace

static const reluva::test::Suite kSuite("stawayteam.unit.menus",
                                        {
                                            {"font", [](const std::filesystem::path &) { testFont(); }},
                                            {"buttons", [](const std::filesystem::path &) { testButtons(); }},
                                            {"credits", [](const std::filesystem::path &) { testCredits(); }},
                                            {"widgets", [](const std::filesystem::path &) { testWidgets(); }},
                                        });
