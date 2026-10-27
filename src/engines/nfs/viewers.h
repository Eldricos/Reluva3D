// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/gfx3d.h"
#include "common/image.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/nfs/fce.h"
#include "engines/nfs/col.h"
#include "engines/nfs/frd.h"
#include "engines/nfs/hrz.h"
#include "engines/nfs/bnk.h"
#include "engines/nfs/mad.h"
#include "engines/nfs/mus.h"
#include "engines/nfs/ffn.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/text_table.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
    // Whether Esc goes to the page rather than back to the launcher.
    virtual bool capturesEscape() const { return false; }
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
    // 2D pages: called often while shown, for playback; true when the page
    // needs drawing again.
    virtual bool tick(uint64_t /*nowMs*/) { return false; }
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

// The tracks in 3D, with their textures, objects, lane markings and sky.
//   PageUp/PageDown: track   W/S/A/D: move   arrows or drag: look
//   Space/C: up/down   Shift: faster   N/B: next/previous block
//   1/2/3: road detail   H: day, night, weather   T: textures
//   V: vertex light   L: lanes   O: objects   G: the virtual road's walls
//   X: other texture set   F: back faces   R: start
class TrackPage : public Page {
public:
    TrackPage(const GameFiles& files, const GameFolders& folders);
    ~TrackPage() override;
    const char* name() const override { return "Tracks"; }
    bool empty() const override { return tracks_.empty(); }
    void activate(System& sys) override;
    void deactivate() override;
    bool uses3D() const override { return renderer_ != nullptr; }
    void handleEvent(const Event& ev) override;
    void draw(Surface& screen) override; // when 3D isn't available
    void update(uint64_t nowMs) override;

private:
    // Draw calls of one kind of polygon, a range of the mesh's indices
    // each, one per picture.
    struct Batch {
        gfx::TextureId texture = 0;
        uint32_t firstIndex = 0, indexCount = 0;
        bool additive = false;
    };
    enum Kind { kRoadLow, kRoadMedium, kRoadHigh, kObjects, kLanes, kWalls, kKinds };
    // An object moving along its keys; its vertices are stored as in the
    // file (unmirrored, relative to the key) and placed each frame.
    struct Moving {
        std::vector<Track::ExtraObject::Key> keys;
        int keyDelay = 0;
        std::vector<Batch> batches;
    };

    void load(int index);
    void loadSky();
    void release();
    void releaseSky();
    void toBlock(int block);
    gfx::TextureId picture(const std::string& sheet, int number, bool lane, bool* additive);
    gfx::TextureId skyPicture(const std::string& name, std::string* text);

    const GameFiles& files_;
    GameFolders folders_;
    std::vector<std::string> tracks_; // trkNNN folders
    int index_ = 0;
    gfx::Renderer3D* renderer_ = nullptr;
    std::string unavailable_;
    Surface overlay_;
    Track track_;
    std::string error_;
    gfx::MeshId mesh_ = 0;
    std::vector<gfx::TextureId> textures_;
    std::map<std::string, ShapeBank> banks_; // loaded sheets, by file name
    std::map<std::string, std::pair<gfx::TextureId, bool>> pictures_; // "sheet/number" -> texture, additive
    std::array<std::vector<Batch>, kKinds> batches_;
    std::vector<Moving> moving_;
    TrackCollision collision_;
    std::string collisionError_;
    // The sky, for the variant shown (0 day, 1 night, 2 weather, 3 both).
    Horizon horizon_;
    std::string skyError_;
    gfx::MeshId skyMesh_ = 0;
    gfx::TextureId horizonPicture_ = 0;
    uint32_t domeIndices_ = 0, bandIndices_ = 0;
    int variant_ = 0;
    int detail_ = 2, textureSet_ = 0, block_ = 0;
    bool textured_ = true, vertexLight_ = true, lanes_ = true, objects_ = true, cullBack_ = false, walls_ = false;
    std::set<uint32_t> held_;
    bool dragging_ = false;
    int lastX_ = 0, lastY_ = 0;
    std::array<float, 3> eye_{};
    float yaw_ = 0, pitch_ = 0;
    uint64_t lastMs_ = 0;
};

