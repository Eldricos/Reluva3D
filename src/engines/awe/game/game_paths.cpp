// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game/game_paths.h"

#include "engines/awe/games.h"

#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace reluva::awe {

namespace {

constexpr uint32_t kGridOffset = 8; // the object's copy of the grid, a * 64 + b

} // namespace

GamePathFinder::GamePathFinder() = default;
GamePathFinder::~GamePathFinder() = default;

Error GamePathFinder::load(const std::vector<uint8_t> &exe, const GameInfo &game, bool single) {
    const PathFinderCode &code = game.pathFinder;
    _exe = game.exe ? game.exe : game.id;
    _search = code.search.address;
    if (!code.search.address)
        return Error::fail(std::string(game.id) + ": its path-finder isn't known");
    _cpu = std::make_unique<x86::Cpu>(160u << 20);
    _process = std::make_unique<x86::Win32Process>(*_cpu, "host.exe", true);
    const x86::Win32Process::Module *module = nullptr;
    if (Error e = _process->load(_exe, exe, module))
        return e;
    for (const CodeAt &f :
         {code.constructor, code.buildTables, code.search, code.statics, code.operatorNew, code.operatorDelete})
        for (int i = 0; i < 8; i++)
            if (_cpu->read8(f.address + uint32_t(i)) != f.bytes[i])
                return Error::fail("not the " + _exe + " this knows (the English release's)");
    // Its C runtime isn't started, so its heap isn't there: operator new
    // and delete jump to the interpreter's instead.
    auto jump = [&](uint32_t from, uint32_t to) {
        _cpu->write8(from, 0xe9);
        _cpu->write32(from + 1, to - (from + 5));
    };
    x86::Arena &mem = _process->memory();
    jump(code.operatorNew.address, _cpu->addNative("operator new", [&mem](x86::Cpu &c) {
        c.reg[x86::Cpu::EAX] = mem.alloc(std::max(1u, c.arg(0)));
        return 0u;
    }));
    jump(code.operatorDelete.address, _cpu->addNative("operator delete", [&mem](x86::Cpu &c) {
        if (c.arg(0))
            mem.free(c.arg(0));
        return 0u;
    }));
    _cpu->setInstructionLimit(4'000'000'000ull);
    uint32_t result = 0;
    // A static of its own (2 pi, less a little), then the object as the
    // game makes it (0x4025a0): constructed, then its tables built.
    auto at = [&](const CodeAt &f) {
        char buf[16];
        std::snprintf(buf, sizeof buf, " 0x%x: ", f.address);
        return _exe + buf;
    };
    if (Error e = _cpu->call(code.statics.address, {}, result))
        return Error::fail(at(code.statics) + e.message);
    // The statics are made at start-up, at 53 bits; the object after
    // Direct3D's CreateDevice (0x401a34, then 0x4024e0), at 24.
    setSinglePrecision(single);
    _object = mem.alloc(code.objectSize);
    if (!_object)
        return Error::fail("Out of emulated memory");
    _cpu->reg[x86::Cpu::ECX] = _object;
    if (Error e = _cpu->call(code.constructor.address, {}, result))
        return Error::fail(at(code.constructor) + e.message);
    _cpu->reg[x86::Cpu::ECX] = _object;
    if (Error e = _cpu->call(code.buildTables.address, {}, result))
        return Error::fail(at(code.buildTables) + e.message);
    return Error::success();
}

void GamePathFinder::setSinglePrecision(bool on) {
    _cpu->fpu.control = uint16_t((_cpu->fpu.control & ~0x300u) | (on ? 0u : 0x200u));
}

Error GamePathFinder::search(const WalkGrid &grid, bool openThrees, WalkGrid::Cell from, WalkGrid::Cell to,
                             bool allowTwos, std::vector<Point> &out) {
    out.clear();
    if (!_object)
        return Error::fail("the game's path-finder isn't loaded");
    // The grid as motoe.exe 0x42e9e0 copies it (3s cleared for the later
    // tries).
    for (int a = 0; a < WalkGrid::kSize; a++)
        for (int b = 0; b < WalkGrid::kSize; b++) {
            uint8_t v = grid.at(a, b);
            if (openThrees && v == 3)
                v = 0;
            _cpu->write8(_object + kGridOffset + uint32_t(a * 64 + b), v);
        }
    // Search(double startA, double startB, double goalA, double goalB,
    //        int, int, entry* out, int allowTwos) -> points; each entry
    // three doubles: a, b and a heading.
    const uint32_t buffer = _process->memory().alloc(24 * 256);
    std::vector<uint32_t> args;
    for (double d : {double(from.a), double(from.b), double(to.a), double(to.b)}) {
        uint64_t bits;
        std::memcpy(&bits, &d, 8);
        args.push_back(uint32_t(bits));
        args.push_back(uint32_t(bits >> 32));
    }
    args.insert(args.end(), {0, 0, buffer, allowTwos ? 1u : 0u});
    _cpu->reg[x86::Cpu::ECX] = _object;
    uint32_t count = 0;
    Error e = _cpu->call(_search, args, count);
    if (!e)
        for (int32_t i = 0; i < int32_t(count) && i < 256; i++) {
            Point p;
            for (int k = 0; k < 3; k++) {
                uint64_t bits = uint64_t(_cpu->read32(buffer + uint32_t(24 * i + 8 * k))) |
                                uint64_t(_cpu->read32(buffer + uint32_t(24 * i + 8 * k + 4))) << 32;
                std::memcpy(k == 0 ? &p.a : k == 1 ? &p.b : &p.heading, &bits, 8);
            }
            out.push_back(p);
        }
    _process->memory().free(buffer);
    char buf[16];
    std::snprintf(buf, sizeof buf, " 0x%x: ", _search);
    return e ? Error::fail(_exe + buf + e.message) : Error::success();
}

std::vector<WalkGrid::Cell> GamePathFinder::findPath(const WalkGrid &grid, WalkGrid::Cell from, WalkGrid::Cell to,
                                                     bool allowTwos) {
    if (grid.empty() || !from.valid() || !to.valid() || std::abs(from.a - to.a) + std::abs(from.b - to.b) < 3)
        return {};
    // As 0x440ce0 tries: the grid as it is, accepting at most 20 points;
    // with 3s open, fewer than 32; then with the start and goal cells
    // open too, fewer than 32.
    WalkGrid open = grid;
    open.set(from, 0);
    open.set(to, 0);
    std::vector<Point> points;
    if ((search(grid, false, from, to, allowTwos, points) || points.empty() || points.size() > 20) &&
        (search(grid, true, from, to, allowTwos, points) || points.empty() || points.size() >= 32) &&
        (search(open, true, from, to, allowTwos, points) || points.empty() || points.size() >= 32))
        return {};
    // Goal first, without the start; each a cell, as 0x441089 takes them
    // (truncated). Walking order is from the start.
    std::vector<WalkGrid::Cell> path{from};
    for (auto it = points.rbegin(); it != points.rend(); ++it)
        path.push_back({int(it->a), int(it->b)});
    return path;
}

} // namespace reluva::awe
