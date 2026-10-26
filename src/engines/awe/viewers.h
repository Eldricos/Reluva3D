// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/gfx3d.h"
#include "common/image.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/awe/ac2.h"
#include "engines/awe/gr2.h"
#include "engines/awe/granny_dll.h"
#include "engines/awe/model.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// Data viewers used by the AWE engine until the game itself runs. Each is
// a page of the engine's 1024x768 window (the game's own size); Tab moves
// between them, and each names its keys at the bottom of the screen.

namespace reluva::awe {

constexpr int kScreenW = 1024;
constexpr int kScreenH = 768;

struct ViewerContext {
    System& sys;
    const Archive& files;
    GrannyDll* granny;    // null if granny2.dll couldn't be loaded
    audio::Mixer* mixer;  // null without an audio device
    Surface& screen;      // kScreenW x kScreenH, RGBA32
};

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;
    virtual const char* name() const = 0;
    virtual size_t count() const = 0;
    // Called when the page is shown, and before it's left.
    virtual void activate() = 0;
    virtual void deactivate() {}
    virtual void handleEvent(const Event& ev) = 0;
    // Each frame: 2D pages show the framebuffer again.
    virtual void update(uint64_t nowMs) {
        (void)nowMs;
        present();
    }

protected:
    ViewerContext& ctx_;

    // 2D pages: the framebuffer, cleared, and the help line at the bottom.
    void begin2D();
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, Color c, uint8_t alpha = 255);
    void frame(int x, int y, int w, int h, Color c);
    int text(int x, int y, const std::string& s, Color c = {230, 230, 230}, int scale = 1);
    // Text on a dark box, for reading over pictures.
    void label(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void help(const std::string& keys);
    void present() { ctx_.sys.updateScreen(ctx_.screen); }
    // A list step from a key: -1/+1 for Left/Right, -10/+10 for
    // PageUp/PageDown, to the ends for Home/End; 0 for other keys.
    static int step(uint32_t key, int index, int count);

    // A picture from the game's files (cached; an empty image if it can't
    // be read).
    const image::Image& picture(const std::string& name);
    std::map<std::string, image::Image> pictures_;
};

// Every picture in the game: backgrounds, cutouts, interface, inventory.
class PicturePage : public Page {
public:
    explicit PicturePage(ViewerContext& ctx);
    const char* name() const override { return "Pictures"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void show();
    std::vector<std::string> names_;
    int index_ = 0;
};

// Scenes (Locations/**/*.ac2): their pictures where the scene places
// them, hotspots, and what else the scene holds.
class ScenePage : public Page {
public:
    explicit ScenePage(ViewerContext& ctx);
    const char* name() const override { return "Scenes"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void load();
    void draw();
    std::vector<std::string> names_;
    int index_ = 0;
    Ac2Scene scene_;
    std::string error_;
    int selected_ = -1; // object highlighted, -1 none
    bool hotspots_ = true, info_ = true;
};

// Characters and objects (Models/**/*_model.gr2), with their animations.
class ModelPage : public Page {
public:
    explicit ModelPage(ViewerContext& ctx);
    ~ModelPage() override;
    const char* name() const override { return "Models"; }
    size_t count() const override { return models_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;

private:
    struct Entry {
        std::string model;
        std::vector<std::string> animations;
    };
    void load();
    void loadAnimation();
    void releaseGpu();

    std::vector<Entry> models_;
    int index_ = 0, animation_ = 0; // 0: rest pose, else animations[animation_ - 1]
    Model model_;
    Animation anim_;
    bool hasAnim_ = false;
    std::string error_;
    gfx::Renderer3D* renderer_ = nullptr;
    std::vector<gfx::TextureId> textures_;
    std::vector<gfx::MeshId> meshes_;
    Surface overlay_;
    float yaw_ = -1.5708f, pitch_ = 0.15f, distance_ = 400;
    float center_[3] = {0, 0, 0};
    bool playing_ = true, helpers_ = false, dragging_ = false;
    int dragX_ = 0, dragY_ = 0;
    float time_ = 0;
    uint64_t lastMs_ = 0;
};

// Text files: tables, dialogue, subtitles.
class TextPage : public Page {
public:
    explicit TextPage(ViewerContext& ctx);
    const char* name() const override { return "Text"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void load();
    void draw();
    std::vector<std::string> names_;
    int index_ = 0, scroll_ = 0;
    std::vector<std::string> lines_;
};

// Sounds: voices, effects and music.
class SoundPage : public Page {
public:
    explicit SoundPage(ViewerContext& ctx);
    const char* name() const override { return "Sounds"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;

private:
    void draw();
    void play();
    std::vector<std::string> names_;
    int index_ = 0;
    std::string info_;
    audio::SoundHandle playing_ = audio::kNoSound;
};

} // namespace reluva::awe
