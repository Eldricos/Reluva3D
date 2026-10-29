// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/viewers.h"

#include "common/audio/decode.h"
#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;
constexpr int kRowH = 12;

} // namespace

SoundViewer::SoundViewer(System& sys, Surface& screen, const GameData& data, audio::Mixer* mixer)
    : sys_(sys), screen_(screen), data_(data), mixer_(mixer) {
    files_ = data_.withExtension(".wav");
    for (auto& p : data_.withExtension(".mp3")) // ripped music may be MP3
        files_.push_back(p);
}

void SoundViewer::activate() {
    info_.clear();
    draw();
}

void SoundViewer::deactivate() { stop(); }

void SoundViewer::stop() {
    if (mixer_ && playing_ != audio::kNoSound)
        mixer_->stop(playing_);
    playing_ = audio::kNoSound;
}

void SoundViewer::play() {
    stop();
    if (files_.empty())
        return;
    const auto& path = files_[size_t(current_)];
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto s = std::make_shared<audio::Sound>();
    if (Error err = audio::decodeSound(bytes, *s)) {
        info_ = err.message;
        return;
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s, %d Hz, %s, %.2f s", audio::formatName(s->format), s->rate,
                  s->channels == 2 ? "stereo" : "mono", s->rate ? double(s->frames()) / s->rate : 0.0);
    info_ = buf;
    sound_ = s;
    if (!mixer_) {
        info_ += "  (no audio output)";
        return;
    }
    // Music is the CD's tracks; the rest is speech (the suspects' lines)
    // and effects, which share the effects volume here.
    const std::string name = fs::toLower(path.filename().string());
    const auto type = name.rfind("track", 0) == 0 ? audio::SoundType::Music : audio::SoundType::Effects;
    playing_ = mixer_->play(sound_, type);
}

void SoundViewer::handleKey(uint32_t key) {
    if (files_.empty())
        return;
    const int n = int(files_.size());
    const int before = current_;
    switch (key) {
    case key::Right: current_ = std::min(n - 1, current_ + 1); break;
    case key::Left: current_ = std::max(0, current_ - 1); break;
    case key::PageDown: current_ = std::min(n - 1, current_ + 10); break;
    case key::PageUp: current_ = std::max(0, current_ - 10); break;
    case key::Home: current_ = 0; break;
    case key::End: current_ = n - 1; break;
    case key::Space:
    case key::Return:
        if (mixer_ && playing_ != audio::kNoSound && mixer_->isPlaying(playing_))
            stop();
        else
            play();
        break;
    case 'a': autoPlay_ = !autoPlay_; break;
    default: return;
    }
    if (current_ != before) {
        stop();
        info_.clear();
        if (autoPlay_)
            play();
    }
    draw();
}

void SoundViewer::update(uint64_t nowMs) {
    // Redraw now and then while playing, for the position.
    if (playing_ != audio::kNoSound && nowMs - lastDraw_ >= 100)
        draw();
}

void SoundViewer::draw() {
    lastDraw_ = sys_.getMillis();
    fillRect(screen_, 0, 0, kScreenW, kScreenH, {24, 24, 32});
    if (files_.empty()) {
        drawInfo(screen_, "No sounds found");
        return;
    }
    const int rows = (kScreenH - kInfoH - 8) / kRowH;
    const int first = std::clamp(current_ - rows / 2, 0, std::max(0, int(files_.size()) - rows));
    for (int i = 0; i < rows && first + i < int(files_.size()); ++i) {
        const int f = first + i;
        const std::filesystem::path& p = files_[size_t(f)];
        const std::string where = p.parent_path().filename().string();
        if (f == current_)
            fillRect(screen_, 0, 4 + i * kRowH - 1, kScreenW, kRowH, {60, 60, 100});
        font::drawText(screen_, 8, 4 + i * kRowH, p.filename().string(), {230, 230, 230});
        font::drawText(screen_, 400, 4 + i * kRowH, where, {150, 150, 170});
    }
    std::string state = info_;
    const bool on = mixer_ && playing_ != audio::kNoSound && mixer_->isPlaying(playing_);
    if (on && sound_ && sound_->rate) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "  playing %.1f s", mixer_->position(playing_) / sound_->rate);
        state += buf;
    }
    if (autoPlay_)
        state += "  [autoplay]";
    drawInfo(screen_,
             files_[size_t(current_)].filename().string() + "   " + std::to_string(current_ + 1) + "/" +
                 std::to_string(files_.size()),
             state);
    sys_.setWindowTitle("Clue sounds: " + files_[size_t(current_)].filename().string());
}

} // namespace reluva::clue
