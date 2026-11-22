// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/events.h"
#include "graphics/gfx3d.h"
#include "image/image.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/nfs/formats/col.h"
#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/nfs3/formats/mad.h"
#include "engines/nfs/formats/mus.h"
#include "engines/nfs/formats/ffn.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/formats/text_table.h"
#include "engines/nfs/ui/page.h"
#include "engines/nfs/ui/page_draw.h"

#include <array>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// The Need for Speed engine's data viewer, in reluva-viewer: pages showing
// what the game's files hold, and the game's own (PlayPage). The 2D ones
// draw into the engine's 640x480 RGBA32 screen when asked; the 3D ones
// take the GPU renderer while shown and draw a frame each update. Tab
// switches between them (runPages).

namespace reluva::nfs {

namespace page {

/** A viewer page's title line and, at the bottom, its keys. */
inline void frame(Surface &s, const std::string &title, const std::string &keys) {
    clear(s);
    text(s, kMargin, 8, title, kTitleColor);
    text(s, kMargin, s.h - font::kLineHeight - 4, keys + "   Tab: next page   Esc: back", kLabelColor);
}

} // namespace page

// How the Track and Cars pages sample the game's textures: bilinear, no
// mipmaps. The game never sends THRASH's filter, LOD bias or mipmap
// states (7, 8, 0xb), but voodoo2a.dll sends itself 7 1 and 0xb 1 after
// setvideomode, so on a Voodoo2 the game's textures are bilinear-filtered
// (thrash.md, Texture filtering, from running the DLL on the Glide
// stand-in); its textures have one level each, so no mipmaps.
// reluva.ini's texturefilter=point holds them point-sampled (makePages).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): a reluva.ini setting every viewer page reads
inline bool g_pagesPointSampled = false;
/** The texture parameters the 3D pages use; `clamp` for a texture that doesn't repeat. */
inline gfx::TextureParams glideTextureParams(bool clamp = false) {
    gfx::TextureParams params;
    params.clamp = clamp;
    params.mipmaps = false;
    params.nearest = g_pagesPointSampled;
    return params;
}

/** What the engine found: the folders, the CD's files, tracks and cars. */
class InfoPage : public Page {
public:
    /** Lists what's in `folders` once. */
    InfoPage(const char *gameName, GameFolders folders);
    const char *name() const override { return "Info"; }
    void handleKey(uint32_t, uint16_t) override {}
    void draw(Surface &screen) override;

private:
    std::string _gameName;
    GameFolders _folders;
    std::vector<std::string> _tracks;
    std::vector<std::string> _cars;
    std::vector<std::string> _music;
    std::vector<std::string> _movies;
};

/**
 * Every picture in every .fsh/.qfs, loose or in a .viv.
 *   Up/Down: bank   PageUp/PageDown: 10 banks   Left/Right: picture
 *   Home/End: first/last picture   B: background   A: alpha on/off
 */
class TexturePage : public Page {
public:
    /** Shows the .fsh and .qfs files of `files`, which must outlive the page. */
    explicit TexturePage(const GameFiles &files);
    const char *name() const override { return "Textures"; }
    bool empty() const override { return _banks.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;

private:
    void load(int bank);
    void decodeCurrent();

    const GameFiles &_files;
    std::vector<FileRef> _banks;
    int _bank = -1;
    int _shape = 0;
    ShapeBank _loaded;
    std::string _error;
    image::Image _image;
    std::string _imageError;
    int _background = 0; // checkerboard, black, white
    bool _alpha = true;
};

/**
 * Each car's cockpit (dash.qfs): the dash, the steering wheel, the gear
 * indicator and the speedometer and tachometer needles, drawn as the game
 * draws them.
 *   Left/Right: car   Space: sweep the needles and wheel   Up/Down: move
 *   the needles (paused)   A/D: steer   G: next gear
 */
class CockpitPage : public Page {
public:
    /** Shows the dash.qfs files of `files`, which must outlive the page. */
    explicit CockpitPage(const GameFiles &files);
    const char *name() const override { return "Cockpits"; }
    bool empty() const override { return _dashes.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int index);

