// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/host/game_process.h"

#include "common/fs.h"
#include "common/stream.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>
#include <tuple>
#include <vector>

namespace reluva::sith {

namespace {

// REG_BINARY, as the game keeps all its values.
constexpr uint32_t kBinary = 3;
// The values of Setup's display and 3D (setDirect3D).
constexpr const char *kDisplayValues[] = {"b3DAccel", "displayDeviceGUID", "displayMode", "3DDeviceGUID"};

// What a machine of the time without the device answers: no waveOut
// device. The program learns it has none, as it would there. (DirectPlay's
// lobby is DirectPlay's own: it tells the game no lobby started it.)
void addAbsentDevices(x86::Win32Process &process) {
    auto absent = [&](const char *dll, const char *name, int args, uint32_t result) {
        process.provideStdcall(dll, name, args, [result](x86::Cpu &) { return result; });
    };
    absent("WINMM.dll", "waveOutOpen", 6, 2); // MMSYSERR_BADDEVICEID
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
    : _folders(folders), _game(folders.game ? *folders.game : games().front()),
      _registryKey(std::string("HKEY_LOCAL_MACHINE\\") + _game.registryKey),
      _cpu(std::make_unique<x86::Cpu>(256u << 20)) {
    std::error_code ec;
    std::filesystem::create_directories(saveFolder, ec);
    _process = std::make_unique<x86::Win32Process>(*_cpu, _game.program, true, true);
    x86::Win32Process &p = *_process;
    p.setProgram(_game.program, "C:\\");
    p.setCommandLine(_game.program + std::string(commandLine.empty() ? "" : " ") + commandLine);
    p.addDrive({'C', folders.install, saveFolder, 3, "JEDI", 0x12345678});
    insertCd(1);
    // The settings the game keeps in the registry (its display, its last
    // player...), as it left them; then where it's installed, as its
    // installer would have them.
    _registryFile = saveFolder / "registry.txt";
    loadRegistry();
    p.setRegistryString(_registryKey, "CD Path", "D:\\");
    p.setRegistryString(_registryKey, "Install Path", "C:\\");
    p.registryWritten = [this](const std::string &key, const std::string &name) {
        if (fs::toLower(key) != fs::toLower(_registryKey))
            return;
        if (std::find(_registryNames.begin(), _registryNames.end(), name) == _registryNames.end())
            _registryNames.push_back(name);
        saveRegistry();
    };
    // The program's and Smacker's threads each on a thread of this machine.
    p.useHostThreads();
    _directX = std::make_unique<x86::DirectX>(p);
    // The menus and the software renderer draw in 8-bit colour.
    _directX->modes.insert(_directX->modes.begin(), {{640, 480, 8}, {800, 600, 8}, {1024, 768, 8}});
    _directInput = std::make_unique<x86::DirectInput>(p);
    _directPlay = std::make_unique<x86::DirectPlay>(p);
    // No joystick until the player's controller is connected (Player).
    _joysticks = std::make_unique<x86::Joysticks>(p);
    // The network answered between the game's own calls to DirectPlay too,
    // so players can join a host whose menus aren't asking; and a captured
    // joystick's messages posted, though the game captures none.
    p.onSlice = [this](x86::Cpu &) {
        const int64_t now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        int64_t last = _networkAt.load();
        if (now - last >= 20 && _networkAt.compare_exchange_strong(last, now)) {
            _directPlay->update();
            _joysticks->update(uint32_t(now));
        }
    };
    _directSound = std::make_unique<x86::DirectSound>(p);
    // The music of the CD in drive D: its audio tracks, from a cue sheet and
    // its image, or ripped, beside its files.
    _cdAudio = std::make_unique<x86::CdAudio>(p);
    _cdAudio->disc = [this] {
        return _cdInDrive ? x86::CdAudio::discIn(_folders.cds[size_t(_cdInDrive - 1)]) : x86::CdAudio::Disc();
    };
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
    const auto exePath = fs::findFile(_folders.install, _game.program);
    if (!exePath)
        return Error::fail(std::string("The game's program, ") + _game.program + ", isn't in " +
                           _folders.install.string());
    if (!readFile(*exePath, file))
        return Error::fail("can't read " + exePath->string());
    if (Error e = _process->load(_game.program, file, _exe))
        return Error::fail(std::string(_game.program) + ": " + e.message);
    hookCdCheck();
    if (_widescreen)
        hookFocalLength();
    if (smack)
        if (Error e = _process->attach(*smack))
            return e;
    return Error::success();
}

// The registry file: a value a line, its name, type and bytes in hex,
// separated by tabs.
void GameProcess::loadRegistry() {
    std::ifstream f(_registryFile);
    std::string line;
    while (std::getline(f, line)) {
        const size_t a = line.find('\t');
        const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos)
            continue;
        const std::string name = line.substr(0, a);
        const uint32_t type = uint32_t(std::strtoul(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10));
        const std::string hex = line.substr(b + 1);
        std::vector<uint8_t> data;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            data.push_back(uint8_t(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
        _registryNames.push_back(name);
        _process->setRegistryValue(_registryKey, name, type, std::move(data));
    }
}

void GameProcess::saveRegistry() {
    std::ofstream f(_registryFile);
    for (const std::string &name : _registryNames) {
        uint32_t type = 0;
        auto value = _process->registryValue(_registryKey, name, &type);
        if (name == "CD Path" || name == "Install Path")
            continue;
        for (const auto &[kept, keptValue] : _keptValues)
            if (name == kept)
                value = keptValue, type = kBinary;
        if (!value)
            continue;
        f << name << '\t' << type << '\t';
        static const char kHex[] = "0123456789abcdef";
        for (uint8_t byte : *value)
            f << kHex[byte >> 4] << kHex[byte & 15];
        f << '\n';
    }
}

bool GameProcess::insertCd(int number) {
    if (number < 1 || number > kCdCount || _folders.cds[size_t(number - 1)].empty())
        return false;
    if (number == _cdInDrive)
        return true;
    _process->addDrive({'D',
                        _folders.cds[size_t(number - 1)],
                        {},
                        5,
                        _game.cdLabels[size_t(number - 1)],
                        0x0ced0u + uint32_t(number)});
    _cdInDrive = number;
    if (_cdAudio)
        _cdAudio->discChanged();
    return true;
}

namespace {

// Where the host reaches into each program, as the US releases have them.
struct ProgramCode {
    const char *program;
    // The CD check: where it starts, its first instructions (the first six
    // a sub esp, which the trampoline runs in its place), what its first
    // argument asks for (the CD to put in the drive, 0 for none), and
    // whether its second is the drive to search after, to be searched from
    // the first instead.
    uint32_t cdCheck;
    uint8_t cdCheckStart[8];
    int (*cdFor)(uint32_t argument);
    bool fromFirstDrive;
    // The saving of Setup's settings to the registry, a step of the
    // program's end, and its first instruction: mov eax, [...]; push eax.
    uint32_t saveSettings;
    uint8_t saveSettingsStart[6];
    // Each frame's check, before the world is moved, that more than 20 ms
    // have passed since it last was: mov eax, [last update]; add eax, 20;
    // cmp esi, eax. Unused places 0.
    uint32_t worldCheck[3];
    uint32_t lastUpdate;
    // In the 3D's focal length, half the view's width over the tangent of
    // half its field of view: the call of the tangent, and its function.
    uint32_t tangentCall;
    uint32_t tangent;
};

constexpr ProgramCode kPrograms[] = {
    // CD check (0x40ebb0): the CD a step of the episode is on, 0 for
    // either; it reads gamedata\resource\jk_.cd from each CD drive and, if
    // it isn't that CD's, asks for it and waits. Play's frame (0x403d40)
    // moves the world with 0x4c4c00. (docs/engines/sith/games/jk/systems/hosting.md)
    {"JK.EXE",
     0x40ebb0,
     {0x81, 0xec, 0x8c, 0x01, 0x00, 0x00, 0x53, 0x55}, // sub esp, 0x18c; push ebx; push ebp
     [](uint32_t a) { return int(a); },
     false,
     0x4148f0,
     {0xa1, 0x88, 0x65, 0x55, 0x00, 0x50},
     {0x403d62, 0, 0},
     0x552b9c,
     0x4436e6, // in 0x443670
     0x432396},
    // CD check (0x410db0): 2 for Jedi Knight's CD 2, which it asks for
    // once, 0x13 for its own; it looks from the drive after its second
    // argument's letter, so right after finding Jedi Knight's CD it would
    // miss its own in the same drive. Three kinds of frame (0x403c80,
    // 0x4042e0 and one ending at 0x404662) each move the world with 0x510b80.
    // (docs/engines/sith/games/mots/README.md)
    {"JKM.EXE",
     0x410db0,
     {0x81, 0xec, 0x5c, 0x02, 0x00, 0x00, 0x53, 0x8b}, // sub esp, 0x25c; push ebx; mov ebx, [esp + 0x264]
     [](uint32_t a) { return a == 2 ? 2 : 1; },
     true,
     0x417560,
     {0xa1, 0xa0, 0x11, 0x5b, 0x00, 0x50},
     {0x403cd6, 0x404302, 0x40455f},
     0x5ad42c,
     0x447e26, // in 0x447db0
     0x436386},
};

} // namespace

const void *GameProcess::code() const {
    for (const ProgramCode &c : kPrograms)
        if (fs::toLower(c.program) == fs::toLower(_game.program))
            return &c;
    return nullptr;
}

bool GameProcess::matches(uint32_t at, const uint8_t *bytes, size_t n) const {
    for (uint32_t i = 0; i < n; ++i)
        if (!_cpu->valid(at + i, 1) || _cpu->read8(at + i) != bytes[i])
            return false;
    return true;
}

bool GameProcess::saveSettings() {
    const auto *code = static_cast<const ProgramCode *>(this->code());
    if (!code || !matches(code->saveSettings, code->saveSettingsStart, sizeof code->saveSettingsStart))
        return false;
    x86::Cpu &c = *_cpu;
    // The stopped thread's stack may be anywhere: a fresh one for the call.
    constexpr uint32_t kStack = 0x10000;
    const uint32_t stack = _process->memory().alloc(kStack);
    if (!stack)
        return false;
    const uint32_t esp = c.reg[x86::Cpu::ESP];
    c.reg[x86::Cpu::ESP] = stack + kStack - 16;
    uint32_t result = 0;
    c.clearStop();
    const Error e = c.call(code->saveSettings, {}, result);
    c.reg[x86::Cpu::ESP] = esp;
    _process->memory().free(stack);
    return !e;
}

bool GameProcess::useDirect3D() {
    const auto on = _process->registryValue(_registryKey, "b3DAccel");
    if (on && on->size() >= 4 && (*on)[0] != 0)
        return false;
    setDirect3D(3); // of the modes it lists: 640x480, 16-bit
    return true;
}

bool GameProcess::useWidescreen(int width) {
    if (width <= 640 || _widescreen)
        return false;
    // The game sorts the modes it's offered by their bits a pixel, then
    // width, then height (0x427b00), and keeps its choice as an index
    // into that list.
    const x86::DirectX::Mode wide{width, 480, 16};
    auto &modes = _directX->modes;
    const auto before = std::count_if(modes.begin(), modes.end(), [&](const x86::DirectX::Mode &m) {
        return std::tie(m.bits, m.width, m.height) < std::tie(wide.bits, wide.width, wide.height);
    });
    modes.push_back(wide);
    // The player's own display, kept for the sessions without widescreen,
    // where that index is another mode.
    for (const char *name : kDisplayValues)
        _keptValues.emplace_back(name, _process->registryValue(_registryKey, name));
    setDirect3D(uint32_t(before));
    _widescreen = true;
    return true;
}

void GameProcess::setDirect3D(uint32_t mode) {
    x86::Win32Process &p = *_process;
    p.setRegistryValue(_registryKey, "b3DAccel", kBinary, {1, 0, 0, 0});
    p.setRegistryValue(_registryKey, "displayDeviceGUID", kBinary, std::vector<uint8_t>(16, 0)); // the primary
    p.setRegistryValue(_registryKey, "displayMode", kBinary, {uint8_t(mode), uint8_t(mode >> 8), 0, 0});
    // IID_IDirect3DHALDevice, {84e63de0-46aa-11cf-816f-0000c020156e}
    p.setRegistryValue(_registryKey, "3DDeviceGUID", kBinary,
                       {0xe0, 0x3d, 0xe6, 0x84, 0xaa, 0x46, 0xcf, 0x11, 0x81, 0x6f, 0, 0, 0xc0, 0x20, 0x15, 0x6e});
}

bool GameProcess::updateEveryFrame() {
    const auto *code = static_cast<const ProgramCode *>(this->code());
    if (!code)
        return false;
    const uint8_t last[4] = {uint8_t(code->lastUpdate), uint8_t(code->lastUpdate >> 8), uint8_t(code->lastUpdate >> 16),
                             uint8_t(code->lastUpdate >> 24)};
    const uint8_t expected[] = {0xa1, last[0], last[1], last[2], last[3], 0x83, 0xc0, 0x14, 0x3b, 0xf0};
    for (uint32_t at : code->worldCheck)
        if (at && !matches(at, expected, sizeof expected))
            return false;
    // More than 9 ms: at least 10, the shortest step its clock takes as it
    // is, below which the game would run fast.
    for (uint32_t at : code->worldCheck)
        if (at)
            _cpu->write8(at + 7, 9);
    return true;
}

// The tangent the focal length is worked out from made larger by as much
// as the screen is wider than 4:3, so the view keeps the height a 4:3
// screen shows and sees more at the sides. The field of view the game
// keeps, which its zoom reads back, stays as it set it.
void GameProcess::hookFocalLength() {
    const auto *code = static_cast<const ProgramCode *>(this->code());
    if (!code || !_cpu->valid(code->tangentCall, 5) || _cpu->read8(code->tangentCall) != 0xe8 ||
        code->tangentCall + 5 + _cpu->read32(code->tangentCall + 1) != code->tangent)
        return;
    x86::Cpu &c = *_cpu;
    const uint32_t trampoline = _process->memory().alloc(32);
    const uint32_t factor = trampoline + 24;
    // Worked out at each call: the game's Setup can change the mode.
    const uint32_t native = c.addNative("focal length's widening", [this, factor](x86::Cpu &cpu) {
        const x86::Win32Process &p = *_process;
        float k = 1;
        if (p.screenWidth > 0 && p.screenHeight > 0)
            k = std::max(1.0f, float(p.screenWidth) * 3 / (float(p.screenHeight) * 4));
        uint32_t bits = 0;
        std::memcpy(&bits, &k, 4);
        cpu.write32(factor, bits);
        return 0u;
    });
    if (!native || !trampoline)
        return;
    // push [esp + 4]; call tangent; add esp, 4; call native; fmul [factor]; ret
    const uint8_t bytes[] = {0xff, 0x74, 0x24, 0x04, 0xe8, 0,    0,    0, 0, 0x83, 0xc4, 0x04,
                             0xe8, 0,    0,    0,    0,    0xd8, 0x0d, 0, 0, 0,    0,    0xc3};
    for (uint32_t i = 0; i < sizeof bytes; ++i)
        c.write8(trampoline + i, bytes[i]);
    c.write32(trampoline + 5, code->tangent - (trampoline + 9));
    c.write32(trampoline + 13, native - (trampoline + 17));
    c.write32(trampoline + 19, factor);
    c.write32(factor, 0x3f800000); // 1.0
    c.write32(code->tangentCall + 1, trampoline - (code->tangentCall + 5));
    _viewWidened = true;
}

// A trampoline at the CD check's start puts the CD it asks for in drive D
// first, as a player would have, then runs the instruction it replaced
// and goes on.
void GameProcess::hookCdCheck() {
    const auto *check = static_cast<const ProgramCode *>(code());
    if (!check || !matches(check->cdCheck, check->cdCheckStart, sizeof check->cdCheckStart))
        return;
    x86::Cpu &c = *_cpu;
    // cdecl, called from the trampoline: its return address, the check's
    // caller's, then the check's arguments.
    const uint32_t native = c.addNative("CD check", [this, check](x86::Cpu &cpu) {
        insertCd(check->cdFor(cpu.arg(1)));
        if (check->fromFirstDrive && cpu.valid(cpu.arg(2), 1))
            cpu.write8(cpu.arg(2), 0);
        return 0u;
    });
    const uint32_t trampoline = _process->memory().alloc(16);
    if (!native || !trampoline)
        return;
    const uint8_t code[16] = {0xe8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xe9, 0, 0, 0, 0};
    for (uint32_t i = 0; i < 16; ++i)
        c.write8(trampoline + i, i >= 5 && i < 11 ? check->cdCheckStart[i - 5] : code[i]);
    c.write32(trampoline + 1, native - (trampoline + 5));                 // call the native
    c.write32(trampoline + 12, (check->cdCheck + 6) - (trampoline + 16)); // jmp back after sub esp
    c.write8(check->cdCheck, 0xe9);                                       // jmp trampoline
    c.write32(check->cdCheck + 1, trampoline - (check->cdCheck + 5));
    c.write8(check->cdCheck + 5, 0x90);
}

Error GameProcess::run(uint32_t &exitCode) {
    if (!_exe)
        return Error::fail(std::string(_game.program) + " isn't loaded");
    return _process->run(*_exe, exitCode);
}

} // namespace reluva::sith
