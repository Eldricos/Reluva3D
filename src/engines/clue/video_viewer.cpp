// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/viewers.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;

} // namespace

VideoViewer::VideoViewer(System& sys, Surface& screen, const GameData& data, audio::Mixer* mixer)
    : sys_(sys), screen_(screen), data_(data), mixer_(mixer) {
    files_ = data_.withExtension(".avi");
}

void VideoViewer::activate() {
    load();
    start();
}

void VideoViewer::deactivate() {
    stop();
    running_ = false;
}

void VideoViewer::load() {
    stop();
    running_ = false;
    error_.clear();
    sound_.reset();
    drawnFrame_ = size_t(-1);
    if (files_.empty())
        return;
    const auto& path = files_[size_t(current_)];
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (Error err = player_.open(std::move(bytes))) {
        error_ = err.message;
        return;
    }
    if (player_.file().hasAudio()) {
        auto s = std::make_shared<audio::Sound>();
        if (Error err = player_.file().audio(*s))
            error_ = err.message;
        else
            sound_ = s;
    }
    sys_.setWindowTitle("Clue videos: " + path.filename().string());
}

void VideoViewer::start() {
    stop();
    if (!error_.empty() || files_.empty())
        return;
    startMs_ = sys_.getMillis();
    running_ = true;
    drawnFrame_ = size_t(-1);
    if (mixer_ && sound_)
        playing_ = mixer_->play(sound_, audio::SoundType::Speech);
}

void VideoViewer::stop() {
    if (mixer_ && playing_ != audio::kNoSound)
        mixer_->stop(playing_);
    playing_ = audio::kNoSound;
}

void VideoViewer::handleKey(uint32_t key) {
    if (files_.empty())
        return;
    const int n = int(files_.size());
    switch (key) {
    case key::Right:
        current_ = (current_ + 1) % n;
        load();
        start();
        break;
    case key::Left:
        current_ = (current_ + n - 1) % n;
        load();
        start();
        break;
    case key::Space:
    case key::Return:
        if (running_) {
            stop();
            running_ = false;
        } else {
            start();
        }
        break;
    case 'z': zoom_ = zoom_ == 0 ? 1 : zoom_ == 1 ? 2 : 0; break;
    default: return;
    }
    drawnFrame_ = size_t(-1);
    draw();
}

void VideoViewer::update(uint64_t nowMs) {
    now_ = nowMs;
    if (running_ && error_.empty()) {
        const double t = double(nowMs - startMs_) / 1000.0;
        if (Error err = player_.seek(t)) {
            error_ = err.message;
            stop();
            running_ = false;
        } else if (player_.finished(t) && (playing_ == audio::kNoSound || !mixer_ || !mixer_->isPlaying(playing_))) {
            running_ = false;
            playing_ = audio::kNoSound;
        }
    }
    if (player_.frameShown() != drawnFrame_ || !error_.empty())
        draw();
}

void VideoViewer::draw() {
    fillRect(screen_, 0, 0, kScreenW, kScreenH, {0, 0, 0});
    if (files_.empty()) {
        drawInfo(screen_, "No videos found");
        return;
    }
    const std::string name = files_[size_t(current_)].filename().string();
    if (!error_.empty()) {
        drawInfo(screen_, name, error_);
        return;
    }
    drawnFrame_ = player_.frameShown();
    const image::Image& pic = player_.picture();
    // As large as fits (twice the size for 320x240, which then fills the
    // screen under the info line), or as Z chose; shrunk if it doesn't fit.
    const int areaH = kScreenH - kInfoH;
    double scale = zoom_ ? zoom_ : std::max(1, std::min(kScreenW / pic.w, kScreenH / pic.h));
    if (pic.w * scale > kScreenW || pic.h * scale > kScreenH)
        scale = std::min(double(kScreenW) / pic.w, double(areaH) / pic.h);
    const int w = int(pic.w * scale), h = int(pic.h * scale);
    const int x0 = (kScreenW - w) / 2, y0 = std::max(0, (h > areaH ? kScreenH - h : areaH - h) / 2);
    for (int y = 0; y < h && y0 + y < kScreenH; ++y) {
        const int sy = std::min(pic.h - 1, int(y / scale));
        uint8_t* d = screen_.row(y0 + y) + x0 * 4;
        for (int x = 0; x < w; ++x, d += 4) {
            const uint8_t* p = pic.pixel(std::min(pic.w - 1, int(x / scale)), sy);
            d[0] = p[0];
            d[1] = p[1];
            d[2] = p[2];
            d[3] = 255;
        }
    }
    const video::AviFile& avi = player_.file();
    char line[160];
    std::snprintf(line, sizeof line, "%dx%d %s, frame %zu/%zu at %.0f a second%s%s", avi.width(), avi.height(),
                  avi.videoCodec().c_str(), player_.frameShown() + 1, avi.frameCount(), avi.framesPerSecond(),
                  sound_ ? ", with sound" : "", running_ ? "" : "  (stopped)");
    drawInfo(screen_, name + "   " + std::to_string(current_ + 1) + "/" + std::to_string(files_.size()), line);
}

} // namespace reluva::clue
