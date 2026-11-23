// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Sith command for running the retail JK.EXE in the x86
// emulator: how far it gets, and what it asks for that nothing stands in
// for (docs/engines/sith/games/jk/systems/hosting.md).

#include "common/fs.h"
#include "common/stream.h"
#include "devtools/tool.h"
#include "common/pe.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/disasm.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/win32.h"
#include "engines/sith/folders.h"
#include "image/image.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <functional>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace reluva::tool;
using reluva::Error;
namespace x86 = reluva::x86;
namespace sith = reluva::sith;

constexpr const char *kRegistryKey = "HKEY_LOCAL_MACHINE\\Software\\LucasArts Entertainment Company\\JediKnight\\v1.0";

// What a machine of the time without the device answers: no sound card, no
// joystick, no CD audio, no network service provider. The program learns
// it has none, as it would there.
void addAbsentDevices(x86::Win32Process &process, std::map<std::string, int> &calls) {
    auto absent = [&](const char *dll, const char *name, int args, uint32_t result) {
        const std::string key = std::string(dll) + "!" + name;
        process.provideStdcall(dll, name, args, [&calls, key, result](x86::Cpu &) {
            ++calls[key];
            return result;
        });
    };
    // DSERR_NODRIVER: no sound device.
    absent("DSOUND.dll", "DirectSoundCreate", 3, 0x88780078);
    // DirectPlayLobbyCreateA by its ordinal: DPERR_UNAVAILABLE.
    absent("DPLAYX.dll", "#4", 5, 0x88770154);
    // No joysticks, no mixer lines; MCI and waveOut fail as with no device.
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

// Calls answered simply, as Windows would for a program that only asks.
void addSimple(x86::Win32Process &process, std::map<std::string, int> &calls) {
    auto simple = [&](const char *dll, const char *name, int args, const std::function<uint32_t(x86::Cpu &)> &fn) {
        const std::string key = std::string(dll) + "!" + name;
        process.provideStdcall(dll, name, args, [&calls, key, fn](x86::Cpu &c) {
            ++calls[key];
            return fn(c);
        });
    };
    auto value = [](uint32_t v) { return [v](x86::Cpu &) { return v; }; };
    simple("COMCTL32.dll", "#17", 0, value(0)); // InitCommonControls
    simple("WINMM.dll", "timeBeginPeriod", 1, value(0));
    simple("WINMM.dll", "timeEndPeriod", 1, value(0));
    simple("KERNEL32.dll", "SetEnvironmentVariableA", 2, value(1));
    auto copyString = [](x86::Cpu &c) {
        uint32_t i = 0;
        for (uint8_t ch = 1; ch != 0 && i < (1u << 16); ++i) {
            ch = c.read8(c.arg(1) + i);
            c.write8(c.arg(0) + i, ch);
        }
        return c.arg(0);
    };
    simple("KERNEL32.dll", "lstrcpyA", 2, copyString);
    simple("KERNEL32.DLL", "lstrcpyA", 2, copyString);
    // SYSTEMTIME, from this machine's clock.
    simple("KERNEL32.dll", "GetLocalTime", 1, [](x86::Cpu &c) {
        const std::time_t now = std::time(nullptr);
        std::tm t{};
#ifdef _WIN32
        localtime_s(&t, &now);
#else
        localtime_r(&now, &t);
#endif
        const uint16_t fields[8] = {
            uint16_t(t.tm_year + 1900), uint16_t(t.tm_mon + 1), uint16_t(t.tm_wday), uint16_t(t.tm_mday),
            uint16_t(t.tm_hour),        uint16_t(t.tm_min),     uint16_t(t.tm_sec),  0};
        for (int i = 0; i < 8; ++i)
            c.write16(c.arg(0) + uint32_t(i) * 2, fields[i]);
        return 0u;
    });
    // Local time is taken to be UTC: a FILETIME copied, a time zone of none.
    simple("KERNEL32.dll", "FileTimeToLocalFileTime", 2, [](x86::Cpu &c) {
        c.write32(c.arg(1), c.read32(c.arg(0)));
        c.write32(c.arg(1) + 4, c.read32(c.arg(0) + 4));
        return 1u;
    });
    simple("KERNEL32.dll", "GetTimeZoneInformation", 1, [](x86::Cpu &c) {
        for (uint32_t i = 0; i < 172; i += 4)
            c.write32(c.arg(0) + i, 0);
        return 0u; // TIME_ZONE_ID_UNKNOWN
    });
    // FILETIME (100 ns since 1601) to SYSTEMTIME, by the civil calendar.
    simple("KERNEL32.dll", "FileTimeToSystemTime", 2, [](x86::Cpu &c) {
        const uint64_t ticks = uint64_t(c.read32(c.arg(0) + 4)) << 32 | c.read32(c.arg(0));
        const uint64_t ms = ticks / 10000;
        int64_t days = int64_t(ms / 86400000) - 134774; // to days since 1970
        const uint64_t inDay = ms % 86400000;
        const int64_t weekday = (days % 7 + 11) % 7;
        days += 719468;
        const int64_t era = days / 146097;
        const int64_t doe = days - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        const int64_t day = doy - (153 * mp + 2) / 5 + 1;
        const int64_t month = mp < 10 ? mp + 3 : mp - 9;
        const int64_t year = yoe + era * 400 + (month <= 2);
        const uint16_t fields[8] = {uint16_t(year),
                                    uint16_t(month),
                                    uint16_t(weekday),
                                    uint16_t(day),
                                    uint16_t(inDay / 3600000),
                                    uint16_t(inDay / 60000 % 60),
                                    uint16_t(inDay / 1000 % 60),
                                    uint16_t(inDay % 1000)};
        for (int i = 0; i < 8; ++i)
            c.write16(c.arg(1) + uint32_t(i) * 2, fields[i]);
        return 1u;
    });
    // No console can be made; what's written to one is printed.
    simple("KERNEL32.dll", "AllocConsole", 0, value(0));
    simple("KERNEL32.dll", "FreeConsole", 0, value(1));
    simple("KERNEL32.dll", "SetConsoleTitleA", 1, value(1));
    simple("KERNEL32.dll", "SetConsoleTextAttribute", 2, value(1));
    simple("KERNEL32.dll", "SetConsoleCursorInfo", 2, value(1));
    simple("KERNEL32.dll", "SetConsoleCursorPosition", 2, value(1));
    simple("KERNEL32.dll", "FlushConsoleInputBuffer", 1, value(1));
    simple("KERNEL32.dll", "GetConsoleScreenBufferInfo", 2, value(0));
    simple("KERNEL32.dll", "FillConsoleOutputCharacterA", 5, value(1));
    simple("KERNEL32.dll", "WriteConsoleA", 5, [](x86::Cpu &c) {
        std::string text;
        for (uint32_t i = 0; i < c.arg(2); ++i)
            text.push_back(char(c.read8(c.arg(1) + i)));
        std::printf("console: %s", text.c_str());
        if (c.arg(3))
            c.write32(c.arg(3), c.arg(2));
        return 1u;
    });
    // The text of a message box in UTF-16, as ASCII; answered OK.
    simple("USER32.dll", "MessageBoxW", 4, [](x86::Cpu &c) {
        auto wide = [&c](uint32_t at) {
            std::string out;
            for (uint32_t i = 0; at && i < 4096; i += 2) {
                const uint16_t ch = c.read16(at + i);
                if (!ch)
                    break;
                out.push_back(ch < 128 ? char(ch) : '?');
            }
            return out;
        };
        std::printf("message box: %s: %s\n", wide(c.arg(2)).c_str(), wide(c.arg(1)).c_str());
        return 1u; // IDOK
    });
    simple("USER32.dll", "GetDesktopWindow", 0, value(0x10010));
    simple("USER32.dll", "ValidateRect", 2, value(1));
    simple("USER32.dll", "SetActiveWindow", 1, [](x86::Cpu &c) { return c.arg(0); });
    simple("USER32.dll", "MessageBeep", 1, value(1));
    simple("USER32.dll", "GetUpdateRect", 3, value(0));
    // A PAINTSTRUCT with nothing to paint, and the window's DC.
    const uint32_t getDc = process.standIn("USER32.dll", "GetDC");
    simple("USER32.dll", "BeginPaint", 2, [getDc](x86::Cpu &c) {
        uint32_t dc = 0;
        c.call(getDc, {c.arg(0)}, dc);
        for (uint32_t i = 0; i < 64; i += 4)
            c.write32(c.arg(1) + i, 0);
        c.write32(c.arg(1), dc);
        return dc;
    });
    simple("USER32.dll", "EndPaint", 2, value(1));
}

// JK.EXE under the emulator for up to `seconds`, from `install` with CD 1
// in drive D, saying what it opens, writes and shows.
int cmdHost(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool sith host <install> [-seconds N] [-args \"...\"] [-png file] "
                             "[-files] [-calls] [-peek address] [-write folder]\n");
        return 2;
    }
    const std::filesystem::path install = argv[2];
    double seconds = 20;
    std::string args;
    std::string png;
    bool listFiles = false;
    bool listCalls = false;
    std::vector<uint32_t> peeks;
    std::filesystem::path writable = std::filesystem::temp_directory_path() / "reluva-sith-host";
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-seconds" && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else if (a == "-args" && i + 1 < argc)
            args = argv[++i];
        else if (a == "-png" && i + 1 < argc)
            png = argv[++i];
        else if (a == "-files")
            listFiles = true;
        else if (a == "-calls")
            listCalls = true;
        else if (a == "-peek" && i + 1 < argc)
            peeks.push_back(uint32_t(std::strtoul(argv[++i], nullptr, 16)));
        else if (a == "-write" && i + 1 < argc)
            writable = argv[++i];
    }
    const auto exePath = reluva::fs::findFile(install, "JK.EXE");
    if (!exePath) {
        std::fprintf(stderr, "no JK.EXE in %s\n", install.string().c_str());
        return 1;
    }
    std::error_code ec;
    std::filesystem::create_directories(writable, ec);

    x86::Cpu cpu(256u << 20);
    x86::Win32Process process(cpu, "JK.EXE", true, true);
    process.setProgram("JK.EXE", "C:\\");
    process.setCommandLine("JK.EXE" + std::string(args.empty() ? "" : " ") + args);
    process.addDrive({'C', install, writable, 3, "JEDI", 0x12345678});
    const sith::GameFolders folders = sith::findFolders(install, {});
    if (!folders.cds[0].empty())
        process.addDrive({'D', folders.cds[0], {}, 5, "JEDI_KNIGHT_1", 0x0ced1});
    process.setRegistryString(kRegistryKey, "CD Path", "D:\\");
    process.setRegistryString(kRegistryKey, "Install Path", "C:\\");

    std::map<std::string, int> calls;
    std::vector<std::string> opened;
    process.output = [](const std::string &text) { std::printf("debug output: %s", text.c_str()); };
    process.messageBox = [](const std::string &caption, const std::string &text) {
        std::printf("message box: %s: %s\n", caption.c_str(), text.c_str());
    };
    process.fileOpened = [&](const std::string &path, bool found) {
        opened.push_back(path + (found ? "" : "  (not found)"));
    };
    process.registryWritten = [](const std::string &key, const std::string &name) {
        std::printf("registry written: %s\\%s\n", key.c_str(), name.c_str());
    };
    // The program's and Smacker's threads each on a thread of this machine.
    process.useHostThreads();
    x86::DirectX directX(process);
    x86::DirectPlay directPlay(process);
    // The menus are drawn in 8-bit colour, at 640 by 480.
    directX.modes.insert(directX.modes.begin(), {{640, 480, 8}, {800, 600, 8}, {1024, 768, 8}});
    x86::DirectInput directInput(process);
    addAbsentDevices(process, calls);
    addSimple(process, calls);

    // SMACKW32.DLL as it shipped, then the program.
    std::vector<uint8_t> file;
    const x86::Win32Process::Module *smack = nullptr;
    const x86::Win32Process::Module *exe = nullptr;
    if (const auto smackPath = reluva::fs::findFile(install, "SMACKW32.DLL")) {
        if (!reluva::readFile(*smackPath, file)) {
            std::fprintf(stderr, "can't read %s\n", smackPath->string().c_str());
            return 1;
        }
        if (Error err = process.load("SMACKW32.DLL", file, smack)) {
            std::fprintf(stderr, "SMACKW32.DLL: %s\n", err.message.c_str());
            return 1;
        }
    }
    if (!reluva::readFile(*exePath, file)) {
        std::fprintf(stderr, "can't read %s\n", exePath->string().c_str());
        return 1;
    }
    if (Error err = process.load("JK.EXE", file, exe)) {
        std::fprintf(stderr, "JK.EXE: %s\n", err.message.c_str());
        return 1;
    }
    for (const std::string &m : process.missingImports())
        std::printf("not stood in for: %s\n", m.c_str());
    if (smack)
        if (Error err = process.attach(*smack)) {
            std::printf("SMACKW32.DLL's start-up: %s\n", err.message.c_str());
            return 1;
        }

    // Stopped after `seconds` of this machine's time.
    const auto start = std::chrono::steady_clock::now();
    process.onSlice = [&](x86::Cpu &c) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > seconds)
            c.requestStop();
    };
    // Every stand-in called, and the last few in order.
    std::map<std::string, uint64_t> called;
    std::deque<std::string> lastCalled;
    if (listCalls)
        cpu.nativeTrace = [&](x86::Cpu &, const std::string &name) {
            ++called[name];
            lastCalled.push_back(name);
            if (lastCalled.size() > 40)
                lastCalled.pop_front();
        };
    uint64_t frames = 0;
    directX.presented = [&](const x86::DirectX::Frame &) { ++frames; };
    uint32_t exitCode = 0;
    const Error err = process.run(*exe, exitCode);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (listFiles)
        for (const std::string &f : opened)
            std::printf("opened: %s\n", f.c_str());
    else
        std::printf("%zu files opened (-files lists them)\n", opened.size());
    for (const auto &[name, n] : calls)
        std::printf("trial stand-in called: %s x%d\n", name.c_str(), n);
    for (const auto &[name, n] : called)
        std::printf("called: %s x%llu\n", name.c_str(), (unsigned long long)n);
    for (const std::string &name : lastCalled)
        std::printf("called last: %s\n", name.c_str());
    for (uint32_t a : peeks)
        std::printf("at %08x: %08x\n", a, cpu.valid(a, 4) ? cpu.read32(a) : 0u);
    const x86::DirectX::Frame &frame = directX.frame();
    std::printf("frames shown: %llu, the last %dx%d\n", (unsigned long long)frames, frame.width, frame.height);
    if (!png.empty() && frame.width > 0) {
        reluva::image::Image img;
        img.w = frame.width;
        img.h = frame.height;
        img.rgba.resize(size_t(img.w) * img.h * 4);
        for (size_t i = 0; i < frame.pixels.size() && i * 4 < img.rgba.size(); ++i) {
            const uint32_t p = frame.pixels[i];
            img.rgba[i * 4] = uint8_t(p >> 16);
            img.rgba[i * 4 + 1] = uint8_t(p >> 8);
            img.rgba[i * 4 + 2] = uint8_t(p);
            img.rgba[i * 4 + 3] = 255;
        }
        reluva::writeFile(png, reluva::image::encodePng(img));
    }
    if (err) {
        std::printf("stopped: %s\n", err.message.c_str());
        const auto &r = cpu.faultReg;
        std::printf("registers: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x\n", r[0], r[1],
                    r[2], r[3], r[4], r[5], r[6], r[7]);
        // Return addresses on the stack: the words that point into the program's code.
        std::printf("on the stack, into JK.EXE's code:");
        for (uint32_t i = 0, shown = 0; i < 256 && shown < 12; ++i) {
            const uint32_t at = r[4] + i * 4;
            if (at + 4 > cpu.memory().size())
                break;
            const uint32_t v = cpu.read32(at);
            if (v >= 0x401000 && v < 0x522000) {
                std::printf(" %08x", v);
                ++shown;
            }
        }
        std::printf("\n");
    } else {
        std::printf("the program ended, exit code %u\n", exitCode);
    }
    std::printf("%llu instructions in %.1f s (%.1f million a second)\n", (unsigned long long)cpu.executed(), took,
                took > 0 ? double(cpu.executed()) / took / 1e6 : 0.0);
    return err ? 1 : 0;
}

