// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "common/audio/sound.h"
#include "common/events.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/clue/game_data.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Data viewers the Clue engine shows until the game itself runs. Each
// draws into the engine's 640x480 RGBA32 screen, with a line of information
// at the bottom, and reports its state in the window title.

namespace reluva::clue {

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

// Drawing helpers the viewers share (viewer_draw in image_viewer.cpp).
void fillRect(Surface& s, int x, int y, int w, int h, Color c);
void drawCheckerboard(Surface& s, int y0, int y1);
void drawInfo(Surface& s, const std::string& line1, const std::string& line2 = {});
// An image with its top-left corner at (x, y), scaled up by a whole number.
void drawImage(Surface& s, const image::Image& img, int x, int y, int scale = 1);

// Every image of every .res file.
//   Left/Right: previous/next image   PageUp/PageDown: 10 at a time
//   Up/Down: previous/next file   Home/End: first/last image   Z: zoom (1x, 2x, 4x)
//   B: background (checks, black, white)
class ImageViewer {
public:
    ImageViewer(System& sys, Surface& screen, const GameData& data);
    size_t count() const { return total_; }
    void activate();
    void handleKey(uint32_t key);

private:
    void draw();

    System& sys_;
    Surface& screen_;
    const GameData& data_;
    std::vector<std::filesystem::path> files_;
    size_t total_ = 0;
    int file_ = 0, image_ = 0;
    int zoom_ = 1, background_ = 0;
};

// Every animation of every .a file, playing.
//   Left/Right: previous/next animation   Up/Down: previous/next file
//   Space: pause or play   ,/.: step a frame back or on (pausing)
//   Z: zoom (1x, 2x)   A: show the anchor point   L: show one layer at a time
class AnimationViewer {
public:
    AnimationViewer(System& sys, Surface& screen, const GameData& data);
    size_t count() const { return total_; }
    void activate();
    void handleKey(uint32_t key);
    void update(uint64_t nowMs);

private:
    void select();
    void draw();

    System& sys_;
    Surface& screen_;
    const GameData& data_;
    std::vector<std::filesystem::path> files_;
    size_t total_ = 0;
    int file_ = 0, anim_ = 0;
    const Animation* current_ = nullptr;
    std::string name_, error_;
    size_t frame_ = 0;
    uint64_t frameStart_ = 0;
    bool paused_ = false, showAnchor_ = true;
    int zoom_ = 1;
    int layer_ = -1; // all
    bool dirty_ = true;
};

// Every sound: the game's WAV files, and the CD's music (track02.wav ...)
// if it's been ripped into the game folder.
//   Left/Right: previous/next   PageUp/PageDown: 10 at a time
//   Space or Enter: play or stop   A: play each as it's selected
class SoundViewer {
public:
    // mixer: the engine's, playing into the System's output; nullptr when
    // there's no output.
    SoundViewer(System& sys, Surface& screen, const GameData& data, audio::Mixer* mixer);
    size_t count() const { return files_.size(); }
    void activate();
    void deactivate(); // stops playing when switching to another viewer
    void handleKey(uint32_t key);
    void update(uint64_t nowMs);

private:
    void play();
    void stop();
    void draw();

    System& sys_;
    Surface& screen_;
    const GameData& data_;
    audio::Mixer* mixer_;
    std::vector<std::filesystem::path> files_;
    int current_ = 0;
    bool autoPlay_ = false;
    audio::SoundHandle playing_ = audio::kNoSound;
    std::shared_ptr<audio::Sound> sound_;
    std::string info_;
    uint64_t lastDraw_ = 0;
};

// The text files: How to Play, the ticker's sentences, credits.
//   Left/Right: previous/next file   Up/Down: scroll a line
//   PageUp/PageDown: scroll a page   Home/End: top/bottom
class TextViewer {
public:
    TextViewer(System& sys, Surface& screen, const GameData& data);
    size_t count() const { return files_.size(); }
    void activate();
    void handleKey(uint32_t key);

private:
    void load();
    void draw();

    System& sys_;
    Surface& screen_;
    const GameData& data_;
    std::vector<std::filesystem::path> files_;
    int current_ = 0;
    std::vector<std::string> lines_; // wrapped, UTF-8
    int top_ = 0;
};

} // namespace reluva::clue
