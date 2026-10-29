// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/viewers.h"

#include <algorithm>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;
// Frames whose time isn't given (still frames, the short ISOA form and
// animations without one) are shown this long while playing.
constexpr uint64_t kDefaultFrameMs = 100;

} // namespace

AnimationViewer::AnimationViewer(System& sys, Surface& screen, const GameData& data)
    : sys_(sys), screen_(screen), data_(data) {
    for (const auto& path : data_.withExtension(".a")) {
        const DataDrive* d = data_.drive(path.stem().string(), 'A');
        if (d && !d->entries().empty()) {
            files_.push_back(path);
            total_ += d->entries().size();
        }
    }
}

void AnimationViewer::activate() {
    select();
    frameStart_ = sys_.getMillis();
}

void AnimationViewer::select() {
    current_ = nullptr;
    frame_ = 0;
    layer_ = -1;
    dirty_ = true;
    if (files_.empty())
        return;
    const std::string drive = files_[size_t(file_)].stem().string();
    const DataDrive* d = data_.drive(drive, 'A');
    name_ = drive + ":" + d->entries()[size_t(anim_)].name;
    current_ = data_.animation(name_);
    error_ = current_ ? std::string() : data_.lastError();
    sys_.setWindowTitle("Clue animations: " + name_);
}

void AnimationViewer::handleKey(uint32_t key) {
    if (files_.empty())
        return;
    const int n = int(data_.drive(files_[size_t(file_)].stem().string(), 'A')->entries().size());
    const size_t frames = current_ ? current_->frameCount() : 0;
    switch (key) {
    case key::Right: anim_ = (anim_ + 1) % n; select(); break;
    case key::Left: anim_ = (anim_ + n - 1) % n; select(); break;
    case key::Down:
        file_ = (file_ + 1) % int(files_.size());
        anim_ = 0;
        select();
        break;
    case key::Up:
        file_ = (file_ + int(files_.size()) - 1) % int(files_.size());
        anim_ = 0;
        select();
        break;
    case key::Space: paused_ = !paused_; break;
    case '.':
        paused_ = true;
        if (frames)
            frame_ = (frame_ + 1) % frames;
        break;
    case ',':
        paused_ = true;
        if (frames)
            frame_ = (frame_ + frames - 1) % frames;
        break;
    case 'z': zoom_ = zoom_ == 1 ? 2 : 1; break;
    case 'a': showAnchor_ = !showAnchor_; break;
    case 'l':
        if (current_ && !current_->layers.empty())
            layer_ = layer_ + 1 >= int(current_->layers.size()) ? -1 : layer_ + 1;
        break;
    default: return;
    }
    frameStart_ = sys_.getMillis();
    dirty_ = true;
}

void AnimationViewer::update(uint64_t nowMs) {
    if (current_ && !paused_ && current_->frameCount() > 1) {
        const uint16_t d = current_->frames[frame_].duration;
        if (nowMs - frameStart_ >= (d ? d : kDefaultFrameMs)) {
            frame_ = (frame_ + 1) % current_->frameCount();
            frameStart_ = nowMs;
            dirty_ = true;
        }
    }
    if (dirty_)
        draw();
}

void AnimationViewer::draw() {
    dirty_ = false;
    const int areaH = kScreenH - kInfoH;
    drawCheckerboard(screen_, 0, areaH);
    if (files_.empty()) {
        drawInfo(screen_, "No animations found");
        return;
    }
    if (!current_) {
        drawInfo(screen_, name_, error_);
        return;
    }
    const Animation& a = *current_;
    const int z = (a.width * 2 <= kScreenW && a.height * 2 <= areaH) ? zoom_ : 1;
    // Isometric animations hang from their anchor point (a character's
    // feet), put low in the middle; the others are centred, among them the
    // short ISOA form's (the interface's, fonts), whose anchors are mostly 0.
    const bool hangs = a.isometric && !a.shortForm && !a.frames.empty();
    int ax = kScreenW / 2, ay = areaH / 2;
    int left = ax - a.width * z / 2, top = ay - a.height * z / 2;
    if (hangs) {
        ay = std::min(areaH - 8, areaH / 2 + a.height * z / 2);
        left = ax + a.frames[frame_].anchorX * z;
        top = ay + a.frames[frame_].anchorY * z;
    }
    std::string missing;
    for (size_t l = 0; l < a.layers.size(); ++l) {
        if (layer_ >= 0 && int(l) != layer_)
            continue;
        if (frame_ >= a.layers[l].size())
            continue;
        const Animation::Part& part = a.layers[l][frame_];
        if (part.image >= a.images.size())
            continue;
        const Image* img = data_.image(a.images[part.image]);
        if (!img) {
            missing = data_.lastError();
            continue;
        }
        drawImage(screen_, img->pixels, left + (part.x + img->offsetX) * z, top + (part.y + img->offsetY) * z, z);
    }
    if (showAnchor_ && hangs) {
        fillRect(screen_, ax - 6, ay, 13, 1, {255, 64, 64});
        fillRect(screen_, ax, ay - 6, 1, 13, {255, 64, 64});
    }
    std::string line2 = "frame " + std::to_string(frame_ + 1) + "/" + std::to_string(a.frameCount());
    if (!a.frames.empty() && a.frames[frame_].duration)
        line2 += " (" + std::to_string(a.frames[frame_].duration) + " ms)";
    line2 += "  " + std::to_string(a.width) + "x" + std::to_string(a.height) + "  " +
             std::to_string(a.images.size()) + " images, " + std::to_string(a.layers.size()) + " layer(s)";
    if (layer_ >= 0)
        line2 += " showing " + std::to_string(layer_ + 1);
    if (a.isometric)
        line2 += a.shortForm ? "  ISOA (short)" : "  ISOA";
    if (paused_)
        line2 += "  paused";
    if (!missing.empty())
        line2 += "  " + missing;
    const DataDrive* d = data_.drive(files_[size_t(file_)].stem().string(), 'A');
    drawInfo(screen_,
             name_ + "   " + std::to_string(anim_ + 1) + "/" + std::to_string(d->entries().size()) + " (file " +
                 std::to_string(file_ + 1) + "/" + std::to_string(files_.size()) + ")",
             line2);
}

} // namespace reluva::clue
