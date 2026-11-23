// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool storm3d host <game folder> [-ms N] [-frames dir] [-every N] [-save dir]:
// runs the retail Armada.exe in Reluva's x86 emulator, with its own DLLs
// from the install and Reluva's stand-ins for Windows, DirectX and the C
// runtimes, as a reference to study, never as the game is played
// (docs/engines/storm3d/games/armada/systems/hosting.md). It reports the
// files the program opens, the frames it shows (written as PNG), the
// imports no stand-in answers, and where and why it stops.

#include "common/fs.h"
#include "common/pe.h"
#include "common/text.h"
#include "devtools/tool.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/msvcp60.h"
#include "emulation/x86/msvcrt.h"
#include "emulation/x86/win32.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace reluva;

constexpr const char *kProgram = "Armada.exe";
// Where the program believes it's installed: the installer's default.
constexpr const char *kInstallFolder = "C:\\Program Files\\Activision\\Star Trek Armada";
constexpr uint32_t kMemoryMb = 256;

struct Options {
    std::filesystem::path dir;
    uint64_t limitMs = 20000; // of the program's clock
    std::filesystem::path framesDir;
    uint64_t every = 1; // with framesDir: every Nth frame written
    std::filesystem::path saveDir;
};

Error readFile(const std::filesystem::path &path, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Cannot read " + path.string());
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return Error::success();
}

// Loads the program and each DLL of the install it imports from, the
// imported ones first, with Reluva's C++ library before all; the DLLs, in
// that order, for their start-ups.
Error loadModules(x86::Win32Process &process, const std::filesystem::path &dir,
                  std::vector<const x86::Win32Process::Module *> &dlls) {
    struct Pending {
        std::string name;
        std::vector<uint8_t> file;
        std::vector<std::string> needs; // lower-case names of the install's DLLs it imports from
    };
    std::map<std::string, Pending> found;
    std::function<Error(const std::string &)> find = [&](const std::string &name) -> Error {
        const std::string key = text::toLower(name);
        if (found.count(key))
            return Error::success();
        const auto path = fs::findFile(dir, name);
        if (!path)
            return Error::success(); // Windows', stood in for
        Pending p;
        p.name = path->filename().string();
        if (Error err = readFile(*path, p.file))
            return err;
        PeImage pe;
        if (Error err = parsePe(p.file, pe))
            return Error::fail(p.name + ": " + err.message);
        found[key] = std::move(p);
        for (const PeImage::ImportedDll &dll : pe.imports)
            if (fs::findFile(dir, dll.name)) {
                found[key].needs.push_back(text::toLower(dll.name));
                if (Error err = find(dll.name))
                    return err;
            }
        return Error::success();
    };
    if (Error err = find(kProgram))
        return err;
    const x86::Win32Process::Module *library = nullptr;
    if (Error err = x86::loadMsvcp60(process, library))
        return err;
    std::set<std::string> done;
    std::function<Error(const std::string &, int)> place = [&](const std::string &key, int depth) -> Error {
        if (done.count(key))
            return Error::success();
        if (depth > 64)
            return Error::fail("The modules import from each other in a loop");
        Pending &p = found.at(key);
        for (const std::string &need : p.needs)
            if (Error err = place(need, depth + 1))
                return err;
        done.insert(key);
        const x86::Win32Process::Module *m = nullptr;
        if (Error err = process.load(p.name, p.file, m))
            return err;
        std::printf("loaded %-18s at %08x\n", p.name.c_str(), m->base);
        if (key != text::toLower(kProgram))
            dlls.push_back(m);
        return Error::success();
    };
    return place(text::toLower(kProgram), 0);
}

void writeFrame(const x86::DirectX::Frame &f, const std::filesystem::path &path) {
    image::Image img;
    img.w = f.width;
    img.h = f.height;
    img.rgba.resize(size_t(f.width) * size_t(f.height) * 4);
    for (size_t i = 0; i < f.pixels.size() && i * 4 + 3 < img.rgba.size(); ++i) {
        const uint32_t p = f.pixels[i];
        img.rgba[i * 4] = uint8_t(p >> 16);
        img.rgba[i * 4 + 1] = uint8_t(p >> 8);
        img.rgba[i * 4 + 2] = uint8_t(p);
        img.rgba[i * 4 + 3] = 255;
    }
    const std::vector<uint8_t> png = image::encodePng(img);
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
}

