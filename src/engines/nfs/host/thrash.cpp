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
        if (shortName == "talloc")
            ++thrash_.textures;
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
        // The instructions run inside count as the renderer's (the other
        // threads may take turns meanwhile; those are few).
        const uint64_t before = c.executed();
        uint32_t result = 0;
        if (Error e = c.call(address, args, result))
            c.abort("THRASH_" + shortName + ": " + e.message);
        thrash_.rendererInstructions += c.executed() - before;
        c.reg[Cpu::EAX] = result;
        return uint32_t(argBytes);
    });
}

} // namespace reluva::nfs
