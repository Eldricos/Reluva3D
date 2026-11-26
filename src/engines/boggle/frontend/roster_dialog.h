// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/frontend/layout.h"
#include "engines/boggle/classic/computer.h"
#include "common/image.h"
#include "engines/boggle/frontend/panel_art.h"
#include <string_view>
namespace reluva::boggle {
class FrontendRosterDialog {
public:
    Error read(const std::vector<PeResource>& resources,uint16_t language=0x0409);
    bool installed() const {return installed_;}
    Error artwork(const Resources& resources,const std::filesystem::path& root,bool master){return installed_?art_.open(resources,root,master?"Bitmaps/ClsM1PN.bmp":"Bitmaps/ClsO1PN.bmp",550,200):Error::success();}
    const std::array<SelectionRect,4>& fields() const {return fields_;}
    const SelectionRect& done() const {return done_;}
    const SelectionRect& cancel() const {return cancel_;}
    void draw(Surface& screen,const std::array<std::string,4>& roster,const std::array<ComputerLevel,4>& kinds,std::string_view error) const;
private:
    bool installed_=false;
    PanelArt art_;
    std::string captionText_,doneText_,cancelText_;
    SelectionRect bounds_{},message_{},done_{340,388,252,48},cancel_{48,388,252,48};
    bool portraits_=false;
    std::array<SelectionRect,4> portraitsRects_{};
    std::array<image::Image,3> portraitsImages_{};
    std::array<SelectionRect,4> fields_{{{72,142,496,38},{72,192,496,38},{72,242,496,38},{72,292,496,38}}};
};
} // namespace reluva::boggle
