// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/colormap.h"
#include "engines/sith/game_files.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

    /** Opens the archives and reads the colormaps and palettes. */
    Error open(const std::filesystem::path &install);
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

} // namespace reluva::sith
