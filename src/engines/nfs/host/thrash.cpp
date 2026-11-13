// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The renderer DLL's functions (THRASH), counted on their way through:
// GetProcAddress gives the game a native function for each, which counts
// the call and calls the DLL's own. Their names carry the bytes of
// arguments each takes (stdcall: "_THRASH_drawtri@12"). A page flip ends
// a frame, which gives the instructions each frame takes.

#include "engines/nfs/host/game_host.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace reluva::nfs {

using x86::Cpu;

uint32_t GameHost::wrapThrash(const std::string &name, uint32_t address) {
    const size_t at = name.rfind('@');
    const int argBytes = at == std::string::npos ? 0 : std::atoi(name.c_str() + at + 1);
    std::string shortName = name;
    if (shortName.rfind("_THRASH_", 0) == 0)
        shortName = shortName.substr(8, at == std::string::npos ? std::string::npos : at - 8);
    return _cpu->addNative("THRASH " + shortName, [this, shortName, address, argBytes](Cpu &c) {
        ++_thrash.calls[shortName];
        std::vector<uint32_t> args;
        for (int i = 0; i < argBytes / 4; ++i)
            args.push_back(c.arg(i));
        if (shortName == "talloc") {
            ++_thrash.textures;
            ++_thrash.textureFormats[int(args[2])];
        } else if (shortName == "setvideomode") {
            say("  THRASH_setvideomode(" + std::to_string(args[0]) + ", " + std::to_string(args[1]) + ", " +
                std::to_string(args[2]) + ")");
        } else if (shortName == "pageflip") {
            ++_thrash.frames;
            _thrash.lastFrameInstructions = c.executed() - _lastFlip;
            _thrash.frameInstructionsTotal += _thrash.lastFrameInstructions;
            const uint64_t idleMicros = _process->idleMicros;
            const uint64_t idleWaits = _process->idleWaits;
            const uint64_t own = _process->instructionsOn(_process->currentThread());
            const uint64_t timer = _process->timerInstructions();
            _thrash.frameLog.push_back(
                {_process->now(), _thrash.lastFrameInstructions, _thrash.rendererInstructions - _lastFlipRenderer,
                 idleMicros - _thrash.lastIdleMicros, idleWaits - _thrash.lastIdleWaits,
                 own - std::min(own, _thrash.lastOwn), timer - std::min(timer, _thrash.lastTimer)});
            _thrash.lastOwn = own;
            _thrash.lastTimer = timer;
            _thrash.lastIdleMicros = idleMicros;
            _thrash.lastIdleWaits = idleWaits;
            _lastFlip = c.executed();
            _lastFlipRenderer = _thrash.rendererInstructions;
        }
        // Drawing done natively, if asked: the DLL isn't called.
        uint32_t result = 1;
        // With Glide and a native copy (Options::glideCompare) the DLL draws
        // and the native rasterizer draws the same calls beside it.
        const bool mirror = _glide && _native;
        if (!(_native && !mirror && nativeDraw(shortName, args, result))) {
            // The instructions run inside count as the renderer's (the
            // other threads may take turns meanwhile; those are few).
            const uint64_t before = c.executed();
            if (Error e = c.call(address, args, result))
                c.abort("THRASH_" + shortName + ": " + e.message);
            _thrash.rendererInstructions += c.executed() - before;
            _thrash.callInstructions[shortName] += c.executed() - before;
            // The native side keeps its own copy of what drawing needs.
            if (_native) {
                if (shortName == "talloc" && result)
                    _native->talloc(result, int(args[0]), int(args[1]), int(args[2]));
                else if (shortName == "treset")
                    _native->treset();
                else if (shortName == "setstate")
                    _native->setState(args[0], args[1]);
                else if (shortName == "clip")
                    _native->clip(int(args[0]), int(args[1]), int(args[2]), int(args[3]));
            }
            if (shortName == "window" && result)
                _thrashWindow = args[0];
            // lockwindow's structure: the buffer, its pitch, then (after a
            // word) its width and height.
            if (shortName == "lockwindow" && result && c.valid(result, 20) && !mirror)
                _windowBuffers[_thrashWindow] = {c.read32(result), c.read32(result + 4), c.read32(result + 12),
                                                 c.read32(result + 16)};
        }
        if (mirror)
            mirrorNative(shortName, args);
        // What was drawn or written straight to the front buffer shows at
        // once, as on the card (the movies, written through lockwindow).
        if (_glide && _glide->core().takeFrontChanged()) {
            _glide->core().pipeline().present(_frame);
            if (onFrame)
                onFrame(_frame);
        }
        c.reg[Cpu::EAX] = result;
        if (_options.thrashLogTo && _thrash.frames >= _options.thrashLogFrom &&
            _thrash.frames <= _options.thrashLogTo) {
            // The call, then up to 64 bytes at each argument that points
            // into memory.
            char buf[64];
            std::string line = "  [frame " + std::to_string(_thrash.frames) + "] " + shortName + "(";
            for (size_t i = 0; i < args.size(); ++i) {
                std::snprintf(buf, sizeof buf, "%s0x%x", i ? ", " : "", args[i]);
                line += buf;
            }
            std::snprintf(buf, sizeof buf, ") = 0x%x", result);
            say(line + buf);
            for (size_t i = 0; i < args.size(); ++i)
                if (args[i] >= 0x10000 && c.valid(args[i], 64)) {
                    std::string dump = "      arg" + std::to_string(i) + ":";
                    for (uint32_t k = 0; k < 64; k += 4) {
                        std::snprintf(buf, sizeof buf, " %08x", c.read32(args[i] + k));
                        dump += buf;
                    }
                    say(dump);
                }
        }
        return uint32_t(argBytes);
    });
}

