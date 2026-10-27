// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/play_page.h"

#include "engines/nfs/host/game_host.h"
#include "engines/nfs/page_draw.h"

#include <algorithm>
#include <cstring>

namespace reluva::nfs {

using namespace page;

namespace {

// Reluva's keys as GameHost names them; empty for those the game isn't
// given.
std::string keyName(uint32_t k) {
    if (k >= 'a' && k <= 'z')
        return std::string(1, char(k));
    if (k >= '0' && k <= '9')
        return std::string(1, char(k));
    if (k >= key::F1 && k < key::F1 + 10)
        return "f" + std::to_string(k - key::F1 + 1);
    switch (k) {
    case key::Return:
    case key::KpEnter: return "enter";
    case key::Escape: return "esc";
    case key::Space: return "space";
    case key::Backspace: return "backspace";
    case key::Up: return "up";
    case key::Down: return "down";
    case key::Left: return "left";
    case key::Right: return "right";
    case key::Home: return "home";
    case key::End: return "end";
    case key::PageUp: return "pageup";
    case key::PageDown: return "pagedown";
    case key::Insert: return "insert";
    case key::Delete: return "delete";
    case key::LShift:
    case key::RShift: return "shift";
    case key::LCtrl:
    case key::RCtrl: return "ctrl";
    case key::LAlt:
    case key::RAlt: return "alt";
    default: return "";
    }
}

} // namespace

PlayPage::PlayPage(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer* mixer, int renderScale,
                   bool smoothTextures)
    : folders_(std::move(folders)), saveFolder_(std::move(saveFolder)), mixer_(mixer),
      renderScale_(std::clamp(renderScale, 1, 4)) {
    gpuDraw_.scale = renderScale_;
    gpuDraw_.nearest = !smoothTextures;
}

PlayPage::~PlayPage() {
    if (mixer_ && sound_ != audio::kNoSound)
        mixer_->stop(sound_); // the stream reads the host's memory
    if (host_)
        host_->process().requestExit();
    if (thread_.joinable())
        thread_.join();
}

void PlayPage::say(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.push_back(line);
    while (lines_.size() > 20)
        lines_.pop_front();
}

void PlayPage::activate(System& sys) {
    // The GPU if there is one: asked for each time the page is shown (the
    // engine puts the framebuffer back when it's left). Whether the game
    // draws for it is decided once, when it starts.
    const bool starting = !host_ && result_.empty(); // started once; a failed start stays reported
    if (starting || gpu_) {
        gfx::Renderer3D* renderer = nullptr;
        if (!sys.init3D(640 * renderScale_, 480 * renderScale_, renderer) && renderer) {
            gpu_ = gpu_ || starting;
            gpuDraw_.setRenderer(renderer);
        } else {
            gpuDraw_.setRenderer(nullptr);
        }
    }
    if (starting)
        start();
}

void PlayPage::deactivate() { gpuDraw_.setRenderer(nullptr); }

void PlayPage::start() {
    if (host_)
        return;
    GameHost::Options options;
    // The game plays its movies itself (its Madcow player runs in the
    // interpreter like the rest).
    options.commandLine = "nfs3.exe";
    options.saveFolder = saveFolder_.empty() ? folders_.install / "reluva-save" : saveFolder_ / "game";
    options.nativeThrash = true;
    options.gpuThrash = gpu_;
    host_ = std::make_unique<GameHost>(folders_, options);
    host_->log = [this](const std::string& line) { say(line); };
    host_->onFrame = [this](const Surface& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_ = frame;
        newFrame_ = true;
        thrashLatest_ = false;
    };
    host_->onThrashFrame = [this](ThrashFrame&& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Texture changes add up until drawn; the frame itself replaces the last.
        if (frame.resetTextures)
            textureUpdates_ = ThrashFrame{};
        textureUpdates_.resetTextures = textureUpdates_.resetTextures || frame.resetTextures;
        for (ThrashFrame::Texture& t : frame.textures)
            textureUpdates_.textures.push_back(std::move(t));
        frame.textures.clear();
        thrashFrame_ = std::make_shared<const ThrashFrame>(std::move(frame));
        thrashLatest_ = true;
    };
    if (Error err = host_->load()) {
        result_ = "Couldn't start the game: " + err.message;
        host_.reset();
        return;
    }
    if (mixer_)
        sound_ = mixer_->playStream(host_->soundStream(), audio::SoundType::Effects);
    running_ = true;
    thread_ = std::thread([this] {
        uint32_t exitCode = 0;
        const Error err = host_->run(exitCode);
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = err ? "The game stopped: " + err.message : "The game ended (exit code " + std::to_string(exitCode) + ")";
        running_ = false;
    });
}

void PlayPage::handleEvent(const Event& ev) {
    if (!host_ || !running_)
        return;
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: {
        if (ev.repeat)
            return;
        const std::string name = keyName(ev.key);
        if (!name.empty())
            host_->queueKey(name, ev.type == EventType::KeyDown);
        break;
    }
    case EventType::MouseMove: {
        // DirectInput's mouse moves by how far it went; the window's
        // pointer stands in for it, in the game's pixels (the GPU's frame
        // may be bigger).
        const int scale = gpu_ ? renderScale_ : 1;
        const int x = ev.mouseX / scale, y = ev.mouseY / scale;
        if (mouseKnown_)
            host_->queueMouseMove(x - mouseX_, y - mouseY_);
        mouseX_ = x;
        mouseY_ = y;
        mouseKnown_ = true;
        break;
    }
    case EventType::MouseDown:
    case EventType::MouseUp:
        if (ev.button == MouseButton::Left)
            host_->queueMouseButton(ev.type == EventType::MouseDown);
        break;
    default: break;
    }
}

