// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/play_page.h"

#include "common/image.h"
#include "engines/nfs/host/game_host.h"
#include "engines/nfs/page_draw.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace reluva::nfs {

using namespace page;

namespace {

// THRASH frames waiting to be drawn, at most, in vertices: two or three
// of a race's. More are dropped, oldest first: drawing them all would take
// longer each time, and most frames draw the whole screen anyway.
constexpr size_t kMaxQueuedVertices = 150000;
// A frame with this many vertices is taken to draw the whole screen.
constexpr size_t kWholeFrameVertices = 1000;

// Reluva's keys as GameHost names them; empty for those the game isn't
// given.
std::string keyName(uint32_t k) {
    if (k >= 'a' && k <= 'z')
        return std::string(1, char(k));
    if ((k >= '0' && k <= '9') || k == '.' || k == ',' || k == '-' || k == '/' || k == ';')
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

// The game plays its movies (320x240) in its 640x480 mode on every other
// line, leaving the lines between black: as it looked then. Those lines
// filled with the one above, if every odd line is black and some even one
// isn't; false for any other frame (a menu's picture, the game's 3D).
bool fillMovieLines(Surface& frame) {
    if (frame.h < 2 || frame.w <= 0)
        return false;
    bool picture = false;
    for (int y = 0; y < frame.h; ++y) {
        const uint8_t* row = frame.row(y);
        bool black = true;
        for (int x = 0; x < frame.w && black; ++x)
            black = !(row[4 * x] | row[4 * x + 1] | row[4 * x + 2]);
        if (y % 2 && !black)
            return false;
        picture = picture || (y % 2 == 0 && !black);
    }
    if (!picture)
        return false;
    for (int y = 1; y < frame.h; y += 2)
        std::memcpy(frame.row(y), frame.row(y - 1), size_t(frame.w) * 4);
    return true;
}

} // namespace

PlayPage::PlayPage(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer* mixer, int renderScale,
                   bool smoothTextures, bool offerJoystick, bool forceFeedback, std::string wheelPedals,
                   bool network, bool fillMovies, std::string serial, std::string modem)
    : folders_(std::move(folders)), saveFolder_(std::move(saveFolder)), mixer_(mixer), offerJoystick_(offerJoystick),
      forceFeedback_(forceFeedback), network_(network), serial_(std::move(serial)), modem_(std::move(modem)),
      fillMovies_(fillMovies), wheelPedals_(std::move(wheelPedals)),
      renderScale_(std::clamp(renderScale, 1, 4)) {
    gpuDraw_.scale = renderScale_;
    gpuDraw_.nearest = !smoothTextures;
    // A debugging aid: the GPU's frames written beside the software
    // rasterizer's of the same page flip, at the game's size, every so often.
    if (const char* folder = std::getenv("RELUVA_NFS_COMPARE")) {
        compareFolder_ = folder;
        renderScale_ = 1;
        gpuDraw_.scale = 1;
        gpuDraw_.nearest = true;
    }
}

PlayPage::~PlayPage() {
    stopForces();
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
    sys_ = &sys;
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
    // The game draws its own pointer, moved by the mouse's moves: the
    // window holds the mouse while the page is shown.
    sys.setRelativeMouse(true);
}

void PlayPage::deactivate() {
    gpuDraw_.setRenderer(nullptr);
    stopForces();
    if (sys_)
        sys_->setRelativeMouse(false);
    mouseLeftX_ = mouseLeftY_ = 0;
}

void PlayPage::stopForces() {
    if (sys_ && forceShown_)
        sys_->setGamepadForce({});
    force_ = {};
    forceShown_ = false;
}

void PlayPage::passForces() {
    if (!padOffered_ || !pad_.forceFeedback || !forceFeedback_ || !sys_ || !host_)
        return;
    if (!running_) {
        stopForces();
        return;
    }
    const GamepadForce f = host_->gamepadForce();
    if (forceShown_ && f == force_)
        return;
    sys_->setGamepadForce(f);
    force_ = f;
    forceShown_ = true;
}

void PlayPage::start() {
    if (host_)
        return;
    GameHost::Options options;
    // The game plays its movies itself (its Madcow player runs in the
    // interpreter like the rest).
    options.commandLine = "nfs3.exe";
    options.saveFolder = saveFolder_.empty() ? folders_.install / "reluva-save" : saveFolder_ / "game";
    options.nativeThrash = true;
    // Told it has a Voodoo 2, the game draws its sky and fog (it fogs only
    // for 3dfx cards); the drawing is still Reluva's.
    options.presentedRenderer = "voodoo2a.dll";
    options.gpuThrash = gpu_;
    options.gpuAndSoftware = gpu_ && !compareFolder_.empty();
    if (network_ && sys_) {
        options.sockets = sys_->sockets();
        options.serial = serial_;
        options.modem = modem_;
    }
    // A controller connected now is offered to the game as a joystick (it
    // looks for them once, as it starts).
    // A wheel as one (its pedals together on Y), and its forces if it can
    // give them (force feedback, or a pad's rumble).
    if (offerJoystick_ && sys_) {
        int accelerator = -1, brake = -1;
        if (std::sscanf(wheelPedals_.c_str(), "%d,%d", &accelerator, &brake) == 2)
            sys_->setWheelPedals(accelerator, brake);
        if (sys_->readGamepad(pad_)) {
            options.joystick = pad_.name.empty() ? (pad_.wheel ? "Wheel" : "Gamepad") : pad_.name;
            options.joystickWheel = pad_.wheel;
            options.forceFeedback = pad_.forceFeedback && forceFeedback_;
            padOffered_ = true;
        }
    }
    host_ = std::make_unique<GameHost>(folders_, options);
    host_->log = [this](const std::string& line) { say(line); };
    host_->onFrame = [this](const Surface& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_ = frame;
        if (fillMovies_)
            fillMovieLines(frame_);
        if (!gpu_) {
            ++statGameFrames_;
            statInstructions_ = host_->cpu().executed();
            statOwn_ = host_->thrash().lastOwn;
            statTimer_ = host_->thrash().lastTimer;
        }
        newFrame_ = true;
        thrashLatest_ = false;
        if (gpu_ && compareFolder_.empty()) { // DirectDraw's picture (in GPU mode, the only frames here)
            primary_ = frame_;
            primaryShown_ = true;
        }
    };
    host_->onThrashFrame = [this](ThrashFrame&& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        // A flip copies its rectangle to the screen, leaving the rest: what
        // DirectDraw drew stays outside it until a flip covers everything.
        if (primaryShown_) {
            const int x0 = std::max(frame.flip[0], 0), y0 = std::max(frame.flip[1], 0);
            const int x1 = std::min(frame.flip[2], primary_.w), y1 = std::min(frame.flip[3], primary_.h);
            if (x0 <= 0 && y0 <= 0 && x1 >= primary_.w && y1 >= primary_.h)
                primaryShown_ = false;
            else
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                        primary_.row(y)[x * 4 + 3] = 0;
        }
        ++statGameFrames_;
        statInstructions_ = host_->cpu().executed();
        statOwn_ = host_->thrash().lastOwn;
        statTimer_ = host_->thrash().lastTimer;
        Pending p{std::move(frame), nullptr};
        if (!compareFolder_.empty())
            p.software = std::make_shared<const Surface>(frame_); // drawn at the same flip, just before
        pending_.push_back(std::move(p));
        // Too much waiting (the page not shown, say): the oldest dropped,
        // its texture changes kept for the frame after. Counted in vertices,
        // not frames: a loading screen sends dozens of small ones that each
        // add to the last, and those are all wanted.
        size_t queued = 0;
        for (const Pending& q : pending_)
            queued += q.frame.vertices.size();
        while (pending_.size() > 1 && queued > kMaxQueuedVertices) {
            queued -= pending_.front().frame.vertices.size();
            Pending& next = pending_[1];
            if (!next.frame.resetTextures) {
                next.frame.resetTextures = pending_.front().frame.resetTextures;
                auto& older = pending_.front().frame.textures;
                next.frame.textures.insert(next.frame.textures.begin(), std::make_move_iterator(older.begin()),
                                           std::make_move_iterator(older.end()));
            }
            pending_.pop_front();
            ++statDropped_;
        }
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
        exited_ = !err; // the player left it: back to the launcher
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
        // DirectInput's mouse moves by how far it went, as the mouse does
        // here: the window holds it (System::setRelativeMouse), the game
        // drawing its own pointer. Fractions are kept for the next move.
        mouseLeftX_ += ev.moveX;
        mouseLeftY_ += ev.moveY;
        const int dx = int(mouseLeftX_), dy = int(mouseLeftY_);
        mouseLeftX_ -= float(dx);
        mouseLeftY_ -= float(dy);
        if (dx || dy)
            host_->queueMouseMove(dx, dy);
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

void PlayPage::pollGamepad() {
    if (!padOffered_ || !sys_ || !host_ || !running_)
        return;
    GamepadState state;
    sys_->readGamepad(state); // disconnected: all at rest
    if (state.buttons == pad_.buttons && std::equal(state.axes, state.axes + 6, pad_.axes))
        return;
    pad_ = state;
    host_->setJoystick(state);
}

bool PlayPage::tick(uint64_t nowMs) {
    pollGamepad();
    reportSpeed(nowMs);
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

void PlayPage::reportSpeed(uint64_t nowMs) {
    // Every 5 seconds, on the console: how fast the game runs and how much
    // of it is shown, for telling a slow interpreter from frames lost on
    // the way to the screen.
    if (!statFrom_) {
        statFrom_ = nowMs;
        return;
    }
    if (nowMs - statFrom_ < 5000 || !running_)
        return;
    const double seconds = double(nowMs - statFrom_) / 1000.0;
    const uint64_t instructions = statInstructions_, own = statOwn_, timer = statTimer_;
    const uint64_t frames = statGameFrames_, run = instructions - statLastInstructions_;
    // Where the instructions went: the game's main thread, its timer
    // thread, and the rest (its other threads).
    const uint64_t ownRun = own - std::min(own, statLastOwn_), timerRun = timer - std::min(timer, statLastTimer_);
    auto share = [run](uint64_t n) { return run ? 100.0 * double(n) / double(run) : 0.0; };
    std::printf("Play: the game made %.1f frames a second at %.1f million instructions a second (%.2f million a "
                "frame: main thread %.0f%%, timer %.0f%%, others %.0f%%); %s drew %.1f a second (%.1f not needed, "
                "%.1f dropped), shown %.1f times a second\n",
                double(frames) / seconds, double(run) / seconds / 1e6, frames ? double(run) / double(frames) / 1e6 : 0.0,
                share(ownRun), share(timerRun), share(run - std::min(run, ownRun + timerRun)),
                gpu_ ? "the GPU" : "software", double(statDrawn_) / seconds, double(statSkipped_) / seconds,
                double(statDropped_) / seconds, double(statShown_) / seconds);
    std::fflush(stdout);
    statFrom_ = nowMs;
    statLastInstructions_ = instructions;
    statLastOwn_ = own;
    statLastTimer_ = timer;
    statGameFrames_ = statDropped_ = 0;
    statDrawn_ = statSkipped_ = statShown_ = 0;
}

void PlayPage::update(uint64_t nowMs) {
    pollGamepad();
    passForces();
    reportSpeed(nowMs);
    std::deque<Pending> frames;
    Surface overlay;
    bool thrash = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frames.swap(pending_);
        thrash = thrashLatest_ && running_;
        if (thrash && primaryShown_)
            overlay = primary_;
        if (!thrash) {
            // A DirectDraw frame (a movie's) or, before the first and after
            // the game ends, how it's going: shown over the last frame.
            if (!frame_.pixels.empty() && running_) {
                overlay = frame_;
            } else {
                overlay.create(640, 480, PixelFormat::RGBA32);
                drawStatus(overlay);
            }
        }
        newFrame_ = false;
    }
    const Surface* over = overlay.pixels.empty() ? nullptr : &overlay;
    // Nothing new: what was drawn shown again (drawing a frame twice would
    // blend its translucent parts twice).
    if (frames.empty() || !thrash) {
        for (const Pending& p : frames)
            gpuDraw_.updateTextures(p.frame);
        gpuDraw_.draw(ThrashFrame{}, over);
        return;
    }
    // A frame of many vertices draws the whole screen (a menu's, a race's):
    // those before the last of them needn't be drawn, only their textures
    // taken. The small ones after it may add to it (a loading screen's
    // progress bar), so they are.
    size_t from = 0;
    for (size_t i = 0; i < frames.size(); ++i)
        if (frames[i].frame.vertices.size() >= kWholeFrameVertices)
            from = i;
    for (size_t i = 0; i < from; ++i)
        gpuDraw_.updateTextures(frames[i].frame);
    frames.erase(frames.begin(), frames.begin() + std::ptrdiff_t(from));
    statSkipped_ += from;
    statDrawn_ += frames.size();
    ++statShown_;
    std::vector<const ThrashFrame*> list;
    bool compare = false;
    for (const Pending& p : frames) {
        list.push_back(&p.frame);
        compare = compare || (!compareFolder_.empty() && p.software && ++newFrames_ % 60 == 0);
    }
    if (compare)
        gpuDraw_.captureNext();
    gpuDraw_.draw(list, over);
    std::vector<uint8_t> rgba;
    const Pending& last = frames.back();
    if (compare && last.software && gpuDraw_.capture(rgba)) {
        std::error_code ec;
        std::filesystem::create_directories(compareFolder_, ec);
        auto write = [](const std::filesystem::path& path, int w, int h, const std::vector<uint8_t>& pixels) {
            image::Image img;
            img.w = w;
            img.h = h;
            img.rgba = pixels;
            const std::vector<uint8_t> tga = image::encodeTga(img);
            if (std::FILE* f = std::fopen(path.string().c_str(), "wb")) {
                std::fwrite(tga.data(), 1, tga.size(), f);
                std::fclose(f);
            }
        };
        char name[32];
        std::snprintf(name, sizeof name, "%04d", compared_++);
        write(compareFolder_ / (std::string("gpu") + name + ".tga"), last.frame.width, last.frame.height, rgba);
        write(compareFolder_ / (std::string("cpu") + name + ".tga"), last.software->w, last.software->h,
              last.software->pixels);
    }
}

} // namespace reluva::nfs
