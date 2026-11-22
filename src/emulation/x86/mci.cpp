// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Movies through MCI and the movie window, from the Windows SDK's
// documentation of MCIWnd (vfw.h's messages) and of MCI's commands
// (docs/common/x86/mci.md).

#include "emulation/x86/mci.h"

#include "audio/mixer.h"
#include "audio/sound.h"
#include "emulation/x86/gdi.h"
#include "video/avi.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <unordered_map>

namespace reluva::x86 {

namespace {

constexpr uint32_t kWmNcCreate = 0x0081;
constexpr uint32_t kWmCreate = 0x0001;
constexpr uint32_t kWmDestroy = 0x0002;
constexpr uint32_t kWmClose = 0x0010;
constexpr uint32_t kWsChild = 0x40000000;
constexpr uint32_t kMmMciNotify = 0x03b9;
// MCI's commands, also sent to a movie window as messages.
constexpr uint32_t kMciOpen = 0x0803;
constexpr uint32_t kMciClose = 0x0804;
constexpr uint32_t kMciPlay = 0x0806;
constexpr uint32_t kMciStop = 0x0808;
constexpr uint32_t kMciPause = 0x0809;
constexpr uint32_t kMciStatus = 0x0814;
// The movie window's own (WM_USER + n).
constexpr uint32_t kMciWndmGetDeviceId = 0x0464;
constexpr uint32_t kMciWndmGetPositionA = 0x0466;
constexpr uint32_t kMciWndmGetLength = 0x0468;
constexpr uint32_t kMciWndmPutDest = 0x048f;
constexpr uint32_t kMciWndmOpenA = 0x0499;
// mciSendCommand's flags and answers.
constexpr uint32_t kMciNotify = 0x1;
constexpr uint32_t kMciFrom = 0x4;
constexpr uint32_t kMciTo = 0x8;
constexpr uint32_t kMciOpenElement = 0x200;
constexpr uint32_t kMciStatusItem = 0x100;
constexpr uint32_t kNotifySuccessful = 1;
constexpr uint32_t kNotifyAborted = 4;
constexpr uint32_t kMciErrInvalidDeviceId = 257;
constexpr uint32_t kMciErrUnsupportedFunction = 274;
constexpr uint32_t kMciErrFileNotFound = 275;
constexpr uint32_t kMciModeStop = 525;
constexpr uint32_t kMciModePlay = 526;

} // namespace

struct MciVideo::State {
    MciVideo &owner;
    Win32Process &process;
    Cpu &cpu;
    struct Movie {
        std::unique_ptr<avi::Decoder> decoder;
        uint32_t window = 0;
        // Where the picture goes in the window, left, top, width, height;
        // the movie's own size until PUT_DEST says otherwise.
        int dest[4] = {0, 0, 0, 0};
        bool playing = false;
        uint64_t startMs = 0;
        int startFrame = 0;
        int endFrame = 0;    // played up to, not including
        uint32_t notify = 0; // the window to tell when it ends, 0 for none
        audio::SoundHandle sound = audio::kNoSound;
        std::shared_ptr<audio::Sound> audioTrack;
        uint64_t playCount = 0; // which play the drawing thread serves
    };
    std::map<uint32_t, Movie> movies; // by device id
    std::map<uint32_t, uint32_t> byWindow;
    uint32_t nextDevice = 1;
    int opened = 0;
    std::unordered_map<uint32_t, uint32_t> nearest; // an 8-bit screen's colours, RGB -> index
    State(MciVideo &o, Win32Process &p) : owner(o), process(p), cpu(p.cpu()) {}

