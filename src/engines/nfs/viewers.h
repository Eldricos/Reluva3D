// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/gfx3d.h"
#include "common/image.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/nfs/fce.h"
#include "engines/nfs/ffn.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/text_table.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Data viewers used by the Need for Speed engine until the game itself
// runs. The 2D ones draw into the engine's 640x480 RGBA32 screen when
// asked; the 3D ones take the GPU renderer while shown and draw a frame
// each update. Tab switches between them (nfs.cpp).

namespace reluva::nfs {

class Page {
public:
    virtual ~Page() = default;
    virtual const char* name() const = 0;
    // Whether there's anything to show; pages without are skipped.
    virtual bool empty() const { return false; }
    // Called when the page is shown and before switching away. A 3D page
    // takes the renderer in activate(); the engine puts the framebuffer
    // back after it's left.
    virtual void activate(System& /*sys*/) {}
    virtual void deactivate() {}
    // Whether the page is drawing with the GPU (else draw() is used).
    virtual bool uses3D() const { return false; }
    virtual void handleEvent(const Event& ev) {
        if (ev.type == EventType::KeyDown)
            handleKey(ev.key, ev.mods);
    }
    virtual void handleKey(uint32_t /*key*/, uint16_t /*mods*/) {}
    // 2D pages: draws the page into the screen, after any event.
    virtual void draw(Surface& /*screen*/) {}
    // 3D pages: draws a frame.
    virtual void update(uint64_t /*nowMs*/) {}
};

// What the engine found: the folders, the CD's files, tracks and cars.
class InfoPage : public Page {
public:
    InfoPage(const char* gameName, GameFolders folders);
    const char* name() const override { return "Info"; }
    void handleKey(uint32_t, uint16_t) override {}
    void draw(Surface& screen) override;

private:
    std::string gameName_;
    GameFolders folders_;
    std::vector<std::string> tracks_, cars_, music_, movies_;
};

// Every picture in every .fsh/.qfs, loose or in a .viv.
//   Up/Down: bank   PageUp/PageDown: 10 banks   Left/Right: picture
//   Home/End: first/last picture   B: background   A: alpha on/off
class TexturePage : public Page {
public:
    explicit TexturePage(const GameFiles& files);
    const char* name() const override { return "Textures"; }
    bool empty() const override { return banks_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int bank);
    void decodeCurrent();

    const GameFiles& files_;
    std::vector<FileRef> banks_;
    int bank_ = -1, shape_ = 0;
    ShapeBank loaded_;
    std::string error_;
    image::Image image_;
    std::string imageError_;
    int background_ = 0; // checkerboard, black, white
    bool alpha_ = true;
};

// The front-end string tables, in the game's own font.
//   Left/Right: language   Up/Down: line   PageUp/PageDown: page
//   Home/End: first/last   F: the game's font or Reluva's
class StringPage : public Page {
public:
    explicit StringPage(const GameFiles& files);
    const char* name() const override { return "Strings"; }
    bool empty() const override { return tables_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int table);
    int linesPerPage() const;

    const GameFiles& files_;
    std::vector<FileRef> tables_;
    int table_ = -1, top_ = 0;
    TextTable loaded_;
    std::string error_;
    std::optional<Font> font_; // arial10b.ffn, if it reads
    bool gameFont_ = true;
};

// Every font, with all its characters.
//   Left/Right: font   +/-: size
class FontPage : public Page {
public:
    explicit FontPage(const GameFiles& files);
    const char* name() const override { return "Fonts"; }
    bool empty() const override { return fonts_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int font);

    const GameFiles& files_;
    std::vector<FileRef> fonts_;
    int font_ = -1;
    Font loaded_;
    std::string error_;
    int scale_ = 1;
};

// Every model (cars, traffic, police, the front end's track models) in 3D,
// with its skin.
//   Left/Right: model   drag or A/D/W/S: turn   wheel or +/-: zoom
//   L: detail level   T: textures   C: back faces   Space: spin   R: reset
class CarPage : public Page {
public:
    explicit CarPage(const GameFiles& files);
    ~CarPage() override;
    const char* name() const override { return "Cars"; }
    bool empty() const override { return models_.empty(); }
    void activate(System& sys) override;
    void deactivate() override;
    bool uses3D() const override { return renderer_ != nullptr; }
    void handleEvent(const Event& ev) override;
    void draw(Surface& screen) override; // when 3D isn't available
    void update(uint64_t nowMs) override;

private:
    void load(int index);
    void release();
    void resetView();

    const GameFiles& files_;
    std::vector<FileRef> models_;
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::string unavailable_; // why there's no 3D
    Surface overlay_;
    Model model_;
    std::string error_, skinName_;
    gfx::MeshId mesh_ = 0;
    gfx::TextureId skin_ = 0;
    std::vector<int> partLevels_; // 0 high, 1 medium, 2 small, 3 tiny
    int level_ = 0;               // 0-3, or 4 for all
    bool textures_ = true, cullBack_ = true, spin_ = false, dragging_ = false;
    float yaw_ = 0, pitch_ = 0, distance_ = 5, radius_ = 2;
    int lastX_ = 0, lastY_ = 0;
    uint64_t lastMs_ = 0;
};

// Every page, in Tab order, those with nothing to show left out.
std::vector<std::unique_ptr<Page>> makePages(const char* gameName, const GameFolders& folders,
                                             const GameFiles& files);

} // namespace reluva::nfs
