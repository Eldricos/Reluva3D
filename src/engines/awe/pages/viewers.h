// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/events.h"
#include "graphics/gfx3d.h"
#include "engines/awe/formats/gr2.h"
#include "engines/awe/formats/model.h"
#include "engines/awe/render/model_draw.h"
#include "video/movie_player.h"
#include "engines/awe/ui/game_text.h"
#include "engines/awe/ui/page.h"

#include <memory>
#include <string>
#include <vector>

// The data viewer's pages, beside the game's own (ScenePage), in
// reluva-viewer: Tab moves between them.

namespace reluva::awe {

/** Every picture in the game: backgrounds, cutouts, interface, inventory. */
class PicturePage : public Page {
public:
    /** Lists the pictures; none is read until shown. */
    explicit PicturePage(ViewerContext &ctx);
    const char *name() const override { return "Pictures"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void handleEvent(const Event &ev) override;

private:
    void show();
    std::vector<std::string> _names;
    int _index = 0;
};
/**
 * Characters and objects (each `<who>_model.gr2` under Models/ or
 * People/), with their animations.
 */
class ModelPage : public Page {
public:
    /** Lists the models, each with the other files of its folder as its animations. */
    explicit ModelPage(ViewerContext &ctx);
    ~ModelPage() override;
    const char *name() const override { return "Models"; }
    size_t count() const override { return _models.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;

private:
    struct Entry {
        std::string model;
        std::vector<std::string> animations;
    };
    void load();
    void loadAnimation();
    void releaseGpu();

    std::vector<Entry> _models;
    // 0: rest pose, else animations[_animation - 1]
    int _index = 0;
    int _animation = 0;
    std::unique_ptr<GpuModel> _gpu;
    Animation _anim;
    bool _hasAnim = false;
    std::string _error;
    gfx::Renderer3D *_renderer = nullptr;
    std::vector<gfx::MeshId> _meshes; // this frame's
    Surface _overlay;
    float _yaw = -1.5708f;
    float _pitch = 0.15f;
    float _distance = 400;
    float _center[3] = {0, 0, 0};
    bool _playing = true;
    bool _helpers = false;
    bool _dragging = false;
    int _dragX = 0;
    int _dragY = 0;
    float _time = 0;
    uint64_t _lastMs = 0;
};

/**
 * Movies (the .bik files in Movies/) and the scenes' video loops (those
 * under Elements/, the cutouts'), with their sound.
 */
class MoviePage : public Page {
public:
    /** Lists the movies; none is opened until shown. */
    explicit MoviePage(ViewerContext &ctx);
    ~MoviePage() override;
    const char *name() const override { return "Movies"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;
    void update(uint64_t nowMs) override;

private:
    void open();
    std::vector<std::string> _names;
    int _index = 0;
    video::MoviePlayer _player;
    std::vector<Subtitle> _subtitles;
    std::string _info;
};

/** Text files: tables, dialogue, subtitles. */
class TextPage : public Page {
public:
    /** Lists the text files; none is read until shown. */
    explicit TextPage(ViewerContext &ctx);
    const char *name() const override { return "Text"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void handleEvent(const Event &ev) override;

private:
    void load();
    void draw();
    std::vector<std::string> _names;
    int _index = 0;
    int _scroll = 0;
    std::vector<std::string> _lines;
};

/** Sounds: voices, effects and music. */
class SoundPage : public Page {
public:
    /** Lists the sounds; none is read until played. */
    explicit SoundPage(ViewerContext &ctx);
    const char *name() const override { return "Sounds"; }
    size_t count() const override { return _names.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event &ev) override;

private:
    void draw();
    void play();
    std::vector<std::string> _names;
    int _index = 0;
    std::string _info;
    audio::SoundHandle _playing = audio::kNoSound;
};

} // namespace reluva::awe
