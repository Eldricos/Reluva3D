// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/resources.h"
#include "common/font_truetype.h"
namespace reluva::boggle {
// Optional runtime data; placement and sampling remain native policies.
class PanelArt {
public:
    Error open(const Resources& resources,const std::filesystem::path& root,std::string_view path,int w,int h);
    bool draw(Surface& screen,int x,int y,int w,int h) const;
    void title(Surface& screen,int x,int y,std::string_view text,Color color,int width) const;
private:
    image::Image image_;
    font::TrueTypeAscii font_;
};
}
