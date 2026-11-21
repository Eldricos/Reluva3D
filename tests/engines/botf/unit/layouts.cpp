// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's unit tests: the .wdf window layouts, the .mnu menus and the
// pictures' colour key.

#include "engines/botf/formats/menu_file.h"
#include "engines/botf/formats/window_layout.h"
#include "engines/botf/ui/assets.h"
#include "image/image.h"

#include "tests/engines/botf/unit/helpers.h"
#include "tests/support/test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::botf;
using namespace reluva::botf::test;

namespace {

void testWindowLayout() {
    Bytes f;
    put32(f, 3); // the file's kind
    putWidget(f, 1, 100, 0, 0, 800, 600, "bkg.tga");
    putWindowCounts(f, {{1, 1}, {2, 1}, {14, 1}});
    // A button: unknown, kind, repeats, rate, normal, selected.
    putWidget(f, 2, 101, 10, 20, 60, 30, "none");
    put32(f, 7);
    f.push_back(2);
    put32(f, 1);
    put32(f, 5);
    putName(f, "BTN_OFF.TGA");
    putName(f, "btn_on.tga\0junk");
    // A text box: text rectangle, font.
    putWidget(f, 14, 102, 100, 200, 300, 40, "none");
    put32(f, 4);
    put32(f, 2);
    put32(f, 290);
    put32(f, 36);
    putFont(f, 255, 128, 0, "human.fnt");
    // A window inside, at (50, 60), with a picture at (5, 5) in it.
    putWidget(f, 1, 103, 50, 60, 200, 100, "none");
    putWindowCounts(f, {{13, 1}});
    putWidget(f, 13, 104, 5, 5, 20, 10, "pic.tga");
    put32(f, 0);

    WindowLayout layout;
    CHECK(!layout.load(f));
    CHECK(layout.kind() == 3);
    const Widget &root = layout.root();
    CHECK(root.type == WidgetType::kWindow && root.id == 100 && root.w == 800 && root.background == "bkg.tga");
    CHECK(root.children.size() == 3);
    const Widget &button = root.children[0];
    CHECK(button.type == WidgetType::kButton && button.x == 10 && button.y == 20 && button.background.empty());
    CHECK(button.buttonKind == 2 && button.repeats && button.repeatsPerSecond == 5);
    CHECK(button.pictures.size() == 2 && button.pictures[0].second == "btn_off.tga" &&
          button.pictures[1].first == "selected" && button.pictures[1].second == "btn_on.tga");
    const Widget &text = root.children[1];
    CHECK(text.type == WidgetType::kTextBox && text.textX == 4 && text.textW == 290);
    CHECK(text.fonts.size() == 1 && text.fonts[0].file == "human.fnt" && text.fonts[0].own);
    CHECK(text.fonts[0].color.r == 255 && text.fonts[0].color.g == 128 && text.fonts[0].color.b == 0);
    const Widget &inner = root.children[2];
    CHECK(inner.children.size() == 1 && inner.children[0].type == WidgetType::kPicture);

    // Screen positions add up through the windows.
    const auto placed = layout.placed();
    CHECK(placed.size() == 5);
    CHECK(placed[4].widget->id == 104 && placed[4].x == 55 && placed[4].y == 65 && placed[4].depth == 2);

    // Cut short, or a type that isn't known: an error, not a crash.
    Bytes cut(f.begin(), f.begin() + 120);
    CHECK(layout.load(cut));
    Bytes odd = f;
    odd[4 + 4 + 33 + 64] = 99; // the first child's type
    CHECK(layout.load(odd));
    CHECK(layout.load(Bytes(3, 0)));
}

void testMenuFile() {
    // Two items: number, picture, label, then the constant tail.
    Bytes f;
    put32(f, 1);
    put32(f, 2);
    auto item = [&](uint32_t id, const std::string &pic, const std::string &label) {
        put32(f, id);
        putName(f, pic);
        std::string l = label;
        l.resize(31, '\0');
        f.insert(f.end(), l.begin(), l.end());
        f.resize(f.size() + 40, 0);
    };
    item(1, "none", "Solo Charge");
    item(900, "ICON.TGA", "Select Ship");
    CHECK(f.size() == 8 + 2 * 88);
    MenuFile m;
    CHECK(!m.load(f));
    CHECK(m.items().size() == 2);
    CHECK(m.items()[0].id == 1 && m.items()[0].picture.empty() && m.items()[0].label == "Solo Charge");
    CHECK(m.items()[1].id == 900 && m.items()[1].picture == "icon.tga" && m.items()[1].label == "Select Ship");
    f[4] = 3; // more items than the file holds
    CHECK(m.load(f));
    CHECK(m.load(Bytes(4, 0)));
}

void testColorKey() {
    image::Image img;
    img.w = 4;
    img.h = 1;
    img.rgba = {255, 0, 255, 255, 248, 0, 248, 255, 255, 0, 254, 255, 255, 40, 255, 255};
    CHECK(applyColorKey(img) == 3);
    CHECK(img.rgba[3] == 0 && img.rgba[7] == 0 && img.rgba[11] == 0 && img.rgba[15] == 255);

    // No magenta, and yellow in the top left corner: yellow is the key.
    img.rgba = {255, 255, 0, 255, 10, 20, 30, 255, 255, 255, 0, 255, 255, 0, 0, 255};
    CHECK(applyColorKey(img) == 2);
    CHECK(img.rgba[3] == 0 && img.rgba[7] == 255 && img.rgba[11] == 0 && img.rgba[15] == 255);
    // With magenta anywhere, yellow is a colour like any other.
    img.rgba = {255, 255, 0, 255, 255, 0, 255, 255, 255, 255, 0, 255, 1, 2, 3, 255};
    CHECK(applyColorKey(img) == 1);
    CHECK(img.rgba[3] == 255 && img.rgba[7] == 0 && img.rgba[11] == 255);
}

// A picture dimmed for a button that can't be used: its colours scaled,
// its colour key kept, and the picture itself left as it was.
void testDimmed() {
    image::Image img;
    img.w = 2;
    img.h = 1;
    img.rgba = {200, 100, 50, 255, 255, 0, 255, 255};
    MemoryArchive archive;
    archive.files["b.tga"] = image::encodeTga(img, true);
    Assets assets(archive);
    const image::Image *dim = assets.dimmed("b.tga", 35);
    CHECK(dim && dim->rgba[0] == 70 && dim->rgba[1] == 35 && dim->rgba[2] == 17 && dim->rgba[3] == 255);
    CHECK(dim && dim->rgba[7] == 0); // magenta stays see-through
    CHECK(assets.dimmed("b.tga", 35) == dim && assets.picture("b.tga")->rgba[0] == 200);
    CHECK(assets.dimmed("missing.tga", 35) == nullptr);
}

} // namespace

static const reluva::test::Suite
    kSuite("botf.unit.layouts", {
                                    {"window layouts", [](const std::filesystem::path &) { testWindowLayout(); }},
                                    {"menus", [](const std::filesystem::path &) { testMenuFile(); }},
                                    {"colour key", [](const std::filesystem::path &) { testColorKey(); }},
                                    {"dimmed pictures", [](const std::filesystem::path &) { testDimmed(); }},
                                });
