// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/sound.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/legoisland/formats/si.h"
#include "image/image.h"
#include "video/flic.h"
#include "video/smacker.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// LEGO Island's data viewer, until the game itself runs. Its pages draw
// into a 1024x768 RGBA32 screen, larger than the game's 640x480 so a
// picture fits beside the list.

namespace reluva::legoisland {

/** The viewer's screen, in pixels. */
///@{
constexpr int kViewerW = 1024;
constexpr int kViewerH = 768;
///@}

/**
 * The SI files' objects, as a tree, and the selected one's details and
 * medium: a still drawn, an animation played, a sound played.
 *   Up/Down: previous/next object   PageUp/PageDown: jump 20
 *   Home/End: first/last   Left/Right: previous/next SI file
 *   M: media only, or every object   Space: play or stop
 *   A: play each sound or animation as it's selected
 */
class SiViewer {
public:
    /** `files` are the SI files to show; `mixer` may be null (no audio device): sounds aren't played. */
    SiViewer(System &sys, Surface &screen, std::vector<std::filesystem::path> files, audio::Mixer *mixer);
    /** How many files it shows; the viewer skips a page with none. */
    size_t count() const { return _files.size(); }
    /** Draws the page as it's shown. */
    void activate();
    /** Called as another page is shown: stops what's playing. */
    void deactivate();
    /** A key pressed on the page; it draws again. */
    void handleKey(uint32_t key);
    /** Moves an animation on, and draws again, while one plays; `nowMs` is the system's clock. */
    void update(uint64_t nowMs);

private:
    struct Row {
        const SiObject *object;
        int depth;
    };
    void openFile(size_t index);
    void buildRows();
    void select(size_t row);
    void loadMedium();
    void play();
    void stop();
    void redraw();
    void drawList(int x, int y, int w, int h);
    void drawDetails(int x, int y, int w);
    void drawPicture(int x, int y, int w, int h);

    System &_sys;
    Surface &_screen;
    std::vector<std::filesystem::path> _files;
    audio::Mixer *_mixer;
    size_t _file = 0;
    SiFile _si;
    std::string _fileError;
    std::vector<Row> _rows;
    size_t _current = 0;
    bool _mediaOnly = false;
    bool _autoPlay = false;

    // The selected medium, rebuilt as its file.
    std::string _mediumInfo;
    std::string _mediumError;
    size_t _pieces = 0;
    image::Image _still;
    std::unique_ptr<flic::Decoder> _flic;
    std::unique_ptr<smacker::Decoder> _smk;
    std::shared_ptr<audio::Sound> _sound;
    image::Image _frame; // an animation's last frame, in colour
    bool _animating = false;
    uint64_t _nextFrameMs = 0;
    audio::SoundHandle _playing = audio::kNoSound;
    uint64_t _lastDrawMs = 0;
};

} // namespace reluva::legoisland
