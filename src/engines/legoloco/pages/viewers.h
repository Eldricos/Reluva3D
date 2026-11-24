// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/sound.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/legoloco/formats/board.h"
#include "engines/legoloco/formats/object_definition.h"
#include "engines/legoloco/game_files.h"
#include "image/bmp.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// LEGO Loco's data viewer, in reluva-viewer: pages drawing into a 1024x768
// RGBA32 screen, the size of a 64 by 48 board of 16-pixel squares
// (docs/engines/legoloco/systems/data-viewer.md).

namespace reluva::legoloco {

/** The viewer's screen, in pixels. */
///@{
constexpr int kScreenW = 1024;
constexpr int kScreenH = 768;
///@}

/** A board square's size in pixels, which the pieces' pictures are drawn in. */
constexpr int kSquare = 16;

/** A page of the viewer. */
class Page {
public:
    virtual ~Page() = default;
    /** Its name, for the status line. */
    virtual const char *name() const = 0;
    /** How many items it shows; the viewer skips a page with none. */
    virtual size_t count() const = 0;
    /** Draws the page as it's shown. */
    virtual void activate() = 0;
    /** Called as another page is shown. */
    virtual void deactivate() {}
    /** A key pressed on the page; it draws again. */
    virtual void handleKey(uint32_t key) = 0;
    /** Called every pass of the viewer's loop, with the system's clock. */
    virtual void update(uint64_t nowMs) { (void)nowMs; }
};

/** A picture and, if it has one, the definition beside it. */
struct Picture {
    image::Bitmap bitmap;
    bool hasDefinition = false;
    ObjectDefinition definition;
    int frames = 1;
    int frameW = 0; // frameWidth() of the bitmap's width

    /** The number of rows of squares its footprint fills, or 1. */
    int footprintRows() const;
};

/**
 * Reads a picture from the resource file, and its `.dat` if there is one.
 * A `.but` (a toolbox button) is read without one.
 */
Error loadPicture(const GameFiles &files, size_t entry, Picture &out);

/**
 * Draws frame `frame` of a picture with its top left at (x, y), in its own
 * palette, each pixel `scale` pixels square; index 0 left out when
 * `transparent`, mirrored when `flip`.
 */
void drawFrame(Surface &dst, const Picture &picture, int frame, int x, int y, int scale, bool transparent,
               bool flip = false);

/** Fills a rectangle of an RGBA32 surface, clipped. */
void fillRect(Surface &s, int x, int y, int w, int h, Color c);

/** Up to three lines of status at the screen's foot, on a dark band. */
void drawStatus(Surface &s, const std::string &line1, const std::string &line2 = {}, const std::string &line3 = {});

/**
 * The pictures (`.bmp`, `.but`) of the resource file, each cut into its
 * frames by its definition, and its frame sets played.
 *   Left/Right: previous/next   PageUp/PageDown: jump 20   Home/End: first/last
 *   Up/Down: previous/next frame set   Space: play or stop the frame set
 *   ,/.: previous/next frame   W: the whole picture or the frames
 *   Z: zoom (fitted to the screen, then 1, 2, 4, 8)   K: index 0 transparent or not
 */
class PicturePage : public Page {
public:
    /** `sys`, `screen` and `files` must outlive it. */
    PicturePage(System &sys, Surface &screen, const GameFiles &files);
    const char *name() const override { return "Pictures"; }
    size_t count() const override { return _entries.size(); }
    void activate() override { load(); }
    void handleKey(uint32_t key) override;
    void update(uint64_t nowMs) override;

private:
    void load();
    void draw();
    void startSet(size_t set);
    void step();

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<size_t> _entries; // the resource file's pictures
    size_t _current = 0;
    Picture _picture;
    std::string _error;
    size_t _set = 0;
    int _frame = 0;
    bool _playing = false;
    uint64_t _nextStepMs = 0;  // the next tick
    int _ticks = 0;            // the set's counter, as the game keeps it
    uint64_t _holdUntilMs = 0; // at the set's end: when the next set starts, 0 if not held
    bool _whole = false;
    int _zoom = 0; // 0: fitted to the screen
    bool _transparent = true;
};

/**
 * Plays the resource file's sounds (`.wav`).
 *   Up/Down: previous/next   PageUp/PageDown: jump 20   Home/End: first/last
 *   Space or Enter: play or stop   A: play each as it's selected
 */
class SoundPage : public Page {
public:
    /** `mixer` may be null (no audio device): sounds are listed, not played. */
    SoundPage(System &sys, Surface &screen, const GameFiles &files, audio::Mixer *mixer);
    const char *name() const override { return "Sounds"; }
    size_t count() const override { return _entries.size(); }
    void activate() override { draw(); }
    void deactivate() override { stop(); }
    void handleKey(uint32_t key) override;
    void update(uint64_t nowMs) override;

private:
    void select(size_t index);
    void play();
    void stop();
    void draw();

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<size_t> _entries;
    size_t _current = 0;
    std::shared_ptr<audio::Sound> _sound;
    std::string _error;
    audio::SoundHandle _playing = audio::kNoSound;
    bool _autoPlay = false;
    uint64_t _lastDrawMs = 0;
};

/**
 * The object definitions (`.dat`): each part's keywords, the occupancy
 * grids drawn, and the frame-set table.
 *   Left/Right: previous/next   PageUp/PageDown: jump 20   Home/End: first/last
 *   Up/Down: scroll
 */
class DefinitionPage : public Page {
public:
    /** `sys`, `screen` and `files` must outlive it. */
    DefinitionPage(System &sys, Surface &screen, const GameFiles &files);
    const char *name() const override { return "Definitions"; }
    size_t count() const override { return _entries.size(); }
    void activate() override { load(); }
    void handleKey(uint32_t key) override;

private:
    void load();
    void draw();

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<size_t> _entries;
    size_t _current = 0;
    std::vector<std::string> _lines;
    ObjectDefinition _definition;
    Picture _picture;
    bool _hasPicture = false;
    std::string _error;
    size_t _scroll = 0;
};

/**
 * The boards (`.sav`): the resource file's area layouts, and the layouts
 * and screen saver boards in art-res, drawn with their pieces' pictures on
 * their backdrop.
 *   Left/Right: previous/next board   Home/End: first/last
 *   Up/Down: scroll a board taller than the screen   [/]: scroll one wider
 *   S: the squares' values over the pieces   P: the pieces on or off
 *   B: the backdrop on or off
 */
class BoardPage : public Page {
public:
    /** `sys`, `screen` and `files` must outlive it. */
    BoardPage(System &sys, Surface &screen, const GameFiles &files);
    const char *name() const override { return "Boards"; }
    size_t count() const override { return _boards.size(); }
    void activate() override { load(); }
    void handleKey(uint32_t key) override;

private:
    struct Source {
        std::string name;
        int entry = -1;             // in the resource file, or
        std::filesystem::path path; // a file in art-res
    };
    void load();
    void draw();
    const Picture *piecePicture(uint16_t resource);

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<Source> _boards;
    size_t _current = 0;
    Board _board;
    std::string _error;
    std::map<uint16_t, std::unique_ptr<Picture>> _pictures; // by resource number; null if it can't be had
    std::unique_ptr<image::Bitmap> _backdrop;
    int _scrollX = 0;
    int _scrollY = 0;
    bool _squares = false;
    bool _pieces = true;
    bool _showBackdrop = true;
    int _missing = 0;
};

} // namespace reluva::legoloco
