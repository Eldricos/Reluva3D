// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <string>
namespace reluva::boggle {
struct DialogField {
    bool ordinal=false;
    uint16_t id=0;
    std::u16string text;
};
struct DialogRect {int x=0,y=0,w=0,h=0;};
struct DialogControl {
    uint32_t id=0,style=0,extendedStyle=0,helpId=0;
    DialogRect rect;
    DialogField windowClass,title;
    std::vector<uint8_t> creationData;
};
struct DialogTemplate {
    bool extended=false,hasFont=false;
    uint32_t style=0,extendedStyle=0,helpId=0;
    DialogRect rect;
    DialogField menu,windowClass;
    std::u16string title,typeface;
    uint16_t pointSize=0,weight=0;
    uint8_t italic=0,charset=0;
    std::vector<DialogControl> controls;
};
// Bounded standard/extended Windows dialog templates, read only as data.
// UTF-16 is validated; controls, IDs, styles and creation data are retained.
Error readDialog(const std::vector<uint8_t>& bytes,DialogTemplate& out);
} // namespace reluva::boggle
