// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Angel Game Engine data viewer's pages. Tab switches pages; Esc
// goes back to the launcher.
//
// Models (3D): every model (geometry/*.pkg), its parts placed by their
// .mtx files. Left/Right step through them (PageUp/PageDown by 10,
// Home/End), Up/Down jump to the next kind (the name's first part:
// vp cars, sp props...), drag to turn, the wheel (or +/-) zooms, L
// changes the level of detail, P the paint job, G shows the parts that
// only show when lit (headlights' and sirens' glows, traffic lights),
// I shows or hides the info and R resets the view.
//
// Textures: every texture (texture/*.tex and .tga, jpg/*.jpg) over a
// checkerboard where it's transparent. Left/Right step (PageUp/PageDown
// by 10, Home/End), Up/Down jump to the next kind (the name's first
// part), +/- zoom, A shows alpha as transparency or not.
//
// Files: every file in the search path with its size and archive.
// Up/Down, PageUp/PageDown and Home/End move, the wheel scrolls,
// Left/Right show one top-level folder at a time (anim, aud, bound,
// city...), Enter opens the file: text as text, anything else as a hex
// dump, scrolled the same way; Esc or Backspace goes back to the list.

#include "common/archive.h"
#include "common/system.h"
#include "common/gfx3d.h"
#include "engines/age/games.h"
#include "engines/age/pkg.h"
#include "engines/age/texture.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

struct ViewerContext {
    System& sys;
    Surface& screen; // RGBA32
    const SearchSet& files;
    const GameInfo& game;
    const std::vector<std::string>& warnings; // problems loading the game's files
};

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;

    virtual const char* name() const = 0;
    virtual size_t count() const = 0; // pages with nothing to show are skipped
    virtual void activate() = 0;      // redraw and retitle after switching here
    virtual void deactivate() {}      // before switching away
    virtual void handleEvent(const Event& ev) = 0;
    virtual void update(uint64_t /*nowMs*/) {}
    // Pages that draw with System::init3D; the engine puts the 2D
    // framebuffer back when leaving one.
    virtual bool uses3D() const { return false; }
    // Whether Esc and Tab (which otherwise leave and switch pages) go to
    // the page instead.
    virtual bool takesKey(const Event& /*ev*/) const { return false; }

protected:
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void setTitle(const std::string& detail);

    ViewerContext& ctx_;
};

// The first part of a name's file name, for jumping between kinds:
// "geometry/vpbug.pkg" -> "vpbug", "texture/sw_stc_tan.tex" -> "sw".
std::string kindOf(const std::string& name);

// Whether a model's part is one the game shows only when it's lit: a
// car's lamps and their glows (HEADLIGHT0, HLIGHT, TLIGHT, BLIGHT,
// RLIGHT, SLIGHT0, SIREN0, SRN0), a traffic light's (REDGLOWDAY...) and a
// crossing's signs (WALK_NIGHT...).
bool isLightPart(const std::string& part);

// A file's contents as lines to show: its text (tabs expanded, CR LF and
// LF ends) if it looks like text, else a hex dump of 16 bytes a line.
std::vector<std::string> fileLines(const std::vector<uint8_t>& data, bool& isText);

class FileViewer : public Page {
public:
    explicit FileViewer(ViewerContext& ctx);
    const char* name() const override { return "Files"; }
    size_t count() const override { return files_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;
    bool takesKey(const Event& ev) const override;

private:
    struct Row {
        std::string name;
        std::string source; // the archive's file name
        uint64_t size = 0;
    };

    void applyFolder();
    void open();
    void draw();
    int rows() const;

    std::vector<Row> files_;
    std::vector<std::string> folders_; // top-level folders, for Left/Right
    int folder_ = -1;                  // index into folders_, -1: all
    std::vector<size_t> shown_;        // files_ indices in the folder
    int selected_ = 0, top_ = 0;

    // The open file, if any.
    bool viewing_ = false, isText_ = false;
    std::string viewName_;
    std::vector<std::string> lines_;
    int lineTop_ = 0;
};

class TextureViewer : public Page {
public:
    explicit TextureViewer(ViewerContext& ctx);
    const char* name() const override { return "Textures"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void show(int index);
    void draw();

    std::vector<std::string> names_;
    int index_ = 0;
    int zoom_ = 0; // 0: fit the screen, else that many times the size
    bool alpha_ = true;
    Tex tex_;
    std::string error_;
};

class ModelViewer : public Page {
public:
    explicit ModelViewer(ViewerContext& ctx);
    ~ModelViewer() override;
    const char* name() const override { return "Models"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool uses3D() const override { return true; }

private:
    // One draw: a group's triangles in the uploaded mesh, with its shader.
    struct Draw {
        uint32_t firstIndex = 0, indexCount = 0;
        gfx::TextureId texture = 0;
        std::array<float, 4> color{1, 1, 1, 1};
        bool translucent = false;
        bool lit = true;  // by the light (the mesh has normals), else as coloured
        bool glow = false; // added to what's behind (a light's halo)
    };

    void show(int index);
    void build(); // the current level of detail and paint job, uploaded
    void freeGpu();
    gfx::TextureId texture(const std::string& name, bool& translucent);
    void resetView();
    void drawInfo();

    std::vector<std::string> names_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    Surface overlay_;

    Pkg pkg_;
    std::string error_;
    std::vector<std::string> levels_; // the model's levels of detail, finest first
    int level_ = 0;
    uint32_t paintJob_ = 0;
    gfx::MeshId mesh_ = 0;
    std::vector<Draw> draws_;
    struct LoadedTexture {
        gfx::TextureId id = 0;
        bool translucent = false;
    };
    std::map<std::string, LoadedTexture> textures_; // by lower-case name; id 0: missing
    std::vector<std::string> missing_;
    size_t triangles_ = 0, vertices_ = 0;

    gfx::Vec3 center_{};
    float radius_ = 1;
    float yaw_ = 0, pitch_ = 0, distance_ = 1;
    bool dragging_ = false, showInfo_ = true, showLights_ = false;
    int lastX_ = 0, lastY_ = 0;
};

} // namespace reluva::age