void GameHost::addGlide() {
    // A DLL from outside the install (the CD's Voodoo3 patch) that loads
    // glide3x.dll gets Glide 3 on a Voodoo3; the retail voodoo2a.dll Glide
    // 2 on a Voodoo 2 with 4 MB of frame buffer and two TMUs of 4 MB (the
    // 12 MB board), which it reads as a Voodoo 2 (thrash.md, The Glide
    // stand-in).
    bool glide3 = false;
    if (!_options.glideDll.empty()) {
        std::ifstream in(_options.glideDll, std::ios::binary);
        std::string file((std::istreambuf_iterator<char>(in)), {});
        for (char &ch : file)
            ch = char(std::tolower(uint8_t(ch)));
        glide3 = file.find("glide3x.dll") != std::string::npos;
    }
    // The board: Options::glideBoard if given (a Voodoo Graphics for the
    // install's voodooa.dll...), else the Voodoo2 or the Voodoo3.
    glide::Board board = glide3 ? glide::voodoo3Board() : glide::voodoo2Board();
    if (!_options.glideBoard.empty() && !glide::boardByName(_options.glideBoard, board))
        say("  unknown Glide board " + _options.glideBoard + ": the default");
    if (glide3)
        _glide = std::make_unique<glide::Glide3Dll>(*_cpu, _process->memory(), board);
    else
        _glide = std::make_unique<glide::Glide2Dll>(*_cpu, _process->memory(), board);
    glide::Glide2 &g = _glide->core();
    g.log = [this](const std::string &line) { say("  " + line); };
    // gu3dfLoad's files, by the program's own paths.
    _glide->readFile = [this](const std::string &path, std::vector<uint8_t> &bytes) {
        std::ifstream in(_process->hostPath(path, false), std::ios::binary);
        if (!in)
            return false;
        bytes.assign(std::istreambuf_iterator<char>(in), {});
        return true;
    };
    // The GPU backend, if asked for and there's a GPU (Options::glideGpu).
    if (_options.glideGpu) {
        std::string error;
        std::unique_ptr<gfx::Compute> gpu = gfx::createCompute(error);
        const std::string device = gpu ? gpu->deviceName() : std::string();
        std::unique_ptr<glide::Renderer> renderer = gpu ? glide::makeGpuRenderer(std::move(gpu), error) : nullptr;
        if (renderer) {
            _glideRenderer = "GPU (" + device + ")";
            if (_options.glideGpu == 2) {
                renderer = glide::makeComparingRenderer(std::move(renderer), [this](const glide::FrameComparison &f) {
                    if (onGlideComparison)
                        onGlideComparison(f);
                });
                _glideRenderer += ", compared with the software reference";
            }
            g.pipeline().setRenderer(std::move(renderer));
        } else {
            say("  Glide on the GPU: " + error + "; drawn in software");
        }
    }
    static const char *const kGenerations[] = {"Voodoo Graphics", "Voodoo Rush", "Voodoo2", "Voodoo Banshee",
                                               "Voodoo3"};
    say("  " + _glide->dllName() + " stood in for on a " + kGenerations[int(board.generation)] + ", drawn by " +
        _glideRenderer);
    g.onSwap = [this] {
        _glide->core().pipeline().present(_frame);
        if (onFrame)
            onFrame(_frame);
    };
    const uint32_t handle = 0x6c000000u;
    addNativeModule(_glide->dllName(), handle);
    for (const auto &[name, address] : _glide->exports())
        _nativeExports[handle][name] = address;
    // For comparisons: the texture filter held to one mode whatever the
    // DLL asks (it asks for bilinear, thrash.md, Texture filtering).
    if (_options.glideFilter >= 0)
        _nativeExports[handle]["_grTexFilterMode@12"] =
            _cpu->addNative(_glide->dllName() + " grTexFilterMode (held)", [this](Cpu &c) {
                _glide->core().texFilterMode(int(c.arg(0)), _options.glideFilter, _options.glideFilter);
                c.reg[Cpu::EAX] = 0;
                return 12u;
            });
}