    const GameFiles &_files;
    std::vector<FileRef> _dashes;
    int _index = -1;
    std::string _error;
    image::Image _dash;
    image::Image _wheel;
    // where the wheel's pivot goes
    int _wheelX = 0;
    int _wheelY = 0;
    // in the wheel's picture
    int _wheelPivotX = 0;
    int _wheelPivotY = 0;
    std::array<image::Image, std::size(kGearPictures)> _gears;
    // the gear indicator's top left
    int _gearX = 0;
    int _gearY = 0;
    bool _haveGauges = false;
    Cockpit _cockpit;
    float _position = 0; // 0-1 of each gauge's range
    int _steering = 0;   // -128 to 127, as the car's
    int _gear = 1;       // neutral
    bool _sweeping = true;
    uint64_t _lastMs = 0;
};

/**
 * The front-end string tables, in the game's own font.
 *   Left/Right: language   Up/Down: line   PageUp/PageDown: page
 *   Home/End: first/last   F: the game's font or Reluva's
 */
class StringPage : public Page {
public:
    /** Shows the string tables of `files`, which must outlive the page. */
    explicit StringPage(const GameFiles &files);
    const char *name() const override { return "Strings"; }
    bool empty() const override { return _tables.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;

private:
    void load(int table);
    int linesPerPage() const;

    const GameFiles &_files;
    std::vector<FileRef> _tables;
    int _table = -1;
    int _top = 0;
    TextTable _loaded;
    std::string _error;
    std::optional<Font> _font; // arial10b.ffn, if it reads
    bool _gameFont = true;
};

/**
 * Every font, with all its characters.
 *   Left/Right: font   +/-: size
 */
class FontPage : public Page {
public:
    /** Shows the .ffn files of `files`, which must outlive the page. */
    explicit FontPage(const GameFiles &files);
    const char *name() const override { return "Fonts"; }
    bool empty() const override { return _fonts.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;

private:
    void load(int font);

    const GameFiles &_files;
    std::vector<FileRef> _fonts;
    int _font = -1;
    Font _loaded;
    std::string _error;
    int _scale = 1;
};

/**
 * Every sound in every bank (.bnk), loose or in a .viv: speech, engines,
 * effects. Without audio output the sounds are only listed.
 *   Up/Down: sound   PageUp/PageDown: bank   Home/End: first/last sound
 *   Space/Enter: play   S: stop   P: play as selected on/off
 *   Left/Right: a sound with a bend range (most): its bend, or for a car's
 *   sounds its revs; others: the pitch, a semitone (sounds with a loop loop
 *   until stopped)
 */
class SoundPage : public Page {
public:
    /** Lists the .bnk files of `files`, which must outlive the page; `mixer` null for no sound. */
    SoundPage(const GameFiles &files, audio::Mixer *mixer);
    const char *name() const override { return "Sounds"; }
    bool empty() const override { return _banks.empty(); }
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int bank);
    void select(int sound);
    void play();

    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<FileRef> _banks;
    int _bank = -1;
    std::vector<uint8_t> _data;
    SoundBank _loaded;
    std::vector<int> _sounds; // the bank's slots that hold a sound
    int _sound = 0;
    std::string _error;
    std::string _soundError;
    std::shared_ptr<audio::Sound> _decoded;
    audio::SoundHandle _playing = audio::kNoSound;
    bool _autoPlay = true;
    int _semitones = 0; // the pitch, up or down
    // Engines, played as the game plays them (bnk.md): at key 60, bent by
    // the revs, with the car's revs from its carp.txt when the bank has one
    // beside it (else the bend is chosen directly).
    int _bend = 64;
    int _rpm = 0;
    int _minRpm = 0;
    int _redline = 0;
    bool engine() const;            // the selected sound has a bend range (0x0A)
    double rate() const;            // the rate to play it at, of its own
    std::string engineText() const; // the revs, bend and pitch, for the header
};

/**
 * The music streams (.mus), played as their Pathfinder map (.map) leads
 * from segment to segment by an intensity, or in the linear order (.lin).
 *   Up/Down: stream   Space: play/stop   Left/Right: intensity
 *   L: map or linear order   N: next segment now   Home: first segment
 *   0-9: an event for the next segment (the game's: 1 a pursuit, 4-9 the
 *   track's zones)
 */
class MusicPage : public Page {
public:
    /** Lists the .mus files of `files`, which must outlive the page; `mixer` null for no sound. */
    MusicPage(const GameFiles &files, audio::Mixer *mixer);
    const char *name() const override { return "Music"; }
    bool empty() const override { return _streams.empty(); }
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int stream);
    void start(int section);
    void stop();
    int following(int section); // takes the pending event, if any
    int firstSection() const;
    const MusicMap *activeMap() const;

    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<FileRef> _streams;
    int _stream = -1;
    MusicStream _loaded;
    MusicMap _map;
    MusicMap _linear;
    bool _haveMap = false;
    bool _haveLinear = false;
    bool _useLinear = false;
    std::string _error;
    std::string _mapError;
    int _intensity = 50;
    int _pendingEvent = -1; // an event (0-9 keys) for the next section, as the game's SNDpathevent
    int _section = -1;      // playing
    std::vector<int> _history;
    std::shared_ptr<audio::Sound> _current;
    // The segments playing one after another, one queued behind the one
    // playing; the section last queued.
    std::shared_ptr<audio::SoundQueue> _queue;
    int _queuedSection = -1;
    audio::SoundHandle _playing = audio::kNoSound;
    uint64_t _lastDraw = 0;
    std::shared_ptr<audio::Sound> decodeSection(int section);
};

/**
 * The movies (.mad) with their sound.
 *   Up/Down: movie   Space: pause   Left/Right: 5 seconds back/on
 *   Home: start   PageUp/PageDown: frame by frame (paused)
 *   G: shown as the game shows it (its decoder, then its display modes 3,
 *   2 or 1, mad.md) or by Reluva's plain conversion
 */
class MoviePage : public Page {
public:
    /** Lists the .mad files of `files`, which must outlive the page; `mixer` null for no sound. */
    MoviePage(const GameFiles &files, audio::Mixer *mixer);
    const char *name() const override { return "Movies"; }
    bool empty() const override { return _movies.empty(); }
    void activate(System &sys) override;
    void deactivate() override;
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override;
    bool tick(uint64_t nowMs) override;

private:
    void load(int movie);
    void seek(int frame, uint64_t nowMs);
    void showFrame(int frame);
    void stopSound();
    double frameMs() const;

