// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The NetImmerse engine's data viewer: pages showing what LEGO Alpha Team's
// archives hold, opened in reluva-viewer (viewer.cpp). Tab moves to the next
// page, Esc leaves. Its use is described in docs/engines/netimmerse/README.md.
//
// Archives: what each .pac archive holds.
// Models:   every .FIN model in 3D. Left/Right step, PageUp/PageDown jump
//           10; drag to turn, wheel or +/- to zoom, W/A/S/D/Q/E to move
//           the centre, T for textures, L for lightmaps, B for a level's
//           sky, H for the hidden effects (sparks, stars...) and the
//           characters' far-away stand-ins, G to mark the lights (and
//           again to light the shapes with them), F for
//           the footprint, R to reset the view, I to show or hide the
//           info. Animated models play: C and V pick the clip (with its
//           sound), Space pauses.
// Textures: every image. Left/Right step, PageUp/PageDown jump 10,
//           Home/End; F picks the archive.
// Fonts:    the game's .DDF fonts, drawn from their TGA pages. Left/Right
//           step, P shows the pages instead of the sample.
// Sounds:   every sound. Left/Right (or Up/Down) step, PageUp/PageDown
//           jump 10, Space or Enter plays or stops, A plays each as it's
//           selected, F picks the archive.
// Text:     the language's text files and the puzzles. Left/Right pick a
//           file, Up/Down, PageUp/PageDown and Home/End scroll.

#include "engines/netimmerse/formats/ddf.h"
#include "engines/netimmerse/formats/fin.h"
#include "engines/netimmerse/formats/pac_archive.h"

#include "common/archive.h"
#include "audio/mixer.h"
#include "common/events.h"
#include "graphics/gfx3d.h"
#include "common/system.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::netimmerse {

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

// What every page shares: the platform, a 2D screen for the pages that
// draw on it, and the game's archives by name ("Map.pac") and together.
struct ViewerContext {
    System &sys;
    Surface &screen;
    std::string title;                                  // the game's, for the window
    std::map<std::string, const PacArchive *> archives; // by file name
    const SearchSet &files;
    audio::Mixer *mixer; // nullptr without an audio device
};

class Page {
public:
    explicit Page(ViewerContext &ctx) : _ctx(ctx) {}
    virtual ~Page() = default;

    // Whether the page has anything to show; empty ones are skipped.
    virtual bool empty() const = 0;
    virtual void activate() = 0;
    virtual void deactivate() {}
    virtual void handleEvent(const Event &ev) = 0;
    // Draws a frame (and presents it).
    virtual void update(uint64_t nowMs) = 0;

protected:
    void setTitle(const std::string &what);
    // The 2D screen, for pages that use it.
    void use2D();
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string &s, Color c = {230, 230, 230}, int scale = 1);

    ViewerContext &_ctx;
};

// What each archive holds.
class ArchivesPage : public Page {
public:
    ArchivesPage(ViewerContext &ctx, std::vector<std::pair<std::string, std::string>> lines,
                 std::vector<std::string> errors);
    bool empty() const override { return false; }
    void activate() override;
    void handleEvent(const Event &) override {}
    void update(uint64_t) override;

private:
    std::vector<std::pair<std::string, std::string>> _lines; // archive, what it holds
    std::vector<std::string> _errors;
};

