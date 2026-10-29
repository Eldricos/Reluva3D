// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The NetImmerse engine's data viewer: pages showing what LEGO Alpha Team's
// archives hold, while the game itself can't run yet. Tab moves to the next
// page, Esc returns to the launcher.
//
// Archives: what each .pac archive holds.
// Models:   every .FIN model in 3D. Left/Right step, PageUp/PageDown jump
//           10; drag to turn, wheel or +/- to zoom, W/A/S/D/Q/E to move
//           the centre, T for textures, L for lightmaps, B for a level's
//           sky, H for the hidden effects (sparks, stars...) and the
//           characters' far-away stand-ins, G to mark the lights, R to
//           reset the view, I to show or hide the info. Animated models
//           play: C and V pick the clip, Space pauses.
// Textures: every image. Left/Right step, PageUp/PageDown jump 10,
//           Home/End; F picks the archive.
// Fonts:    the game's .DDF fonts, drawn from their TGA pages. Left/Right
//           step, P shows the pages instead of the sample.
// Sounds:   every sound. Left/Right (or Up/Down) step, PageUp/PageDown
//           jump 10, Space or Enter plays or stops, A plays each as it's
//           selected, F picks the archive.
// Text:     the language's text files and the puzzles. Left/Right pick a
//           file, Up/Down, PageUp/PageDown and Home/End scroll.

#include "engines/netimmerse/ddf.h"
#include "engines/netimmerse/fin.h"
#include "engines/netimmerse/pac_archive.h"

#include "common/archive.h"
#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/gfx3d.h"
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
    System& sys;
    Surface& screen;
    std::string title; // the game's, for the window
    std::map<std::string, const PacArchive*> archives; // by file name
    const SearchSet& files;
    audio::Mixer* mixer; // nullptr without an audio device
};

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;

    // Whether the page has anything to show; empty ones are skipped.
    virtual bool empty() const = 0;
    virtual void activate() = 0;
    virtual void deactivate() {}
    virtual void handleEvent(const Event& ev) = 0;
    // Draws a frame (and presents it).
    virtual void update(uint64_t nowMs) = 0;

protected:
    void setTitle(const std::string& what);
    // The 2D screen, for pages that use it.
    void use2D();
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string& s, Color c = {230, 230, 230}, int scale = 1);

    ViewerContext& ctx_;
};

// What each archive holds.
class ArchivesPage : public Page {
public:
    ArchivesPage(ViewerContext& ctx, std::vector<std::pair<std::string, std::string>> lines,
                 std::vector<std::string> errors);
    bool empty() const override { return false; }
    void activate() override;
    void handleEvent(const Event&) override {}
    void update(uint64_t) override;

private:
    std::vector<std::pair<std::string, std::string>> lines_; // archive, what it holds
    std::vector<std::string> errors_;
};

class ModelPage : public Page {
public:
    explicit ModelPage(ViewerContext& ctx);
    ~ModelPage() override;
    bool empty() const override { return names_.empty(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
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
    gfx::TextureId texture(const std::string& name);
    gfx::TextureId glowTexture();
    float clipTicks() const; // where the animation is

    std::vector<std::string> names_; // the models in Fin.pac
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    Surface overlay_;
    FinFile model_;
    std::vector<Item> items_;
    std::map<uint32_t, size_t> itemOf_; // by shape id
    std::map<std::string, gfx::TextureId> textures_; // by image name, lower case; 0 if it couldn't be read
    gfx::TextureId glow_ = 0;
    std::string error_;
    std::vector<std::string> info_;
    gfx::Vec3 centre_{}, home_{};
    float radius_ = 1, yaw_ = 0.8f, pitch_ = 0.5f, distance_ = 1;
    bool dragging_ = false, showInfo_ = true, useTextures_ = true, useLightmaps_ = true, showBackdrop_ = true;
    bool showHidden_ = false; // the effects the game shows on events
    bool showLights_ = false;
    unsigned hiddenCount_ = 0;
    int lastX_ = 0, lastY_ = 0;
    // Animation: the actor's tracks by node, the clips (0: the whole
    // timeline), where in the clip it is and whether it's playing.
    const FinActor* actor_ = nullptr;
    std::map<uint32_t, size_t> trackOf_;
    std::map<uint32_t, std::vector<const FinMaterialAnimator*>> animatorsOf_; // by material
    std::map<uint32_t, const FinFlip*> flipOf_;                                // by texture property
    std::vector<FinActor::Clip> clips_;
    int clip_ = 0;
    float clipTime_ = 0; // seconds into the clip
    bool playing_ = true;
    uint64_t lastMs_ = 0, clockMs_ = 0;
    float posedTicks_ = -1;
};

class TexturePage : public Page {
public:
    explicit TexturePage(ViewerContext& ctx);
    bool empty() const override { return entries_.empty(); }
    void activate() override { show(index_); }
    void handleEvent(const Event& ev) override;
    void update(uint64_t) override;

private:
    struct Entry {
        std::string archive, name;
    };
    void show(int index);
    std::vector<Entry> entries_;
    int index_ = 0;
};

class FontPage : public Page {
public:
    explicit FontPage(ViewerContext& ctx);
    bool empty() const override { return entries_.empty(); }
    void activate() override { show(index_); }
    void handleEvent(const Event& ev) override;
    void update(uint64_t) override;

private:
    struct Entry {
        std::string archive, name;
    };
    void show(int index);
    std::vector<Entry> entries_;
    int index_ = 0;
    bool showPages_ = false;
    DdfFont font_;
    std::vector<image::Image> pages_;
    std::string error_;
};

class SoundPage : public Page {
public:
    explicit SoundPage(ViewerContext& ctx);
    bool empty() const override { return entries_.empty(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t) override;

private:
    struct Entry {
        std::string archive, name;
    };
    void select(int index);
    void play();
    std::vector<Entry> entries_;
    int index_ = 0;
    bool autoPlay_ = false;
    audio::SoundHandle playing_ = audio::kNoSound;
    std::shared_ptr<const audio::Sound> sound_;
    std::string info_;
};

class TextPage : public Page {
public:
    explicit TextPage(ViewerContext& ctx);
    bool empty() const override { return files_.empty(); }
    void activate() override { show(index_); }
    void handleEvent(const Event& ev) override;
    void update(uint64_t) override;

private:
    struct File {
        std::string archive, name;
    };
    void show(int index);
    std::vector<File> files_;
    std::vector<std::string> lines_;
    int index_ = 0, scroll_ = 0;
};

} // namespace reluva::netimmerse
