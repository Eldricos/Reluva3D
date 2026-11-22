// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/sound.h"
#include "common/events.h"
#include "common/system.h"
#include "common/types.h"
#include "common/zip.h"
#include "engines/sfc/formats/bft_font.h"
#include "engines/sfc/formats/model.h"
#include "engines/sfc/formats/sprite_archive.h"
#include "engines/sfc/formats/text_tables.h"
#include "graphics/gfx3d.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The Starfleet Command engine's data viewer, in reluva-viewer only: a
// page for each kind of the game's files, each drawing into an 800x600
// RGBA32 screen (the Models page with the 3D renderer instead).

namespace reluva::sfc {

/** The viewer's screen: the game's default resolution. */
///@{
constexpr int kScreenW = 800;
constexpr int kScreenH = 600;
///@}

/** What every page shares; it outlives them. */
struct ViewerContext {
    System &sys;
    Surface &screen;               // RGBA32, kScreenW x kScreenH
    std::filesystem::path root;    // the game's folder
    audio::Mixer *mixer = nullptr; // none without an audio device
    std::string title;             // the game's description, for the window's title
};

/** A page of the viewer. */
class Page {
public:
    /** `ctx` must outlive the page. */
    explicit Page(ViewerContext &ctx) : _ctx(ctx) {}
    virtual ~Page() = default;

    /** Its name, for the window's title. */
    virtual const char *name() const = 0;
    /** How many things it has to show; a page with none is skipped. */
    virtual size_t count() const = 0;
    /** Redraws, after switching here. */
    virtual void activate() = 0;
    /** Before switching away. */
    virtual void deactivate() {}
    /** A key or mouse event. */
    virtual void handleEvent(const Event &ev) = 0;
    /** Called every frame, with the system's clock. */
    virtual void update(uint64_t /*nowMs*/) {}
    /** Whether the page draws with System::init3D; the 2D screen comes back after it. */
    virtual bool uses3D() const { return false; }

protected:
    /** Drawing on the 2D screen: all of it, a rectangle (clipped), a line of text. */
    ///@{
    void clear(Color c);
    void fill(int x, int y, int w, int h, Color c);
    void text(int x, int y, const std::string &s, Color c = {230, 230, 230});
    ///@}
    /** The window's title: the game's and the page's names, then `detail`. */
    void setTitle(const std::string &detail);

    ViewerContext &_ctx;
};

/**
 * Runs the pages until the user leaves (Esc, or the window closed), Tab
 * moving to the next page with something to show. Fails if none has.
 */
Error runPages(ViewerContext &ctx, std::vector<std::unique_ptr<Page>> &pages);

/**
 * The pictures: sprites.q3's images, then every .bmp under the game's
 * Assets folder (the models' textures, the interface's other pictures).
 *   Left/Right: previous/next   PageUp/PageDown: by 10   Home/End: first/last
 *   S: the other source   N: the next named image   B: the background
 *   (dark, light, or a checkerboard showing what's transparent)
 *   Z: whole-size or fitted to the screen
 */
class ImagePage : public Page {
public:
    /** Finds sprites.q3 and the BMPs under the game's folder. */
    explicit ImagePage(ViewerContext &ctx);
    const char *name() const override { return "Images"; }
    size_t count() const override { return _sprites.images().size() + _bitmaps.size(); }
    void activate() override { show(); }
    void handleEvent(const Event &ev) override;

private:
    void show();

    SpriteArchive _sprites;
    std::string _spriteError;
    std::vector<std::filesystem::path> _bitmaps; // relative to the root
    size_t _current = 0;                         // sprites first, then the bitmaps
    int _background = 0;
    bool _fit = true;
};

/**
 * The .bft fonts: each glyph's box, and sample text.
 *   Left/Right: previous/next font   B: the glyphs' boxes on/off
 */
class FontPage : public Page {
public:
    /** Finds the fonts under the game's folder. */
    explicit FontPage(ViewerContext &ctx);
    const char *name() const override { return "Fonts"; }
    size_t count() const override { return _files.size(); }
    void activate() override { show(); }
    void handleEvent(const Event &ev) override;

private:
    void show();

