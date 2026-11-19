// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/events.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/engine.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/games.h"

#include <functional>
#include <memory>
#include <vector>

namespace reluva::nfs {

/** The window's size for the 2D pages: the game's own 640x480. */
constexpr int kScreenWidth = 640;
constexpr int kScreenHeight = 480;

/**
 * What fills the engine's window: the game (PlayPage), or one of the data
 * viewer's pages.
 *
 * A 2D page draws into the 640x480 RGBA32 screen when asked; a 3D page
 * takes the GPU renderer while shown and draws a frame each update.
 */
class Page {
public:
    virtual ~Page() = default;
    virtual const char *name() const = 0;
    /** Whether there's anything to show; pages without are skipped. */
    virtual bool empty() const { return false; }
    /**
     * Called when the page is shown. A 3D page takes the renderer here;
     * runPages puts the framebuffer back after it's left.
     */
    virtual void activate(System & /*sys*/) {}
    /** Called before the page is left. */
    virtual void deactivate() {}
    /** Whether Esc goes to the page rather than back to the launcher. */
    virtual bool capturesEscape() const { return false; }
    /** Whether the page is done (the game it ran has ended): back to the launcher. */
    virtual bool finished() const { return false; }
    /**
     * Why the page can't go on (the game couldn't start, or stopped), for
     * runPages to end the game's run with; success while it can.
     */
    virtual Error failure() const { return Error::success(); }
    /** Whether the page is drawing with the GPU (else draw() is used). */
    virtual bool uses3D() const { return false; }
    virtual void handleEvent(const Event &ev) {
        if (ev.type == EventType::kKeyDown)
            handleKey(ev.key, ev.mods);
    }
    virtual void handleKey(uint32_t /*key*/, uint16_t /*mods*/) {}
    /** 2D pages: draws the page into the screen, after any event. */
    virtual void draw(Surface & /*screen*/) {}
    /** 3D pages: draws a frame. */
    virtual void update(uint64_t /*nowMs*/) {}
    /**
     * 2D pages: called often while shown, for playback.
     * @return Whether the page needs drawing again.
     */
    virtual bool tick(uint64_t /*nowMs*/) { return false; }
};

/** The pages to run, made once the game's folders are found; `mixer` is null without audio output. */
using MakePages = std::function<std::vector<std::unique_ptr<Page>>(const GameFolders &folders, audio::Mixer *mixer)>;

/**
 * Runs pages for the game in `target`, as an Engine's run(): until the
 * player leaves (Esc, unless the page takes it), the launcher is asked
 * for, or the page is finished.
 *
 * With `browse` (the data viewer), Tab and Shift+Tab move between the
 * pages, each named in the window's title, and a page that fails says so
 * on itself. Without (the game), the first page has the window to itself,
 * and its failure ends the run.
 * @return An error if the game's tracks aren't found, the window can't be
 *         opened, or (without `browse`) the page fails.
 */
Error runPages(System &sys, const GameTarget &target, const GameInfo &game, const MakePages &makePages, bool browse);

} // namespace reluva::nfs