// JK.EXE's code from an address on, an instruction a line.
int cmdDisasm(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool sith disasm <JK.EXE> <address> [count]\n");
        return 2;
    }
    std::vector<uint8_t> file;
    reluva::PeImage pe;
    if (!reluva::readFile(argv[2], file) || reluva::parsePe(file, pe)) {
        std::fprintf(stderr, "can't read %s\n", argv[2]);
        return 1;
    }
    uint32_t address = uint32_t(std::strtoul(argv[3], nullptr, 16));
    const int count = argc > 4 ? std::atoi(argv[4]) : 30;
    for (int i = 0; i < count; ++i) {
        const uint32_t rva = address - uint32_t(pe.imageBase);
        const reluva::PeImage::Section *in = nullptr;
        for (const auto &sec : pe.sections)
            if (rva >= sec.virtualAddress && rva < sec.virtualAddress + std::max(sec.virtualSize, sec.rawSize))
                in = &sec;
        if (!in || rva - in->virtualAddress >= in->rawSize)
            break;
        const size_t at = in->rawOffset + (rva - in->virtualAddress);
        const x86::Instruction ins = x86::disassemble(&file[at], file.size() - at, address);
        std::printf("%08x  %-8s %s\n", address, ins.mnemonic.c_str(), ins.operands.c_str());
        address += uint32_t(ins.size);
    }
    return 0;
}

const Commands kCommands("sith",
                         {
                             {"host",
                              "<install> [-seconds N] [-args \"...\"] [-png file] [-files] [-calls] [-peek address]",
                              "run the retail JK.EXE in the x86 emulator: how far it gets", cmdHost},
                             {"disasm", "<JK.EXE> <address> [count]",
                              "the program's instructions from an address (hexadecimal) on", cmdDisasm},
                         });

} // namespace
