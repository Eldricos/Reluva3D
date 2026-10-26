// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game_paths.h"

#include "common/x86/cpu.h"
#include "common/x86/win32.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace reluva::awe {

namespace {

// Where the English 1.0 motoe.exe has what's called, and its first bytes
// there (to be sure it is that executable).
struct Entry {
    uint32_t address;
    uint8_t bytes[8];
};
constexpr Entry kConstructor{0x42e840, {0x8b, 0xc1, 0xb1, 0xff, 0xc7, 0x00, 0x20, 0x2d}};
constexpr Entry kBuildTables{0x42e900, {0x56, 0x8b, 0xf1, 0x57, 0xb9, 0x00, 0x04, 0x00}};
constexpr Entry kSearch{0x42ef40, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec}};
constexpr Entry kStatics{0x42e820, {0xdd, 0x05, 0x18, 0x2d, 0x48, 0x00, 0xdc, 0x25}};
constexpr Entry kNew{0x472551, {0x6a, 0x01, 0xff, 0x74, 0x24, 0x08, 0xe8, 0xdb}};
constexpr Entry kDelete{0x4722d2, {0xff, 0x74, 0x24, 0x04, 0xe8, 0xe1, 0x11, 0x00}};
constexpr uint32_t kObjectSize = 0x4d4600;
constexpr uint32_t kGridOffset = 8; // the object's copy of the grid, a * 64 + b

} // namespace

GamePathFinder::GamePathFinder() = default;
GamePathFinder::~GamePathFinder() = default;

Error GamePathFinder::load(const std::vector<uint8_t>& exe) {
    cpu_ = std::make_unique<x86::Cpu>(160u << 20);
    process_ = std::make_unique<x86::Win32Process>(*cpu_);
    const x86::Win32Process::Module* module = nullptr;
    if (Error e = process_->load("motoe.exe", exe, module))
        return e;
    for (const Entry& f : {kConstructor, kBuildTables, kSearch, kStatics, kNew, kDelete})
        for (int i = 0; i < 8; i++)
            if (cpu_->read8(f.address + uint32_t(i)) != f.bytes[i])
                return Error::fail("not the motoe.exe this knows (English 1.0)");
    // Its C runtime isn't started, so its heap isn't there: operator new
    // and delete jump to the interpreter's instead.
    auto jump = [&](uint32_t from, uint32_t to) {
        cpu_->write8(from, 0xe9);
        cpu_->write32(from + 1, to - (from + 5));
    };
    x86::Arena& mem = process_->memory();
    jump(kNew.address, cpu_->addNative("operator new", [&mem](x86::Cpu& c) {
             c.reg[x86::Cpu::EAX] = mem.alloc(std::max(1u, c.arg(0)));
             return 0u;
         }));
    jump(kDelete.address, cpu_->addNative("operator delete", [&mem](x86::Cpu& c) {
             if (c.arg(0))
                 mem.free(c.arg(0));
             return 0u;
         }));
    cpu_->setInstructionLimit(4'000'000'000ull);
    uint32_t result = 0;
    // A static of its own (2 pi, less a little), then the object as the
    // game makes it (0x4025a0): constructed, then its tables built.
    if (Error e = cpu_->call(kStatics.address, {}, result))
        return Error::fail("motoe.exe 0x42e820: " + e.message);
    object_ = mem.alloc(kObjectSize);
    if (!object_)
        return Error::fail("Out of emulated memory");
    cpu_->reg[x86::Cpu::ECX] = object_;
    if (Error e = cpu_->call(kConstructor.address, {}, result))
        return Error::fail("motoe.exe 0x42e840: " + e.message);
    cpu_->reg[x86::Cpu::ECX] = object_;
    if (Error e = cpu_->call(kBuildTables.address, {}, result))
        return Error::fail("motoe.exe 0x42e900: " + e.message);
    return Error::success();
}

Error GamePathFinder::search(const WalkGrid& grid, bool openThrees, WalkGrid::Cell from, WalkGrid::Cell to,
                             bool allowTwos, std::vector<Point>& out) {
    out.clear();
    if (!object_)
        return Error::fail("motoe.exe isn't loaded");
    // The grid as 0x42e9e0 copies it (3s cleared for the later tries).
    for (int a = 0; a < WalkGrid::kSize; a++)
        for (int b = 0; b < WalkGrid::kSize; b++) {
            uint8_t v = grid.at(a, b);
            if (openThrees && v == 3)
                v = 0;
            cpu_->write8(object_ + kGridOffset + uint32_t(a * 64 + b), v);
        }
    // Search(double startA, double startB, double goalA, double goalB,
    //        int, int, entry* out, int allowTwos) -> points; each entry
    // three doubles: a, b and a heading.
    const uint32_t buffer = process_->memory().alloc(24 * 256);
    std::vector<uint32_t> args;
    for (double d : {double(from.a), double(from.b), double(to.a), double(to.b)}) {
        uint64_t bits;
        std::memcpy(&bits, &d, 8);
        args.push_back(uint32_t(bits));
        args.push_back(uint32_t(bits >> 32));
    }
    args.insert(args.end(), {0, 0, buffer, allowTwos ? 1u : 0u});
    cpu_->reg[x86::Cpu::ECX] = object_;
    uint32_t count = 0;
    Error e = cpu_->call(kSearch.address, args, count);
    if (!e)
        for (int32_t i = 0; i < int32_t(count) && i < 256; i++) {
            Point p;
            for (int k = 0; k < 3; k++) {
                uint64_t bits = uint64_t(cpu_->read32(buffer + uint32_t(24 * i + 8 * k))) |
                                uint64_t(cpu_->read32(buffer + uint32_t(24 * i + 8 * k + 4))) << 32;
                std::memcpy(k == 0 ? &p.a : k == 1 ? &p.b : &p.heading, &bits, 8);
            }
            out.push_back(p);
        }
    process_->memory().free(buffer);
    return e ? Error::fail("motoe.exe 0x42ef40: " + e.message) : Error::success();
}

std::vector<WalkGrid::Cell> GamePathFinder::findPath(const WalkGrid& grid, WalkGrid::Cell from, WalkGrid::Cell to,
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