class ModelPage : public Page {
public:
    explicit ModelPage(ViewerContext &ctx);
    ~ModelPage() override;
    bool empty() const override { return _names.empty(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;

private:
    struct Item;
    void show(int index);
    // Walks the scene graph as it is `ticks` into the animation: creating
    // the items (`create`), or placing them again.
    void walk(float ticks, bool create);
    void pose(float ticks);
    void resetView();
    void clearItems();
    gfx::TextureId texture(const std::string &name);
    gfx::TextureId glowTexture();
    float clipTicks() const; // where the animation is

    std::vector<std::string> _names; // the models in Fin.pac
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
    Surface _overlay;
    FinFile _model;
    std::vector<Item> _items;
    std::map<uint32_t, size_t> _itemOf;              // by shape id
    std::map<std::string, gfx::TextureId> _textures; // by image name, lower case; 0 if it couldn't be read
    gfx::TextureId _glow = 0;
    std::string _error;
    std::vector<std::string> _info;
    gfx::Vec3 _centre{};
    gfx::Vec3 _home{};
    float _radius = 1;
    float _yaw = 0.8f;
    float _pitch = 0.5f;
    float _distance = 1;
    bool _dragging = false;
    bool _showInfo = true;
    bool _useTextures = true;
    bool _useLightmaps = true;
    bool _showBackdrop = true;
    bool _showHidden = false; // the effects the game shows on events
    int _lightMode = 0;       // G: 0 off, 1 marked, 2 marked and lighting the shapes
    unsigned _hiddenCount = 0;
    int _lastX = 0;
    int _lastY = 0;
    // Animation: the actor's tracks by node, the clips (0: the whole
    // timeline), where in the clip it is and whether it's playing.
    const FinActor *_actor = nullptr;
    std::map<uint32_t, size_t> _trackOf;
    std::map<uint32_t, std::vector<const FinMaterialAnimator *>> _animatorsOf; // by material
    std::map<uint32_t, const FinFlip *> _flipOf;                               // by texture property
    std::map<const FinFlip *, uint16_t> _flipNumber; // in the file's order, as clips' lists number them
    std::map<uint32_t, uint16_t> _morphNumber;       // likewise, by id
    // Where the tracks and morph shapes a clip doesn't move were left, as
    // the game leaves them: by node, the pose (and whether it's shown); by
    // morph shape, the weights.
    struct HeldPose {
        gfx::Mat4 local;
        bool shown = true;
    };
    std::map<uint32_t, HeldPose> _heldPose;
    std::map<uint32_t, std::vector<float>> _heldWeights;
    // Whether the clip moves what it lists by number (the whole timeline:
    // everything).
    bool clipMoves(const std::vector<uint16_t> FinActor::Clip::*list, size_t number) const;
    std::vector<FinActor::Clip> _clips;
    int _clip = 0;
    float _clipTime = 0; // seconds into the clip
    bool _playing = true;
    uint64_t _lastMs = 0;
    float _posedTicks = -1;
    // The clip's sound: started as the clip starts (and as it comes round
    // again, unless the sound loops itself).
    void clipSound();
    void stopClipSound();
    audio::SoundHandle _clipSound = audio::kNoSound;
    int _soundClip = -1;
    long _soundPass = -1;
    std::map<std::string, std::shared_ptr<const audio::Sound>> _sounds; // by name; nullptr if missing
    // The actor's footprint on the floor (F).
    bool _showFootprint = false;
    gfx::MeshId _footprint = 0;
    uint32_t _footprintCount = 0;
};

class TexturePage : public Page {
public:
    explicit TexturePage(ViewerContext &ctx);
    bool empty() const override { return _entries.empty(); }
    void activate() override { show(_index); }
    void handleEvent(const Event &ev) override;
    void update(uint64_t) override;

private:
    struct Entry {
        std::string archive;
        std::string name;
    };
    void show(int index);
    std::vector<Entry> _entries;
    int _index = 0;
};

class FontPage : public Page {
public:
    explicit FontPage(ViewerContext &ctx);
    bool empty() const override { return _entries.empty(); }
    void activate() override { show(_index); }
    void handleEvent(const Event &ev) override;
    void update(uint64_t) override;

private:
    struct Entry {
        std::string archive;
        std::string name;
    };
    void show(int index);
    std::vector<Entry> _entries;
    int _index = 0;
    bool _showPages = false;
    DdfFont _font;
    std::vector<image::Image> _pages;
    std::string _error;
};

class SoundPage : public Page {
public:
    explicit SoundPage(ViewerContext &ctx);
    bool empty() const override { return _entries.empty(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t) override;

private:
    struct Entry {
        std::string archive;
        std::string name;
    };
    void select(int index);
    void play();
    std::vector<Entry> _entries;
    int _index = 0;
    bool _autoPlay = false;
    audio::SoundHandle _playing = audio::kNoSound;
    std::shared_ptr<const audio::Sound> _sound;
    std::string _info;
};

class TextPage : public Page {
public:
    explicit TextPage(ViewerContext &ctx);
    bool empty() const override { return _files.empty(); }
    void activate() override { show(_index); }
    void handleEvent(const Event &ev) override;
    void update(uint64_t) override;

private:
    struct File {
        std::string archive;
        std::string name;
    };
    void show(int index);
    std::vector<File> _files;
    std::vector<std::string> _lines;
    int _index = 0;
    int _scroll = 0;
};

} // namespace reluva::netimmerse
