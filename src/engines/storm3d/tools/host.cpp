// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool storm3d host <game folder> [-ms N] [-frames dir] [-every N] [-save dir] [-input file]
//                          [-cd folder] [-toc file] [-cd-label name] [-fonts folder] [-clock instructions|real] [-dump frame file] [-dump-ms ms file] [-draws frame file]:
// runs the retail Armada.exe in Reluva's x86 emulator, with its own DLLs
// from the install and Reluva's stand-ins for Windows, DirectX and the C
// runtimes, as a reference to study, never as the game is played
// (docs/engines/storm3d/games/armada/systems/hosting.md). It reports the
// files the program opens, the frames it shows (written as PNG), the
// imports no stand-in answers, and where and why it stops. The input file
// is the player's mouse and keys, by the program's clock. The CD is the
// disc's files on D:, its table of contents from the disc's cue sheet or
// media descriptor (.mds), which the game's CD check reads through MCI.
// Text is drawn in fonts found in the folders given, then the system's.
// The program's clock follows the instructions it runs, so a run, its
// input included, comes out the same each time; or it's the real one.

#include "common/cue.h"
#include "common/fs.h"
#include "common/pe.h"
#include "common/text.h"
#include "devtools/tool.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/directx/dsound.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/msvcp60.h"
#include "emulation/x86/msvcrt.h"
#include "emulation/x86/win32.h"
#include "engines/storm3d/formats/zfs_archive.h"
#include "image/image.h"

#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
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
#include <sstream>
#include <string>
#include <thread>
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
    std::filesystem::path inputFile;
    std::filesystem::path cdFolder;
    std::filesystem::path tocFile;
    std::string cdLabel = "ARMADA";
    std::vector<std::filesystem::path> fontFolders;
    bool realClock = false;
    // -dump <frame> <file>: the process's memory as the frame is shown.
    std::map<uint64_t, std::filesystem::path> dumps;
    // -draws <frame> <file>: the Direct3D draws that make that frame.
    std::map<uint64_t, std::filesystem::path> draws;
    // -dump-ms <ms> <file>: the same at a moment of the program's clock.
    std::map<uint64_t, std::filesystem::path> dumpsAt;
};

// The clock that follows the instructions run: a Pentium II of 300 MHz
// running one a cycle (guessed: as fast as the game's time asked for).
constexpr uint64_t kInstructionsPerMs = 300000;
// With it, the date starts at noon (UTC) on 15 March 2000, days after the
// program was built.
constexpr int64_t kStartDate = 953121600000;

// The most a motion moves the mouse at once: within the 640 x 480 screen
// from its centre.
constexpr int kMotionStepX = 300;
constexpr int kMotionStepY = 220;

/** What the player does, when (ms of the program's clock). */
struct InputEvent {
    uint64_t at = 0;
    enum class Kind { kMove, kMotion, kButton, kKey } kind = Kind::kMove;
    // kMove: the pointer on the screen; kMotion: how far the mouse moves;
    // kButton: 0 left, 1 right; kKey: a virtual key
    int x = 0;
    int y = 0;
    bool down = false;
};

// An input file's lines: `<ms> move <x> <y>`, `<ms> motion <dx> <dy>`
// (the mouse moved by that much), `<ms> click [<x> <y>]` (the left button,
// released 100 ms later; where the pointer is, without a place),
// `<ms> key <virtual key> [held ms]` (pressed, and released after 100 ms
// or as long as said). '#' starts a comment.
// A key's scan code, as a US keyboard gives it: 0x100 marks an extended
// key (the grey arrows, Insert, the keypad's /), which Input.map tells
// from the keypad's ("GreyRightArrow"). 0 for one not listed.
uint32_t scanCodeOf(uint32_t virtualKey) {
    static const std::map<uint32_t, uint32_t> kScanCodes = {
        {0x08, 0x0e},  {0x09, 0x0f},  {0x0d, 0x1c},  {0x10, 0x2a},  {0x11, 0x1d},  {0x12, 0x38},
        {0x1b, 0x01},  {0x20, 0x39},  {0x21, 0x149}, {0x22, 0x151}, {0x23, 0x14f}, {0x24, 0x147},
        {0x25, 0x14b}, {0x26, 0x148}, {0x27, 0x14d}, {0x28, 0x150}, {0x2d, 0x152}, {0x2e, 0x153},
        {0x6a, 0x37},  {0x6b, 0x4e},  {0x6d, 0x4a},  {0x6f, 0x135}, {0x70, 0x3b},  {0x71, 0x3c},
    };
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        static const uint8_t kLetters[26] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                             0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                             0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
        return kLetters[virtualKey - 'A'];
    }
    if (virtualKey >= '1' && virtualKey <= '9')
        return 0x02 + (virtualKey - '1');
    if (virtualKey == '0')
        return 0x0b;
    const auto it = kScanCodes.find(virtualKey);
    return it == kScanCodes.end() ? 0 : it->second;
}

