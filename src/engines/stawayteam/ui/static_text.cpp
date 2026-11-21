// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/static_text.h"

#include "engines/stawayteam/ui/layout.h"

#include <cstdlib>

namespace reluva::stawayteam {

void StaticText::draw(Surface &screen, int originX, int originY) const {
    if (font.loaded())
        font.drawWrapped(screen, originX + x, originY + y, wrap, text, align, 2);
}

std::vector<StaticText> readStaticTexts(const Archive &data, const TextObject *definition) {
    std::vector<StaticText> out;
    const TextObject *nernies = definition ? definition->object("Nernie") : nullptr;
    for (const TextField *item : nernies ? nernies->items() : std::vector<const TextField *>{}) {
        const TextObject *nernie = item->object.get();
        if (!nernie || nernie->className != "CStaticTextNernie")
            continue;
        StaticText t;
        readPoint(nernie, "Position", t.x, t.y);
        t.text = readText(nernie, "Text");
        (void)t.font.load(data, readText(nernie, "Font"));
        const std::string justify = readText(nernie, "Justification");
        t.align = justify == "Right"    ? Font::Align::kRight
                  : justify == "Center" ? Font::Align::kCenter
                                        : Font::Align::kLeft;
        t.wrap = std::atoi(readText(nernie, "Wrap Width").c_str());
        out.push_back(std::move(t));
    }
    return out;
}

} // namespace reluva::stawayteam
