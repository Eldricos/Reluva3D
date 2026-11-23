// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: which files the data viewer's Images page shows,
// and in what order.

#include "engines/storm3d/pages/image_page.h"

#include "tests/support/test.h"

#include <algorithm>
#include <string>
#include <vector>

using reluva::storm3d::ImagePage;

namespace {

// By folder (ignoring case), then by name; a texture before its mipmap
// levels, and the odd extensions after those.
void testOrder() {
    std::vector<std::string> names = {"Textures/RGB/Bresear.2",   "Textures/RGB/Fpod.56",     "Textures/RGB/Bresear.1",
                                      "Textures/Index8/Xspark01", "Textures/RGB/Bresear.tga", "Textures/RGB/Fpod.tga",
                                      "bitmaps/main/mainbk.BMP"};
    std::sort(names.begin(), names.end(), ImagePage::listedBefore);
    const std::vector<std::string> expected = {
        "bitmaps/main/mainbk.BMP", "Textures/Index8/Xspark01", "Textures/RGB/Bresear.tga", "Textures/RGB/Bresear.1",
        "Textures/RGB/Bresear.2",  "Textures/RGB/Fpod.tga",    "Textures/RGB/Fpod.56"};
    CHECK(names == expected);
}

// TGAs and BMPs anywhere, and in Textures/ anything but the palette and
// Paint Shop Pro's browse file.
void testWhich() {
    CHECK(ImagePage::isPicture("bzn/2blue.bmp"));
    CHECK(ImagePage::isPicture("Textures/RGB/Bresear.TGA"));
    CHECK(ImagePage::isPicture("Textures/RGB/Bresear.3"));
    CHECK(ImagePage::isPicture("Textures/Index8/Xspark01"));
    CHECK(!ImagePage::isPicture("Textures/RGB/StarTrek.pal"));
    CHECK(!ImagePage::isPicture("Textures/RGB/pspbrwse.jbf"));
    CHECK(!ImagePage::isPicture("SOD/Bbase.SOD"));
    CHECK(!ImagePage::isPicture("Armada.pal"));
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.images",
                                        {
                                            {"order", [](const std::filesystem::path &) { testOrder(); }},
                                            {"which", [](const std::filesystem::path &) { testWhich(); }},
                                        });