Error readInput(const std::filesystem::path &path, std::vector<InputEvent> &out) {
    std::ifstream in(path);
    if (!in)
        return Error::fail("Cannot read " + path.string());
    std::string line;
    for (int number = 1; std::getline(in, line); ++number) {
        line = line.substr(0, line.find('#'));
        std::istringstream words(line);
        uint64_t at = 0;
        std::string action;
        if (!(words >> at >> action))
            continue;
        int a = 0;
        int b = 0;
        if (action == "move" && words >> a >> b) {
            out.push_back({at, InputEvent::Kind::kMove, a, b, false});
        } else if (action == "motion" && words >> a >> b) {
            // In steps the screen holds, 50 ms apart: a mission's pointer
            // hears the cursor's distance from the centre each frame, then
            // puts it back there, so a step can't go further than an edge.
            for (uint64_t step = at; a || b; step += 50) {
                const int dx = std::clamp(a, -kMotionStepX, kMotionStepX);
                const int dy = std::clamp(b, -kMotionStepY, kMotionStepY);
                out.push_back({step, InputEvent::Kind::kMotion, dx, dy, false});
                a -= dx;
                b -= dy;
            }
        } else if (action == "click") {
            if (words >> a >> b)
                out.push_back({at, InputEvent::Kind::kMove, a, b, false});
            else if (!words.eof())
                return Error::fail(path.string() + ": line " + std::to_string(number) + " isn't understood");
            out.push_back({at, InputEvent::Kind::kButton, 0, 0, true});
            out.push_back({at + 100, InputEvent::Kind::kButton, 0, 0, false});
        } else if (action == "key" && words >> a) {
            int hold = 100;
            if (!(words >> hold))
                hold = 100;
            out.push_back({at, InputEvent::Kind::kKey, a, 0, true});
            out.push_back({at + uint64_t(std::max(hold, 1)), InputEvent::Kind::kKey, a, 0, false});
        } else {
            return Error::fail(path.string() + ": line " + std::to_string(number) + " isn't understood");
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const InputEvent &l, const InputEvent &r) { return l.at < r.at; });
    return Error::success();
}

// A frame's draws as text: each draw's primitive, vertex format and
// texture, then its vertices: screen x, y, z and 1/w, colour (ARGB) and
// texture coordinates for the formats Armada uses (D3DFVF_XYZRHW with
// D3DFVF_DIFFUSE, and D3DFVF_TEX1 perhaps), else their bytes in hex.
void writeDraws(const std::vector<x86::DirectX::Draw> &draws, const std::filesystem::path &path) {
    std::ofstream out(path);
    for (size_t i = 0; i < draws.size(); ++i) {
        const x86::DirectX::Draw &d = draws[i];
        out << "draw " << i << " primitive " << d.primitive << " fvf " << text::format("%x", d.fvf) << " vertices "
            << (d.stride ? d.vertices.size() / d.stride : 0) << " indices " << d.indices.size() << " texture "
            << text::format("%x", d.texture) << ' ' << d.textureWidth << 'x' << d.textureHeight << " blend "
            << d.srcBlend << ' ' << d.destBlend << " depth " << (d.depthTest ? "test" : "-")
            << (d.depthWrite ? " write" : "") << '\n';
        const bool known = d.fvf == 0x44 || d.fvf == 0x144;
        for (size_t at = 0; d.stride && at + d.stride <= d.vertices.size(); at += d.stride) {
            const uint8_t *v = d.vertices.data() + at;
            if (!known) {
                for (uint32_t k = 0; k < d.stride; ++k)
                    out << text::format("%02x", v[k]);
                out << '\n';
                continue;
            }
            float f[6] = {};
            uint32_t colour = 0;
            std::memcpy(f, v, 16);
            std::memcpy(&colour, v + 16, 4);
            if (d.fvf == 0x144)
                std::memcpy(f + 4, v + 20, 8);
            out << text::format("  %.2f %.2f %.5f %.6f %08x", double(f[0]), double(f[1]), double(f[2]), double(f[3]),
                                colour);
            if (d.fvf == 0x144)
                out << text::format(" %.4f %.4f", double(f[4]), double(f[5]));
            out << '\n';
        }
        if (!d.indices.empty()) {
            out << "  indices";
            for (const uint16_t ix : d.indices)
                out << ' ' << ix;
            out << '\n';
        }
    }
}