    const GameFiles &_files;
    audio::Mixer *_mixer;
    std::vector<FileRef> _movies;
    int _movie = -1;
    nfs3::MadMovie _loaded;
    std::string _error;
    std::shared_ptr<audio::Sound> _audio;
    nfs3::MadDecoder _decoder;
    int _decoded = -1; // the frame the decoder holds
    int _display = 3;  // the game's display mode; 0: Reluva's conversion
    Surface _picture;
    bool _paused = false;
    // The clock: the audio's position while it plays, else time since
    // _startMs from _startFrame.
    int _startFrame = 0;
    uint64_t _startMs = 0;
    uint64_t _nowMs = 0;
    audio::SoundHandle _playing = audio::kNoSound;
};

/**
 * Every page, in Tab order, those with nothing to show left out. The
 * mixer plays the pages' sounds; null without audio output. The game
 * writes under `saveFolder`. `settings` are the game's section of
 * reluva.ini, for the Play page (makePlayPage) and texturefilter (point:
 * the viewer's textures point-sampled too).
 */
std::vector<std::unique_ptr<Page>> makePages(const char *gameName, const GameFolders &folders, const GameFiles &files,
                                             audio::Mixer *mixer = nullptr,
                                             const std::filesystem::path &saveFolder = {},
                                             const std::map<std::string, std::string> &settings = {});

} // namespace reluva::nfs