void GameHost::mirrorNative(const std::string &name, const std::vector<uint32_t> &args) {
    // The native rasterizer's own 640x480 5:6:5 buffer, for both windows.
    if (!_compareBuffer) {
        _compareBuffer = _process->memory().alloc(640 * 480 * 2);
        _windowBuffers[1] = _windowBuffers[2] = {_compareBuffer, 1280, 640, 480};
    }
    if (name == "tupdate" && args.size() >= 2)
        _native->tupdate(args[0], args[1], args.size() > 2 ? args[2] : 0);
    else if (name == "setstate" && args.size() >= 2 && args[0] == 3)
        _compareClear = args[1];
    else if (name == "clip" && args.size() >= 4)
        _compareClip = {int(args[0]), int(args[1]), int(args[2]), int(args[3])};
    else if (name == "clearwindow") {
        // The clear colour (0xAARRGGBB) as 5:6:5, over the clip rectangle.
        const uint32_t v = (_compareClear >> 8 & 0xf800) | (_compareClear >> 5 & 0x7e0) | (_compareClear >> 3 & 0x1f);
        for (int y = std::max(_compareClip[1], 0); y < std::min(_compareClip[3], 480); ++y)
            for (int x = std::max(_compareClip[0], 0); x < std::min(_compareClip[2], 640); ++x)
                _cpu->write16(_compareBuffer + uint32_t(y * 1280 + x * 2), uint16_t(v));
    } else if (name == "pageflip") {
        Surface native;
        native.create(640, 480, PixelFormat::kRGBA32);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) {
                const uint16_t v = _cpu->read16(_compareBuffer + uint32_t(y * 1280 + x * 2));
                uint8_t *d = native.row(y) + x * 4;
                // Widened as the Glide frame is (the top bits repeated), so
                // that only the drawing differs.
                const glide::Rgba w = glide::from565(v);
                d[0] = uint8_t(w.r);
                d[1] = uint8_t(w.g);
                d[2] = uint8_t(w.b);
                d[3] = 255;
            }
        if (onCompareFrame)
            onCompareFrame(native);
    } else if (name.rfind("draw", 0) == 0) {
        uint32_t ignored = 0;
        nativeDraw(name, args, ignored);
    }
}

