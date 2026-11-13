// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/pages/play_page.h"

#include "graphics/glide/pipeline.h"
#include "image/image.h"
#include "engines/nfs/host/game_host.h"
#include "engines/nfs/pages/page_draw.h"

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
    case key::KpEnter:
        return "enter";
    case key::Escape:
        return "esc";
    case key::Space:
        return "space";
    case key::Backspace:
        return "backspace";
    case key::Up:
        return "up";
    case key::Down:
        return "down";
    case key::Left:
        return "left";
    case key::Right:
        return "right";
    case key::Home:
        return "home";
    case key::End:
        return "end";
    case key::PageUp:
        return "pageup";
    case key::PageDown:
        return "pagedown";
    case key::Insert:
        return "insert";
    case key::Delete:
        return "delete";
    case key::LShift:
    case key::RShift:
        return "shift";
    case key::LCtrl:
    case key::RCtrl:
        return "ctrl";
    case key::LAlt:
    case key::RAlt:
        return "alt";
    default:
        return "";
    }
}

// The game plays its movies (320x240) in its 640x480 mode on every other
// line, leaving the lines between black: as it looked then. Those lines
// filled with the one above, if every odd line is black and some even one
// isn't; false for any other frame (a menu's picture, the game's 3D).
bool fillMovieLines(Surface &frame) {
    if (frame.h < 2 || frame.w <= 0)
        return false;
    bool picture = false;
    for (int y = 0; y < frame.h; ++y) {
        const uint8_t *row = frame.row(y);
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

PlayPage::PlayPage(GameFolders folders, std::filesystem::path saveFolder, audio::Mixer *mixer, int renderScale,
                   bool smoothTextures, bool offerJoystick, bool forceFeedback, std::string wheelPedals, bool network,
                   bool fillMovies, std::string serial, std::string modem, bool glide, bool direct3d,
                   bool glideSoftware, std::string glideDll, std::string glideBoard)
    : _folders(std::move(folders)), _saveFolder(std::move(saveFolder)), _mixer(mixer), _offerJoystick(offerJoystick),
      _forceFeedback(forceFeedback), _smoothTextures(smoothTextures), _network(network), _serial(std::move(serial)),
      _modem(std::move(modem)), _fillMovies(fillMovies), _wheelPedals(std::move(wheelPedals)),
      _renderScale(std::clamp(renderScale, 1, 4)), _glide(glide), _glideSoftware(glideSoftware),
      _glideDll(std::move(glideDll)), _glideBoard(std::move(glideBoard)), _direct3d(direct3d && !glide) {
    _gpuDraw.scale = _renderScale;
    _gpuDraw.nearest = !smoothTextures;
    // A debugging aid: the GPU's frames written beside the software
    // rasterizer's of the same page flip, at the game's size, every so often.
    if (const char *folder = std::getenv("RELUVA_NFS_COMPARE")) {
        _compareFolder = folder;
        _renderScale = 1;
        _gpuDraw.scale = 1;
    }
}

PlayPage::~PlayPage() {
    stopForces();
    if (_mixer && _sound != audio::kNoSound)
        _mixer->stop(_sound); // the stream reads the host's memory
    if (_host)
        _host->process().requestExit();
    if (_thread.joinable())
        _thread.join();
}

void PlayPage::say(const std::string &line) {
    std::lock_guard<std::mutex> lock(_mutex);
    _lines.push_back(line);
    while (_lines.size() > 20)
        _lines.pop_front();
}

void PlayPage::activate(System &sys) {
    _sys = &sys;
    // The GPU if there is one: asked for each time the page is shown (the
    // engine puts the framebuffer back when it's left). Whether the game
    // draws for it is decided once, when it starts.
    const bool starting = !_host && _result.empty(); // started once; a failed start stays reported
    if ((starting || _gpu) && !_glide) {
        gfx::Renderer3D *renderer = nullptr;
        if (!sys.init3D(640 * _renderScale, 480 * _renderScale, renderer) && renderer) {
            _gpu = _gpu || starting;
            _gpuDraw.setRenderer(renderer);
        } else {
            _gpuDraw.setRenderer(nullptr);
        }
    }
    if (starting)
        start();
    // The game draws its own pointer, moved by the mouse's moves: the
    // window holds the mouse while the page is shown.
    sys.setRelativeMouse(true);
}

void PlayPage::deactivate() {
    _gpuDraw.setRenderer(nullptr);
    stopForces();
    if (_sys)
        _sys->setRelativeMouse(false);
    _mouseLeftX = _mouseLeftY = 0;
}

void PlayPage::stopForces() {
    if (_sys && _forceShown)
        _sys->setGamepadForce({});
    _force = {};
    _forceShown = false;
}

void PlayPage::passForces() {
    if (!_padOffered || !_pad.forceFeedback || !_forceFeedback || !_sys || !_host)
        return;
    if (!_running) {
        stopForces();
        return;
    }
    const GamepadForce f = _host->gamepadForce();
    if (_forceShown && f == _force)
        return;
    _sys->setGamepadForce(f);
    _force = f;
    _forceShown = true;
}

void PlayPage::start() {
    if (_host)
        return;
    GameHost::Options options;
    // The game plays its movies itself (its Madcow player runs in the
    // interpreter like the rest).
    options.commandLine = "nfs3.exe";
    options.saveFolder = _saveFolder.empty() ? _folders.install / "reluva-save" : _saveFolder / "game";
    options.nativeThrash = true;
    options.pointSampled = !_smoothTextures;
    options.positionalUpdates = _positionalUpdates;
    // Told it has a Voodoo 2, the game draws its sky and fog (it fogs only
    // for 3dfx cards); the drawing is still Reluva's. Or, told of d3da.dll,
    // it takes its Direct3D path, drawn as d3da's device draws.
    options.presentedRenderer = _direct3d ? "d3da.dll" : "voodoo2a.dll";
    options.gpuThrash = _gpu;
    // Or the game's own Glide renderer on Reluva's Glide: on the GPU if
    // there is one, unless the software reference is asked for.
    options.glide = _glide;
    options.glideGpu = _glideSoftware ? 0 : 1;
    options.glideDll = _glideDll;
    options.glideBoard = _glideBoard;
    // A Voodoo Rush shares its memory with the desktop its window is on:
    // this machine's, unless reluva.ini's glideboard= names one
    // (glide::setDesktop; one the Rush couldn't have had is made one it
    // could, and said so).
    const std::string boardName = _glideBoard.substr(0, _glideBoard.find(','));
    int desktopW = 0;
    int desktopH = 0;
    int desktopBits = 0;
    if (_glide && (boardName == "rush" || boardName == "sst96") && _glideBoard.find(",desktop=") == std::string::npos &&
        _sys && _sys->desktopMode(desktopW, desktopH, desktopBits)) {
        const std::string spec = _glideBoard + ",desktop=" + std::to_string(desktopW) + "x" + std::to_string(desktopH) +
                                 "x" + std::to_string(desktopBits);
        glide::Board board;
        bool clamped = false;
        const bool taken = glide::boardByName(spec, board, &clamped); // false for a depth it can't take
        if (taken)
            options.glideBoard = spec;
        if (taken && clamped)
            say("The Voodoo Rush's desktop: " + std::to_string(desktopW) + "x" + std::to_string(desktopH) + " at " +
                std::to_string(desktopBits) + " bits here, " + std::to_string(board.desktopWidth) + "x" +
                std::to_string(board.desktopHeight) + " at " + std::to_string(board.desktopBits) +
                " on the Rush (one it could have had)");
    }
    options.gpuAndSoftware = _gpu && !_compareFolder.empty();
    if (_network && _sys) {
        options.sockets = _sys->sockets();
        options.serial = _serial;
        options.modem = _modem;
    }
    // A controller connected now is offered to the game as a joystick (it
    // looks for them once, as it starts).
    // A wheel as one (its pedals together on Y), and its forces if it can
    // give them (force feedback, or a pad's rumble).
    if (_offerJoystick && _sys) {
        int accelerator = -1;
        int brake = -1;
        if (std::sscanf(_wheelPedals.c_str(), "%d,%d", &accelerator, &brake) == 2)
            _sys->setWheelPedals(accelerator, brake);
        if (_sys->readGamepad(_pad)) {
            options.joystick = _pad.name.empty() ? (_pad.wheel ? "Wheel" : "Gamepad") : _pad.name;
            options.joystickWheel = _pad.wheel;
            options.forceFeedback = _pad.forceFeedback && _forceFeedback;
            _padOffered = true;
        }
    }
    _host = std::make_unique<GameHost>(_folders, options);
    _host->log = [this](const std::string &line) { say(line); };
    _host->onFrame = [this](const Surface &frame) {
        std::lock_guard<std::mutex> lock(_mutex);
        _frame = frame;
        if (_fillMovies)
            fillMovieLines(_frame);
        if (!_gpu) {
            ++_statGameFrames;
            _statInstructions = _host->cpu().executed();
            _statOwn = _host->thrash().lastOwn;
            _statTimer = _host->thrash().lastTimer;
        }
        _newFrame = true;
        _thrashLatest = false;
        if (_gpu && _compareFolder.empty()) { // DirectDraw's picture (in GPU mode, the only frames here)
            _primary = _frame;
            _primaryShown = true;
        }
    };
    _host->onThrashFrame = [this](ThrashFrame &&frame) {
        std::lock_guard<std::mutex> lock(_mutex);
        // A flip copies its rectangle to the screen, leaving the rest: what
        // DirectDraw drew stays outside it until a flip covers everything.
        if (_primaryShown) {
            const int x0 = std::max(frame.flip[0], 0);
            const int y0 = std::max(frame.flip[1], 0);
            const int x1 = std::min(frame.flip[2], _primary.w);
            const int y1 = std::min(frame.flip[3], _primary.h);
            if (x0 <= 0 && y0 <= 0 && x1 >= _primary.w && y1 >= _primary.h)
                _primaryShown = false;
            else
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                        _primary.row(y)[x * 4 + 3] = 0;
        }
        ++_statGameFrames;
        _statInstructions = _host->cpu().executed();
        _statOwn = _host->thrash().lastOwn;
        _statTimer = _host->thrash().lastTimer;
        Pending p{std::move(frame), nullptr};
        if (!_compareFolder.empty())
            p.software = std::make_shared<const Surface>(_frame); // drawn at the same flip, just before
        _pending.push_back(std::move(p));
        // Too much waiting (the page not shown, say): the oldest dropped,
        // its texture changes kept for the frame after. Counted in vertices,
        // not frames: a loading screen sends dozens of small ones that each
        // add to the last, and those are all wanted.
        size_t queued = 0;
        for (const Pending &q : _pending)
            queued += q.frame.vertices.size();
        while (_pending.size() > 1 && queued > kMaxQueuedVertices) {
            queued -= _pending.front().frame.vertices.size();
            Pending &next = _pending[1];
            if (!next.frame.resetTextures) {
                next.frame.resetTextures = _pending.front().frame.resetTextures;
                auto &older = _pending.front().frame.textures;
                next.frame.textures.insert(next.frame.textures.begin(), std::make_move_iterator(older.begin()),
                                           std::make_move_iterator(older.end()));
            }
            _pending.pop_front();
            ++_statDropped;
        }
        _thrashLatest = true;
    };
    if (Error err = _host->load()) {
        _result = "Couldn't start the game: " + err.message;
        _host.reset();
        return;
    }
    if (_mixer)
        _sound = _mixer->playOutput(_host->soundStream(), audio::SoundType::Effects);
    _running = true;
    _thread = std::thread([this] {
        uint32_t exitCode = 0;
        const Error err = _host->run(exitCode);
        std::lock_guard<std::mutex> lock(_mutex);
        _result =
            err ? "The game stopped: " + err.message : "The game ended (exit code " + std::to_string(exitCode) + ")";
        _exited = !err; // the player left it: back to the launcher
        _running = false;
    });
}

void PlayPage::handleEvent(const Event &ev) {
    if (!_host || !_running)
        return;
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: {
        if (ev.repeat)
            return;
        const std::string name = keyName(ev.key);
        if (!name.empty())
            _host->queueKey(name, ev.type == EventType::KeyDown);
        break;
    }
    case EventType::MouseMove: {
        // DirectInput's mouse moves by how far it went, as the mouse does
        // here: the window holds it (System::setRelativeMouse), the game
        // drawing its own pointer. Fractions are kept for the next move.
        _mouseLeftX += ev.moveX;
        _mouseLeftY += ev.moveY;
        const int dx = int(_mouseLeftX);
        const int dy = int(_mouseLeftY);
        _mouseLeftX -= float(dx);
        _mouseLeftY -= float(dy);
        if (dx || dy)
            _host->queueMouseMove(dx, dy);
        break;
    }
    case EventType::MouseDown:
    case EventType::MouseUp:
        if (ev.button == MouseButton::Left)
            _host->queueMouseButton(ev.type == EventType::MouseDown);
        break;
    default:
        break;
    }
}

void PlayPage::pollGamepad() {
    if (!_padOffered || !_sys || !_host || !_running)
        return;
    GamepadState state;
    _sys->readGamepad(state); // disconnected: all at rest
    if (state.buttons == _pad.buttons && std::equal(state.axes, state.axes + 6, _pad.axes))
        return;
    _pad = state;
    _host->setJoystick(state);
}

bool PlayPage::tick(uint64_t nowMs) {
    pollGamepad();
    reportSpeed(nowMs);
    std::lock_guard<std::mutex> lock(_mutex);
    // Until the first frame, the log shows how start-up is going.
    return _newFrame || _frame.pixels.empty() || !_running;
}

void PlayPage::draw(Surface &screen) {
    std::lock_guard<std::mutex> lock(_mutex);
    _newFrame = false;
    if (!_frame.pixels.empty() && _running) {
        // The game's frames are the screen's size (640x480): copied as they are.
        for (int y = 0; y < std::min(screen.h, _frame.h); ++y)
            std::memcpy(screen.row(y), _frame.row(y), size_t(std::min(screen.w, _frame.w)) * 4);
        return;
    }
    drawStatus(screen);
}

void PlayPage::drawStatus(Surface &screen) {
    clear(screen);
    text(screen, kMargin, 8, "Need for Speed III", kTitleColor);
    text(screen, kMargin, screen.h - font::kLineHeight - 4,
         "Keys and mouse go to the game   Tab: next page   Ctrl+F5: launcher", kLabelColor);
    int y = 8 + font::kLineHeight * 2;
    if (!_result.empty()) {
        text(screen, kMargin, y, _result, kWarnColor);
        y += font::kLineHeight * 2;
    } else {
        text(screen, kMargin, y, "Starting nfs3.exe...", kTextColor);
        y += font::kLineHeight * 2;
    }
    for (const std::string &line : _lines) {
        text(screen, kMargin, y, line, kLabelColor);
        y += font::kLineHeight;
    }
}

void PlayPage::reportSpeed(uint64_t nowMs) {
    // Every 5 seconds, on the console: how fast the game runs and how much
    // of it is shown, for telling a slow interpreter from frames lost on
    // the way to the screen.
    if (!_statFrom) {
        _statFrom = nowMs;
        return;
    }
    if (nowMs - _statFrom < 5000 || !_running)
        return;
    const double seconds = double(nowMs - _statFrom) / 1000.0;
    const uint64_t instructions = _statInstructions;
    const uint64_t own = _statOwn;
    const uint64_t timer = _statTimer;
    const uint64_t frames = _statGameFrames;
    const uint64_t run = instructions - _statLastInstructions;
    // Where the instructions went: the game's main thread, its timer
    // thread, and the rest (its other threads).
    const uint64_t ownRun = own - std::min(own, _statLastOwn);
    const uint64_t timerRun = timer - std::min(timer, _statLastTimer);
    auto share = [run](uint64_t n) { return run ? 100.0 * double(n) / double(run) : 0.0; };
    std::printf("Play: the game made %.1f frames a second at %.1f million instructions a second (%.2f million a "
                "frame: main thread %.0f%%, timer %.0f%%, others %.0f%%); %s drew %.1f a second (%.1f not needed, "
                "%.1f dropped), shown %.1f times a second\n",
                double(frames) / seconds, double(run) / seconds / 1e6,
                frames ? double(run) / double(frames) / 1e6 : 0.0, share(ownRun), share(timerRun),
                share(run - std::min(run, ownRun + timerRun)), _gpu ? "the GPU" : "software",
                double(_statDrawn) / seconds, double(_statSkipped) / seconds, double(_statDropped) / seconds,
                double(_statShown) / seconds);
    std::fflush(stdout);
    _statFrom = nowMs;
    _statLastInstructions = instructions;
    _statLastOwn = own;
    _statLastTimer = timer;
    _statGameFrames = _statDropped = 0;
    _statDrawn = _statSkipped = _statShown = 0;
}

void PlayPage::update(uint64_t nowMs) {
    pollGamepad();
    passForces();
    reportSpeed(nowMs);
    std::deque<Pending> frames;
    Surface overlay;
    bool thrash = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        frames.swap(_pending);
        thrash = _thrashLatest && _running;
        if (thrash && _primaryShown)
            overlay = _primary;
        if (!thrash) {
            // A DirectDraw frame (a movie's) or, before the first and after
            // the game ends, how it's going: shown over the last frame.
            if (!_frame.pixels.empty() && _running) {
                overlay = _frame;
            } else {
                overlay.create(640, 480, PixelFormat::kRGBA32);
                drawStatus(overlay);
            }
        }
        _newFrame = false;
    }
    const Surface *over = overlay.pixels.empty() ? nullptr : &overlay;
    // Nothing new: what was drawn shown again (drawing a frame twice would
    // blend its translucent parts twice).
    if (frames.empty() || !thrash) {
        for (const Pending &p : frames)
            _gpuDraw.updateTextures(p.frame);
        _gpuDraw.draw(ThrashFrame{}, over);
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
        _gpuDraw.updateTextures(frames[i].frame);
    frames.erase(frames.begin(), frames.begin() + std::ptrdiff_t(from));
    _statSkipped += from;
    _statDrawn += frames.size();
    ++_statShown;
    std::vector<const ThrashFrame *> list;
    bool compare = false;
    for (const Pending &p : frames) {
        list.push_back(&p.frame);
        compare = compare || (!_compareFolder.empty() && p.software && ++_newFrames % 60 == 0);
    }
    if (compare)
        _gpuDraw.captureNext();
    _gpuDraw.draw(list, over);
    std::vector<uint8_t> rgba;
    const Pending &last = frames.back();
    if (compare && last.software && _gpuDraw.capture(rgba)) {
        std::error_code ec;
        std::filesystem::create_directories(_compareFolder, ec);
        auto write = [](const std::filesystem::path &path, int w, int h, const std::vector<uint8_t> &pixels) {
            image::Image img;
            img.w = w;
            img.h = h;
            img.rgba = pixels;
            const std::vector<uint8_t> tga = image::encodeTga(img);
            if (std::FILE *f = std::fopen(path.string().c_str(), "wb")) {
                std::fwrite(tga.data(), 1, tga.size(), f);
                std::fclose(f);
            }
        };
        char name[32];
        std::snprintf(name, sizeof name, "%04d", _compared++);
        write(_compareFolder / (std::string("gpu") + name + ".tga"), last.frame.width, last.frame.height, rgba);
        write(_compareFolder / (std::string("cpu") + name + ".tga"), last.software->w, last.software->h,
              last.software->pixels);
    }
}

} // namespace reluva::nfs
