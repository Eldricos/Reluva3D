// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/types.h"
#include "engines/sith/folders.h"
#include "engines/sith/formats/colormap.h"
#include "engines/sith/formats/font.h"
#include "engines/sith/game_files.h"
#include "image/image.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Sith's data viewer, in reluva-viewer only: pages showing what Jedi
// Knight's archives hold. Each page draws into an RGBA32 surface of the
// viewer's size, so reluva-tool can draw one without a window.

namespace reluva::sith {

/** The viewer's size, in pixels. */
///@{
constexpr int kViewerW = 800;
constexpr int kViewerH = 600;
///@}

/** What every page reads from: the install's archives, and its colormaps. */
struct ViewerData {
    GameFiles files;
    /** Every colormap in the archives, by name, sorted. */
    std::vector<std::pair<std::string, Colormap>> colormaps;
    /**
     * The palettes a picture without one of its own may take, by where
     * they're from: uicolormap.cmp's first (the interface's), then the
     * other colormaps', then each picture's with one of its own (the
     * menus' backgrounds).
     */
    std::vector<std::pair<std::string, Palette>> palettes;
    /** The CDs found for the install, for their cut scenes. */
    GameFolders folders;
    /** Where sounds play; nullptr without sound, when pages play nothing. */
    audio::Mixer *mixer = nullptr;

    /**
     * Opens the archives, reads the colormaps and palettes, and finds the
     * CDs (by `settings`' cd1path and cd2path, as the game does).
     */
    Error open(const std::filesystem::path &install, const std::map<std::string, std::string> &settings = {});
    /** The colormap whose name ends with `name` ("dflt.cmp"), or -1. */
    int findColormap(std::string_view name) const;
};

/** One page of the viewer. */
class Page {
public:
    virtual ~Page() = default;
    /** Its name, as the status line and reluva-tool give it. */
    virtual const char *name() const = 0;
    /** How many items it has; the viewer skips a page with none. */
    virtual size_t count() const = 0;
    /** Draws it into `screen`, an RGBA32 surface of the viewer's size. */
    virtual void draw(Surface &screen) = 0;
    /** A key pressed on the page. @return Whether it changed what's drawn. */
    virtual bool handleKey(uint32_t pressed) = 0;
    /** Its keys, for the window's title. */
    virtual const char *keys() const = 0;
    /**
     * Moves on what plays (a sound, a cut scene) to `millis`, the viewer's
     * clock. @return Whether it changed what's drawn.
     */
    virtual bool update(uint64_t /*millis*/) { return false; }
    /** The page is left: what it plays stops. */
    virtual void leave() {}
};

/** The pages, in the order Tab steps through them; `data` must outlive them. */
std::vector<std::unique_ptr<Page>> makePages(const ViewerData &data);

/** Each kind's page. */
///@{
std::unique_ptr<Page> makePicturePage(const ViewerData &data);
std::unique_ptr<Page> makeMaterialPage(const ViewerData &data);
std::unique_ptr<Page> makeColormapPage(const ViewerData &data);
std::unique_ptr<Page> makeFontPage(const ViewerData &data);
std::unique_ptr<Page> makeStringPage(const ViewerData &data);
std::unique_ptr<Page> makeSoundPage(const ViewerData &data);
std::unique_ptr<Page> makeSoundClassPage(const ViewerData &data);
std::unique_ptr<Page> makeEpisodePage(const ViewerData &data);
std::unique_ptr<Page> makeCutScenePage(const ViewerData &data);
std::unique_ptr<Page> makeModelPage(const ViewerData &data);
std::unique_ptr<Page> makeSpritePage(const ViewerData &data);
std::unique_ptr<Page> makeLevelPage(const ViewerData &data);
///@}

/** A file in one of the archives. */
using FileRef = std::pair<const GobArchive *, std::string>;

/** Every file in the archives whose name ends with `ext` (".bm"), in the archives' order. */
std::vector<FileRef> filesEnding(const GameFiles &files, std::string_view ext);

/** Fills a rectangle of an RGBA32 surface, clipped to it. */
void fillRect(Surface &s, int x, int y, int w, int h, Color c);

/** The status line along the bottom: its text over a dark band. */
void drawStatus(Surface &s, std::string_view line);

/**
 * Steps `index` through `n` items by the usual keys: Left/Right one,
 * PageUp/PageDown ten, Home/End to the ends.
 * @return Whether the key was one of them.
 */
bool stepIndex(uint32_t pressed, size_t n, size_t &index);

/**
 * How wide `text` is in `font`, in the font's pixels, spaced as the game
 * spaces it (docs/engines/sith/games/jk/formats/sft.md): each glyph and the
 * font's gap after it, white space (the C library's, a line break among it)
 * the font's space width, a character the font lacks its fallback's; a tab
 * is taken as a space.
 */
int fontTextWidth(const Font &font, std::string_view text);
/**
 * Draws `text` in `font` at (x, y), spaced as fontTextWidth, each pixel
 * `scale` times; `strip` is the font's glyph strip as RGBA (bitmapToImage).
 * What would go past `right` isn't drawn.
 */
void drawFontText(Surface &s, const Font &font, const image::Image &strip, int x, int y, std::string_view text,
                  int scale = 1, int right = 1 << 30);
/**
 * `text` broken into lines `width` pixels wide in `font`, as the game breaks
 * a subtitle (0x4352c0): word by word, a word with the white space after it;
 * a line ends before a word that would take it to `width` or past, unless
 * it's the line's first. A line keeps the white space it ends with, which
 * counts when it's centred; a line break is white space like any other.
 */
std::vector<std::string> subtitleLines(const Font &font, std::string_view text, int width);

} // namespace reluva::sith