bool PlayPage::tick(uint64_t) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Until the first frame, the log shows how start-up is going.
    return newFrame_ || frame_.pixels.empty() || !running_;
}

void PlayPage::draw(Surface& screen) {
    std::lock_guard<std::mutex> lock(mutex_);
    newFrame_ = false;
    if (!frame_.pixels.empty() && running_) {
        // The game's frames are the screen's size (640x480): copied as they are.
        for (int y = 0; y < std::min(screen.h, frame_.h); ++y)
            std::memcpy(screen.row(y), frame_.row(y), size_t(std::min(screen.w, frame_.w)) * 4);
        return;
    }
    drawStatus(screen);
}

void PlayPage::drawStatus(Surface& screen) {
    clear(screen);
    text(screen, kMargin, 8, "Need for Speed III", kTitleColor);
    text(screen, kMargin, screen.h - font::kLineHeight - 4, "Keys and mouse go to the game   Tab: next page   Ctrl+F5: launcher",
         kLabelColor);
    int y = 8 + font::kLineHeight * 2;
    if (!result_.empty()) {
        text(screen, kMargin, y, result_, kWarnColor);
        y += font::kLineHeight * 2;
    } else {
        text(screen, kMargin, y, "Starting nfs3.exe...", kTextColor);
        y += font::kLineHeight * 2;
    }
    for (const std::string& line : lines_) {
        text(screen, kMargin, y, line, kLabelColor);
        y += font::kLineHeight;
    }
}

void PlayPage::update(uint64_t) {
    std::shared_ptr<const ThrashFrame> frame;
    ThrashFrame updates;
    Surface overlay;
    bool thrash = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame = thrashFrame_;
        updates = std::move(textureUpdates_);
        textureUpdates_ = ThrashFrame{};
        thrash = thrashLatest_ && frame && running_;
        if (!thrash) {
            // A DirectDraw frame (a movie's) or, before the first and after
            // the game ends, how it's going: shown over an empty frame.
            if (!frame_.pixels.empty() && running_) {
                overlay = frame_;
            } else {
                overlay.create(640, 480, PixelFormat::RGBA32);
                drawStatus(overlay);
            }
        }
        newFrame_ = false;
    }
    gpuDraw_.updateTextures(updates);
    if (thrash)
        gpuDraw_.draw(*frame, nullptr);
    else
        gpuDraw_.draw(ThrashFrame{}, &overlay);
}

} // namespace reluva::nfs
