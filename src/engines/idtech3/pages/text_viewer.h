// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/page.h"
#include "engines/idtech3/raven/fonts.h"
#include "engines/idtech3/raven/strings.h"

#include "image/image.h"

#include <string>
#include <vector>

namespace reluva::idtech3 {

// The data viewer's Text page: Raven's string packages (Jedi Outcast's
// strip/*.sp), each entry by its key, its text in one of the game's own
// fonts (fonts/*.fontdat) in any of its languages. Nothing for a game
// without them.
class TextViewer : public Page {
public:
    explicit TextViewer(PageContext &ctx);
    const char *name() const override { return "Text"; }
    size_t count() const override { return _strings.packages().size(); }
    void activate() override { show(); }
    void handleEvent(const Event &ev) override;

private:
    void show();
    // `text` in the current font at (x, y), each colour code's colour; how
    // far down it reached.
    int drawText(int x, int y, const std::string &text, int maxWidth);

    raven::StringTable _strings;
    std::vector<std::string> _warnings;
    struct LoadedFont {
        raven::Font font;
        image::Image image;
    };
    std::vector<LoadedFont> _fonts;
    int _package = 0;
    int _first = 0; // the first entry shown
    int _font = 0;
    raven::StringTable::Language _language = raven::StringTable::Language::kEnglish;
};

} // namespace reluva::idtech3
