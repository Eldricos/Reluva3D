// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "audio/mixer.h"
#include "audio/sound.h"
#include "image/image.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/formats/data_table.h"
#include "engines/botf/formats/font.h"
#include "engines/botf/formats/menu_file.h"
#include "engines/botf/formats/snd_archive.h"
#include "engines/botf/formats/text_table.h"
#include "engines/botf/formats/window_layout.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Data viewers used by the Birth of the Federation engine until the game
// itself runs. Each draws into the engine's 800x600 RGBA32 screen (the
// game's own resolution) and reports its state in the window title.

namespace reluva::botf {

constexpr int kScreenW = 800;
constexpr int kScreenH = 600;

/** Fills a rectangle of an RGBA32 surface, clipped to it. */
void fillRect(Surface &s, int x, int y, int w, int h, Color c);

/**
 * Browses the TGA images of the given archives (stbof.res, alt.res).
 *   Left/Right: previous/next   PageUp/PageDown: jump 10   Home/End: first/last
 *   S: the next archive's first image   I: the status line on/off
 *   K: the colour key (magenta shown as transparent) on/off
 */
class ImageViewer {
public:
    ImageViewer(System &sys, Surface &screen, const std::vector<const Archive *> &archives);
    size_t count() const { return _images.size(); }
    void activate();
    void handleKey(uint32_t key);

private:
    struct ImageRef {
        const Archive *archive;
        std::string name;
    };
    void show(size_t index);

    System &_sys;
    Surface &_screen;
    std::vector<ImageRef> _images;
    size_t _current = 0;
    bool _info = true;
    bool _key = false;
};

/**
 * Shows the .fnt fonts of an archive (stbof.res): the glyph sheet, and
 * sample text drawn with the font.
 *   Left/Right: previous/next font   +/-: the sample text's size
 */
class FontViewer {
public:
    FontViewer(System &sys, Surface &screen, const Archive &archive);
    size_t count() const { return _names.size(); }
    void activate();
    void handleKey(uint32_t key);

private:
    void show();

    System &_sys;
    Surface &_screen;
    const Archive &_archive;
    std::vector<std::string> _names;
    size_t _current = 0;
    int _scale = 1;
};

/**
 * Shows the .tdb text tables of an archive (stbof.res), each entry's key
 * and its text drawn in one of the game's fonts.
 *   Left/Right: previous/next table   Up/Down: scroll   PageUp/PageDown:
 *   scroll a screen   Home/End: first/last entry   F: the next font
 */
class TextViewer {
public:
    TextViewer(System &sys, Surface &screen, const Archive &archive);
    size_t count() const { return _tables.size(); }
    void activate();
    void handleKey(uint32_t key);

private:
    void load();
    void show();

    System &_sys;
    Surface &_screen;
    const Archive &_archive;
    std::vector<std::string> _tables;
    std::vector<std::string> _fonts;
    size_t _current = 0;
    size_t _font = 0;
    size_t _first = 0;
    TextTable _table;
    Font _fontData;
    std::string _error;
};

/**
 * Shows the data tables worked out so far (tableLayouts()): the records by
 * name, and the selected one's fields, the bytes no field covers yet, and
 * its description.
 *   Left/Right: previous/next table   Up/Down: previous/next record
 *   PageUp/PageDown: jump 20   Home/End: first/last record
 */
class DataViewer {
public:
    DataViewer(System &sys, Surface &screen, const Archive &archive);
    size_t count() const { return _files.size(); }
    void activate();
    void handleKey(uint32_t key);

private:
    void load();
    void show();

    System &_sys;
    Surface &_screen;
    const Archive &_archive;
    std::vector<const TableLayout *> _files; // the layouts whose file is in the archive
    size_t _current = 0;
    size_t _record = 0;
    DataTable _table;
    std::string _error;
};

/**
 * Shows the .wdf window layouts of an archive (stbof.res): each widget's
 * pictures where the layout puts them, outlined by type, and the selected
 * widget's details.
 *   Left/Right: previous/next layout   Up/Down: previous/next widget
 *   PageUp/PageDown: jump 10 layouts   O: outlines on/off
 *   S: buttons' selected pictures   I: the details on/off
 * A selected menu widget is shown open: its .mnu's items in its fonts,
 * framed by its pictures (how the game frames it isn't known yet).
 */
class LayoutViewer {
public:
    LayoutViewer(System &sys, Surface &screen, const Archive &archive);
    size_t count() const { return _names.size(); }
    void activate();
    void handleKey(uint32_t key);

private:
    void load();
    void show();
    void drawMenu(const WindowLayout::Placed &placed, std::vector<std::string> &lines);

    System &_sys;
    Surface &_screen;
    std::vector<std::string> _names;
    size_t _current = 0;
    size_t _widget = 0;
    WindowLayout _layout;
    std::vector<WindowLayout::Placed> _placed;
    std::string _error;
    Assets _assets;
    bool _outlines = true;
    bool _selected = false;
    bool _info = true;
};

/**
 * Plays the .snd archives' sounds: sfx.snd's effects, the language's speech,
 * and music.snd's pieces (each played whole).
 *   Up/Down: previous/next   PageUp/PageDown: jump 20   Home/End: first/last
 *   Left/Right: previous/next archive   Space or Enter: play or stop
 *   A: play each sound as it's selected   R: the ADPCM sounds' sample rate
 *   (11,025, 22,050 or 44,100 Hz; it isn't stored, and 22,050 is right)
 */
class SoundViewer {
public:
    /** `mixer` may be null (no audio device): sounds are listed, not played. */
    SoundViewer(System &sys, Surface &screen, std::vector<std::pair<std::string, const SndArchive *>> archives,
                audio::Mixer *mixer);
    size_t count() const { return _items.size(); }
    void activate();
    void deactivate(); // stops what's playing
    void handleKey(uint32_t key);
    void update(uint64_t nowMs); // redraws while a sound plays

private:
    struct Item {
        size_t archive; // index into archives_
        size_t index;   // entry, or piece for music.snd
    };
    void select(size_t index);
    void play();
    void stop();
    void decodeCurrent();
    void redraw();
    std::string describe(size_t item) const;

    System &_sys;
    Surface &_screen;
    std::vector<std::pair<std::string, const SndArchive *>> _archives;
    audio::Mixer *_mixer;
    std::vector<Item> _items;
    size_t _current = 0;
    std::shared_ptr<audio::Sound> _sound; // the current item, decoded
    std::string _error;
    audio::SoundHandle _playing = audio::kNoSound;
    bool _autoPlay = false;
    int _rate = kDefaultAdpcmRate;
    uint64_t _lastDrawMs = 0;
};

} // namespace reluva::botf
