// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/engine.h"
#include "engines/idtech3/client/page.h"
#include "engines/idtech3/client/start_page.h"
#include "engines/idtech3/formats/shader.h"
#include "engines/idtech3/games.h"

#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::idtech3 {

/**
 * A game run: its files mounted, its settings, and its pages (the Start
 * page, single player, Holomatch's menus and play) until the player
 * leaves.
 *
 * Esc a page doesn't take goes back to the Start page, and Esc there
 * leaves. A mod chosen in the menus mounts the files again and builds the
 * pages anew. A subclass may add pages of its own (the data viewer's).
 */
class Session : public Engine {
public:
    /** The pages shown, the Start page first. */
    using Pages = std::vector<std::unique_ptr<Page>>;

    /** The game `target` names; Elite Force for an id this engine doesn't know. */
    Session(System &system, GameTarget target);

    Error run() override;

protected:
    /**
     * Adds pages after the game's own (single player, Menu, Play), and the
     * Start page's choices for them; called each time the pages are built.
     */
    virtual void addPages(PageContext &ctx, Pages &pages, std::vector<StartPage::Choice> &choices);

    /**
     * The page a key the shown page doesn't take switches to, if any (Esc
     * is the session's own).
     * @param page The shown page's index in `pages`.
     */
    virtual std::optional<size_t> pageKey(const Event &ev, const Pages &pages, size_t page);

    /** Called after each switch from one page to another, by their indices. */
    virtual void switched(size_t from, size_t to);

private:
    Error mountFiles(const std::string &mod, const std::vector<std::string> &skip, std::vector<std::string> &warnings);
    void startSettings(PageContext &ctx);
    Error runPages(PageContext &ctx, const std::string &resume, std::string &switchedFrom);

    System &_sys;
    GameTarget _target;
    const GameInfo &_game;
    SearchSet _files;
    ShaderLibrary _shaders;
    Surface _screen;
};

} // namespace reluva::idtech3
