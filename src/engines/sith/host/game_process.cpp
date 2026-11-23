// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/host/game_process.h"

#include "common/fs.h"
#include "common/stream.h"

#include <system_error>
#include <vector>

namespace reluva::sith {

namespace {

constexpr const char *kRegistryKey = "HKEY_LOCAL_MACHINE\\Software\\LucasArts Entertainment Company\\JediKnight\\v1.0";

// What a machine of the time without the device answers: no joystick, no
// CD audio, no DirectPlay lobby, no waveOut device. The program learns it has
// none, as it would there.
void addAbsentDevices(x86::Win32Process &process) {
    auto absent = [&](const char *dll, const char *name, int args, uint32_t result) {
        process.provideStdcall(dll, name, args, [result](x86::Cpu &) { return result; });
    };
    absent("DPLAYX.dll", "#4", 5, 0x88770154); // DirectPlayLobbyCreateA: DPERR_UNAVAILABLE
    absent("WINMM.dll", "joyGetNumDevs", 0, 0);
    absent("WINMM.dll", "joyGetPos", 2, 167);      // JOYERR_UNPLUGGED
    absent("WINMM.dll", "joyGetPosEx", 2, 167);    // JOYERR_UNPLUGGED
    absent("WINMM.dll", "joyGetDevCapsA", 3, 165); // JOYERR_PARMS
    absent("WINMM.dll", "auxGetNumDevs", 0, 0);
    absent("WINMM.dll", "auxGetDevCapsA", 3, 2); // MMSYSERR_BADDEVICEID
    absent("WINMM.dll", "auxGetVolume", 2, 2);
    absent("WINMM.dll", "auxSetVolume", 2, 2);
    absent("WINMM.dll", "mciSendCommandA", 4, 275); // MCIERR_CANNOT_LOAD_DRIVER
    absent("WINMM.dll", "waveOutOpen", 6, 2);       // MMSYSERR_BADDEVICEID
    for (const char *name : {"waveOutClose", "waveOutReset"})
        absent("WINMM.dll", name, 1, 5); // MMSYSERR_INVALHANDLE
    for (const char *name : {"waveOutPrepareHeader", "waveOutUnprepareHeader", "waveOutWrite"})
        absent("WINMM.dll", name, 3, 5);
    absent("WINMM.dll", "waveOutSetVolume", 2, 5);
    absent("WINMM.dll", "waveOutGetID", 2, 5);
}

} // namespace

GameProcess::GameProcess(const GameFolders &folders, const std::filesystem::path &saveFolder,
                         const std::string &commandLine)
    : _folders(folders), _cpu(std::make_unique<x86::Cpu>(256u << 20)) {
    std::error_code ec;
    std::filesystem::create_directories(saveFolder, ec);
    _process = std::make_unique<x86::Win32Process>(*_cpu, "JK.EXE", true, true);
    x86::Win32Process &p = *_process;
    p.setProgram("JK.EXE", "C:\\");
    p.setCommandLine("JK.EXE" + std::string(commandLine.empty() ? "" : " ") + commandLine);
    p.addDrive({'C', folders.install, saveFolder, 3, "JEDI", 0x12345678});
    if (!folders.cds[0].empty())
        p.addDrive({'D', folders.cds[0], {}, 5, "JEDI_KNIGHT_1", 0x0ced1});
    p.setRegistryString(kRegistryKey, "CD Path", "D:\\");
    p.setRegistryString(kRegistryKey, "Install Path", "C:\\");
    // The program's and Smacker's threads each on a thread of this machine.
    p.useHostThreads();
    _directX = std::make_unique<x86::DirectX>(p);
    // The menus and the software renderer draw in 8-bit colour.
    _directX->modes.insert(_directX->modes.begin(), {{640, 480, 8}, {800, 600, 8}, {1024, 768, 8}});
    _directInput = std::make_unique<x86::DirectInput>(p);
    _directPlay = std::make_unique<x86::DirectPlay>(p);
    _directSound = std::make_unique<x86::DirectSound>(p);
    addAbsentDevices(p);
}

GameProcess::~GameProcess() = default;

Error GameProcess::load() {
    std::vector<uint8_t> file;
    const x86::Win32Process::Module *smack = nullptr;
    if (const auto path = fs::findFile(_folders.install, "SMACKW32.DLL")) {
        if (!readFile(*path, file))
            return Error::fail("can't read " + path->string());
        if (Error e = _process->load("SMACKW32.DLL", file, smack))
            return Error::fail("SMACKW32.DLL: " + e.message);
    }
    const auto exePath = fs::findFile(_folders.install, "JK.EXE");
    if (!exePath)
        return Error::fail("Jedi Knight's program, JK.EXE, isn't in " + _folders.install.string());
    if (!readFile(*exePath, file))
        return Error::fail("can't read " + exePath->string());
    if (Error e = _process->load("JK.EXE", file, _exe))
        return Error::fail("JK.EXE: " + e.message);
    if (smack)
        if (Error e = _process->attach(*smack))
            return e;
    return Error::success();
}

Error GameProcess::run(uint32_t &exitCode) {
    if (!_exe)
        return Error::fail("JK.EXE isn't loaded");
    return _process->run(*_exe, exitCode);
}

} // namespace reluva::sith
