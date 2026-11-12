// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/image.h"
#include "common/pe_resources.h"
namespace reluva::boggle {
// Windows indexed RT_CURSOR bitmap, with the hotspot prefix and separate
// AND/XOR planes. XOR is preserved: some pixels invert the destination.
struct CursorImage {
    image::Image colors;
    std::vector<uint8_t> mask;
    unsigned hotspotX=0,hotspotY=0,depth=0;
    void draw(Surface& screen,int x,int y,int numerator=5,int denominator=4) const;
};
Error readCursorImage(const std::vector<uint8_t>& bytes,CursorImage& out);
// Exact language only. Select the highest indexed depth in a bounded group;
// every member must agree with its directory dimensions, depth and byte count.
Error readCursorGroup(const std::vector<PeResource>& resources,uint32_t id,uint16_t language,CursorImage& out);
} // namespace reluva::boggle
