// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Need for Speed engine's reluva-tool commands for the retail game run
// in the x86 emulator: the game itself, and its routines done natively
// checked against the originals.

#include "backends/networking/sockets/socket_net.h"
#include "audio/wav.h"
#include "common/events.h"
#include "common/fs.h"
#include "image/image.h"
#include "emulation/x86/pokes.h"
#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/formats/col.h"
#include "engines/nfs/formats/fce.h"
#include "engines/nfs/formats/ffn.h"
#include "engines/nfs/formats/frd.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/host/game_host.h"
#include "engines/nfs/formats/hrz.h"
#include "engines/nfs/formats/mad.h"
#include "engines/nfs/formats/mus.h"
#include "engines/nfs/pages/play_page.h"
#include "engines/nfs/formats/refpack.h"
#include "engines/nfs/formats/text_table.h"
#include "engines/nfs/pages/viewers.h"
#include "engines/nfs/formats/viv.h"
#include "backends/graphics/sdl3/sdl_compute.h"
#include "devtools/tool.h"
#include "engines/nfs/tools/helpers.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using reluva::Error;
using reluva::nfs::ShapeBank;
using reluva::nfs::VivArchive;
using namespace reluva::nfs::tools;
namespace stdfs = std::filesystem;

// Runs the retail nfs3.exe in the x86 interpreter (Phase 4's spike) and
// reports how far it gets: the system calls it makes, the files it opens,
// what it asks of THRASH, and where it stops.
int cmdNfsHost(int argc, char **argv) {
    // GPU work, if asked for (-glidegpu): a device of its own, without a window.
    reluva::gfx::setComputeFactory(reluva::makeSdlCompute);
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs host <game folder> [-args \"nfs3.exe -d3d\"] [-limit N] [-log N] "
                             "[-save dir] [-trace N]\n");
        return 2;
    }
    reluva::nfs::GameHost::Options options;
    options.saveFolder = stdfs::temp_directory_path() / "reluva_nfs3_save";
    // As the Play page: the game told of a Voodoo 2, so that it draws its sky
    // and fog (docs/engines/nfs/games/nfs3/PLAN.md, Sky and fog).
    options.presentedRenderer = "voodoo2a.dll";
    int traceCount = 0; // traceCount: the last instructions kept, for a failure (0: none)
    int every = 50;
    std::vector<uint32_t> watch;                        // addresses at which to print the registers
    std::vector<std::pair<uint32_t, uint64_t>> counted; // addresses whose runs are counted (-count)
    int watchLimit = 50;                                // how many times, at most
    std::vector<std::pair<int, uint32_t>> watchReads;   // dwords printed at each watch: register (-1 none) + offset
    std::string dumpFile;                               // the emulated memory, written at the end
    uint32_t slice = 0;                                 // instructions between thread changes (0: the default)
    // memory whose stores are printed
    uint32_t watchMem = 0;
    uint32_t watchMemSize = 0;
    std::string keyScript;    // "ms:key,ms:key...": each pressed for 100 ms
    std::string frameLogFile; // each frame's time and instructions, as CSV
    std::string wavFile;      // what the game's sound buffers played
    std::string voicesFile;   // what the game's mixer adds, front and behind, each 100 ms (CSV)
    // instructions counted by where they run, over this range
    uint64_t hotFrom = 0;
    uint64_t hotTo = 0;
    bool gpuLog = false;
    uint64_t gpuDumpList = 0; // a list (1 the first) written out in full
    std::string gpuDumpDir;
    std::string framesDir;
    std::string glideCompareFile;    // each flip's Glide frame against the native one, as CSV
    std::string glideGpuCompareFile; // each Glide frame on the GPU against the software reference, as CSV
    uint64_t framesFrom = 0;         // the first frame shown that -frames writes
    std::string forceLogFile;        // the forces the game asks for, each time they change, as CSV
    reluva::x86::Pokes pokes;        // memory written, or saved, at moments of the run
    // -drive: a human's car steered at another car, its controls written as
    // the game unpacks them (0x4314c9, after the steering and pedals): a
    // steady pair of hands for scripted chases.
    // TARGET `road`: steered along the track's virtual road instead (its
    // points at 0x5dd948, 36 bytes, the count at 0x5dd940; col.md), at a
    // point a little ahead of the car's slice (+0x1C), GAP metres to the
    // point's right, slowed for the bends ahead: a driver that needs no other
    // car, for races run to their finish.
    struct Driver {
        uint32_t car = 0, target = 0;
        bool road = false;
        uint64_t from = 0, to = ~0ull;
        float gap = 0; // 0: full gas; else metres behind the target, at its speed (road: metres right)
        uint64_t logged = 0;
    };
    std::vector<Driver> drivers;
    float driveSign = 1; // the steering's sign (the car's right row against the keys)
    for (int a = 3; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "-args" && a + 1 < argc)
            options.commandLine = argv[++a];
        else if (arg == "-limit" && a + 1 < argc)
            options.instructionLimit = std::strtoull(argv[++a], nullptr, 0);
        else if (arg == "-log" && a + 1 < argc)
            options.callLog = std::atoi(argv[++a]);
        else if (arg == "-save" && a + 1 < argc)
            options.saveFolder = argv[++a];
        else if (arg == "-trace" && a + 1 < argc)
            traceCount = std::atoi(argv[++a]);
        else if (arg == "-frames" && a + 1 < argc)
            framesDir = argv[++a];
        else if (arg == "-watchlimit" && a + 1 < argc)
            watchLimit = std::atoi(argv[++a]);
        else if (arg == "-watch" && a + 1 < argc)
            watch.push_back(uint32_t(std::strtoul(argv[++a], nullptr, 0)));
        else if (arg == "-drive" && a + 1 < argc) {
            Driver d;
            char *at = argv[++a];
            d.car = uint32_t(std::strtoul(at, &at, 0));
            if (*at == ':' && std::strncmp(at + 1, "road", 4) == 0) {
                d.road = true;
                at += 5;
            } else if (*at == ':')
                d.target = uint32_t(std::strtoul(at + 1, &at, 0));
            if (*at == ':')
                d.from = std::strtoull(at + 1, &at, 10);
            if (*at == ':')
                d.to = std::strtoull(at + 1, &at, 10);
            if (*at == ':')
                d.gap = std::strtof(at + 1, &at);
            if (!d.car || (!d.target && !d.road))
                return fail(Error::fail("-drive: CAR:TARGET:FROMMS[:TOMS[:GAP]]"));
            drivers.push_back(d);
        } else if (arg == "-drivesign" && a + 1 < argc)
            driveSign = std::strtof(argv[++a], nullptr) < 0 ? -1.0f : 1.0f;
        else if (arg == "-count" && a + 1 < argc)
            counted.push_back({uint32_t(std::strtoul(argv[++a], nullptr, 0)), 0});
        else if (arg == "-watchread" && a + 1 < argc) { // ADDR, or reg+OFF / reg-OFF (eax ... edi)
            const std::string spec = lowerAscii(argv[++a]);
            static const char *const kRegs[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
            int r = -1;
            for (int i = 0; i < 8; ++i)
                if (spec.rfind(kRegs[i], 0) == 0)
                    r = i;
            uint32_t off = 0;
            if (r < 0)
                off = uint32_t(std::strtoul(spec.c_str(), nullptr, 0));
            else if (spec.size() > 4)
                off = spec[3] == '-' ? 0u - uint32_t(std::strtoul(spec.c_str() + 4, nullptr, 0))
                                     : uint32_t(std::strtoul(spec.c_str() + 4, nullptr, 0));
            watchReads.push_back({r, off});
        } else if (arg == "-thrashlog" && a + 2 < argc) {
            options.thrashLogFrom = std::strtoull(argv[++a], nullptr, 0);
            options.thrashLogTo = std::strtoull(argv[++a], nullptr, 0);
        } else if (arg == "-present" && a + 1 < argc) {
            options.presentedRenderer = argv[++a];
            if (options.presentedRenderer == "none")
                options.presentedRenderer.clear();
        } else if (arg == "-renderer" && a + 1 < argc) { // the game told of this one, and given it
            options.renderer = argv[++a];
            options.presentedRenderer.clear();
        } else if (arg == "-native") {
            options.nativeThrash = true;
        } else if (arg == "-positional") { // network races with positional updates (net.md)
            options.positionalUpdates = true;
        } else if (arg == "-pointfilter") { // the native THRASH's textures point-sampled, not bilinear
            options.pointSampled = true;
        } else if (arg == "-direct3d") { // the game told of d3da.dll, drawn natively as d3da's device draws
            options.presentedRenderer = "d3da.dll";
            options.renderer = "softtria.dll";
            options.nativeThrash = true;
        } else if (arg == "-glide") { // the retail voodoo2a.dll on Reluva's glide2x.dll
            options.glide = true;
        } else if (arg == "-glidegpu") { // ... Glide's pixels drawn on the GPU
            options.glide = true;
            options.glideGpu = 1;
        } else if (arg == "-glidegpucompare" && a + 1 < argc) { // ... and in software beside it, compared
            options.glide = true;
            options.glideGpu = 2;
            glideGpuCompareFile = argv[++a];
        } else if (arg == "-glidedll" && a + 1 < argc) { // ... with this THRASH DLL file in voodoo2a.dll's place
            options.glide = true;
            options.glideDll = argv[++a];
        } else if (arg == "-glideboard" && a + 1 < argc) { // ... on this board (voodoo1, rush, voodoo2...)
            options.glide = true;
            options.glideBoard = argv[++a];
        } else if (arg == "-glidefilter" && a + 1 < argc) { // ... its texture filter held: point or bilinear
            options.glideFilter = std::string(argv[++a]) == "point" ? 0 : 1;
        } else if (arg == "-glidecompare" && a + 1 < argc) { // ... and the native drawing beside it
            options.glide = true;
            options.glideCompare = true;
            glideCompareFile = argv[++a];
        } else if (arg == "-deterministic" && a + 1 < argc) {
            options.instructionsPerMs = uint32_t(std::strtoul(argv[++a], nullptr, 0));
        } else if (arg == "-mmx") {
            options.mmx = true;
        } else if (arg == "-mhz" && a + 1 < argc) {
            options.cpuMhz = std::max(1u, uint32_t(std::strtoul(argv[++a], nullptr, 0)));
        } else if (arg == "-timescale" && a + 1 < argc)
            options.timeScale = std::atof(argv[++a]);
        else if (arg == "-framelog" && a + 1 < argc)
            frameLogFile = argv[++a];
        else if (arg == "-wav" && a + 1 < argc)
            wavFile = argv[++a];
        else if (arg == "-voices" && a + 1 < argc)
            voicesFile = argv[++a];
        else if (arg == "-hotspots" && a + 2 < argc) {
            hotFrom = std::strtoull(argv[++a], nullptr, 10);
            hotTo = std::strtoull(argv[++a], nullptr, 10);
        } else if (arg == "-gpulog") { // THRASH recorded as for the GPU, each frame's list summed up
            options.nativeThrash = true;
            options.gpuThrash = true;
            gpuLog = true;
        } else if (arg == "-gpudump" && a + 2 < argc) { // a list in full: its vertices, by batch, and textures
            options.nativeThrash = true;
            options.gpuThrash = true;
            gpuDumpList = std::strtoull(argv[++a], nullptr, 10);
            gpuDumpDir = argv[++a];
        } else if (arg == "-joystick" && a + 1 < argc)
            options.joystick = argv[++a];
        else if (arg == "-net")
            options.sockets = reluva::net::systemSockets();
        else if (arg == "-netlog" && a + 1 < argc)
            options.netLog = argv[++a];
        else if (arg == "-netdrop" && a + 1 < argc) { // N[:fromMs[-toMs]]
            char *end = nullptr;
            options.netDrop = uint32_t(std::strtoul(argv[++a], &end, 10));
            if (end && *end == ':')
                options.netDropFrom = uint32_t(std::strtoul(end + 1, &end, 10));
            if (end && *end == '-')
                options.netDropTo = uint32_t(std::strtoul(end + 1, nullptr, 10));
        } else if (arg == "-serial" && a + 1 < argc)
            options.serial = argv[++a];
        else if (arg == "-modem" && a + 1 < argc)
            options.modem = argv[++a];
        else if ((arg == "-netaddr" || arg == "-broadcast") && a + 1 < argc) {
            const std::string list = argv[++a];
            for (size_t at = 0; at < list.size();) {
                const size_t comma = std::min(list.find(',', at), list.size());
                reluva::net::Address parsed;
                if (!reluva::net::parseAddress(list.substr(at, comma - at), 0, parsed))
                    return fail(reluva::Error::fail("not an address: " + list.substr(at, comma - at)));
                if (arg == "-netaddr")
                    options.netAddress = parsed.ip;
                else
                    options.broadcastTo.push_back(parsed.ip);
                at = comma + 1;
            }
        } else if (arg == "-wheel")
            options.joystickWheel = true;
        else if (arg == "-ff")
            options.forceFeedback = true;
        else if (arg == "-forcelog" && a + 1 < argc)
            forceLogFile = argv[++a];
        else if (arg == "-keys" && a + 1 < argc)
            keyScript = argv[++a];
        else if (arg == "-slice" && a + 1 < argc)
            slice = uint32_t(std::strtoul(argv[++a], nullptr, 0));
        else if (arg == "-watchmem" && a + 2 < argc) {
            watchMem = uint32_t(std::strtoul(argv[++a], nullptr, 0));
            watchMemSize = uint32_t(std::strtoul(argv[++a], nullptr, 0));
        } else if (arg == "-dump" && a + 1 < argc)
            dumpFile = argv[++a];
        else if ((arg == "-poke" || arg == "-pokeevery" || arg == "-dumpmem") && a + 1 < argc) {
            reluva::x86::Poke p;
            std::string error;
            const bool ok = arg == "-dumpmem" ? reluva::x86::parseDump(argv[++a], p, error)
                                              : reluva::x86::parsePoke(argv[++a], arg == "-pokeevery", p, error);
            if (!ok)
                return fail(Error::fail(arg + ": " + error));
            pokes.list.push_back(std::move(p));
        } else if (arg == "-every" && a + 1 < argc)
            every = std::max(1, std::atoi(argv[++a]));
        else if (arg == "-framesfrom" && a + 1 < argc)
            framesFrom = std::strtoull(argv[++a], nullptr, 10);
    }
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    std::printf("install %s, CD %s, writes to %s\n", folders.install.string().c_str(),
                folders.cd.empty() ? "(none)" : folders.cd.string().c_str(), options.saveFolder.string().c_str());
    reluva::nfs::GameHost host(folders, options);
    host.log = [](const std::string &line) { std::printf("%s\n", line.c_str()); };
    // Frames shown: every so many written as pictures.
    int shown = 0;
    // The forces, as each frame finds them: a row whenever they change.
    std::string forceCsv = "ms,constant,spring,centre,saturation,damper,vibration,period,square,autocentre\n";
    reluva::GamepadForce lastForce;
    int forceChanges = 0;
    float strongestPush = 0;
    float strongestVibration = 0;
    reluva::Surface lastShown; // -glidecompare: the Glide frame of the flip under way
    host.onFrame = [&](const reluva::Surface &frame) {
        if (options.forceFeedback) {
            const reluva::GamepadForce f = host.gamepadForce();
            if (!(f == lastForce) || forceChanges == 0) {
                char row[200];
                std::snprintf(row, sizeof row, "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%d,%d\n",
                              (unsigned long long)host.process().now(), f.constant, f.spring, f.springCentre,
                              f.springSaturation, f.damper, f.vibration, f.vibrationPeriodMs, int(f.vibrationSquare),
                              int(f.autoCentre));
                forceCsv += row;
                ++forceChanges;
                lastForce = f;
                strongestPush = std::max(strongestPush, std::fabs(f.constant));
                strongestVibration = std::max(strongestVibration, f.vibration);
            }
        }
        if (!glideCompareFile.empty())
            lastShown = frame;
        if (framesDir.empty() || shown++ % every || uint64_t(shown - 1) < framesFrom)
            return;
        reluva::image::Image img;
        img.w = frame.w;
        img.h = frame.h;
        img.rgba = frame.pixels;
        char name[32];
        std::snprintf(name, sizeof name, "frame%05d.tga", shown - 1);
        writeFile(stdfs::path(framesDir) / name, reluva::image::encodeTga(img));
    };
    // -glidegpucompare: per buffer swap, the GPU frame against the software
    // reference's.
    std::string gpuCompareCsv = "swap,mean,differing,over24,max\n";
    double gpuCompareSum = 0;
    uint64_t gpuCompared = 0;
    if (!glideGpuCompareFile.empty())
        host.onGlideComparison = [&](const reluva::glide::FrameComparison &f) {
            char row[160];
            std::snprintf(row, sizeof row, "%llu,%.4f,%llu,%llu,%d\n", (unsigned long long)f.swap, f.meanDifference,
                          (unsigned long long)f.pixelsDiffering, (unsigned long long)f.pixelsOver24, f.maxDifference);
            gpuCompareCsv += row;
            gpuCompareSum += f.meanDifference;
            ++gpuCompared;
        };
    // -glidecompare: per flip, the mean difference of each channel and the
    // pixels off by more than 24 in any; the native frame written beside
    // the Glide one when that one is.
    std::string compareCsv = "frame,dr,dg,db,over24\n";
    int compared = 0;
    if (!glideCompareFile.empty())
        host.onCompareFrame = [&](const reluva::Surface &native) {
            if (lastShown.w != native.w || lastShown.h != native.h)
                return;
            double d[3] = {0, 0, 0};
            int over = 0;
            for (int y = 0; y < native.h; ++y) {
                const uint8_t *a = lastShown.row(y);
                const uint8_t *b = native.row(y);
                for (int x = 0; x < native.w; ++x) {
                    int worst = 0;
                    for (int ch = 0; ch < 3; ++ch) {
                        const int e = std::abs(int(a[4 * x + ch]) - int(b[4 * x + ch]));
                        d[ch] += e;
                        worst = std::max(worst, e);
                    }
                    over += worst > 24;
                }
            }
            const double n = double(native.w) * native.h;
            char line[128];
            std::snprintf(line, sizeof line, "%d,%.3f,%.3f,%.3f,%d\n", compared, d[0] / n, d[1] / n, d[2] / n, over);
            compareCsv += line;
            ++compared;
            if (!framesDir.empty() && (shown - 1) % every == 0 && uint64_t(shown - 1) >= framesFrom) {
                reluva::image::Image img;
                img.w = native.w;
                img.h = native.h;
                img.rgba = native.pixels;
                char name[32];
                std::snprintf(name, sizeof name, "native%05d.tga", shown - 1);
                writeFile(stdfs::path(framesDir) / name, reluva::image::encodeTga(img));
            }
        };
    std::vector<uint32_t> recent(size_t(std::max(traceCount, 1)));
    size_t recentAt = 0;
    int watched = 0;
    // Every texture as it now is, for -gpudump.
    std::map<uint32_t, reluva::nfs::ThrashFrame::Texture> dumpTextures;
    uint64_t lists = 0;
    if (gpuLog || gpuDumpList)
        host.onThrashFrame = [&](reluva::nfs::ThrashFrame &&f) {
            if (f.resetTextures)
                dumpTextures.clear();
            for (auto &t : f.textures)
                dumpTextures[t.handle] = t;
            if (++lists == gpuDumpList) {
                // batch,texture,width,height,blend,mode,cull,fog,fogcolour,filter then x,y,z,rhw,diffuse,u,v
                // per vertex; filter is the batch's texture filter (-1 as the drawing's default, 0 point
                // sampled, 1 bilinear: ThrashFrame::Batch::filter)
                std::string csv =
                    "batch,texture,width,height,blend,mode,cull,fog,fogcolour,filter,x,y,z,rhw,diffuse,u,v\n";
                char line[256];
                std::set<uint32_t> used;
                for (size_t i = 0; i < f.batches.size(); ++i) {
                    const auto &b = f.batches[i];
                    auto t = dumpTextures.find(b.texture);
                    const int w = t == dumpTextures.end() ? 0 : t->second.width;
                    const int h = t == dumpTextures.end() ? 0 : t->second.height;
                    used.insert(b.texture);
                    for (uint32_t v = b.first; v < b.first + b.count; ++v) {
                        const auto &x = f.vertices[v];
                        std::snprintf(line, sizeof line, "%zu,%u,%d,%d,%d,%d,%d,%g,%06x,%d,%g,%g,%g,%g,%08x,%g,%g\n", i,
                                      b.texture, w, h, int(b.blend), b.blendMode, b.cull, double(b.fog), b.fogColour,
                                      b.filter, double(x.x), double(x.y), double(x.z), double(x.rhw), x.diffuse,
                                      double(x.u), double(x.v));
                        csv += line;
                    }
                }
                stdfs::create_directories(gpuDumpDir);
                writeFile(stdfs::path(gpuDumpDir) / "list.csv", std::vector<uint8_t>(csv.begin(), csv.end()));
                for (uint32_t handle : used) {
                    auto t = dumpTextures.find(handle);
                    if (t == dumpTextures.end() || t->second.rgba.empty())
                        continue;
                    reluva::image::Image img;
                    img.w = t->second.width;
                    img.h = t->second.height;
                    img.rgba.resize(t->second.rgba.size() * 4);
                    for (size_t i = 0; i < t->second.rgba.size(); ++i) {
                        const uint32_t c = t->second.rgba[i];
                        img.rgba[4 * i] = uint8_t(c >> 16);
                        img.rgba[4 * i + 1] = uint8_t(c >> 8);
                        img.rgba[4 * i + 2] = uint8_t(c);
                        img.rgba[4 * i + 3] = uint8_t(c >> 24);
                    }
                    writeFile(stdfs::path(gpuDumpDir) / ("texture" + std::to_string(handle) + ".tga"),
                              reluva::image::encodeTga(img));
                }
                std::printf("  list %llu written to %s\n", (unsigned long long)lists, gpuDumpDir.c_str());
            }
            if (!gpuLog)
                return;
            size_t textured = 0;
            for (const auto &b : f.batches)
                textured += b.texture ? b.count : 0;
            std::printf(
                "  list at %llu ms: %zu batches, %zu vertices (%zu textured), %zu textures%s, flip %d,%d-%d,%d\n",
                (unsigned long long)host.process().now(), f.batches.size(), f.vertices.size(), textured,
                f.textures.size(), f.resetTextures ? " after a reset" : "", f.flip[0], f.flip[1], f.flip[2], f.flip[3]);
        };
    // Where instructions run, by 16-byte block, over a range of the run.
    std::map<uint32_t, uint64_t> hot;
    // Tracing every instruction costs: only when asked for.
    const bool hotspots = hotTo > hotFrom;
    const bool tracePokes = pokes.needsTrace();
    if (hotspots || traceCount > 0 || !watch.empty() || tracePokes || !counted.empty() || !drivers.empty())
        host.cpu().trace = [&](reluva::x86::Cpu &c) {
            if (c.eip == 0x4314c9)
                for (auto &d : drivers) {
                    const uint64_t now = host.process().now();
                    if (c.reg[reluva::x86::Cpu::ECX] != d.car || now < d.from || now >= d.to)
                        continue;
                    auto f = [&](uint32_t a) { return std::bit_cast<float>(c.read32(a)); };
                    auto s8 = [&](uint32_t a) { return float(int8_t(c.read8(a))); };
                    float tx = 0;
                    float tz = 0;
                    const uint32_t roadPoints = c.read32(0x5dd948);
                    const uint32_t n = c.read32(0x5dd940);
                    const uint32_t slice = c.read32(d.car + 0x1c);
                    float fwd0[3] = {0, 0, 0};
                    if (d.road) {
                        if (!roadPoints || !n || n > 100000)
                            continue;
                        const float vx = f(d.car + 0xa4);
                        const float vz = f(d.car + 0xac);
                        // About 18 m and 0.6 s ahead.
                        const uint32_t k =
                            uint32_t(std::clamp(3.0f + std::sqrt(vx * vx + vz * vz) * 0.1f, 3.0f, 12.0f));
                        const uint32_t p = roadPoints + 36 * ((slice + k) % n);
                        const uint32_t p0 = roadPoints + 36 * (slice % n);
                        for (int i = 0; i < 3; ++i)
                            fwd0[i] = s8(p0 + 20 + i);
                        tx = float(int32_t(c.read32(p))) / 65536 + d.gap * s8(p + 24) / 128;
                        tz = float(int32_t(c.read32(p + 8))) / 65536 + d.gap * s8(p + 26) / 128;
                    } else {
                        tx = f(d.target + 0x98);
                        tz = f(d.target + 0xa0);
                    }
                    const float dx = tx - f(d.car + 0x98);
                    const float dz = tz - f(d.car + 0xa0);
                    const float across = dx * f(d.car + 0x190) + dz * f(d.car + 0x198);
                    const float along = dx * f(d.car + 0x1a8) + dz * f(d.car + 0x1b0);
                    const float angle = std::atan2(across, along);
                    const float dist = std::sqrt(dx * dx + dz * dz);
                    auto speed = [&](uint32_t car) {
                        const float x = f(car + 0xa4);
                        const float y = f(car + 0xa8);
                        const float z = f(car + 0xac);
                        return std::sqrt(x * x + y * y + z * z);
                    };
                    const float v = speed(d.car);
                    const float vt = d.road ? 0.0f : speed(d.target);
                    const int steer = int(std::clamp(driveSign * angle / 0.35f * 127.0f, -128.0f, 127.0f));
                    int gas = 255;
                    int brake = 0;
                    if (d.road) {
                        // The car's slice's forward against those up to about
                        // 2.5 s on (6 m a slice): the sharper the bend ahead, the slower.
                        float bend = 0;
                        const uint32_t ahead = uint32_t(std::clamp(4.0f + v * 0.42f, 4.0f, 25.0f));
                        for (uint32_t j = 1; j <= ahead && n; ++j) {
                            const uint32_t q = roadPoints + 36 * ((slice + j) % n);
                            const float dot = fwd0[0] * s8(q + 20) + fwd0[1] * s8(q + 21) + fwd0[2] * s8(q + 22);
                            bend = std::max(bend, std::acos(std::clamp(dot / (128.0f * 128.0f), -1.0f, 1.0f)));
                        }
                        const float wanted = std::clamp(60.0f - 45.0f * (bend - 0.2f), 16.0f, 60.0f);
                        gas = v < wanted ? 255 : 0;
                        brake = v > wanted + 3 ? 255 : 0;
                    } else if (d.gap > 0) {
                        const float wanted = vt + 0.5f * (dist - d.gap);
                        gas = v < wanted ? 255 : 0;
                        brake = v > wanted + 2 ? 255 : 0;
                    }
                    if (c.read8(d.car + 0x50b) == 1) // in neutral (a cop starts so): ask for first gear
                        c.reg[reluva::x86::Cpu::EAX] = (c.reg[reluva::x86::Cpu::EAX] & ~0xffu) | 2;
                    c.write32(d.car + 0x518, uint32_t(steer));
                    c.write8(d.car + 0x504, uint8_t(gas));
                    c.write8(d.car + 0x505, uint8_t(brake));
                    if (now >= d.logged + 500) {
                        d.logged = now;
                        std::printf(
                            "  drive %08x at %08x ms %llu: %.1f m, %.2f rad, %.1f against %.1f m/s, steer %d gas %d"
                            " brake %d slice %u\n",
                            d.car, d.target, (unsigned long long)now, dist, angle, v, vt, steer, gas, brake, slice);
                    }
                }
            for (auto &[address, n] : counted)
                n += c.eip == address;
            if (hotspots && c.executed() >= hotFrom && c.executed() < hotTo)
                ++hot[c.eip & ~0xfu];
            if (tracePokes)
                pokes.instruction(c, host.process().now());
            recent[recentAt++ % recent.size()] = c.eip;
            if (!watch.empty() && watched < watchLimit && std::find(watch.begin(), watch.end(), c.eip) != watch.end()) {
                ++watched;
                std::printf(
                    "  at %08x: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x [esp] %08x"
                    " ms %llu\n",
                    c.eip, c.reg[0], c.reg[1], c.reg[2], c.reg[3], c.reg[4], c.reg[5], c.reg[6], c.reg[7],
                    c.valid(c.reg[4], 4) ? c.read32(c.reg[4]) : 0u, (unsigned long long)host.process().now());
                if (!watchReads.empty()) {
                    std::string line = "   read";
                    for (const auto &[r, off] : watchReads) {
                        const uint32_t at = (r < 0 ? 0u : c.reg[r]) + off;
                        char item[32];
                        std::snprintf(item, sizeof item, " %08x", c.valid(at, 4) ? c.read32(at) : 0u);
                        line += item;
                    }
                    std::printf("%s\n", line.c_str());
                }
            }
        };
    if (!pokes.empty()) {
        pokes.log = [](const std::string &line) { std::printf("%s\n", line.c_str()); };
        pokes.save = [](const std::string &file, const std::vector<uint8_t> &bytes) { writeFile(file, bytes); };
        host.process().onSlice = [&](reluva::x86::Cpu &c) { pokes.time(c, host.process().now()); };
    }
    if (slice)
        host.process().slice = slice;
    reluva::GamepadState pad; // the joystick's state, as the script sets it
    pad.wheel = options.joystickWheel;
    pad.forceFeedback = options.forceFeedback;
    for (size_t at = 0; at < keyScript.size();) {
        const size_t comma = keyScript.find(',', at);
        const std::string item = keyScript.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        at = comma == std::string::npos ? keyScript.size() : comma + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos)
            continue;
        const uint64_t ms = std::strtoull(item.c_str(), nullptr, 10);
        const std::string key = item.substr(colon + 1);
        int dx = 0;
        int dy = 0;
        int index = 0;
        float value = 0;
        if (key.rfind("joy:", 0) == 0) { // joy:A=V (axis A 0-5) or joy:bB=V (button B), the pad's state so far
            if (std::sscanf(key.c_str(), "joy:b%d=%f", &index, &value) == 2 && index >= 0 && index < 32)
                pad.buttons = value != 0 ? pad.buttons | 1u << index : pad.buttons & ~(1u << index);
            else if (std::sscanf(key.c_str(), "joy:%d=%f", &index, &value) == 2 && index >= 0 && index < 6)
                pad.axes[index] = value;
            host.setJoystick(pad, ms);
        } else if (key == "click") {
            host.queueMouseButton(true, ms);
            host.queueMouseButton(false, ms + 100);
        } else if (std::sscanf(key.c_str(), "move%d/%d", &dx, &dy) == 2) {
            host.queueMouseMove(dx, dy, ms);
        } else if ((key[0] == '+' || key[0] == '-') && key.size() > 1) { // held, let go
            if (!host.queueKey(key.substr(1), key[0] == '+', ms))
                std::fprintf(stderr, "unknown key '%s'\n", key.c_str());
        } else if (!host.queueKey(key, true, ms) || !host.queueKey(key, false, ms + 100)) {
            std::fprintf(stderr, "unknown key '%s'\n", key.c_str());
        }
    }
    if (watchMemSize) {
        host.cpu().watchStart = watchMem;
        host.cpu().watchEnd = watchMem + watchMemSize;
        host.cpu().onWatchedStore = [&host](reluva::x86::Cpu &c, uint32_t address, uint32_t value, int size) {
            const auto *t = host.process().currentThread();
            std::printf("  store %08x <- %08x (%d bytes) at %08x [thread %u] esp %08x\n", address, value, size, c.eip,
                        t ? t->id : 0, c.reg[reluva::x86::Cpu::ESP]);
        };
    }
    if (Error err = host.load())
        return fail(err);
    uint32_t exitCode = 0;
    // The sound, by the game's clock (GameHost::recordSound).
    reluva::audio::Sound sound;
    sound.rate = 22050;
    sound.channels = 2;
    if (!wavFile.empty())
        host.recordSound(&sound.samples, sound.rate);
    std::vector<reluva::nfs::GameHost::VoiceEnergy> voices;
    if (!voicesFile.empty())
        host.logVoices(&voices, 100);
    const auto start = std::chrono::steady_clock::now();
    const Error err = host.run(exitCode);
    if (!glideCompareFile.empty())
        writeFile(glideCompareFile, std::vector<uint8_t>(compareCsv.begin(), compareCsv.end()));
    if (!glideGpuCompareFile.empty()) {
        writeFile(glideGpuCompareFile, std::vector<uint8_t>(gpuCompareCsv.begin(), gpuCompareCsv.end()));
        std::printf("glide on the GPU against the software reference: %llu frames, mean difference %.4f of 255\n",
                    (unsigned long long)gpuCompared, gpuCompared ? gpuCompareSum / double(gpuCompared) : 0.0);
    }
    if (auto *g = host.glide()) {
        std::string list;
        for (const auto &[name, count] : g->calls())
            list += (list.empty() ? "" : ", ") + name + " " + std::to_string(count);
        std::printf("%s: %s\n", g->dllName().c_str(), list.c_str());
        for (const auto &[name, counts] : g->modes()) {
            std::string line;
            for (const auto &[value, count] : counts)
                line += " " + std::to_string(value) + " x" + std::to_string(count);
            std::printf("  %s:%s\n", name.c_str(), line.c_str());
        }
        std::printf("  drawn by %s\n", host.glideRenderer().c_str());
    }
    if (!wavFile.empty()) {
        host.recordSound(nullptr, 0);
        writeFile(wavFile, reluva::audio::encodePcmWav(sound));
        const auto stats = host.soundStats();
        std::printf("sound: %llu blocks of 256 bytes played, %llu of them again without being written since "
                    "(%llu not silent; those of a streamed buffer played as silence)\n",
                    (unsigned long long)stats.blocks, (unsigned long long)stats.stale,
                    (unsigned long long)stats.staleAudible);
        std::map<uint64_t, int> bySecond;
        for (uint64_t ms : stats.staleAudibleMs)
            ++bySecond[ms / 1000];
        if (!bySecond.empty()) {
            std::printf("  not silent, by second of the game's clock:");
            for (const auto &[second, n] : bySecond)
                std::printf(" %llus:%d", (unsigned long long)second, n);
            std::printf("\n");
        }
    }
    if (!voicesFile.empty()) {
        host.logVoices(nullptr, 0);
        std::string csv = "ms,calls,rear calls,front L+R,front L-R,rear L+R,rear L-R\n";
        char line[200];
        for (const auto &v : voices) {
            std::snprintf(line, sizeof line, "%llu,%u,%u,%.4g,%.4g,%.4g,%.4g\n", (unsigned long long)v.ms, v.calls,
                          v.rearCalls, v.frontC, v.frontS, v.rearC, v.rearS);
            csv += line;
        }
        writeFile(voicesFile, std::vector<uint8_t>(csv.begin(), csv.end()));
    }
    if (options.instructionsPerMs) {
        // The state the run ended in, for comparing builds: memory and
        // registers hashed (FNV-1a).
        uint64_t h = 0xcbf29ce484222325ull;
        for (uint8_t byte : host.cpu().memory())
            h = (h ^ byte) * 0x100000001b3ull;
        for (uint32_t r : host.cpu().reg)
            h = (h ^ r) * 0x100000001b3ull;
        std::printf("state: %016llx after %llu instructions, eip %08x\n", (unsigned long long)h,
                    (unsigned long long)host.cpu().executed(), host.cpu().eip);
    }
    if (options.forceFeedback) {
        std::printf("forces: %d changes, the strongest push %.2f, the strongest vibration %.2f\n", forceChanges,
                    strongestPush, strongestVibration);
        if (!forceLogFile.empty())
            writeFile(forceLogFile, std::vector<uint8_t>(forceCsv.begin(), forceCsv.end()));
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (err) {
        std::printf("stopped: %s\nlast instructions:", err.message.c_str());
        for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0; i < recentAt; ++i)
            std::printf(" %08x", recent[i % recent.size()]);
        const auto &r = host.cpu().faultReg;
        std::printf("\nregisters: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x\n", r[0],
                    r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    } else {
        std::printf("the game ended the process, exit code %u\n", exitCode);
    }
    if (!dumpFile.empty())
        writeFile(dumpFile, std::vector<uint8_t>(host.cpu().memory().begin(), host.cpu().memory().end()));
    const uint64_t n = host.cpu().executed();
    std::printf("%llu instructions in %.1f s (%.1f million a second)\n", (unsigned long long)n, seconds,
                seconds > 0 ? n / seconds / 1e6 : 0.0);
    std::vector<std::pair<uint64_t, std::string>> counts;
    for (const auto &[name, count] : host.callCounts())
        counts.push_back({count, name});
    std::sort(counts.rbegin(), counts.rend());
    std::printf("system calls (%zu kinds):", counts.size());
    for (size_t i = 0; i < counts.size() && i < 400; ++i)
        std::printf("%s %s %llu", i ? "," : "", counts[i].second.c_str(), (unsigned long long)counts[i].first);
    std::printf("\n");
    for (const auto &[address, n] : counted)
        std::printf("%08x ran %llu times\n", address, (unsigned long long)n);
    if (!hot.empty()) {
        std::vector<std::pair<uint64_t, uint32_t>> ranked;
        uint64_t total = 0;
        for (const auto &[block, n] : hot) {
            ranked.push_back({n, block});
            total += n;
        }
        std::sort(ranked.rbegin(), ranked.rend());
        std::printf("hot spots (%llu instructions):\n", (unsigned long long)total);
        for (size_t i = 0; i < ranked.size() && i < 40; ++i) {
            const auto *m = host.process().moduleContaining(ranked[i].second);
            std::printf("  %08x %5.1f%%  %s\n", ranked[i].second, 100.0 * double(ranked[i].first) / double(total),
                        m ? m->name.c_str() : "");
        }
    }
    const auto &t = host.thrash();
    if (!t.dll.empty() || !t.calls.empty()) {
        std::printf("THRASH (%s): %llu frames, %llu textures; %.0f%% of all instructions in its functions\n  calls:",
                    t.dll.c_str(), (unsigned long long)t.frames, (unsigned long long)t.textures,
                    100.0 * double(t.rendererInstructions) / double(std::max<uint64_t>(host.cpu().executed(), 1)));
        for (const auto &[name, count] : t.calls) {
            auto it = t.callInstructions.find(name);
            std::printf(" %s %llu", name.c_str(), (unsigned long long)count);
            if (it != t.callInstructions.end() && it->second > 1000000)
                std::printf(" (%.0fM)", it->second / 1e6);
        }
        std::printf("\n  texture formats:");
        for (const auto &[format, count] : t.textureFormats)
            std::printf(" %d: %llu", format, (unsigned long long)count);
        std::printf("\n");
        if (!frameLogFile.empty()) {
            std::string csv = "ms,instructions,renderer\n";
            for (const auto &f : t.frameLog)
                csv += std::to_string(f.ms) + "," + std::to_string(f.instructions) + "," + std::to_string(f.renderer) +
                       "\n";
            writeFile(frameLogFile, std::vector<uint8_t>(csv.begin(), csv.end()));
        }
        // The frames in one-second steps: how many, and their instructions.
        // ... and the time no thread could run (all waiting), and how often.
        // ... and the shares of the game's own thread and the timer's.
        uint64_t second = 0;
        uint64_t frames = 0;
        uint64_t instructions = 0;
        uint64_t renderer = 0;
        uint64_t idleMicros = 0;
        uint64_t idleWaits = 0;
        uint64_t own = 0;
        uint64_t timer = 0;
        auto flush = [&] {
            if (frames)
                std::printf(
                    "  second %3llu: %3llu frames, %6.2f million instructions each, %6.2f of them the renderer's,"
                    " idle %4llu ms (%llu waits); game thread %2.0f%%, timer %2.0f%%\n",
                    (unsigned long long)second, (unsigned long long)frames, instructions / double(frames) / 1e6,
                    renderer / double(frames) / 1e6, (unsigned long long)(idleMicros / 1000),
                    (unsigned long long)idleWaits, 100.0 * double(own) / double(std::max<uint64_t>(instructions, 1)),
                    100.0 * double(timer) / double(std::max<uint64_t>(instructions, 1)));
        };
        for (const auto &f : t.frameLog) {
            if (f.ms / 1000 != second) {
                flush();
                second = f.ms / 1000;
                frames = instructions = renderer = idleMicros = idleWaits = own = timer = 0;
            }
            ++frames;
            instructions += f.instructions;
            renderer += f.renderer;
            idleMicros += f.idleMicros;
            idleWaits += f.idleWaits;
            own += f.ownThread;
            timer += f.timerThread;
        }
        flush();
    }
    return err ? 1 : 0;
}

