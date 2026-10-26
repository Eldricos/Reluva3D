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
#include "common/video/bink_file.h"
#include "common/video/bink_video.h"
#include "engines/awe/ac2.h"
#include "engines/awe/conditions.h"
#include "engines/awe/game_paths.h"
#include "engines/awe/particles.h"
#include "engines/awe/walk_grid.h"
#include "engines/awe/game_text.h"
#include "engines/awe/gr2.h"
#include "engines/awe/granny_dll.h"
#include "engines/awe/model.h"
#include "engines/awe/model_draw.h"

#include <filesystem>
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
    std::filesystem::path savePath; // where screenshots go (F12)
    const GameFonts* fonts = nullptr;  // the game's fonts, if they loaded
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
        finishScreenshot(nullptr, &ctx_.screen);
    }
    // Saves the next frame shown, as a TGA file (the window title says
    // where).
    void requestScreenshot(std::filesystem::path file) { shotFile_ = std::move(file); }

protected:
    ViewerContext& ctx_;
    std::filesystem::path shotFile_;

    // For pages that draw in 3D: before endFrame, asks the renderer to
    // keep the frame if a screenshot is due.
    void prepareScreenshot(gfx::Renderer3D* renderer) {
        if (renderer && !shotFile_.empty())
            renderer->captureNextFrame();
    }
    // After the frame: saves it (the 3D frame with `overlay` over it, or
    // `overlay` alone without a renderer) if a screenshot is due.
    void finishScreenshot(gfx::Renderer3D* renderer, const Surface* overlay);

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
    ~ScenePage() override;
    const char* name() const override { return "Scenes"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;

private:
    // A character placed in the scene, posed by its animation.
    struct Character {
        std::string name;
        std::shared_ptr<GpuModel> model;
        std::shared_ptr<Animation> animation;
        std::string animationName;
        gfx::Mat4 placement{};
        // Where it stands and faces; while walking, the points still to
        // reach (world x, z) and its walk.
        float x = 0, y = 0, z = 0, rotY = 0;
        bool player = false;
        std::vector<std::pair<float, float>> path;
        std::shared_ptr<Animation> walk;
        float walkSpeed = 0;           // world units a second, from the walk itself
        float walkDrift[3] = {0, 0, 0}; // how far the walk carries the body each loop, in the model's axes
        float walkStart = 0;
    };
    // A video loop the scene shows (the countryside going by a window),
    // by the object that places it.
    struct Loop {
        std::unique_ptr<video::BinkFile> file;
        std::unique_ptr<video::BinkVideoDecoder> video;
        image::Image frame;
        size_t next = 0; // the frame to decode next
    };
    void load();
    void loadLoops();
    // Draws a particle system's particles through the scene's camera.
    void drawParticles(const ParticleSystem& system, bool keepAlpha);
    // Advances the loops to `time` seconds; whether any frame changed.
    bool advanceLoops(float time);
    void loadCharacters();
    void draw();
    void releaseGpu();
    void line(int x0, int y0, int x1, int y1, Color c);
    // A convex polygon (screen points), blended over the screen.
    void fillPolygon(const std::vector<std::pair<float, float>>& points, Color c, uint8_t alpha);
    void drawGrid();
    // Walks the player's character to where the screen point is on the
    // floor, along a path through the grid.
    void walkTo(int sx, int sy);
    bool loadWalk(Character& c);
    void placeCharacter(Character& c, float time) const;

    std::vector<std::string> names_;
    int index_ = 0;
    Ac2Scene scene_;
    std::string base_; // the base scene an act's variant is shown over
    std::string error_;
    int selected_ = -1; // object highlighted, -1 none
    bool hotspots_ = true, info_ = true, showCharacters_ = true;
    // What the scene's conditions test: the train moving or not (M), the
    // weather (W), the act of the scene's variant.
    GameState state_;
    std::map<const Ac2Record*, Loop> loops_;
    std::map<const Ac2Record*, ParticleSystem> particles_;
    // The camera's walk grid (G shows it), the cell under the mouse and
    // the last path found.
    WalkGrid grid_;
    bool showGrid_ = false;
    WalkGrid::Cell hover_;
    std::vector<WalkGrid::Cell> path_;
    // The game's own path-finder, from motoe.exe (loaded on the first
    // walk); Reluva's A* if it can't be.
    std::unique_ptr<GamePathFinder> gamePaths_;
    bool triedGamePaths_ = false;
    std::string pathSource_;
    float time_ = 0;
    image::Image dot_; // stands in for a particle texture that's missing
    uint64_t lastMs_ = 0;
    // With 3D: the pictures behind the characters as one texture, the
    // characters, and the models and animations loaded so far.
    gfx::Renderer3D* renderer_ = nullptr;
    gfx::TextureId back_ = 0;
    gfx::MeshId quad_ = 0;
    std::vector<Character> characters_;
    std::map<std::string, std::shared_ptr<GpuModel>> models_;
    std::map<std::string, std::shared_ptr<Animation>> animations_;
    std::vector<gfx::MeshId> frameMeshes_;
    uint64_t startMs_ = 0;
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
    std::unique_ptr<GpuModel> gpu_;
    Animation anim_;
    bool hasAnim_ = false;
    std::string error_;
    gfx::Renderer3D* renderer_ = nullptr;
    std::vector<gfx::MeshId> meshes_; // this frame's
    Surface overlay_;
    float yaw_ = -1.5708f, pitch_ = 0.15f, distance_ = 400;
    float center_[3] = {0, 0, 0};
    bool playing_ = true, helpers_ = false, dragging_ = false;
    int dragX_ = 0, dragY_ = 0;
    float time_ = 0;
    uint64_t lastMs_ = 0;
};

// Movies (Movies/*.bik) and the scenes' video loops (Elements/**/*.bik,
// cutouts'), with their sound.
class MoviePage : public Page {
public:
    explicit MoviePage(ViewerContext& ctx);
    ~MoviePage() override;
    const char* name() const override { return "Movies"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;

private:
    void open();
    void stop();
    std::vector<std::string> names_;
    int index_ = 0;
    std::unique_ptr<video::BinkFile> file_;
    std::unique_ptr<video::BinkVideoDecoder> video_;
    image::Image frame_; // the frame shown
    std::vector<Subtitle> subtitles_;
    std::string info_;
    size_t next_ = 0;      // the next frame to decode
    uint64_t startMs_ = 0; // when frame 0 was due
    bool paused_ = false, loop_ = true;
    std::shared_ptr<audio::Sound> soundtrack_;
    audio::SoundHandle sound_ = audio::kNoSound;
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
