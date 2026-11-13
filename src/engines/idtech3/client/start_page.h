// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/page.h"

#include "image/image.h"

#include <string>
#include <vector>

namespace reluva::idtech3 {

/**
 * Where a game starts: what to play, as the original's own programs were
 * chosen between (Elite Force's game and its Holomatch).
 *
 * Up/Down or the mouse pick, Enter or a click (or the choice's number)
 * starts it, Esc goes back to the launcher.
 */
class StartPage : public Page {
public:
    /** One thing to start. */
    struct Choice {
        std::string label;
        std::string detail;
        std::string page; ///< the page it starts on (Page::name)
    };
    StartPage(PageContext &ctx, std::vector<Choice> choices);

    const char *name() const override { return "Start"; }
    size_t count() const override { return _choices.size(); }
    void activate() override;
    void handleEvent(const Event &ev) override;

private:
    void draw();
    void choose(size_t i);
    int choiceAt(int x, int y) const; // -1 for none

    std::vector<Choice> _choices;
    size_t _selected = 0;
    image::Image _background; // the first level's picture, if the game has one
};

} // namespace reluva::idtech3