// Runs each of nfs3.exe's routines done natively (GameHost::Options::
// nativeRoutines) against its original in the interpreter, on the same
// random inputs, in two copies of the game loaded side by side; every byte
// they write, and the registers, must come out the same.
// EA's Huffman schemes (30 FB, 32 FB, 34 FB; refpack.md): every file so
// compressed in the game's folders, decompressed by the game's own routine
// (0x50ed50, run in the interpreter) and by Reluva's, which must agree; the
// game's results written to `out dir` if given.
int cmdNfsHuffCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs huff-check <game folder> [out dir]\n");
        return 2;
    }
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameFiles files;
    files.scan(folders);
    reluva::nfs::GameHost::Options options;
    options.saveFolder = stdfs::temp_directory_path() / "reluva-huff-check";
    reluva::nfs::GameHost host(folders, options);
    if (reluva::Error e = host.load())
        return fail(e);
    auto &c = host.cpu();
    const uint32_t stack = host.process().memory().alloc(1 << 16);
    int checked = 0;
    int mismatches = 0;
    for (const reluva::nfs::FileRef &f : files.files()) {
        std::vector<uint8_t> data;
        if (files.read(f, data) || !reluva::nfs::isEaHuffman(data))
            continue;
        const uint32_t size = uint32_t(data[2]) << 16 | uint32_t(data[3]) << 8 | data[4];
        const uint32_t in = host.process().memory().alloc(uint32_t(data.size()) + 16);
        const uint32_t out = host.process().memory().alloc(size + 4096);
        if (!in || !out)
            return fail(reluva::Error::fail("no memory"));
        for (size_t i = 0; i < data.size(); ++i)
            c.write8(in + uint32_t(i), data[i]);
        c.reg[reluva::x86::Cpu::EAX] = in;
        c.reg[reluva::x86::Cpu::EDX] = out;
        c.reg[reluva::x86::Cpu::EBX] = 1;
        c.reg[reluva::x86::Cpu::ESP] = stack + (1 << 16) - 64;
        uint32_t result = 0;
        if (reluva::Error e = c.call(0x50ed50, {}, result)) {
            std::printf("%s: the game's routine failed: %s\n", f.name.c_str(), e.message.c_str());
            continue;
        }
        std::vector<uint8_t> game(result);
        for (uint32_t i = 0; i < result; ++i)
            game[i] = c.read8(out + i);
        std::vector<uint8_t> ours;
        const reluva::Error err = reluva::nfs::decompressEaHuffman(data, ours);
        size_t differ = 0;
        for (size_t i = 0; i < std::min(ours.size(), game.size()); ++i)
            differ += ours[i] != game[i];
        const bool same = !err && ours == game;
        std::printf("%s: %02x fb, %zu bytes to %u: %s\n", f.name.c_str(), data[0], data.size(), result,
                    same ? "the same as the game's"
                         : (err ? err.message.c_str()
                                : ("DIFFERENT (" + std::to_string(ours.size()) + " bytes, " + std::to_string(differ) +
                                   " differ)")
                                      .c_str()));
        mismatches += !same;
        if (argc > 3) {
            std::string name = f.name;
            for (char &ch : name)
                if (ch == '/' || ch == '\\')
                    ch = '_';
            writeFile(stdfs::path(argv[3]) / (name + ".out"), game);
        }
        ++checked;
    }
    std::printf("%d files, %d not the same\n", checked, mismatches);
    return mismatches ? 1 : 0;
}

int cmdNfsNativeCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs native-check <game folder> [trials]\n");
        return 2;
    }
    const int trials = argc > 3 ? std::max(1, std::atoi(argv[3])) : 300;
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameHost::Options interpreted;
    reluva::nfs::GameHost::Options native;
    interpreted.nativeRoutines = false;
    interpreted.saveFolder = native.saveFolder = stdfs::temp_directory_path() / "reluva-native-check";
    reluva::nfs::GameHost a(folders, interpreted);
    reluva::nfs::GameHost b(folders, native);
    if (reluva::Error e = a.load())
        return fail(e);
    if (reluva::Error e = b.load())
        return fail(e);
    // The same memory in each: a stack, and room for a routine's buffers.
    reluva::nfs::GameHost *hosts[2] = {&a, &b};
    uint32_t stack = 0;
    uint32_t area = 0;
    for (auto *h : hosts) {
        stack = h->process().memory().alloc(1 << 16);
        area = h->process().memory().alloc(1 << 16);
    }
    for (auto *h : hosts)
        if (h->process().memory().alloc(16) == 0)
            return fail(reluva::Error::fail("no memory"));
    uint64_t seed = 0x9e3779b97f4a7c15ull;
    auto random = [&seed] {
        seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17;
        return seed;
    };
    auto randomFloat = [&](double range) { return float((double(random() % 2000001) / 1000000.0 - 1.0) * range); };
    struct Routine {
        const char *name;
        uint32_t address;
        int kind; // 0 resample stereo, 1 resample mono, 2 mix, 3 to 16 bits
        uint32_t scratch;
        bool stereoSource;
    };
    const Routine routines[] = {
        {"resample stereo", 0x527330, 0, 0x56b138, false},
        {"resample mono", 0x5273c0, 1, 0x56b148, false},
        {"mix mono", 0x520aee, 2, 0, false},
        {"mix mono full", 0x520b7e, 2, 0, false},
        {"mix mono left", 0x520bed, 2, 0, false},
        {"mix mono right", 0x520c69, 2, 0, false},
        {"mix mono, right full", 0x520ce6, 2, 0, false},
        {"mix mono, left full", 0x520d6b, 2, 0, false},
        {"mix stereo", 0x520eae, 2, 0, true},
        {"mix stereo full", 0x520f40, 2, 0, true},
        {"mix stereo left", 0x520fb1, 2, 0, true},
        {"mix stereo right", 0x52102d, 2, 0, true},
        {"mix stereo, right full", 0x5210ac, 2, 0, true},
        {"mix stereo, left full", 0x521133, 2, 0, true},
        {"mix to 16 bits", 0x521d00, 3, 0, false},
    };
    // Where things go in the area: the source, the destination, then the
    // routine's position, fraction and gains.
    const uint32_t src = area;
    const uint32_t dst = area + 0x4000;
    const uint32_t posAt = area + 0x8000;
    const uint32_t fracAt = posAt + 4;
    const uint32_t gains = posAt + 8;
    int failed = 0;
    for (const Routine &r : routines) {
        if (!b.cpu().nativeName(b.cpu().read32(r.address + 1) + r.address + 5) || b.cpu().read8(r.address) != 0xe9) {
            std::printf("%-24s not replaced (its code isn't the retail one)\n", r.name);
            ++failed;
            continue;
        }
        int mismatches = 0;
        for (int t = 0; t < trials && mismatches < 3; ++t) {
            // The inputs, the same in both.
            std::vector<uint32_t> source(0x1000);
            std::vector<uint32_t> dest(0x1000);
            const double range = r.kind == 3 ? 40000.0 : t % 3 == 0 ? 1.0 : 32768.0;
            for (auto &v : source)
                v = std::bit_cast<uint32_t>(randomFloat(range));
            for (auto &v : dest)
                v = std::bit_cast<uint32_t>(randomFloat(32768.0));
            const int32_t count = int32_t(random() % 44) - 3;
            const uint32_t pos = uint32_t(random() % 100);
            const uint32_t frac = uint32_t(random());
            const uint32_t stepInt = uint32_t(random() % 3);
            const uint32_t stepFrac = uint32_t(random());
            const uint32_t high = t % 4 == 0 ? uint32_t(random() & 0xffff) : 0;
            const float gainLeft = randomFloat(1.5f);
            const float gainRight = randomFloat(1.5f);
            uint32_t regs[8];
            for (auto &v : regs)
                v = uint32_t(random());
            std::vector<uint32_t> args;
            switch (r.kind) {
            case 0:
            case 1:
                args = {uint32_t(count), src, dst, posAt, fracAt, stepInt, stepFrac};
                break;
            case 2:
                args = {gains, uint32_t(count), src, dst};
                break;
            default:
                args = {uint32_t(std::max(count, 0)), src, dst};
                break;
            }
            std::vector<uint8_t> after[2];
            uint32_t regsAfter[2][8];
            std::string error[2];
            for (int i = 0; i < 2; ++i) {
                auto &c = hosts[i]->cpu();
                for (size_t k = 0; k < source.size(); ++k) {
                    c.write32(src + 4 * uint32_t(k), source[k]);
                    c.write32(dst + 4 * uint32_t(k), dest[k]);
                }
                c.write32(posAt, pos);
                c.write32(fracAt, frac);
                c.write32(gains, std::bit_cast<uint32_t>(gainLeft));
                c.write32(gains + 4, std::bit_cast<uint32_t>(gainRight));
                if (r.scratch)
                    c.write16(r.scratch + 4, uint16_t(high));
                for (int k = 0; k < 8; ++k)
                    c.reg[k] = regs[k];
                c.reg[reluva::x86::Cpu::ESP] = stack + (1 << 16) - 64;
                uint32_t result = 0;
                if (reluva::Error e = c.call(r.address, args, result))
                    error[i] = e.message;
                for (int k = 0; k < 8; ++k)
                    regsAfter[i][k] = c.reg[k];
                // Everything it may have written: the buffers, the state, its scratch.
                for (uint32_t o = 0; o < 0x8010; ++o)
                    after[i].push_back(c.read8(area + o));
                if (r.scratch)
                    for (uint32_t o = 0; o < 8; ++o)
                        after[i].push_back(c.read8(r.scratch + o));
            }
            std::string why;
            if (error[0] != error[1])
                why = "failed differently: '" + error[0] + "' and '" + error[1] + "'";
            else if (after[0] != after[1]) {
                size_t k = 0;
                while (after[0][k] == after[1][k])
                    ++k;
                why = "memory differs first at +" + std::to_string(k);
            } else
                for (int k = 0; k < 8 && why.empty(); ++k)
                    if (k != reluva::x86::Cpu::ESP && regsAfter[0][k] != regsAfter[1][k])
                        why = "register " + std::to_string(k) + " differs";
            if (!why.empty()) {
                ++mismatches;
                std::printf("%-24s trial %d (count %d): %s\n", r.name, t, count, why.c_str());
            }
        }
        std::printf("%-24s %s\n", r.name,
                    mismatches ? "DIFFERS" : ("the same in " + std::to_string(trials) + " trials").c_str());
        failed += mismatches ? 1 : 0;
    }
    return failed ? 1 : 0;
}