uint32_t GameHost::softtria(uint32_t address) const {
    return _cpu->read32(address - 0x60000000 + _thrashBase);
}

void GameHost::presentBuffer(uint32_t buffer, int pitch, int x0, int y0, int width, int height) {
    if (_frame.w != _modeWidth || _frame.h != _modeHeight)
        _frame.create(_modeWidth, _modeHeight, PixelFormat::kRGBA32);
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    width = std::min(width, _frame.w - x0);
    height = std::min(height, _frame.h - y0);
    if (width <= 0 || height <= 0 || !_cpu->valid(buffer + uint32_t(y0 * pitch), uint32_t(height * pitch)))
        return;
    for (int y = y0; y < y0 + height; ++y) {
        const uint8_t *src = _cpu->memory().data() + buffer + size_t(y) * size_t(pitch) + size_t(x0) * 2;
        uint8_t *dst = _frame.row(y) + size_t(x0) * 4;
        for (int x = 0; x < width; ++x, dst += 4) {
            const uint16_t v = uint16_t(src[2 * x] | src[2 * x + 1] << 8);
            dst[0] = uint8_t(((v >> 11) & 31) * 255 / 31);
            dst[1] = uint8_t(((v >> 5) & 63) * 255 / 63);
            dst[2] = uint8_t((v & 31) * 255 / 31);
            dst[3] = 255;
        }
    }
    if (onFrame)
        onFrame(_frame);
}

