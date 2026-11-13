// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Testbed: a built-in "game" that exercises the System interface (palette
// graphics, input, timing, audio) so the backend can be checked without any
// game data. Esc returns to the launcher, Space toggles a test tone, C
// switches between the system's pointer and a cursor of the game's own
// (System::setMouseCursor).

#include "graphics/cursor.h"
#include "engines/engine.h"

#include <atomic>
#include <cmath>

namespace reluva {

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 200;
constexpr double kPi = 3.14159265358979323846;

class TestbedEngine : public Engine {
public:
    explicit TestbedEngine(System &system) : _sys(system) {}

    Error run() override {
        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kCLUT8))
            return err;
        _sys.setWindowTitle("Reluva - Testbed (Esc: back, Space: tone, C: cursor)");
        _screen.create(kWidth, kHeight, PixelFormat::kCLUT8);

        const int rate = _sys.audioRate();
        // Audio failing is not fatal for the testbed; it just stays silent.
        _sys.startAudio([this, rate](int16_t *out, int frames) {
            const bool on = _toneOn.load();
            for (int i = 0; i < frames; ++i) {
                int16_t s = 0;
                if (on) {
                    s = static_cast<int16_t>(std::sin(_phase) * 6000);
                    _phase += 2 * kPi * 440.0 / rate;
                    if (_phase > 2 * kPi)
                        _phase -= 2 * kPi;
                }
                out[i * 2] = s;
                out[i * 2 + 1] = s;
            }
        });

        const uint64_t start = _sys.getMillis();
        bool running = true;
        while (running) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                switch (ev.type) {
                case EventType::Quit:
                case EventType::ReturnToLauncher:
                    running = false;
                    break;
                case EventType::KeyDown:
                    if (ev.key == key::Escape)
                        running = false;
                    else if (ev.key == key::Space && !ev.repeat)
                        _toneOn = !_toneOn;
                    else if (ev.key == 'c' && !ev.repeat)
                        toggleCursor();
                    break;
                case EventType::MouseMove:
                    _mouseX = ev.mouseX;
                    _mouseY = ev.mouseY;
                    break;
                default:
                    break;
                }
            }

            const uint64_t t = _sys.getMillis() - start;
            drawFrame(t);
            _sys.updateScreen(_screen);
            _sys.delayMillis(1); // vsync paces the loop; this just yields
        }

        _sys.stopAudio();
        return Error::success();
    }

private:
    // An arrow in two colours, drawn here, with its hotspot at its tip.
    void toggleCursor() {
        _customCursor = !_customCursor;
        if (!_customCursor) {
            _sys.setMouseCursor(nullptr);
            return;
        }
        static const char *const kArrow[] = {
            "X.........", "XX........", "XOX.......", "XOOX......", "XOOOX.....", "XOOOOX....",
            "XOOOOOX...", "XOOOOOOX..", "XOOOOOOOX.", "XOOOOOXXXX", "XOOXOOX...", "XOX.XOOX..",
            "XX..XOOX..", "X....XOOX.", ".....XOOX.", "......XX..",
        };
        constexpr int kW = 10;
        constexpr int kH = 16;
        uint8_t pixels[kW * kH];
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                pixels[y * kW + x] = kArrow[y][x] == 'X' ? 1 : kArrow[y][x] == 'O' ? 2 : 0;
        uint8_t palette[768] = {};
        palette[3] = 20, palette[4] = 20, palette[5] = 60;   // 1: the outline, dark blue
        palette[6] = 255, palette[7] = 200, palette[8] = 40; // 2: the inside, amber
        const Cursor c = cursor::fromIndexed(pixels, kW, kH, kW, palette, 0, 0, 0);
        _sys.setMouseCursor(&c);
    }

    void drawFrame(uint64_t t) {
        // Static plasma pattern in indices 0-254; animation comes purely from
        // palette cycling, the way many games of the era did it.
        if (_plasma.pixels.empty()) {
            _plasma.create(kWidth, kHeight, PixelFormat::kCLUT8);
            for (int y = 0; y < kHeight; ++y) {
                uint8_t *row = _plasma.row(y);
                for (int x = 0; x < kWidth; ++x) {
                    double v = std::sin(x / 16.0) + std::sin(y / 8.0) + std::sin((x + y) / 16.0) +
                               std::sin(std::hypot(x - 160, y - 100) / 8.0);
                    row[x] = static_cast<uint8_t>((v + 4) / 8 * 254);
                }
            }
        }
        _screen.pixels = _plasma.pixels;

        Color pal[256];
        const double shift = t / 1000.0;
        for (int i = 0; i < 255; ++i) {
            double a = i / 254.0 * 2 * kPi + shift;
            pal[i].r = static_cast<uint8_t>(127 + 127 * std::sin(a));
            pal[i].g = static_cast<uint8_t>(127 + 127 * std::sin(a + 2 * kPi / 3));
            pal[i].b = static_cast<uint8_t>(127 + 127 * std::sin(a + 4 * kPi / 3));
        }
        pal[255] = {255, 255, 255};
        _sys.setPalette(pal, 0, 256);

        // Crosshair at the mouse in colour 255, to verify coordinate mapping.
        for (int d = -4; d <= 4; ++d) {
            putPixel(_mouseX + d, _mouseY, 255);
            putPixel(_mouseX, _mouseY + d, 255);
        }
    }

    void putPixel(int x, int y, uint8_t c) {
        if (x >= 0 && y >= 0 && x < kWidth && y < kHeight)
            _screen.row(y)[x] = c;
    }

    System &_sys;
    Surface _screen;
    Surface _plasma;
    int _mouseX = kWidth / 2, _mouseY = kHeight / 2;
    bool _customCursor = false;
    std::atomic<bool> _toneOn{false};
    double _phase = 0; // audio thread only
};

class TestbedMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "testbed"; }
    const char *name() const override { return "Testbed"; }

    // The testbed needs no files; the launcher starts it directly.
    std::vector<DetectedGame> detect(const std::filesystem::path &) const override { return {}; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &) const override {
        return std::make_unique<TestbedEngine>(system);
    }
};

} // namespace

const MetaEngine &testbedMetaEngine() {
    static const TestbedMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
