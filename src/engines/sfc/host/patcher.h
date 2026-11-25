// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <filesystem>
#include <functional>
#include <string>

namespace reluva::sfc {

/**
 * Applies the 2.0.3.6 patch as its own setup does: its Patch.exe (Pocket
 * Soft's RTPatch, with Patchw32.dll) run in the x86 emulator on an install,
 * changing it in place. The install is the program's drive C:, the
 * patch's files D:, its folder, where what it writes goes to a scratch
 * folder instead. docs/engines/sfc/games/sfc2/systems/patching.md.
 */
class Patcher {
public:
    /**
     * Takes the patch's files out of its download (sfc2patch20022036.exe: a
     * Microsoft cabinet holding an InstallShield 5 setup, whose cabinet
     * holds Patch.exe, Patchw32.dll and PATCHSFC2-2002-2036.RTP) into `to`.
     * @return An error if it isn't that patch.
     */
    static Error unpack(const std::filesystem::path &download, const std::filesystem::path &to);

    /**
     * `install` is the game's folder, which the patch changes; `patchFiles`
     * holds Patch.exe, Patchw32.dll and PATCHSFC2-2002-2036.RTP; `scratch`
     * takes what else it writes (made if it isn't there).
     */
    Patcher(std::filesystem::path install, std::filesystem::path patchFiles, std::filesystem::path scratch);

    /** Whether each call into a stand-in is told to output, with its first string, for finding what it needs. */
    bool logCalls = false;

    /** Told each line the patch writes to its console. */
    std::function<void(const std::string &)> output;

    /**
     * Runs it to its end.
     * @param exitCode Patch.exe's exit code (0 when it's applied).
     * @return An error if it couldn't be run, or the run stopped.
     */
    Error run(uint32_t &exitCode);

private:
    std::filesystem::path _install;
    std::filesystem::path _patch;
    std::filesystem::path _scratch;
};

} // namespace reluva::sfc
