// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "common/x86/a3d.h"
#include "common/x86/cpu.h"
#include "common/x86/directx/dinput.h"
#include "common/x86/directx/directx.h"
#include "common/x86/win32.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva::netimmerse::alphateam {

// LEGO Alpha Team's own code (LEGOATeam.exe and the DLLs beside it) run in
// the x86 interpreter, as docs/engines/netimmerse/alphateam.md's phase 3 plans: each
// module loaded once the modules it imports from are, its start-up run,
// then the program's. What the code asks of Windows is stood in for by
// Win32Process and by this engine.
class GameProcess {
public:
    // The folder the program believes it was installed in, as the
    // installer puts it.
    static constexpr const char* kInstallFolder = "C:\\Program Files\\LEGO Media\\Games\\LEGO Alpha Team\\";

    // `memoryMb` of memory for the program (the archives are mapped into it).
    GameProcess(std::filesystem::path gameDir, uint32_t memoryMb = 1024);

    // Where what the program writes in its install folder goes, and is
    // read back from first (GameTarget::savePath). Without one, writing
    // isn't allowed.
    void setSaveFolder(std::filesystem::path dir) { saveDir_ = std::move(dir); }

    // Reads and loads LEGOATeam.exe and every DLL it needs from the game
    // folder, in an order where each comes after those it imports from.
    Error load();
    // Runs each DLL's start-up, in load order; stops at the first that fails.
    Error attach();
    // Runs the program from its entry point.
    Error run(uint32_t& exitCode);

    x86::Cpu& cpu() { return *cpu_; }
    x86::Win32Process& process() { return *process_; }
    x86::DirectX& directX() { return *directX_; }
    x86::A3d& a3d() { return *a3d_; }
    // Module names in the order they were loaded (the program last).
    const std::vector<std::string>& loadOrder() const { return order_; }

private:
    std::filesystem::path dir_;
    std::filesystem::path saveDir_;
    std::unique_ptr<x86::Cpu> cpu_;
    std::unique_ptr<x86::Win32Process> process_;
    std::unique_ptr<x86::DirectX> directX_;
    std::unique_ptr<x86::DirectInput> directInput_;
    std::unique_ptr<x86::A3d> a3d_;
    std::vector<std::string> order_;
    std::vector<const x86::Win32Process::Module*> modules_;
};

} // namespace reluva::netimmerse::alphateam