// Every sound in every bank (.bnk), loose or in a .viv: speech, engines,
// effects. Without audio output the sounds are only listed.
//   Up/Down: sound   PageUp/PageDown: bank   Home/End: first/last sound
//   Space/Enter: play   S: stop   P: play as selected on/off
class SoundPage : public Page {
public:
    SoundPage(const GameFiles& files, audio::Mixer* mixer);
    const char* name() const override { return "Sounds"; }
    bool empty() const override { return banks_.empty(); }
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int bank);
    void select(int sound);
    void play();

    const GameFiles& files_;
    audio::Mixer* mixer_;
    std::vector<FileRef> banks_;
    int bank_ = -1;
    std::vector<uint8_t> data_;
    SoundBank loaded_;
    std::vector<int> sounds_; // the bank's slots that hold a sound
    int sound_ = 0;
    std::string error_, soundError_;
    std::shared_ptr<audio::Sound> decoded_;
    audio::SoundHandle playing_ = audio::kNoSound;
    bool autoPlay_ = true;
};

// The music streams (.mus), played as their Pathfinder map (.map) leads
// from segment to segment by an intensity, or in the linear order (.lin).
//   Up/Down: stream   Space: play/stop   Left/Right: intensity
//   L: map or linear order   N: next segment now   Home: first segment
class MusicPage : public Page {
public:
    MusicPage(const GameFiles& files, audio::Mixer* mixer);
    const char* name() const override { return "Music"; }
    bool empty() const override { return streams_.empty(); }
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int stream);
    void start(int section);
    void stop();
    int following(int section) const;
    const MusicMap* activeMap() const;

    const GameFiles& files_;
    audio::Mixer* mixer_;
    std::vector<FileRef> streams_;
    int stream_ = -1;
    MusicStream loaded_;
    MusicMap map_, linear_;
    bool haveMap_ = false, haveLinear_ = false, useLinear_ = false;
    std::string error_, mapError_;
    int intensity_ = 50;
    int section_ = -1; // playing
    std::vector<int> history_;
    std::shared_ptr<audio::Sound> current_;
    audio::SoundHandle playing_ = audio::kNoSound;
    uint64_t lastDraw_ = 0;
};

// The movies (.mad) with their sound.
//   Up/Down: movie   Space: pause   Left/Right: 5 seconds back/on
//   Home: start   PageUp/PageDown: frame by frame (paused)
class MoviePage : public Page {
public:
    MoviePage(const GameFiles& files, audio::Mixer* mixer);
    const char* name() const override { return "Movies"; }
    bool empty() const override { return movies_.empty(); }
    void activate(System& sys) override;
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int movie);
    void seek(int frame, uint64_t nowMs);
    void showFrame(int frame);
    void stopSound();
    double frameMs() const;

    const GameFiles& files_;
    audio::Mixer* mixer_;
    std::vector<FileRef> movies_;
    int movie_ = -1;
    MadMovie loaded_;
    std::string error_;
    std::shared_ptr<audio::Sound> audio_;
    MadDecoder decoder_;
    int decoded_ = -1; // the frame the decoder holds
    Surface picture_;
    bool paused_ = false;
    // The clock: the audio's position while it plays, else time since
    // startMs_ from startFrame_.
    int startFrame_ = 0;
    uint64_t startMs_ = 0, nowMs_ = 0;
    audio::SoundHandle playing_ = audio::kNoSound;
};

// Every page, in Tab order, those with nothing to show left out. The
// mixer plays the pages' sounds; null without audio output. The game
// writes under `saveFolder`. `settings` are the game's section of
// reluva.ini: renderscale (1-4, the Play page's frame against the game's
// 640x480; 2 if not set) and texturefilter (linear: bilinear textures).
std::vector<std::unique_ptr<Page>> makePages(const char* gameName, const GameFolders& folders,
                                             const GameFiles& files, audio::Mixer* mixer = nullptr,
                                             const std::filesystem::path& saveFolder = {},
                                             const std::map<std::string, std::string>& settings = {});

} // namespace reluva::nfs
