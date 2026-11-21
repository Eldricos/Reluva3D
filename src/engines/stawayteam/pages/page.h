// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstddef>
#include <cstdint>

namespace reluva::stawayteam {

/**
 * A page of the data viewer: one kind of the game's files, one at a time.
 * The viewer moves between pages with Tab and gives the shown one its keys.
 */
class Page {
public:
    virtual ~Page() = default;

    /** How many things it has to show; a page with none is passed over. */
    virtual size_t count() const = 0;
    /** Draws the page as it was left. */
    virtual void activate() = 0;
    /** Called as another page is shown: stops what plays. */
    virtual void deactivate() {}
    virtual void handleKey(uint32_t key) = 0;
    /** Called every frame, for what moves (milliseconds since the start). */
    virtual void update(uint64_t /*nowMs*/) {}
};

} // namespace reluva::stawayteam
