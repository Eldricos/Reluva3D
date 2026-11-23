// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/sound.h"
#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/md2.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/world_view.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/formats/rse.h"
#include "engines/tpw/formats/sdt.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/script/machine.h"
#include "engines/tpw/script/simulated_host.h"
#include "graphics/soft3d/view3d.h"
#include "image/image.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The Theme Park World engine's data viewer, in reluva-viewer only: pages
// showing what a game's files hold, each drawn into a 640x480 RGBA32
// screen. Tab and Shift+Tab move between the pages, Esc leaves; each
// page's keys are with its class.

namespace reluva::tpw {

/** The viewer's screen, in pixels. */
///@{
constexpr int kViewerWidth = 640;
constexpr int kViewerHeight = 480;
///@}

/** What every page reads: the game's files, its language, the sound out. */
struct ViewerContext {
    System &sys;
    const SearchSet &files;
    std::filesystem::path install;
    audio::Mixer *mixer = nullptr; // null without an audio device: sounds are listed, not played
    std::string language;          // "American": Data/Language/<language>/
    CharacterSet characters;       // the language's; empty if it isn't there

    /** The names in the game's files with one of the extensions (".fsh"), in the set's order. */
    std::vector<std::string> namesEndingIn(std::initializer_list<const char *> extensions) const;
};

/** Fills a rectangle of an RGBA32 surface, clipped to it. */
void fillRect(Surface &s, int x, int y, int w, int h, Color c);
/** Draws text in the built-in font, clipped; returns the x after it. */
int drawLabel(Surface &s, int x, int y, const std::string &utf8, Color c);
/** Draws a picture at (x, y), `zoom` screen pixels to one of its, over a grey check where it's clear. */
void drawPicture(Surface &s, const image::Image &picture, int x, int y, int zoom);
/** Text in a game font at (x, y), each of its codes in the language's character set; returns the x after it. */
int drawGameText(Surface &s, const Font &font, const CharacterSet &characters, int x, int y, const std::string &utf8,
                 Color c);

/** A page of the viewer: a list of things, one shown at a time. */
class Page {
public:
    /** A page of the game in `context`, which must outlive it. */
    explicit Page(ViewerContext &context) : _context(context) {}
    virtual ~Page() = default;

    /** The page's name, for the window's title. */
    virtual const char *name() const = 0;
    /** How many things it has; a page without is skipped. */
    virtual size_t count() const = 0;
    /** What's shown, for the window's title. */
    virtual std::string current() const = 0;
    /** Draws the page. */
    virtual void draw(Surface &screen) = 0;
    /**
     * A key pressed on the page. Unless a page takes them, Left and Right
     * go to the previous and next thing, PageUp and PageDown 10 at a time,
     * Home and End to the first and the last.
     */
    virtual void handleKey(uint32_t key);
    /** Called as another page is shown: what's playing stops. */
    virtual void deactivate() {}
    /** Called often while shown; whether it needs drawing again. */
    virtual bool tick(uint64_t /*nowMs*/) { return false; }

protected:
    /** Moves to a thing, kept in range; it's read again when it changes. */
    void select(long index);
    /** Called when the thing shown changes. */
    virtual void selected() {}