// Runs the game's own sound mixer (iSND's, bnk.md) twice on the same
// voices: with its float routines and with its MMX ones, both in the
// interpreter, the game's code as it is. Starts the voices with
// iSNDmixstart (0x5000b8) and mixes with 0x500864, as the game does, after
// setting the mixer up (0x4fff18) with the MMX flag (0x5643b4) clear or
// set; then compares the outputs sample by sample.
int cmdNfsMmxCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool nfs mmx-check <game folder> [trials]\n");
        return 2;
    }
    using reluva::x86::Cpu;
    const int trials = argc > 3 ? std::max(1, std::atoi(argv[3])) : 40;
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs3.exe");
    reluva::nfs::GameHost::Options options;
    options.nativeRoutines = false;
    options.saveFolder = stdfs::temp_directory_path() / "reluva-mmx-check";
    reluva::nfs::GameHost floatHost(folders, options);
    reluva::nfs::GameHost mmxHost(folders, options);
    reluva::nfs::GameHost *hosts[2] = {&floatHost, &mmxHost};
    constexpr uint32_t kDataSize = 1 << 22;
    constexpr uint32_t kOutSize = 1 << 16;
    uint32_t stack[2];
    uint32_t data[2];
    uint32_t out[2];
    for (int i = 0; i < 2; ++i) {
        if (reluva::Error e = hosts[i]->load())
            return fail(e);
        stack[i] = hosts[i]->process().memory().alloc(1 << 16);
        data[i] = hosts[i]->process().memory().alloc(kDataSize);
        out[i] = hosts[i]->process().memory().alloc(kOutSize);
        if (!stack[i] || !data[i] || !out[i])
            return fail(reluva::Error::fail("no memory"));
    }
    // The MMX routines' entries, counted as they run.
    const std::pair<uint32_t, const char *> watched[] = {
        {0x51f5dd, "mix 8-bit mono"},       {0x51f6c7, "mix 8-bit stereo"},       {0x51f7ad, "mix 16-bit mono"},
        {0x51f897, "mix 16-bit stereo"},    {0x5270f0, "resample 8-bit mono"},    {0x527170, "resample 8-bit stereo"},
        {0x51f9de, "8-bit resample edge"},  {0x527210, "resample 16-bit mono"},   {0x527290, "resample 16-bit stereo"},
        {0x52d1a0, "EA-XA mono"},           {0x520830, "mix down 8-bit mono"},    {0x520870, "mix down 8-bit stereo"},
        {0x520a10, "mix down 16-bit mono"}, {0x520920, "mix down 16-bit stereo"}, {0x5209a0, "bus clear"},
    };
    constexpr size_t kWatched = std::size(watched);
    uint64_t runs[kWatched] = {};
    mmxHost.cpu().trace = [&](Cpu &c) {
        if (c.eip < 0x51f5dd || c.eip > 0x52d1a0)
            return;
        for (size_t k = 0; k < kWatched; ++k)
            if (c.eip == watched[k].first)
                ++runs[k];
    };
    // A voice as iSNDplatformplay hands it over: the data as the game holds
    // it, its format (1 8-bit mono, 2 8-bit stereo, 4 16-bit mono, 8 16-bit
    // stereo), compression (0 PCM, 7 EA-XA), levels and 16.16 step.
    struct Voice {
        std::vector<uint8_t> bytes;
        uint32_t samples = 0;
        int format = 4, compression = 0, left = 0, right = 0;
        uint32_t step = 0x10000;
        int quality = 50;
    };
    // The mix of `voices` in `frames` frames of output format `outFormat`
    // (the same flags), by the float (0) or MMX (1) routines, as signed
    // values (8-bit outputs centred).
    auto render = [&](int i, const std::vector<Voice> &voices, int frames, int outFormat, std::vector<int> &result) {
        auto &c = hosts[i]->cpu();
        const uint32_t top = stack[i] + (1 << 16) - 64;
        uint32_t ignored = 0;
        c.write32(0x5643b4, uint32_t(i));
        c.reg[Cpu::ESP] = top;
        c.reg[Cpu::EAX] = uint32_t(outFormat);
        c.reg[Cpu::EDX] = 0x500036; // the voice-ended callback: a ret
        if (reluva::Error e = c.call(0x4fff18, {}, ignored))
            return e;
        uint32_t at = data[i];
        for (size_t k = 0; k < voices.size(); ++k) {
            const Voice &v = voices[k];
            if (at + v.bytes.size() > data[i] + kDataSize)
                return reluva::Error::fail("voices too long");
            for (size_t n = 0; n < v.bytes.size(); ++n)
                c.write8(at + uint32_t(n), v.bytes[n]);
            c.reg[Cpu::ESP] = top;
            c.reg[Cpu::EAX] = uint32_t(k);
            c.reg[Cpu::EDX] = uint32_t(v.format);
            c.reg[Cpu::EBX] = uint32_t(v.compression);
            c.reg[Cpu::ECX] = 1; // played once
            if (reluva::Error e = c.call(0x5000b8,
                                         {at, v.samples, 0xffffffffu, 0xffffffffu, uint32_t(v.left), uint32_t(v.right),
                                          v.step, uint32_t(v.quality), 0},
                                         ignored))
                return e;
            at += (uint32_t(v.bytes.size()) + 15) & ~15u;
        }
        const int perFrame = outFormat == 8 ? 4 : outFormat == 1 ? 1 : 2;
        if (uint32_t(frames * perFrame) > kOutSize)
            return reluva::Error::fail("too many frames");
        c.reg[Cpu::ESP] = top;
        c.reg[Cpu::EAX] = out[i];
        c.reg[Cpu::EDX] = uint32_t(frames);
        if (reluva::Error e = c.call(0x500864, {}, ignored))
            return e;
        result.clear();
        if (outFormat == 8 || outFormat == 4)
            for (int n = 0; n < frames * perFrame / 2; ++n)
                result.push_back(int16_t(c.read16(out[i] + 2 * uint32_t(n))));
        else
            for (int n = 0; n < frames * perFrame; ++n)
                result.push_back(int(c.read8(out[i] + uint32_t(n))) - 128);
        return reluva::Error::success();
    };
    // The differences of MMX (m) from float (f), over many samples.
    struct Diff {
        uint64_t n = 0, nonzero = 0, even = 0, nonzeroF = 0, evenF = 0, clipM = 0, clipF = 0;
        double sum = 0, sum2 = 0, sumG = 0, sumG2 = 0, mf = 0, ff = 0;
        int maxAbs = 0, limit = 32767;
        std::map<int, uint64_t> hist;
        void add(int m, int f) {
            ++n;
            const int d = m - f;
            const double g = m - f * 127.0 / 128.0; // with the gain taken out
            sum += d, sum2 += double(d) * d, sumG += g, sumG2 += g * g;
            maxAbs = std::max(maxAbs, std::abs(d));
            ++hist[std::clamp(int(std::lround(g)), -5, 5)];
            if (m && m != limit) // the top, 32767, is odd
                ++nonzero, even += (m & 1) == 0;
            if (f && f != limit)
                ++nonzeroF, evenF += (f & 1) == 0;
            clipM += m >= limit || m <= -limit - 1;
            clipF += f >= limit || f <= -limit - 1;
            if (std::abs(f) < limit - 1000 && std::abs(m) < limit - 1000)
                mf += double(m) * f, ff += double(f) * f;
        }
        void print(const char *name) const {
            if (!n) {
                std::printf("%-34s no samples\n", name);
                return;
            }
            std::printf("%-34s %7llu: m-f mean %+7.2f rms %7.2f max %5d; gain out: mean "
                        "%+6.2f rms %6.2f; "
                        "gain %.5f; even %5.1f%% (float %4.1f%%); clip %.2f%%/%.2f%%\n",
                        name, (unsigned long long)n, sum / n, std::sqrt(sum2 / n), maxAbs, sumG / n,
                        std::sqrt(sumG2 / n), ff > 0 ? mf / ff : 0.0, nonzero ? 100.0 * even / nonzero : 0.0,
                        nonzeroF ? 100.0 * evenF / nonzeroF : 0.0, 100.0 * clipM / n, 100.0 * clipF / n);
        }
        void printHist() const {
            std::printf("%36s", "m - f x 127/128, rounded:");
            for (const auto &[k, c] : hist)
                std::printf(" %s%d %.1f%%", k == -5 ? "<=" : k == 5 ? ">=" : "", k, 100.0 * c / n);
            std::printf("\n");
        }
    };
    int failures = 0;
    auto compare = [&](const std::vector<Voice> &voices, int frames, int outFormat, Diff &diff,
                       std::vector<int> *floatOut = nullptr, std::vector<int> *mmxOut = nullptr) {
        std::vector<int> r[2];
        for (int i = 0; i < 2; ++i)
            if (reluva::Error e = render(i, voices, frames, outFormat, r[i])) {
                if (failures++ < 5)
                    std::printf("%s mixer failed: %s\n", i ? "MMX" : "float", e.message.c_str());
                return;
            }
        diff.limit = outFormat & 3 ? 127 : 32767;
        for (size_t k = 0; k < r[0].size(); ++k)
            diff.add(r[1][k], r[0][k]);
        if (floatOut)
            *floatOut = r[0];
        if (mmxOut)
            *mmxOut = r[1];
    };

    uint64_t seed = 0x9e3779b97f4a7c15ull;
    auto random = [&seed] {
        seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17;
        return seed;
    };
    // The data of a voice from 16-bit samples (interleaved when stereo):
    // as 16-bit or as signed 8-bit (the high byte).
    auto pcm = [](const std::vector<int16_t> &s, int channels, bool eightBit) {
        Voice v;
        v.format = eightBit ? (channels == 2 ? 2 : 1) : (channels == 2 ? 8 : 4);
        v.samples = uint32_t(s.size() / size_t(channels));
        for (int16_t x : s) {
            if (eightBit)
                v.bytes.push_back(uint8_t(x >> 8));
            else
                v.bytes.push_back(uint8_t(x)), v.bytes.push_back(uint8_t(uint16_t(x) >> 8));
        }
        return v;
    };
    const int frames = 4096;

    // The real sounds: Gen.bnk 29, 32, 41 (PCM), 33, 35 (EA-XA) and the
    // Corvette's car.bnk 0, 1 (stereo PCM, their left channel).
    struct Real {
        std::string name;
        std::vector<int16_t> samples;
        std::vector<uint8_t> eaxa; // the data as in the bank, when EA-XA
        uint32_t count = 0;
    };
    std::vector<Real> real;
    auto addBank = [&](const std::vector<uint8_t> &bankData, const std::string &name,
                       std::initializer_list<int> slots) {
        reluva::nfs::SoundBank bank;
        if (reluva::nfs::parseSoundBank(bankData, bank))
            return;
        for (int s : slots) {
            if (s >= int(bank.slots.size()) || !bank.slots[size_t(s)])
                continue;
            const auto &h = *bank.slots[size_t(s)];
            reluva::audio::Sound sound;
            if (reluva::nfs::decodeBankSound(bankData, h, sound) || sound.samples.empty())
                continue;
            Real r{name + " " + std::to_string(s), {}, {}, h.samples};
            for (size_t k = 0; k < sound.samples.size(); k += size_t(sound.channels)) // the left
                r.samples.push_back(sound.samples[k]);
            if (h.compression == reluva::nfs::SoundHeader::kEaXa)
                r.eaxa.assign(bankData.begin() + h.dataOffset, bankData.end());
            real.push_back(std::move(r));
        }
    };
    std::vector<uint8_t> bankData;
    if (auto p = reluva::fs::findFile(folders.install, "GameData/Audio/SFX/gen.bnk"); p && readFile(*p, bankData))
        addBank(bankData, "Gen.bnk", {29, 32, 41, 33, 35});
    if (auto p = reluva::fs::findFile(folders.install, "GameData/CarModel/Corv/car.viv")) {
        VivArchive viv;
        if (!viv.open(p->string()) && !viv.read("car.bnk", bankData))
            addBank(bankData, "Corv car.bnk", {0, 1});
    }
    if (real.empty())
        return fail(reluva::Error::fail("no Gen.bnk or Corvette car.bnk in the install"));
    std::printf("sounds:");
    for (const Real &r : real)
        std::printf(" %s (%zu samples%s)", r.name.c_str(), r.samples.size(), r.eaxa.empty() ? "" : ", EA-XA");
    std::printf("\n");
    // `count` samples of a real sound from `offset`, repeated as needed,
    // scaled by `scale` (saturated).
    auto realSamples = [&](size_t which, size_t offset, size_t count, double scale = 1.0) {
        const auto &s = real[which % real.size()].samples;
        std::vector<int16_t> v(count);
        for (size_t k = 0; k < count; ++k)
            v[k] = int16_t(std::clamp(std::lround(s[(offset + k) % s.size()] * scale), -32768l, 32767l));
        return v;
    };
    auto need = [&](uint32_t step) { return size_t(uint64_t(frames + 64) * step >> 16) + 64; };

    // The float path's 8-bit samples (its table at 0xa0c2a0, after the set-up).
    {
        std::vector<int> ignored;
        if (reluva::Error e = render(0, {}, 256, 8, ignored))
            return fail(e);
        auto &c = floatHost.cpu();
        auto f = [&](int k) { return std::bit_cast<float>(c.read32(0xa0c2a0 + 4 * uint32_t(k))); };
        std::printf("float 8-bit table: 0x00 %g, 0x7f %g, 0x80 %g, 0xff %g\n", f(0), f(0x7f), f(0x80), f(0xff));
    }

    // 1. One real voice at the output's rate, by level.
    std::printf("\none real voice, step 1, 16-bit mono PCM, by level (L = R):\n");
    for (int level : {127, 100, 64, 32, 8, -19}) {
        Diff d;
        for (size_t s = 0; s < real.size(); ++s) {
            Voice v = pcm(realSamples(s, 0, need(0x10000)), 1, false);
            v.left = v.right = level;
            compare({v}, frames, 8, d);
        }
        d.print(("  L " + std::to_string(level)).c_str());
    }
    // 2. Several real voices into one bus, at L = 32, at the output's rate
    // and pitched.
    for (uint32_t step : {0x10000u, 0x9a00u, 0x16000u}) {
        std::printf("\nreal voices at L = 32, step %.3f:\n", step / 65536.0);
        for (int count : {1, 2, 3, 5}) {
            Diff d;
            for (int t = 0; t < std::max(1, trials / 8); ++t) {
                std::vector<Voice> voices;
                for (int k = 0; k < count; ++k) {
                    Voice v = pcm(realSamples(size_t(k + t), size_t(random() % 20000), need(step)), 1, false);
                    v.left = v.right = 32;
                    v.step = step;
                    voices.push_back(v);
                }
                compare(voices, frames, 8, d);
            }
            d.print(("  " + std::to_string(count) + " voices").c_str());
            if (count == 5 && step == 0x10000)
                d.printHist();
        }
    }
    // 3. Pans and pitches at random, real voices, 1-6 of them, mono and
    // stereo sources.
    {
        Diff d;
        for (int t = 0; t < trials; ++t) {
            std::vector<Voice> voices;
            const int count = 1 + int(random() % 6);
            for (int k = 0; k < count; ++k) {
                const uint32_t step = random() % 4 == 0 ? 0x10000 : 0x4000 + uint32_t(random() % 0x1c000);
                const bool stereo = random() % 3 == 0;
                std::vector<int16_t> s = realSamples(random(), random() % 20000, need(step) * (stereo ? 2 : 1));
                Voice v = pcm(s, stereo ? 2 : 1, false);
                const int volume = int(random() % 128);
                const int pan = int(random() % 128);
                v.left = volume * (127 - pan) / 127, v.right = volume * pan / 127;
                v.step = step;
                voices.push_back(v);
            }
            compare(voices, frames, 8, d);
        }
        std::printf("\n");
        d.print("random pans and pitches, real");
        d.printHist();
    }
    // 4. Noise at full scale, random levels (some negative), formats and pitches.
    {
        Diff d;
        for (int t = 0; t < trials; ++t) {
            std::vector<Voice> voices;
            const int count = 1 + int(random() % 4);
            for (int k = 0; k < count; ++k) {
                const uint32_t step = random() % 4 == 0 ? 0x10000 : 0x2000 + uint32_t(random() % 0x7e000);
                const bool stereo = random() % 2 == 0;
                std::vector<int16_t> s(need(step) * (stereo ? 2 : 1));
                for (auto &x : s)
                    x = int16_t(random());
                Voice v = pcm(s, stereo ? 2 : 1, false);
                v.left = int(random() % 256) - 128, v.right = int(random() % 256) - 128;
                v.step = step;
                voices.push_back(v);
            }
            compare(voices, frames, 8, d);
        }
        d.print("random noise, levels, pitches");
    }
    // 5. EA-XA, as in the bank: the decoders too (0x52d1a0 with MMX). Each
    // path's EA-XA mix is also set against its mix of the samples decoded
    // by Reluva (audio::decodeEaXa), as 16-bit PCM.
    {
        Diff d;
        Diff ownFloat;
        Diff ownMmx;
        for (size_t s = 0; s < real.size(); ++s) {
            if (real[s].eaxa.empty())
                continue;
            Voice v;
            v.bytes = real[s].eaxa;
            if (v.bytes.size() > kDataSize / 2)
                v.bytes.resize(kDataSize / 2);
            v.samples = real[s].count;
            v.compression = 7;
            v.left = v.right = 127;
            const int n = std::min<int>(frames, int(real[s].count) - 64) & ~255;
            if (n <= 0)
                continue;
            std::vector<int> f;
            std::vector<int> m;
            std::vector<int> pf;
            std::vector<int> pm;
            compare({v}, n, 8, d, &f, &m);
            Voice p = pcm(realSamples(s, 0, size_t(n) + 64), 1, false);
            p.left = p.right = 127;
            Diff ignored;
            compare({p}, n, 8, ignored, &pf, &pm);
            for (size_t k = 0; k < f.size() && k < pf.size(); ++k)
                ownFloat.add(f[k], pf[k]), ownMmx.add(m[k], pm[k]);
        }
        std::printf("\n");
        d.print("EA-XA, L 127, step 1");
        ownFloat.print("  float: EA-XA less PCM");
        ownMmx.print("  MMX: EA-XA less PCM");
    }
    // 6. 8-bit sources (no retail sound is 8-bit): the 8-bit MMX mixers and
    // resamplers.
    std::printf("\n8-bit sources (the high bytes of the real sounds):\n");
    for (int stereo = 0; stereo < 2; ++stereo)
        for (uint32_t step : {0x10000u, 0x9a00u, 0x16000u}) {
            Diff d;
            for (int t = 0; t < std::max(1, trials / 8); ++t) {
                std::vector<Voice> voices;
                for (int k = 0; k < 1 + t % 3; ++k) {
                    Voice v = pcm(realSamples(size_t(k + t), random() % 20000, need(step) * (stereo ? 2 : 1)),
                                  stereo ? 2 : 1, true);
                    v.left = 32 + 30 * k, v.right = 90 - 20 * k;
                    v.step = step;
                    voices.push_back(v);
                }
                compare(voices, frames, 8, d);
            }
            char name[64];
            std::snprintf(name, sizeof name, "  %s, step %.3f", stereo ? "stereo" : "mono", step / 65536.0);
            d.print(name);
        }
    for (int level : {127, 64, 32}) {
        Diff d;
        for (size_t s = 0; s < real.size(); ++s) {
            Voice v = pcm(realSamples(s, 0, need(0x10000)), 1, true);
            v.left = v.right = level;
            compare({v}, frames, 8, d);
        }
        d.print(("  one mono voice, step 1, L " + std::to_string(level)).c_str());
    }
    // 7. The other output formats (the game asks for 16-bit stereo).
    std::printf("\noutput formats, real voices at random levels and pitches:\n");
    for (int outFormat : {1, 2, 4, 8}) {
        Diff d;
        for (int t = 0; t < std::max(1, trials / 4); ++t) {
            std::vector<Voice> voices;
            for (int k = 0; k < 1 + t % 3; ++k) {
                const uint32_t step = 0x8000 + uint32_t(random() % 0x10000);
                Voice v = pcm(realSamples(size_t(k + t), random() % 20000, need(step)), 1, false);
                v.left = int(random() % 128), v.right = int(random() % 128);
                v.step = step;
                voices.push_back(v);
            }
            compare(voices, frames, outFormat, d);
        }
        static const char *const names[9] = {"", "  8-bit mono",   "  8-bit stereo", "", "  16-bit mono", "", "",
                                             "", "  16-bit stereo"};
        d.print(names[outFormat]);
    }
    // 8. The bus saturating: a voice made loud (x 4), three copies of it,
    // then two negated; and the other way round.
    std::printf("\nloud voices (a real one x 4, L = 127, step 1):\n");
    {
        size_t loudest = 0;
        for (size_t s = 0; s < real.size(); ++s)
            if (real[s].samples.size() > real[loudest].samples.size())
                loudest = s;
        const std::vector<int16_t> loud = realSamples(loudest, 0, need(0x10000), 4.0);
        std::vector<int16_t> negated(loud.size());
        for (size_t k = 0; k < loud.size(); ++k)
            negated[k] = int16_t(std::min(32767, -int(loud[k])));
        for (int order = 0; order < 2; ++order) {
            std::vector<Voice> voices;
            for (int k = 0; k < 5; ++k) {
                const bool neg = order == 0 ? k >= 3 : k < 2;
                Voice v = pcm(neg ? negated : loud, 1, false);
                v.left = v.right = 127;
                voices.push_back(v);
            }
            Diff d;
            std::vector<int> f;
            std::vector<int> m;
            compare(voices, frames, 8, d, &f, &m);
            d.print(order == 0 ? "  3 loud, then 2 negated" : "  2 negated, then 3 loud");
            double rms[2] = {};
            for (size_t k = 0; k < f.size() && k < m.size(); ++k)
                rms[0] += double(f[k]) * f[k], rms[1] += double(m[k]) * m[k];
            if (!f.empty())
                std::printf("%36s float %.0f, MMX %.0f\n", "output rms:", std::sqrt(rms[0] / double(f.size())),
                            std::sqrt(rms[1] / double(f.size())));
        }
    }
    // 9. The MMX mix down by itself (0x520920) on random bus words.
    {
        auto &c = mmxHost.cpu();
        c.write32(0x5643b4, 1);
        int bad = 0;
        for (int t = 0; t < 8; ++t) {
            std::vector<int16_t> words(512);
            for (size_t k = 0; k < words.size(); ++k) {
                words[k] = int16_t(random());
                c.write16(data[1] + 2 * uint32_t(k), uint16_t(words[k]));
            }
            c.reg[Cpu::ESP] = stack[1] + (1 << 16) - 64;
            uint32_t ignored = 0;
            if (reluva::Error e = c.call(0x520920, {256, data[1], out[1]}, ignored))
                return fail(e);
            for (size_t k = 0; k < words.size(); ++k)
                bad += int16_t(c.read16(out[1] + 2 * uint32_t(k))) != std::clamp(2 * int(words[k]), -32768, 32767);
        }
        std::printf("\nMMX mix down 0x520920 on 4096 random words: %s\n",
                    bad ? (std::to_string(bad) + " not 2 x the word, saturated").c_str()
                        : "each 2 x the word, saturated");
    }
    std::printf("\nMMX routines run:");
    for (size_t k = 0; k < kWatched; ++k)
        std::printf("%s %s (%06x) %llu", k ? "," : "", watched[k].second, watched[k].first,
                    (unsigned long long)runs[k]);
    std::printf("\n");
    return failures ? 1 : 0;
}

