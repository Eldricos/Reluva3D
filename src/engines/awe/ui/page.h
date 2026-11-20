// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "audio/mixer.h"
#include "common/events.h"
#include "graphics/gfx3d.h"
#include "image/image.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/engine.h"
#include "engines/awe/ui/game_text.h"

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The pages of the engine's 1024x768 window (the game's own size): the
// game's scenes (ScenePage), and the data viewer's pages, which name their
// keys at the bottom of the screen (help).

namespace reluva::awe {

struct GameInfo;

/**
 * Orient Express's frame, which its interface's layout is drawn for. A
 * page's own frame is its game's (Page::frameW, GameInfo::width).
 */
///@{
constexpr int kScreenW = 1024;
constexpr int kScreenH = 768;
///@}

/** What every page draws with: the window, the game's files and fonts, the sound. */
struct ViewerContext {
    System &sys;
    const Archive &files;
    audio::Mixer *mixer;              // null without an audio device
    Surface &screen;                  // the game's frame (GameInfo::width x height), RGBA32
    std::filesystem::path savePath;   // where screenshots go (F12)
    const GameFonts *fonts = nullptr; // the game's fonts, if they loaded
    // The window's title, the frame rate after it once the loop has
    // measured one (-1: not shown).
    std::string title;
    int fps = -1;
    const GameInfo *game = nullptr; // the game shown (games.h), if it's known
    /** Sets the window's title, the frame rate after it if it's shown. */
    void setTitle(const std::string &t) {
        title = t;
        sys.setWindowTitle(fps < 0 ? t : t + "  -  " + std::to_string(fps) + " fps");
    }
};

/** A page of the window: the game's (ScenePage), or one of the data viewer's. */
class Page {
public:
    /** A page drawing with `ctx`, which must outlive it. */
    explicit Page(ViewerContext &ctx) : _ctx(ctx) {}
    virtual ~Page() = default;
    /** The page's name, for the window's title and its screenshots' files. */
    virtual const char *name() const = 0;
    /** How many things it has to show; a page with none is passed over. */
    virtual size_t count() const = 0;
    /** Called when the page is shown, and before it's left. */
    ///@{
    virtual void activate() = 0;
    virtual void deactivate() {}
    ///@}
    /** An event from the window, those runPages() doesn't take itself. */
    virtual void handleEvent(const Event &ev) = 0;
    /** Each frame, `nowMs` the system's clock: 2D pages show the framebuffer again. */
    virtual void update(uint64_t nowMs) {
        (void)nowMs;
        present();
        finishScreenshot(nullptr, &_ctx.screen);
    }
    /** Whether it draws in 3D each frame, paced by presenting (vsync). */
    virtual bool uses3D() const { return false; }
    /** Whether Escape is the page's now (a movie skipped), not the way out. */
    virtual bool wantsEscape() const { return false; }
    /** Whether the page asks to go back to the launcher (the game's quit). */
    virtual bool quitRequested() const { return false; }
    /** Saves the next frame shown, as a TGA file (the window title says where). */
    void requestScreenshot(std::filesystem::path file) { _shotFile = std::move(file); }

protected:
    ViewerContext &_ctx;
    std::filesystem::path _shotFile;

    /** The game's frame, in pixels: the window, the scenes' pictures and cameras. */
    ///@{
    int frameW() const { return _ctx.screen.w; }
    int frameH() const { return _ctx.screen.h; }
    ///@}

    /**
     * For pages that draw in 3D: before endFrame, asks the renderer to
     * keep the frame if a screenshot is due.
     */
    void prepareScreenshot(gfx::Renderer3D *renderer) {
        if (renderer && !_shotFile.empty())
            renderer->captureNextFrame();
    }
    /**
     * After the frame: saves it (the 3D frame with `overlay` over it, or
     * `overlay` alone without a renderer) if a screenshot is due. With a
     * renderer that hasn't kept the frame yet, it tries again next frame.
     */
    void finishScreenshot(gfx::Renderer3D *renderer, const Surface *overlay);

    // Drawing a 2D page into the framebuffer (the game's frame, RGBA32).

    /** Sets the window up for 2D, the mouse cursor shown. */
    void begin2D();
    /** Fills the whole frame with one opaque colour. */
    void clear(uint8_t r, uint8_t g, uint8_t b);
    /** Fills a rectangle, clipped, over what's there by `alpha` (an overlay keeps its alpha). */
    void fill(int x, int y, int w, int h, Color c, uint8_t alpha = 255);
    /** A rectangle's one-pixel outline. */
    void frame(int x, int y, int w, int h, Color c);
    /** A movie's subtitles due at `seconds`, at the bottom of the screen, as the game shows them. */
    void drawSubtitles(const std::vector<Subtitle> &subtitles, double seconds);
    /** Text in the built-in font, its top-left at (x, y); returns the x after it. */
    int text(int x, int y, const std::string &s, Color c = {230, 230, 230}, int scale = 1);
    /** Text on a dark box, for reading over pictures. */
    void label(int x, int y, const std::string &s, Color c = {230, 230, 230});
    /** The help line at the bottom: `keys`, then the keys every page has. */
    void help(const std::string &keys);
    /** Shows the framebuffer in the window. */
    void present() { _ctx.sys.updateScreen(_ctx.screen); }
    /**
     * A list step from a key: -1/+1 for Left/Right, -10/+10 for
     * PageUp/PageDown, to the ends for Home/End, wrapping round the list's
     * ends (`count` items, `index` the one shown); 0 for other keys.
     */
    static int step(uint32_t key, int index, int count);

    /**
     * A picture from the game's files, cached; an empty image if it can't
     * be read. The cache is emptied once it holds more than 64, so the
     * reference is good until the next call.
     */
    const image::Image &picture(const std::string &name);
    std::map<std::string, image::Image> _pictures;
};

/** The pages to show, made once the window's context is set up. */
using MakePages = std::function<std::vector<std::unique_ptr<Page>>(ViewerContext &ctx)>;

/**
 * Runs pages in the engine's window, with the game's files, fonts and sound
 * (at the launcher's volume), until the player leaves (Escape no page takes,
 * or back to the launcher) or a page quits. The first page with something
 * to show starts; Tab moves on when there are several. F12 saves a
 * screenshot in the save folder.
 * @param nothing What none of the pages found ("scenes"), for the error.
 */
Error runPages(System &sys, const GameTarget &target, const MakePages &make, const std::string &nothing);

/** The part of `path` after its last slash or backslash. */
std::string fileNameOf(const std::string &path);

/** The part of `path` before its last slash or backslash ("" if none). */
std::string folderOf(const std::string &path);

/** Whether `name` ends with one of `exts` (".jpg"), whatever their case. */
bool hasExtension(const std::string &name, std::initializer_list<const char *> exts);

/**
 * The game's files under `folders` (lower case, "locations/"; none: every
 * folder) with one of `exts`, sorted whatever their case.
 */
std::vector<std::string> listFiles(const Archive &files, std::initializer_list<const char *> folders,
                                   std::initializer_list<const char *> exts);

/** `s` cut to `max` characters, "..." at its end if it was longer. */
std::string shorten(const std::string &s, size_t max);

} // namespace reluva::awe