    ViewerContext &_context;
    size_t _index = 0;
};

/**
 * The pictures: .fsh banks, .tga, .png and .jpg, each on its own.
 *   Z: zoomed to fit, or at its own size
 */
class PicturePage : public Page {
public:
    /** Lists the game's pictures and reads the first. */
    explicit PicturePage(ViewerContext &context);
    const char *name() const override { return "Pictures"; }
    size_t count() const override { return _names.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;

private:
    void selected() override;

    std::vector<std::string> _names;
    image::Image _picture;
    std::string _kind;
    std::string _error;
    bool _fit = true;
};

/**
 * The fonts (.bf4): sample text in the font, then every glyph with its code.
 */
class FontPage : public Page {
public:
    /** Lists the game's fonts and reads the first. */
    explicit FontPage(ViewerContext &context);
    const char *name() const override { return "Fonts"; }
    size_t count() const override { return _names.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;

private:
    void selected() override;

    std::vector<std::string> _names;
    Font _font;
    std::string _error;
};

/**
 * The string tables (.str), each string with its number; the one picked
 * shown whole at the bottom.
 *   Up/Down: the string   F: drawn in the built-in font or a game font
 */
class TextPage : public Page {
public:
    /** Lists the game's string tables and reads the first. */
    explicit TextPage(ViewerContext &context);
    const char *name() const override { return "Text"; }
    size_t count() const override { return _names.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;

private:
    void selected() override;

    std::vector<std::string> _names;
    StringTable _table;
    std::string _error;
    size_t _line = 0;
    Font _gameFont;
    bool _useGameFont = false;
};

/**
 * The settings files (.sam): each setting with its line and values, and
 * the grids.
 *   Up/Down: scroll a line
 */
class SettingsPage : public Page {
public:
    /** Lists the game's settings files and reads the first. */
    explicit SettingsPage(ViewerContext &context);
    const char *name() const override { return "Settings"; }
    size_t count() const override { return _names.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;

private:
    void selected() override;

    std::vector<std::string> _names;
    std::vector<std::string> _lines;
    std::string _error;
    size_t _first = 0;
};

/**
 * The ride and shop scripts (.RSE), each as source, in the language of
 * the sources that ship with the game; or run, on a stand-in for the park
 * with guests coming, its variables and what it asks of the game shown.
 *   Up/Down: scroll a line   PageUp/PageDown here: a screen
 *   Left/Right: the previous and next script   R: run it, or back to its source
 */
class ScriptPage : public Page {
public:
    /** Lists the game's scripts and reads the first. */
    explicit ScriptPage(ViewerContext &context);
    const char *name() const override { return "Scripts"; }
    size_t count() const override { return _names.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;
    void deactivate() override { stop(); }
    bool tick(uint64_t nowMs) override;

private:
    void selected() override;
    void start();
    void stop();
    void drawRun(Surface &screen);

    std::vector<std::string> _names;
    std::vector<std::string> _lines;
    std::string _summary;
    std::string _error;
    size_t _first = 0;
    Script _script;
    std::unique_ptr<SimulatedHost> _host;
    std::unique_ptr<ScriptMachine> _machine;
    uint64_t _lastTick = 0;
};

/**
 * The sound banks (.sdt), each sound with its name and length.
 *   Up/Down: the sound   PageUp/PageDown here: 20 sounds   Space or Enter: play or stop
 *   Left/Right: the previous and next bank
 */
class SoundPage : public Page {
public:
    /** Lists the game's sound banks and reads the first. */
    explicit SoundPage(ViewerContext &context);
    const char *name() const override { return "Sounds"; }
    size_t count() const override { return _names.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;
    void deactivate() override { stop(); }
    bool tick(uint64_t nowMs) override;

private:
    void selected() override;
    void play();
    void stop();

    std::vector<std::string> _names;
    SoundBank _bank;
    std::string _error;
    size_t _sound = 0;
    std::shared_ptr<audio::Sound> _playingSound;
    audio::SoundHandle _playing = audio::kNoSound;
    std::string _playError;
    uint64_t _lastDraw = 0;
};

/**
 * The models (.MD2), drawn in 3D with their textures, turned about their
 * middle; animations and headers alone aren't listed.
 *   A/D: turn   W/S: tilt   Z/X: nearer, further   T: textures on or off
 *   H: the height map's samples (.hmp) over it, or not
 *   N: play its next animation, then none
 */
class ModelPage : public Page {
public:
    /** Lists the game's models and reads the first. */
    explicit ModelPage(ViewerContext &context);
    const char *name() const override { return "Models"; }
    size_t count() const override { return _names.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;

private:
    void selected() override;

    std::vector<std::string> _names;
    LoadedModel _loaded;
    std::string _error;
    soft3d::Rasterizer _rasterizer;
    ModelView _view;
    uint64_t _lastTick = 0;

public:
    bool tick(uint64_t nowMs) override;
};

/**
 * The worlds' terrain: the land around the park and the park's ground,
 * as the game's files give them, before anything is built, with the
 * world's placed objects as markers.
 *   A/D: turn   W/S: tilt   Z/X: nearer, further   T: textures on or off
 *   G: the park's ground on or off   O: its placed objects (scape.omp) on or off
 *   K: its sky on or off
 */
class WorldPage : public Page {
public:
    /** Lists the worlds, `levels/<world>` with a `terrain/base.MD2`, and reads the first. */
    explicit WorldPage(ViewerContext &context);
    const char *name() const override { return "Worlds"; }
    size_t count() const override { return _folders.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;

private:
    void selected() override;

    std::vector<std::string> _folders;
    LoadedWorld _world;
    std::string _error;
    soft3d::Rasterizer _rasterizer;
    WorldView _view;
};

/**
 * The movies: the EA GAMES logo (.tgq) and the training video (.avi).
 *   Space or Enter: play it (a key or a click stops it)
 */
class MoviePage : public Page {
public:
    /** Lists the game's movies: the `.tgq` in `Data/`, the `.avi` beside it. */
    explicit MoviePage(ViewerContext &context);
    const char *name() const override { return "Movies"; }
    size_t count() const override { return _movies.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;

private:
    std::vector<std::filesystem::path> _movies;
    std::string _error;
};

/**
 * Every file of the game's Data folder: its size and where it is (an
 * archive, or loose), and its start, as text or as bytes.
 *   Up/Down: the file   PageUp/PageDown here: 20 files
 */
class FilePage : public Page {
public:
    /** Lists every file of the game's `Data` folder and reads the first. */
    explicit FilePage(ViewerContext &context);
    const char *name() const override { return "Files"; }
    size_t count() const override { return _names.size(); }
    std::string current() const override;
    void draw(Surface &screen) override;
    void handleKey(uint32_t key) override;

private:
    void selected() override;

    std::vector<std::string> _names;
    std::vector<uint8_t> _data;
    std::string _where;
};

} // namespace reluva::tpw