    std::vector<std::filesystem::path> _files;
    size_t _current = 0;
    bool _boxes = false;
};

/**
 * The .mod models, drawn with their textures and lit by a fixed light.
 *   Left/Right: previous/next   PageUp/PageDown: by 10   L: the next level
 *   of detail   T: textures on/off   P: the named points on/off
 *   R: the view reset   I: the details on/off   +/- or the wheel: nearer,
 *   farther   dragging with the left button: turning it
 */
class ModelPage : public Page {
public:
    /** Finds the models under the game's folder. */
    explicit ModelPage(ViewerContext &ctx);
    ~ModelPage() override;
    const char *name() const override { return "Models"; }
    size_t count() const override { return _files.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return true; }

private:
    struct Draw {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        gfx::TextureId texture = 0;
    };
    void load();
    void build();
    void release();
    void drawInfo();

    std::vector<std::filesystem::path> _files;
    size_t _current = 0;
    gfx::Renderer3D *_renderer = nullptr;
    Model _model;
    std::string _error;
    std::vector<std::string> _missing; // textures named and not found
    size_t _level = 0;
    bool _textured = true;
    bool _points = false;
    bool _info = true;
    gfx::MeshId _mesh = 0;
    std::vector<Draw> _draws;
    std::map<std::string, gfx::TextureId> _textures;
    float _yaw = 0.6f;
    float _pitch = 0.4f;
    float _distance = 1;
    bool _dragging = false;
    int _lastX = 0;
    int _lastY = 0;
    Surface _overlay;
};

/**
 * The tables: the ship, fighter, sound and music tables (tab-separated),
 * and the interface's string tables.
 *   Left/Right: previous/next table   Up/Down: a row   PageUp/PageDown: a
 *   screen   Home/End: first/last row   [/]: the columns shown (tab tables)
 *   C: the comment rows shown or hidden
 */
class TablePage : public Page {
public:
    /** Finds the tables under the game's folder. */
    explicit TablePage(ViewerContext &ctx);
    const char *name() const override { return "Tables"; }
    size_t count() const override { return _files.size(); }
    void activate() override;
    void handleEvent(const Event &ev) override;

private:
    void load();
    void show();

    std::vector<std::filesystem::path> _files; // relative to the root
    size_t _current = 0;
    std::vector<std::vector<std::string>> _lines; // the table's rows as cells, as shown
    std::string _error;
    size_t _first = 0;
    size_t _column = 0;
    bool _comments = false;
};

/**
 * The sounds of Assets/Sound's zips: effects, voices and music.
 *   Up/Down: previous/next   PageUp/PageDown: by 20   Home/End: first/last
 *   Left/Right: previous/next zip   Space or Enter: play or stop
 *   A: play each as it's selected
 */
class SoundPage : public Page {
public:
    /** Finds the zips' sounds under the game's folder. */
    explicit SoundPage(ViewerContext &ctx);
    const char *name() const override { return "Sounds"; }
    size_t count() const override;
    void activate() override { select(_current); }
    void deactivate() override { stop(); }
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;

private:
    struct Zip {
        std::string name;
        bool music = false; // played at the music volume
        std::unique_ptr<ZipArchive> archive;
        std::vector<std::string> sounds;
    };
    void select(size_t index);
    void play();
    void stop();
    void redraw();

    std::vector<Zip> _zips;
    size_t _zip = 0;
    size_t _current = 0;
    std::shared_ptr<audio::Sound> _sound;
    std::string _error;
    audio::SoundHandle _playing = audio::kNoSound;
    bool _autoPlay = false;
    uint64_t _lastDrawMs = 0;
};

/** Every file under `dir` whose extension, in lower case, is `ext` ("." first); sorted, relative to `dir`. */
std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path &dir, const std::string &ext);

} // namespace reluva::sfc
