// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The renderer DLL's functions (THRASH), counted on their way through:
// GetProcAddress gives the game a native function for each, which counts
// the call and calls the DLL's own. Their names carry the bytes of
// arguments each takes (stdcall: "_THRASH_drawtri@12"). A page flip ends
// a frame, which gives the instructions each frame takes.

#include "engines/nfs/host/game_host.h"

#include <cstdlib>

namespace reluva::nfs {

using x86::Cpu;

uint32_t GameHost::wrapThrash(const std::string& name, uint32_t address) {
    const size_t at = name.rfind('@');
    const int argBytes = at == std::string::npos ? 0 : std::atoi(name.c_str() + at + 1);
    std::string shortName = name;
    if (shortName.rfind("_THRASH_", 0) == 0)
        shortName = shortName.substr(8, at == std::string::npos ? std::string::npos : at - 8);
    return cpu_->addNative("THRASH " + shortName, [this, shortName, address, argBytes](Cpu& c) {
        ++thrash_.calls[shortName];
        std::vector<uint32_t> args;
        for (int i = 0; i < argBytes / 4; ++i)
            args.push_back(c.arg(i));
        if (shortName == "talloc") {
            ++thrash_.textures;
            ++thrash_.textureFormats[int(args[2])];
        }
        else if (shortName == "setvideomode") {
            say("  THRASH_setvideomode(" + std::to_string(args[0]) + ", " + std::to_string(args[1]) + ", " +
                std::to_string(args[2]) + ")");
        } else if (shortName == "pageflip") {
            ++thrash_.frames;
            thrash_.lastFrameInstructions = c.executed() - lastFlip_;
            thrash_.frameInstructionsTotal += thrash_.lastFrameInstructions;
            thrash_.frameLog.push_back({process_->now(), thrash_.lastFrameInstructions,
                                        thrash_.rendererInstructions - lastFlipRenderer_});
            lastFlip_ = c.executed();
            lastFlipRenderer_ = thrash_.rendererInstructions;
        }
        // Drawing done natively, if asked: the DLL isn't called.
        uint32_t result = 1;
        if (!(native_ && nativeDraw(shortName, args))) {
            // The instructions run inside count as the renderer's (the
            // other threads may take turns meanwhile; those are few).
            const uint64_t before = c.executed();
            if (Error e = c.call(address, args, result))
                c.abort("THRASH_" + shortName + ": " + e.message);
            thrash_.rendererInstructions += c.executed() - before;
            thrash_.callInstructions[shortName] += c.executed() - before;
            // The native side keeps its own copy of what drawing needs.
            if (native_) {
                if (shortName == "talloc" && result)
                    native_->talloc(result, int(args[0]), int(args[1]), int(args[2]));
                else if (shortName == "tupdate")
                    native_->tupdate(args[0], args[1], args.size() > 2 ? args[2] : 0);
                else if (shortName == "treset")
                    native_->treset();
                else if (shortName == "setstate")
                    native_->setState(args[0], args[1]);
                else if (shortName == "clip")
                    native_->clip(int(args[0]), int(args[1]), int(args[2]), int(args[3]));
            }
            if (shortName == "window" && result)
                thrashWindow_ = args[0];
            // lockwindow's structure: the buffer, its pitch, then (after a
            // word) its width and height.
            if (shortName == "lockwindow" && result && c.valid(result, 20))
                windowBuffers_[thrashWindow_] = {c.read32(result), c.read32(result + 4), c.read32(result + 12),
                                                 c.read32(result + 16)};
        }
        c.reg[Cpu::EAX] = result;
        if (options_.thrashLogTo && thrash_.frames >= options_.thrashLogFrom && thrash_.frames <= options_.thrashLogTo) {
            // The call, then up to 64 bytes at each argument that points
            // into memory.
            char buf[64];
            std::string line = "  [frame " + std::to_string(thrash_.frames) + "] " + shortName + "(";
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

bool GameHost::nativeDraw(const std::string& name, const std::vector<uint32_t>& args) {
    if (name.rfind("draw", 0) != 0)
        return false;
    // The buffer the software renderer would draw into: the current
    // window's, as lockwindow told the game.
    auto buffer = windowBuffers_.find(thrashWindow_);
    if (buffer == windowBuffers_.end())
        return true; // nothing known to draw into: as if drawn
    const auto& [address, pitch, width, height] = buffer->second;
    native_->setTarget(address, int(pitch), int(width), int(height));
    NativeThrash& n = *native_;
    auto at = [&](uint32_t base, uint32_t i) { return base + 32 * i; };
    auto index = [&](uint32_t list, uint32_t i) { return cpu_->read32(list + 4 * i); };
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