Error readFile(const std::filesystem::path &path, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Cannot read " + path.string());
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return Error::success();
}

// Loads the program and each DLL of the install it imports from, the
// imported ones first, with Reluva's C++ library before all; the DLLs, in
// that order (the library too), for their start-ups.
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
    dlls.push_back(library); // started first: its DllMain builds its character table and cerr
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
        std::fprintf(
            stderr,
            "usage: reluva-tool storm3d host <game folder> [-ms N] [-frames dir] [-every N] [-save dir] "
            "[-input file] [-cd folder] [-toc file] [-cd-label name] [-fonts folder] [-clock instructions|real] [-dump frame file] [-dump-ms ms file] [-draws frame file]\n");
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
        else if (arg == "-draws" && a + 2 < argc) {
            const uint64_t frame = std::strtoull(argv[++a], nullptr, 10);
            o.draws[frame] = argv[++a];
        } else if (arg == "-dump-ms" && a + 2 < argc) {
            const uint64_t ms = std::strtoull(argv[++a], nullptr, 10);
            o.dumpsAt[ms] = argv[++a];
        } else if (arg == "-dump" && a + 2 < argc) {
            const uint64_t frame = std::strtoull(argv[++a], nullptr, 10);
            o.dumps[frame] = argv[++a];
        } else if (arg == "-every" && a + 1 < argc)
            o.every = std::max<uint64_t>(1, std::strtoull(argv[++a], nullptr, 10));
        else if (arg == "-save" && a + 1 < argc)
            o.saveDir = argv[++a];
        else if (arg == "-input" && a + 1 < argc)
            o.inputFile = argv[++a];
        else if (arg == "-cd" && a + 1 < argc)
            o.cdFolder = argv[++a];
        else if (arg == "-toc" && a + 1 < argc)
            o.tocFile = argv[++a];
        else if (arg == "-cd-label" && a + 1 < argc)
            o.cdLabel = argv[++a];
        else if (arg == "-fonts" && a + 1 < argc)
            o.fontFolders.push_back(argv[++a]);
        else if (arg == "-clock" && a + 1 < argc &&
                 (std::string(argv[a + 1]) == "real" || std::string(argv[a + 1]) == "instructions"))
            o.realClock = std::string(argv[++a]) == "real";
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
    // The clock: the instructions run, and a millisecond each time every
    // thread waits, when time would otherwise pass on its own.
    std::atomic<uint64_t> idled{0};
    // What's due by the clock (input, memory to write): from each slice,
    // and as the clock moves on while every thread waits, as a program
    // waiting in a dialog for the player does.
    std::function<void()> deliverDue;
    if (!o.realClock) {
        process.milliseconds = [&] { return uint32_t(cpu->executed() / kInstructionsPerMs + idled); };
        process.idleTick = [&] {
            ++idled;
            if (deliverDue)
                deliverDue();
            if (idled + cpu->executed() / kInstructionsPerMs >= o.limitMs)
                process.requestExit();
        };
        process.startDate = kStartDate;
    }
    process.setProgram(kProgram, kInstallFolder);
    // Fonts: the folders given, then where systems keep theirs.
    for (const char *folder : {"/usr/share/fonts", "/usr/local/share/fonts", "/Library/Fonts", "C:/Windows/Fonts"})
        o.fontFolders.push_back(folder);
    if (const char *home = std::getenv("HOME"))
        o.fontFolders.push_back(std::filesystem::path(home) / ".fonts");
    process.fontFile = [&](const std::string &face, bool bold, bool italic) {
        std::vector<uint8_t> data;
        const auto file = x86::Win32Process::findFont(o.fontFolders, face, bold, italic);
        const bool read = file && !readFile(*file, data);
        std::printf("font %s%s%s: %s\n", face.c_str(), bold ? " bold" : "", italic ? " italic" : "",
                    read ? file->string().c_str() : "not found, so its text isn't drawn");
        return data;
    };
    // The CD: its files on D:, read only.
    if (!o.cdFolder.empty())
        process.cdLabel = o.cdLabel;
    if (!o.tocFile.empty()) {
        std::vector<uint8_t> toc;
        std::string why;
        if (Error err = readFile(o.tocFile, toc)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 2;
        }
        if (text::hasSuffixIgnoreCase(o.tocFile.string(), ".mds"))
            process.disc = x86::Win32Process::DiscContents::fromMediaDescriptor(toc, why);
        else {
            std::vector<CueTrack> tracks;
            if (Error err = readCueSheet(o.tocFile, tracks))
                why = err.message;
            else
                process.disc = x86::Win32Process::DiscContents::fromCueTracks(tracks);
        }
        if (!process.disc) {
            std::fprintf(stderr, "%s: %s\n", o.tocFile.string().c_str(), why.c_str());
            return 2;
        }
        std::printf("the CD: %zu tracks, %u frames\n", process.disc->tracks.size(), process.disc->end);
    }
    process.mapPath = [&](const std::string &windowsPath, bool forWriting) -> std::optional<std::filesystem::path> {
        if (!o.cdFolder.empty() && windowsPath.size() >= 2 &&
            std::toupper(static_cast<unsigned char>(windowsPath[0])) == 'D' && windowsPath[1] == ':') {
            if (forWriting)
                return std::nullopt;
            std::string rest = windowsPath.substr(2);
            std::replace(rest.begin(), rest.end(), '\\', '/');
            while (!rest.empty() && rest.front() == '/')
                rest.erase(rest.begin());
            if (rest.empty())
                return o.cdFolder;
            if (auto file = fs::findFile(o.cdFolder, rest))
                return file;
            return fs::findDirectory(o.cdFolder, rest);
        }
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
    // What's read from inside the ZFS archives, which the program reads
    // itself: each entry a read's bytes reach, listed with the files when
    // it's first read, as "<archive> > <entry>".
    std::map<std::string, std::unique_ptr<storm3d::ZfsArchive>> archives;
    std::set<std::pair<std::string, uint32_t>> entriesRead;
    process.fileRead = [&](const std::string &path, uint64_t offset, uint32_t bytes) {
        if (!text::toLower(path).ends_with(".zfs"))
            return;
        auto it = archives.find(path);
        if (it == archives.end()) {
            auto archive = std::make_unique<storm3d::ZfsArchive>();
            const auto host = process.mapPath(path, false);
            if (!host || archive->open(*host))
                archive.reset();
            it = archives.emplace(path, std::move(archive)).first;
        }
        if (!it->second)
            return;
        for (const storm3d::ZfsArchive::Entry &e : it->second->entries())
            if (e.size && e.offset < offset + bytes && offset < uint64_t(e.offset) + e.size &&
                entriesRead.insert({path, e.index}).second)
                opened.push_back({path + " > " + e.name, true});
    };
    process.output = [](const std::string &text) { std::printf("debug output: %s", text.c_str()); };
    x86::addMsvcrt(process);
    // A silent sound card, whose sounds end when they would have: the
    // mission's scripts wait for its speech.
    x86::DirectSound directSound(process);
    x86::DirectX directX(process);
    x86::DirectInput directInput(process);
    directX.presentLate = false;
    uint64_t frames = 0;
    // The draws since the last frame shown, while one of -draws' frames may
    // be the next.
    std::vector<x86::DirectX::Draw> drawsSince;
    if (!o.draws.empty())
        directX.drawn = [&](const x86::DirectX::Draw &d) { drawsSince.push_back(d); };
    directX.presented = [&](const x86::DirectX::Frame &f) {
        ++frames;
        if (auto wanted = o.draws.find(f.number); wanted != o.draws.end())
            writeDraws(drawsSince, wanted->second);
        drawsSince.clear();
        if (auto dump = o.dumps.find(f.number); dump != o.dumps.end()) {
            std::ofstream out(dump->second, std::ios::binary);
            out.write(reinterpret_cast<const char *>(cpu->memory().data()), std::streamsize(cpu->memory().size()));
            std::printf("memory at frame %llu written to %s\n", (unsigned long long)f.number,
                        dump->second.string().c_str());
        }
        if (!o.framesDir.empty() && f.number % o.every == 0)
            writeFrame(f, o.framesDir / text::format("frame%05llu.png", (unsigned long long)f.number));
    };
    std::vector<InputEvent> input;
    if (!o.inputFile.empty())
        if (Error err = readInput(o.inputFile, input)) {
            std::fprintf(stderr, "%s\n", err.message.c_str());
            return 2;
        }
    size_t nextInput = 0;
    deliverDue = [&] {
        const uint64_t now = process.now();
        while (!o.dumpsAt.empty() && o.dumpsAt.begin()->first <= now) {
            std::ofstream out(o.dumpsAt.begin()->second, std::ios::binary);
            out.write(reinterpret_cast<const char *>(cpu->memory().data()), std::streamsize(cpu->memory().size()));
            std::printf("memory at %llu ms written to %s\n", (unsigned long long)now,
                        o.dumpsAt.begin()->second.string().c_str());
            o.dumpsAt.erase(o.dumpsAt.begin());
        }
        for (; nextInput < input.size() && input[nextInput].at <= now; ++nextInput) {
            const InputEvent &e = input[nextInput];
            if (e.kind == InputEvent::Kind::kMove)
                process.mouseMoveTo(e.x, e.y);
            else if (e.kind == InputEvent::Kind::kMotion)
                process.mouseMoveTo(process.cursorX + e.x, process.cursorY + e.y);
            else if (e.kind == InputEvent::Kind::kButton)
                process.mouseButton(e.x, e.down);
            else
                process.key(uint32_t(e.x), scanCodeOf(uint32_t(e.x)), e.down);
            std::printf("input at %llu ms: %s\n", (unsigned long long)now,
                        e.kind == InputEvent::Kind::kMove     ? text::format("move to %d, %d", e.x, e.y).c_str()
                        : e.kind == InputEvent::Kind::kMotion ? text::format("motion by %d, %d", e.x, e.y).c_str()
                        : e.kind == InputEvent::Kind::kButton
                            ? (e.down ? "button down" : "button up")
                            : text::format("key %d %s", e.x, e.down ? "down" : "up").c_str());
        }
    };
    process.onSlice = [&](x86::Cpu &) {
        deliverDue();
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

    // On the real clock, the time is also kept here: while the program
    // waits (on the player, say), its threads can go on changing over
    // without a slice ending.
    std::atomic<bool> finished{false};
    std::thread watchdog([&] {
        while (o.realClock && !finished && process.now() < o.limitMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (o.realClock)
            process.requestExit();
    });
    uint32_t exitCode = 0;
    const Error err = process.run(*process.module(kProgram), exitCode);
    finished = true;
    watchdog.join();
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

const tool::Commands kCommands(
    "storm3d",
    {
        {"host",
         "<game folder> [-ms N] [-frames dir] [-every N] [-save dir] [-input file] "
         "[-cd folder] [-toc file] [-cd-label name] [-fonts folder] [-clock instructions|real] [-dump frame file] [-dump-ms ms file] [-draws frame file]",
         "run the retail Armada.exe in the x86 emulator, to study: files "
         "opened, frames shown, imports missing, where it stops",
         cmdHost},
    });

} // namespace