const reluva::tool::Commands kCommands(
    "nfs",
    {
        {"host",
         "<game folder> [-args \"nfs3.exe\"] [-renderer softtria.dll] [-limit N] [-log N] [-save dir] [-frames dir] [-every N] [-framesfrom N] [-watch address]... [-watchlimit 50] [-watchmem address size] [-watchread address | reg+offset]... (dwords printed at each -watch stop) [-slice instructions] [-dump memory.bin] [-keys ms:key,...] [-poke WHEN:ADDR:VALUE[:SIZE]]... [-pokeevery ...]... [-dumpmem WHEN:ADDR:SIZE:file]... (WHEN ms=N or at=ADDR[#N]; VALUE a number, size 1/2/4, or bytes=0a0b...) (keys: enter, up, a..., kp0-kp9, click, moveDX/DY) [-framelog frames.csv] [-timescale 0.1] [-thrashlog fromframe toframe] [-native] [-wav sound.wav] [-voices voices.csv] [-joystick name] (keys also joy:A=V...: axes 0-5 = V, button bB; +key/-key held) [-wheel] [-ff] (the joystick a wheel; with force feedback) [-forcelog forces.csv] [-net] (the network) [-netaddr a.b.c.d] [-broadcast a.b.c.d,...] [-netlog messages.txt] (each message sent and received, in hex) [-netdrop N[:fromMs[-toMs]]] (every Nth packet sent lost, over those game ms) [-serial listen:port | host:port] (COM ports: a link over TCP, with -net) [-modem listen:port | host:port] (a TAPI modem whose calls are such links) [-deterministic instructionsPerMs] (a clock of instructions; the end state hashed) [-mhz N] (the processor's clock the time stamp counter shows; 300 by default) [-mmx] (CPUID reports MMX: the game mixes its sound with its MMX routines) [-count ADDR] (how many times the instruction at ADDR ran; repeatable) [-drive CAR:TARGET:FROMMS[:TOMS[:GAP]]] (CAR, a human's, steered at TARGET from the controls; full gas, or GAP metres behind at its speed) [-drivesign -1] (TARGET road: along the virtual road, GAP metres right of it, slowed for bends) [-hotspots from to] (instructions counted by where they run, 16-byte blocks) [-gpulog] (THRASH recorded as for the GPU; each frame's list summed up) [-gpudump list dir] (that list in full: list.csv and its textures) [-present voodoo2a.dll] (the renderer the game is told of, -renderer's loaded in its place; the default; none: as -renderer) [-positional] (network races with positional updates, every player alike) [-pointfilter] (native drawing: textures point-sampled, not bilinear) [-direct3d] (the game told of d3da.dll, the Direct3D renderer: its Direct3D look, drawn natively as d3da's device draws; -present d3da.dll -native) [-glide] (the game's own voodoo2a.dll, unchanged, on Reluva's Glide stand-in) [-glidecompare out.csv] (-glide, with each flip also drawn natively and compared) [-glidefilter point | bilinear] (with -glide: the texture filter held, to compare) [-glidegpu] (with -glide: Glide's pixels drawn on the GPU, if there is one) [-glidegpucompare out.csv] (-glide on the GPU, each call also drawn in software and each frame compared: its mean difference, of 255, per flip) [-glidedll file] (-glide with this THRASH DLL in voodoo2a.dll's place, from outside the install, e.g. the CD's Patch/Voodoo3/voodoo2a.dll: Glide 3 on a Voodoo3) [-glideboard voodoo1 | rush | voodoo2 | banshee | voodoo3] (-glide on this board, e.g. -glidedll <install>/voodooa.dll -glideboard voodoo1; the Rush's desktop is 1024x768 at 16 bits unless given: -glideboard rush,desktop=1280x1024x16)",
         "run the retail nfs3.exe in the x86 emulator, without a window, and report how it goes: its system calls, files, THRASH's work, frames, sound, the network",
         cmdNfsHost},
        {"huff-check", "<game folder> [out dir]",
         "every Huffman-compressed file decompressed by the game's own routine and by Reluva's, which must agree",
         cmdNfsHuffCheck},
        {"native-check", "<game folder> [trials]",
         "each routine done natively against its original in the emulator, on random inputs", cmdNfsNativeCheck},
        {"mmx-check", "<game folder> [trials]",
         "the game's MMX sound mixing against its plain routines, on random inputs", cmdNfsMmxCheck},
    });

} // namespace