    // Opens an AVI file: its device id, or 0 with `error` set.
    uint32_t open(const std::string &path, uint32_t window, uint32_t &error) {
        const auto host = process.mapPath ? process.mapPath(process.fullPath(path), false) : std::nullopt;
        std::ifstream in(host ? *host : std::filesystem::path(), std::ios::binary);
        if (!host || !in) {
            error = kMciErrFileNotFound;
            return 0;
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto decoder = std::make_unique<avi::Decoder>();
        if (!decoder->load(std::make_unique<MemoryReadStream>(std::move(bytes)))) {
            error = kMciErrFileNotFound;
            return 0;
        }
        const uint32_t id = nextDevice++;
        Movie &m = movies[id];
        m.window = window;
        m.dest[2] = decoder->width();
        m.dest[3] = decoder->height();
        if (decoder->hasAudio() && (decoder->audioBits() == 8 || decoder->audioBits() == 16)) {
            auto sound = std::make_shared<audio::Sound>();
            sound->rate = decoder->audioRate();
            sound->channels = decoder->audioChannels();
            sound->format = 1;
            sound->sourceBits = decoder->audioBits();
            const std::vector<uint8_t> &pcm = decoder->audio();
            if (decoder->audioBits() == 8)
                for (uint8_t v : pcm)
                    sound->samples.push_back(int16_t((int(v) - 128) << 8));
            else
                for (size_t i = 0; i + 1 < pcm.size(); i += 2)
                    sound->samples.push_back(int16_t(pcm[i] | pcm[i + 1] << 8));
            m.audioTrack = sound;
        }
        m.endFrame = decoder->frameCount();
        m.decoder = std::move(decoder);
        if (window)
            byWindow[window] = id;
        ++opened;
        return id;
    }

    // Stops a movie; the window waiting to hear is told how it ended.
    void stop(uint32_t id, uint32_t how) {
        auto it = movies.find(id);
        if (it == movies.end() || !it->second.playing)
            return;
        Movie &m = it->second;
        m.playing = false;
        if (owner.mixer && m.sound != audio::kNoSound)
            owner.mixer->stop(m.sound);
        m.sound = audio::kNoSound;
        if (const uint32_t window = notifyWindow(m.notify))
            process.postMessage(window, kMmMciNotify, how, id);
        m.notify = 0;
    }

    // MCI's callback holds a window handle in its low word only (LEGO
    // Loco masks its own, 0x454391); the process's handles are wider, so
    // the window is the one whose handle ends so.
    uint32_t notifyWindow(uint32_t callback) const {
        if (!callback || process.window(callback))
            return callback;
        for (uint32_t high = 0x10000; high < 0x100000; high += 0x10000)
            if (process.window(high | (callback & 0xffff)))
                return high | (callback & 0xffff);
        return 0;
    }

    void close(uint32_t id) {
        stop(id, kNotifyAborted);
        auto it = movies.find(id);
        if (it == movies.end())
            return;
        byWindow.erase(it->second.window);
        movies.erase(it);
    }

    // Draws the decoder's picture into the screen's pixels where the
    // movie's window is, stretched to its destination.
    void draw(Movie &m) {
        const auto screen = process.gdi().screen ? process.gdi().screen() : std::nullopt;
        const Surface &picture = m.decoder->surface();
        if (!screen || picture.w == 0)
            return;
        int x0 = m.dest[0];
        int y0 = m.dest[1];
        if (const Win32Process::Window *w = process.window(m.window)) {
            x0 += w->x;
            y0 += w->y;
            if ((w->style & kWsChild) && w->parent)
                if (const auto area = process.clientArea(w->parent)) {
                    x0 += (*area)[0];
                    y0 += (*area)[1];
                }
        }
        const int dw = m.dest[2];
        const int dh = m.dest[3];
        for (int y = 0; y < dh; ++y) {
            const int sy = y0 + y;
            if (sy < 0 || sy >= screen->height)
                continue;
            const uint8_t *row = picture.row(y * picture.h / dh);
            const uint32_t line = screen->memory + uint32_t(sy * screen->pitch);
            for (int x = 0; x < dw; ++x) {
                const int sx = x0 + x;
                if (sx < 0 || sx >= screen->width)
                    continue;
                const uint8_t *p = row + (x * picture.w / dw) * 4;
                const uint32_t colorRef = p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16;
                uint32_t v;
                if (screen->bits == 8) {
                    auto c = nearest.find(colorRef);
                    if (c == nearest.end())
                        c = nearest.emplace(colorRef, Gdi::pixelFor(*screen, colorRef)).first;
                    v = c->second;
                } else {
                    v = Gdi::pixelFor(*screen, colorRef);
                }
                switch (screen->bits) {
                case 8:
                    cpu.write8(line + uint32_t(sx), uint8_t(v));
                    break;
                case 16:
                    cpu.write16(line + uint32_t(sx * 2), uint16_t(v));
                    break;
                case 24:
                    cpu.write8(line + uint32_t(sx * 3), uint8_t(v));
                    cpu.write16(line + uint32_t(sx * 3 + 1), uint16_t(v >> 8));
                    break;
                default:
                    cpu.write32(line + uint32_t(sx * 4), v);
                    break;
                }
            }
        }
        if (owner.frameDrawn)
            owner.frameDrawn();
    }

    // Plays from `from` to `to` (frames), telling `notify` at the end. With
    // host threads, a thread of the process draws the frames as their time
    // comes; without, the movie ends at once.
    void play(uint32_t id, int from, int to, uint32_t notify) {
        Movie &m = movies[id];
        stop(id, kNotifyAborted);
        m.startFrame = std::clamp(from, 0, m.decoder->frameCount());
        m.endFrame = std::clamp(to, m.startFrame, m.decoder->frameCount());
        m.notify = notify;
        m.playing = true;
        m.startMs = process.milliseconds();
        const uint64_t count = ++m.playCount;
        if (owner.mixer && m.audioTrack && m.startFrame == 0)
            m.sound = owner.mixer->play(m.audioTrack, audio::SoundType::kMusic);
        if (!process.hostThreads()) {
            stop(id, kNotifySuccessful);
            return;
        }
        process.createThread(
            [this, id, count]() -> uint32_t {
                for (;;) {
                    auto it = movies.find(id);
                    if (it == movies.end() || !it->second.playing || it->second.playCount != count)
                        return 0;
                    Movie &mv = it->second;
                    const uint64_t frameMs = std::max<uint64_t>(1, mv.decoder->frameMicros() / 1000);
                    const uint64_t elapsed = process.milliseconds() - mv.startMs;
                    const int due = mv.startFrame + int(elapsed / frameMs);
                    if (due >= mv.endFrame) {
                        stop(id, kNotifySuccessful);
                        return 0;
                    }
                    if (due > mv.decoder->currentFrame()) {
                        // Frames whose time has passed are decoded (each
                        // builds on the last) but only the newest drawn.
                        while (mv.decoder->currentFrame() < due && mv.decoder->decodeNextFrame()) {}
                        draw(mv);
                    }
                    const uint64_t next = mv.startMs + uint64_t(due + 1 - mv.startFrame) * frameMs;
                    const uint64_t now = process.milliseconds();
                    const uint32_t wait = uint32_t(next > now ? next - now : 0);
                    if (!process.wait(
                            [&] {
                                auto m2 = movies.find(id);
                                return m2 == movies.end() || !m2->second.playing || m2->second.playCount != count;
                            },
                            wait) &&
                        process.ending())
                        return 0;
                }
            },
            false, "movie");
    }

    // MCI_STATUS's items: the length and position in frames, the mode.
    uint32_t status(uint32_t id, uint32_t item) {
        const Movie &m = movies[id];
        switch (item) {
        case 1: // MCI_STATUS_LENGTH
            return uint32_t(m.decoder->frameCount());
        case 2: // MCI_STATUS_POSITION
            return uint32_t(std::max(0, m.decoder->currentFrame()));
        case 4: // MCI_STATUS_MODE
            return m.playing ? kMciModePlay : kMciModeStop;
        default:
            return 0;
        }
    }
};

MciVideo::~MciVideo() = default;

int MciVideo::opened() const {
    return _s->opened;
}

bool MciVideo::playing() const {
    for (const auto &[id, m] : _s->movies)
        if (m.playing)
            return true;
    return false;
}

MciVideo::MciVideo(Win32Process &process) : _s(std::make_unique<State>(*this, process)) {
    State *s = _s.get();

    // The movie window's procedure: the MCI commands it's sent as
    // messages, and its own.
    process.provideStdcall("RELUVA", "MciWndProc", 4, [s](Cpu &c) -> uint32_t {
        const uint32_t h = c.arg(0);
        const uint32_t message = c.arg(1);
        const uint32_t lp = c.arg(3);
        const auto device = s->byWindow.find(h);
        const uint32_t id = device == s->byWindow.end() ? 0 : device->second;
        switch (message) {
        case kWmNcCreate:
            return 1;
        case kWmCreate:
            return 0;
        case kMciWndmOpenA: {
            if (id)
                s->close(id);
            uint32_t error = 0;
            return s->open(c.string(lp), h, error) ? 0 : error;
        }
        case kMciWndmPutDest:
            if (id && c.valid(lp, 16)) {
                State::Movie &m = s->movies[id];
                m.dest[0] = int32_t(c.read32(lp));
                m.dest[1] = int32_t(c.read32(lp + 4));
                m.dest[2] = int32_t(c.read32(lp + 8)) - m.dest[0];
                m.dest[3] = int32_t(c.read32(lp + 12)) - m.dest[1];
            }
            return 0;
        case kMciWndmGetDeviceId:
            return id;
        case kMciWndmGetLength:
            return id ? s->status(id, 1) : 0;
        case kMciWndmGetPositionA:
            return id ? s->status(id, 2) : 0;
        case kMciPlay:
            if (id)
                s->play(id, s->status(id, 2), s->movies[id].decoder->frameCount(), 0);
            return 0;
        case kMciStop:
        case kMciPause:
            if (id)
                s->stop(id, kNotifyAborted);
            return 0;
        case kMciClose:
            if (id)
                s->close(id);
            return 0;
        case kWmClose: {
            uint32_t r = 0;
            c.call(s->process.standIn("USER32.dll", "DestroyWindow"), {h}, r);
            return 0;
        }
        case kWmDestroy:
            if (id)
                s->close(id);
            return 0;
        default: {
            uint32_t r = 0;
            c.call(s->process.standIn("USER32.dll", "DefWindowProcA"), {h, message, c.arg(2), lp}, r);
            return r;
        }
        }
    });
    // MCIWndRegisterClass(): MCIWndClass, through the process's own RegisterClassA.
    process.provideStdcall("MSVFW32.dll", "MCIWndRegisterClass", 0, [s](Cpu &c) -> uint32_t {
        Win32Process &p = s->process;
        const uint32_t cls = p.memory().alloc(64);
        for (uint32_t i = 0; i < 40; i += 4)
            c.write32(cls + i, 0);
        c.writeString(cls + 40, "MCIWndClass", 16);
        c.write32(cls + 4, p.standIn("RELUVA", "MciWndProc"));
        c.write32(cls + 36, cls + 40);
        uint32_t atom = 0;
        c.call(p.standIn("USER32.dll", "RegisterClassA"), {cls}, atom);
        return 1;
    });

    // mciSendCommandA(device, command, flags, parameters): opening an AVI
    // file, playing, stopping, closing and its status.
    process.provideStdcall("WINMM.dll", "mciSendCommandA", 4, [s](Cpu &c) -> uint32_t {
        const uint32_t id = c.arg(0);
        const uint32_t command = c.arg(1);
        const uint32_t flags = c.arg(2);
        const uint32_t parms = c.arg(3);
        if (command == kMciOpen) {
            // MCI_OPEN_PARMS: callback, device id (out), device type, element, alias.
            if (!(flags & kMciOpenElement) || !c.valid(parms, 20))
                return kMciErrUnsupportedFunction;
            uint32_t error = 0;
            const uint32_t opened = s->open(c.string(c.read32(parms + 12)), 0, error);
            if (!opened)
                return error;
            c.write32(parms + 4, opened);
            return 0;
        }
        if (!s->movies.count(id))
            return kMciErrInvalidDeviceId;
        const uint32_t notify = (flags & kMciNotify) && c.valid(parms, 4) ? c.read32(parms) : 0;
        switch (command) {
        case kMciPlay: {
            // MCI_PLAY_PARMS: callback, from, to (frames).
            const int count = s->movies[id].decoder->frameCount();
            const int from = (flags & kMciFrom) ? int(c.read32(parms + 4)) : int(s->status(id, 2));
            const int to = (flags & kMciTo) ? int(c.read32(parms + 8)) : count;
            s->play(id, from, to, notify);
            return 0;
        }
        case kMciStop:
        case kMciPause:
            s->stop(id, kNotifyAborted);
            return 0;
        case kMciClose:
            s->close(id);
            return 0;
        case kMciStatus:
            // MCI_STATUS_PARMS: callback, the answer (out), the item.
            if (!(flags & kMciStatusItem) || !c.valid(parms, 12))
                return kMciErrUnsupportedFunction;
            c.write32(parms + 4, s->status(id, c.read32(parms + 8)));
            return 0;
        default:
            if (s->process.output)
                s->process.output("mciSendCommandA: command " + std::to_string(command) + " isn't stood in for\n");
            return kMciErrUnsupportedFunction;
        }
    });
}

} // namespace reluva::x86