int cmdHost(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: reluva-tool storm3d host <game folder> [-ms N] [-frames dir] [-every N] [-save dir]\n");
        return 2;
    }
    Options o;
    o.dir = argv[2];
    for (int a = 3; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-ms" && a + 1 < argc)
            o.limitMs = std::strtoull(argv[++a], nullptr, 10);
        else if (arg == "-frames" && a + 1 < argc)
            o.framesDir = argv[++a];
        else if (arg == "-every" && a + 1 < argc)
            o.every = std::max<uint64_t>(1, std::strtoull(argv[++a], nullptr, 10));
        else if (arg == "-save" && a + 1 < argc)
            o.saveDir = argv[++a];
        else {
            std::fprintf(stderr, "what is %s?\n", arg.c_str());
            return 2;
        }
    }
    if (!fs::findFile(o.dir, kProgram)) {
        std::fprintf(stderr, "%s isn't in %s\n", kProgram, o.dir.string().c_str());
        return 1;
    }
    // What the program writes goes to a folder of its own, never the install.
    if (o.saveDir.empty())
        o.saveDir = std::filesystem::temp_directory_path() / "reluva-armada-host";
    std::error_code ec;
    std::filesystem::create_directories(o.saveDir, ec);
    if (!o.framesDir.empty())
        std::filesystem::create_directories(o.framesDir, ec);

    auto cpu = std::make_unique<x86::Cpu>(kMemoryMb << 20);
    x86::Win32Process process(*cpu, kProgram, true, true);
    // A whole program: its threads on host threads, with the stand-ins for
    // threads, waits and thread-local storage that come with them.
    process.useHostThreads();
    process.setProgram(kProgram, kInstallFolder);
    process.mapPath = [&](const std::string &windowsPath, bool forWriting) -> std::optional<std::filesystem::path> {
        const std::string folder = text::toLower(kInstallFolder);
        if (text::toLower(windowsPath).rfind(folder, 0) != 0)
            return std::nullopt;
        std::string rest = windowsPath.substr(folder.size());
        std::replace(rest.begin(), rest.end(), '\\', '/');
        while (!rest.empty() && (rest.front() == '/'))
            rest.erase(rest.begin());
        while (!rest.empty() && rest.back() == '/')
            rest.pop_back();
        if (forWriting) {
            if (rest.empty())
                return std::nullopt;
            std::filesystem::create_directories((o.saveDir / rest).parent_path(), ec);
            return o.saveDir / rest;
        }
        if (rest.empty())
            return o.dir;
        if (auto file = fs::findFile(o.saveDir, rest))
            return file;
        if (auto file = fs::findFile(o.dir, rest))
            return file;
        return fs::findDirectory(o.dir, rest);
    };
    std::vector<std::pair<std::string, bool>> opened;
    process.fileOpened = [&](const std::string &path, bool found) { opened.push_back({path, found}); };
    process.output = [](const std::string &text) { std::printf("debug output: %s", text.c_str()); };
    x86::addMsvcrt(process);
    // DirectSoundCreate (DSOUND.dll's first export) fails as on a machine
    // with no sound device: the reference runs silent.
    process.provide("DSOUND.dll", "#1", [](x86::Cpu &c) {
        if (c.arg(1) && c.valid(c.arg(1), 4))
            c.write32(c.arg(1), 0);
        c.reg[x86::Cpu::EAX] = 0x88780078; // DSERR_NODRIVER
        return 12u;
    });
    x86::DirectX directX(process);
    x86::DirectInput directInput(process);
    directX.presentLate = false;
    uint64_t frames = 0;
    directX.presented = [&](const x86::DirectX::Frame &f) {
        ++frames;
        if (!o.framesDir.empty() && f.number % o.every == 0)
            writeFrame(f, o.framesDir / text::format("frame%05llu.png", (unsigned long long)f.number));
    };
    process.onSlice = [&](x86::Cpu &) {
        if (process.now() >= o.limitMs)
            process.requestExit();
    };

    std::vector<const x86::Win32Process::Module *> dlls;
    if (Error err = loadModules(process, o.dir, dlls)) {
        std::printf("not loaded: %s\n", err.message.c_str());
        return 1;
    }
    std::set<std::string> missing(process.missingImports().begin(), process.missingImports().end());
    std::printf("%zu imports not stood in for (a call to one stops the run):\n", missing.size());
    for (const std::string &m : missing)
        std::printf("  %s\n", m.c_str());
    for (const x86::Win32Process::Module *m : dlls)
        if (Error err = process.attach(*m)) {
            std::printf("stopped in %s's start-up: %s\n", m->name.c_str(), err.message.c_str());
            return 1;
        }

    uint32_t exitCode = 0;
    const Error err = process.run(*process.module(kProgram), exitCode);
    std::printf("files opened:\n");
    std::set<std::string> seen;
    for (const auto &[path, found] : opened)
        if (seen.insert(path).second)
            std::printf("  %s %s\n", found ? "  " : "no", path.c_str());
    std::printf("%llu frames shown; program clock %llu ms\n", (unsigned long long)frames,
                (unsigned long long)process.now());
    if (directX.frame().width > 0 && !o.framesDir.empty())
        writeFrame(directX.frame(), o.framesDir / "last.png");
    if (err) {
        std::printf("stopped: %s\n", err.message.c_str());
        return 1;
    }
    std::printf("ended: exit code %u\n", exitCode);
    return 0;
}

const tool::Commands kCommands("storm3d", {
                                              {"host", "<game folder> [-ms N] [-frames dir] [-every N] [-save dir]",
                                               "run the retail Armada.exe in the x86 emulator, to study: files "
                                               "opened, frames shown, imports missing, where it stops",
                                               cmdHost},
                                          });

} // namespace