bool GameHost::nativeDraw(const std::string &name, const std::vector<uint32_t> &args, uint32_t &result) {
    // Texture uploads: only the native copy is kept, as the DLL's drawing
    // (the one user of its own) isn't called.
    if (name == "tupdate" && args.size() >= 2) {
        result = _native->tupdate(args[0], args[1], args.size() > 2 ? args[2] : 0);
        return true;
    }
    // softtria's page flip and clear work on its current window's buffer,
    // over the clip rectangle as it keeps it (whole 16-pixel columns).
    if (_softtriaKnown && (name == "pageflip" || name == "clearwindow")) {
        const uint32_t buffer = softtria(0x6003e0fc);
        const uint32_t pitch = softtria(0x6003e0ec);
        const int x0 = int(softtria(0x6003e0b8)) & ~15;
        const int y0 = int(softtria(0x6003e0bc));
        const int width = (int(softtria(0x6003e0c0)) >> 4) * 16;
        const int height = int(softtria(0x6003e0c4));
        if (name == "clearwindow" && _options.gpuThrash) {
            const uint32_t v = softtria(0x6003e11c) & 0xffff; // 5:6:5
            const uint32_t argb =
                0xff000000u | ((v >> 11) & 31) * 255 / 31 << 16 | ((v >> 5) & 63) * 255 / 63 << 8 | (v & 31) * 255 / 31;
            _native->fill(x0, y0, x0 + width, y0 + height, argb);
            if (!_options.gpuAndSoftware)
                return true;
        }
        if (name == "clearwindow") {
            const uint32_t fill = softtria(0x6003e11c); // the clear colour, two pixels of it
            if (width > 0 && height > 0 && _cpu->valid(buffer + uint32_t(y0) * pitch, uint32_t(height) * pitch))
                for (int y = y0; y < y0 + height; ++y) {
                    uint8_t *row = _cpu->memory().data() + buffer + size_t(y) * pitch + size_t(x0) * 2;
                    for (int x = 0; x < width; x += 2)
                        std::memcpy(row + 2 * x, &fill, 4);
                }
            return true;
        }
        // A function the game gave it, called with 1 before the frame is
        // copied to the screen and 0 after.
        const uint32_t hook = softtria(0x600400b4);
        uint32_t ignored = 0;
        if (hook)
            if (Error e = _cpu->call(hook, {1}, ignored))
                _cpu->abort("THRASH_pageflip: " + e.message);
        if (!_options.gpuThrash || _options.gpuAndSoftware)
            presentBuffer(buffer, int(pitch), x0, y0, width, height);
        if (_options.gpuThrash) {
            ThrashFrame frame = _native->takeFrame();
            // The rectangle the flip copies to the screen: elsewhere, what's
            // there stays (a loading screen's picture, drawn with DirectDraw).
            frame.flip[0] = x0;
            frame.flip[1] = y0;
            frame.flip[2] = x0 + width;
            frame.flip[3] = y0 + height;
            if (onThrashFrame)
                onThrashFrame(std::move(frame));
        }
        if (hook)
            if (Error e = _cpu->call(hook, {0}, ignored))
                _cpu->abort("THRASH_pageflip: " + e.message);
        return true;
    }
    if (name.rfind("draw", 0) != 0)
        return false;
    // The buffer the software renderer would draw into: the current
    // window's, as lockwindow told the game.
    auto buffer = _windowBuffers.find(_thrashWindow);
    if (buffer == _windowBuffers.end())
        return true; // nothing known to draw into: as if drawn
    const auto &[address, pitch, width, height] = buffer->second;
    _native->setTarget(address, int(pitch), int(width), int(height));
    NativeThrash &n = *_native;
    auto at = [&](uint32_t base, uint32_t i) { return base + 32 * i; };
    auto index = [&](uint32_t list, uint32_t i) { return _cpu->read32(list + 4 * i); };
    const uint32_t count = args.empty() ? 0 : std::min<uint32_t>(args[0], 65536);
    if (name == "drawtri")
        n.triangle(args[0], args[1], args[2]);
    else if (name == "drawquad")
        n.quad(args[0], args[1], args[2], args[3]);
    else if (name == "drawtrifan") // count triangles: vertex 0 and each pair after it
        for (uint32_t i = 0; i < count; ++i)
            n.triangle(at(args[1], 0), at(args[1], i + 1), at(args[1], i + 2));
    else if (name == "drawtristrip")
        for (uint32_t i = 0; i < count; ++i)
            n.triangle(at(args[1], i), at(args[1], i + 1), at(args[1], i + 2));
    else if (name == "drawtrimesh") // count triangles, 3 indices each
        for (uint32_t i = 0; i < count; ++i)
            n.triangle(at(args[1], index(args[2], 3 * i)), at(args[1], index(args[2], 3 * i + 1)),
                       at(args[1], index(args[2], 3 * i + 2)));
    else if (name == "drawquadmesh") // count quads, 4 indices each
        for (uint32_t i = 0; i < count; ++i)
            n.quad(at(args[1], index(args[2], 4 * i)), at(args[1], index(args[2], 4 * i + 1)),
                   at(args[1], index(args[2], 4 * i + 2)), at(args[1], index(args[2], 4 * i + 3)));
    else if (name == "drawline")
        n.line(args[0], args[1]);
    else if (name == "drawlinestrip")
        for (uint32_t i = 0; i < count; ++i)
            n.line(at(args[1], i), at(args[1], i + 1));
    else if (name == "drawlinemesh") // count lines, 2 indices each
        for (uint32_t i = 0; i < count; ++i)
            n.line(at(args[1], index(args[2], 2 * i)), at(args[1], index(args[2], 2 * i + 1)));
    else if (name == "drawpoint")
        n.point(args[0]);
    else if (name == "drawpointstrip")
        for (uint32_t i = 0; i < count; ++i)
            n.point(at(args[1], i));
    else if (name == "drawpointmesh")
        for (uint32_t i = 0; i < count; ++i)
            n.point(at(args[1], index(args[2], i)));
    else
        return false;
    return true;
}

} // namespace reluva::nfs
