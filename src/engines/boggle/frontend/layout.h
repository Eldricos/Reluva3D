// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/pe_resources.h"
#include "common/image.h"
#include <filesystem>
#include <array>
namespace reluva::boggle {
class Resources;
struct SelectionRect {
    int x,y,w,h;
    bool contains(int px,int py) const {return px>=x && py>=y && int64_t(px)<int64_t(x)+w && int64_t(py)<int64_t(y)+h;}
};
class FrontendSelectionLayout {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    const std::array<SelectionRect,5>& modes() const {return modes_;}
private:
    bool installed_=false;
    std::array<SelectionRect,5> modes_{{{40,288,112,112},{154,288,110,112},{266,288,110,112},{378,288,110,112},{490,288,110,112}}};
};
class FrontendVersionLayout {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    Error readBackground(const Resources& resources,const std::filesystem::path& root);
    bool drawBackground(Surface& screen) const;
    const image::Image& background() const {return background_;}
    bool installed() const {return installed_;}
    const std::array<SelectionRect,2>& modes() const {return modes_;}
    const SelectionRect& bounds() const {return bounds_;}
    const SelectionRect& prompt() const {return prompt_;}
    SelectionRect less() const {return {174,installed_?344:258,48,36};}
    SelectionRect more() const {return {418,installed_?344:258,48,36};}
    SelectionRect untimed() const {return installed_?SelectionRect{48,344,110,36}:SelectionRect{224,312,192,36};}
private:
    bool installed_=false;
    SelectionRect bounds_{},prompt_{};
    image::Image background_;
    std::array<SelectionRect,2> modes_{{{48,155,252,65},{340,155,252,65}}};
};
} // namespace reluva::boggle
