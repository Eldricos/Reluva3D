// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/x86/jit.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#if defined(_WIN64) && (defined(_M_X64) || defined(__x86_64__))
#define RELUVA_JIT_WINDOWS 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) && defined(__x86_64__)
#define RELUVA_JIT_LINUX 1
#include <sys/mman.h>
extern "C" void __register_frame(void*);
extern "C" void __deregister_frame(void*);
#endif

namespace reluva::x86 {

namespace {

constexpr size_t kBufferSize = size_t(32) << 20;
// The frame: push rbx, push r12, push r13, sub rsp 32 (rsp 16-aligned at
// the calls, and their argument space).
constexpr uint8_t kPrologue[] = {0x53, 0x41, 0x54, 0x41, 0x55, 0x48, 0x83, 0xec, 0x20};
// add rsp 32, pop r13, pop r12, pop rbx, ret.
constexpr uint8_t kEpilogue[] = {0x48, 0x83, 0xc4, 0x20, 0x41, 0x5d, 0x41, 0x5c, 0x5b, 0xc3};
#if defined(RELUVA_JIT_WINDOWS)
constexpr uint8_t kFirstArgCpu[] = {0x48, 0x89, 0xd9};  // mov rcx, rbx
constexpr uint8_t kSecondArgImm64[] = {0x48, 0xba};      // mov rdx, imm64
constexpr uint8_t kSecondArgEax[] = {0x89, 0xc2};        // mov edx, eax
constexpr uint8_t kThirdArgImm32[] = {0x41, 0xb8};       // mov r8d, imm32
constexpr uint8_t kEntryTakeArgs[] = {0x48, 0x89, 0xcb}; // mov rbx, rcx
constexpr uint8_t kEntryJump[] = {0xff, 0xe2};           // jmp rdx
#else
constexpr uint8_t kFirstArgCpu[] = {0x48, 0x89, 0xdf};  // mov rdi, rbx
constexpr uint8_t kSecondArgImm64[] = {0x48, 0xbe};      // mov rsi, imm64
constexpr uint8_t kSecondArgEax[] = {0x89, 0xc6};        // mov esi, eax
constexpr uint8_t kThirdArgImm32[] = {0xba};             // mov edx, imm32
constexpr uint8_t kEntryTakeArgs[] = {0x48, 0x89, 0xfb}; // mov rbx, rdi
constexpr uint8_t kEntryJump[] = {0xff, 0xe6};           // jmp rsi
#endif

#if defined(RELUVA_JIT_WINDOWS) || defined(RELUVA_JIT_LINUX)
// Executable memory, near this program's code if it can be (calls to the
// handlers then take 32-bit offsets).
uint8_t* allocate(size_t size) {
    const uintptr_t here = reinterpret_cast<uintptr_t>(&allocate);
    const uintptr_t step = 64 << 20;
#if defined(RELUVA_JIT_WINDOWS)
    for (uintptr_t d = step; d < (uintptr_t(1) << 30); d += step)
        for (const uintptr_t at : {here - d, here + d})
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at & ~uintptr_t(0xffff)), size,
                                       MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                return static_cast<uint8_t*>(p);
    return static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
    for (uintptr_t d = step; d < (uintptr_t(1) << 30); d += step) {
        void* p = mmap(reinterpret_cast<void*>((here - d) & ~uintptr_t(0xfff)), size,
                       PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
            return nullptr;
        const intptr_t off = intptr_t(reinterpret_cast<uintptr_t>(p) - here);
        if (off > -(intptr_t(1) << 31) + intptr_t(size) && off < (intptr_t(1) << 31) - intptr_t(size))
            return static_cast<uint8_t*>(p);
        munmap(p, size); // the hint not taken: further off, try again
    }
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<uint8_t*>(p);
#endif
}

void release(uint8_t* p, size_t size) {
#if defined(RELUVA_JIT_WINDOWS)
    (void)size;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, size);
#endif
}
#endif

// Writes x86-64 code. The registers it uses: eax, ecx, edx (scratch), rbx
// (the Cpu), r12 (memory), r13 (the pages' marks), r11 (far calls).
struct Emitter {
    uint8_t* at;
    void bytes(const uint8_t* b, size_t n) {
        std::memcpy(at, b, n);
        at += n;
    }
    template <size_t N>
    void bytes(const uint8_t (&b)[N]) {
        bytes(b, N);
    }
    void u8(uint8_t v) { *at++ = v; }
    void u32(uint32_t v) {
        std::memcpy(at, &v, 4);
        at += 4;
    }
    void u64(uint64_t v) {
        std::memcpy(at, &v, 8);
        at += 8;
    }
    // op [rbx + disp32] with ModRM's reg field `r`.
    void rbx(std::initializer_list<uint8_t> op, int r, size_t disp) {
        for (uint8_t b : op)
            u8(b);
        u8(uint8_t(0x83 | r << 3));
        u32(uint32_t(disp));
    }
    // A rel32 jump or call's offset, written later.
    uint8_t* rel32() {
        uint8_t* p = at;
        u32(0);
        return p;
    }
    static void patch(uint8_t* field, const uint8_t* target) {
        const int32_t rel = int32_t(target - (field + 4));
        std::memcpy(field, &rel, 4);
    }
    void call(const void* fn) {
        const intptr_t rel = intptr_t(reinterpret_cast<uintptr_t>(fn)) - intptr_t(reinterpret_cast<uintptr_t>(at + 5));
        if (rel >= INT32_MIN && rel <= INT32_MAX) {
            u8(0xe8), u32(uint32_t(int32_t(rel)));
        } else {
            u8(0x49), u8(0xbb), u64(reinterpret_cast<uint64_t>(fn)); // mov r11, fn
            u8(0x41), u8(0xff), u8(0xd3);                            // call r11
        }
    }
};

enum : int { kEax = 0, kEcx = 1, kEdx = 2 };

} // namespace

bool Jit::supported() {
#if defined(RELUVA_JIT_WINDOWS) || defined(RELUVA_JIT_LINUX)
    return true;
#else
    return false;
#endif
}

Jit::Jit(const Layout& layout) : layout_(layout) {
#if defined(RELUVA_JIT_WINDOWS) || defined(RELUVA_JIT_LINUX)
    base_ = allocate(kBufferSize);
    if (!base_)
        return;
    size_ = kBufferSize;
    Emitter w{base_};
    [[maybe_unused]] size_t entryAt = 0;
#if defined(RELUVA_JIT_WINDOWS)
    // UNWIND_INFO: version 1; a 9-byte prologue; four codes, last first: at
    // 9 UWOP_ALLOC_SMALL 32, at 5 UWOP_PUSH_NONVOL r13, at 3 r12, at 1 rbx.
    const uint8_t info[] = {0x01, 0x09, 0x04, 0x00, 0x09, 0x32, 0x05, 0xd0, 0x03, 0xc0, 0x01, 0x30};
    w.bytes(info);
    entryAt = 16;
    w.at = base_ + entryAt;
#else
    // .eh_frame: a CIE (return address at the CFA - 8, the CFA rsp + 8 on
    // entry) and an FDE for the whole buffer: the pushes (rbx, r12, r13
    // each below the last) and sub rsp 32 leave the CFA at rsp + 64.
    std::vector<uint8_t> eh;
    auto u32 = [&](uint32_t v) {
        eh.insert(eh.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
    };
    auto u64 = [&](uint64_t v) {
        eh.insert(eh.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 8);
    };
    const uint8_t cie[] = {0x01, 0x00, 0x01, 0x78, 0x10, 0x0c, 0x07, 0x08, 0x90, 0x01, 0x00, 0x00};
    u32(4 + sizeof cie);
    u32(0); // a CIE
    eh.insert(eh.end(), cie, cie + sizeof cie);
    const size_t fdeAt = eh.size();
    const uint8_t fde[] = {0x41, 0x0e, 0x10, 0x83, 0x02, // push rbx
                           0x42, 0x0e, 0x18, 0x8c, 0x03, // push r12
                           0x42, 0x0e, 0x20, 0x8d, 0x04, // push r13
                           0x44, 0x0e, 0x40, 0x00, 0x00}; // sub rsp 32
    u32(4 + 16 + sizeof fde);
    u32(uint32_t(fdeAt + 4)); // back to the CIE, from this field
    u64(reinterpret_cast<uint64_t>(base_));
    u64(kBufferSize);
    eh.insert(eh.end(), fde, fde + sizeof fde);
    u32(0); // the end
    uint8_t* frame = new uint8_t[eh.size()];
    std::memcpy(frame, eh.data(), eh.size());
    __register_frame(frame);
    unwind_ = frame;
#endif
    entry_ = reinterpret_cast<void (*)(void*, const uint8_t*)>(w.at);
    w.bytes(kPrologue);
    w.bytes(kEntryTakeArgs);
    w.u8(0x49), w.u8(0xbc), w.u64(reinterpret_cast<uint64_t>(layout_.memory)); // mov r12, memory
    w.u8(0x49), w.u8(0xbd), w.u64(reinterpret_cast<uint64_t>(layout_.pages));  // mov r13, pages
    w.bytes(kEntryJump);
    bodyStart_ = used_ = (size_t(w.at - base_) + 15) & ~size_t(15);
#if defined(RELUVA_JIT_WINDOWS)
    auto* fn = new RUNTIME_FUNCTION{};
    fn->BeginAddress = DWORD(entryAt);
    fn->EndAddress = DWORD(kBufferSize);
    fn->UnwindData = 0;
    if (!RtlAddFunctionTable(fn, 1, reinterpret_cast<DWORD64>(base_))) {
        delete fn;
        release(base_, size_);
        base_ = nullptr;
        return;
    }
    unwind_ = fn;
#endif
#endif
}

Jit::~Jit() {
#if defined(RELUVA_JIT_WINDOWS)
    if (unwind_) {
        RtlDeleteFunctionTable(static_cast<RUNTIME_FUNCTION*>(unwind_));
        delete static_cast<RUNTIME_FUNCTION*>(unwind_);
    }
#elif defined(RELUVA_JIT_LINUX)
    if (unwind_) {
        __deregister_frame(unwind_);
        delete[] static_cast<uint8_t*>(unwind_);
    }
#endif
#if defined(RELUVA_JIT_WINDOWS) || defined(RELUVA_JIT_LINUX)
    if (base_)
        release(base_, size_);
#endif
}

const uint8_t* Jit::compile(const Op* ops, int count, uint32_t end) {
    if (!base_)
        return nullptr;
    const size_t most = size_t(count) * 160 + 64;
    if (used_ + most > size_)
        return nullptr;
    const Layout& L = layout_;
    uint8_t* code = base_ + used_;
    Emitter w{code};
    auto regAt = [&](int r) { return L.reg + 4 * size_t(r); };
    // A register of `size` bytes' place: AL..BL the low bytes, AH..BH the next.
    auto reg8At = [&](int r) { return L.reg + 4 * size_t(r & 3) + size_t(r >> 2); };
    auto loadReg = [&](int dst, int r, int size) { // dst <- the register (zero-extended)
        if (size == 4)
            w.rbx({0x8b}, dst, regAt(r));
        else
            w.rbx({0x0f, 0xb6}, dst, reg8At(r));
    };
    auto storeReg = [&](int r, int src, int size) { // the register <- src
        if (size == 4)
            w.rbx({0x89}, src, regAt(r));
        else
            w.rbx({0x88}, src, reg8At(r)); // src's low byte: al, cl or dl
    };
    // eax <- the memory operand's address, as CachedOps::address (LEA's
    // leaves FS's base out, as CachedOps::lea does).
    auto address = [&](const Op& o, bool withFs = true) {
        w.u8(0xb8), w.u32(o.disp); // mov eax, disp
        if (o.base >= 0)
            w.rbx({0x03}, kEax, regAt(o.base)); // add eax, [base]
        if (o.index >= 0) {
            w.rbx({0x8b}, kEcx, regAt(o.index)); // mov ecx, [index]
            if (o.scale)
                w.u8(0xc1), w.u8(0xe1), w.u8(o.scale); // shl ecx, scale
            w.u8(0x01), w.u8(0xc8);                   // add eax, ecx
        }
        if (o.fs && withFs)
            w.rbx({0x03}, kEax, L.fsBase); // add eax, [fsBase]
    };
    // Faults to make after the block: where to jump from, the instruction,
    // and the access's size.
    struct Fault {
        uint8_t* from;
        const Op* op;
        uint8_t size;
    };
    struct Mark {
        uint8_t* from;
        uint8_t* back;
    };
    Fault faults[64];
    Mark marks[64];
    int nFaults = 0, nMarks = 0;
    // Cpu::valid(eax, size), else the fault: lea ecx, [rax - 4096];
    // cmp ecx, span - size; ja fault.
    auto check = [&](const Op& o, int size) {
        w.u8(0x8d), w.u8(0x88), w.u32(uint32_t(-4096));
        w.u8(0x81), w.u8(0xf9), w.u32(L.span - uint32_t(size));
        w.u8(0x0f), w.u8(0x87);
        faults[nFaults++] = {w.rel32(), &o, uint8_t(size)};
    };
    auto load = [&](const Op& o, int dst, int size) { // dst <- memory at eax
        check(o, size);
        if (size == 4)
            w.u8(0x41), w.u8(0x8b), w.u8(uint8_t(0x04 | dst << 3)), w.u8(0x04); // mov dst, [r12 + rax]
        else
            w.u8(0x41), w.u8(0x0f), w.u8(0xb6), w.u8(uint8_t(0x04 | dst << 3)), w.u8(0x04); // movzx
    };
    auto store = [&](const Op& o, int src, int size) { // memory at eax <- src (ecx is taken)
        check(o, size);
        // Cpu::notePageWrite: mov ecx, eax; shr ecx, 12; cmp byte [r13 + rcx], 0; jne mark.
        w.u8(0x89), w.u8(0xc1);
        w.u8(0xc1), w.u8(0xe9), w.u8(12);
        w.u8(0x41), w.u8(0x80), w.u8(0x7c), w.u8(0x0d), w.u8(0x00), w.u8(0x00);
        w.u8(0x0f), w.u8(0x85);
        uint8_t* from = w.rel32();
        marks[nMarks++] = {from, w.at};
        if (size == 4)
            w.u8(0x41), w.u8(0x89), w.u8(uint8_t(0x04 | src << 3)), w.u8(0x04); // mov [r12 + rax], src
        else
            w.u8(0x41), w.u8(0x88), w.u8(uint8_t(0x04 | src << 3)), w.u8(0x04);
    };
    auto storeImm32 = [&](size_t disp, uint32_t v) { // mov dword [rbx + disp], v
        w.u8(0xc7), w.u8(0x83), w.u32(uint32_t(disp)), w.u32(v);
    };
    bool eipSet = false; // by the last: a handler sets it
    for (int i = 0; i < count; ++i) {
        const Op& o = ops[i];
        eipSet = false;
        switch (o.kind) {
        case Kind::MovRegRm:
            if (o.mem) {
                address(o);
                load(o, kEdx, o.size);
            } else
                loadReg(kEdx, o.rm, o.size);
            storeReg(o.reg, kEdx, o.size);
            break;
        case Kind::MovRmReg:
            loadReg(kEdx, o.reg, o.size);
            if (o.mem) {
                address(o);
                store(o, kEdx, o.size);
            } else
                storeReg(o.rm, kEdx, o.size);
            break;
        case Kind::MovRmImm:
            w.u8(0xba), w.u32(o.imm); // mov edx, imm
            if (o.mem) {
                address(o);
                store(o, kEdx, o.size);
            } else
                storeReg(o.rm, kEdx, o.size);
            break;
        case Kind::MovRegImm:
            storeImm32(regAt(o.rm), o.imm);
            break;
        case Kind::Lea:
            address(o, false);
            storeReg(o.reg, kEax, 4);
            break;
        case Kind::PushReg:
        case Kind::PushImm:
            // Cpu::push: the value first, then ESP down, then the store.
            if (o.kind == Kind::PushReg)
                loadReg(kEdx, o.rm, 4);
            else
                w.u8(0xba), w.u32(o.imm);
            w.rbx({0x8b}, kEax, regAt(4));          // mov eax, [esp]
            w.u8(0x83), w.u8(0xe8), w.u8(4);         // sub eax, 4
            w.rbx({0x89}, kEax, regAt(4));          // mov [esp], eax
            store(o, kEdx, 4);
            break;
        case Kind::PopReg:
            // Cpu::pop: the load, then ESP up, then the register.
            w.rbx({0x8b}, kEax, regAt(4));
            load(o, kEdx, 4);
            w.rbx({0x83}, 0, regAt(4)), w.u8(4); // add dword [esp], 4
            storeReg(o.rm, kEdx, 4);
            break;
        case Kind::Call:
            storeImm32(L.eip, o.start);
            storeImm32(L.start, o.start);
            w.bytes(kFirstArgCpu);
            w.bytes(kSecondArgImm64), w.u64(reinterpret_cast<uint64_t>(o.entry));
            w.call(o.handler);
            eipSet = true;
            break;
        }
    }
    if (!eipSet) { // the last instruction made here: where it goes on, and its start
        storeImm32(L.eip, end);
        storeImm32(L.start, ops[count - 1].start);
    }
    w.bytes(kEpilogue);
    // A write to a page holding cached code: codeEpoch_ on (inc dword), then back.
    for (int i = 0; i < nMarks; ++i) {
        Emitter::patch(marks[i].from, w.at);
        w.rbx({0xff}, 0, L.epoch);
        w.u8(0xe9);
        Emitter::patch(w.rel32(), marks[i].back);
    }
    // Faults: eip and the start as the handler would have had them, then
    // Cpu::memoryFault (which throws; int3 after it, never reached, so
    // the return address isn't taken for an epilogue).
    for (int i = 0; i < nFaults; ++i) {
        Emitter::patch(faults[i].from, w.at);
        storeImm32(L.eip, faults[i].op->start + faults[i].op->length);
        storeImm32(L.start, faults[i].op->start);
        w.bytes(kFirstArgCpu);
        w.bytes(kSecondArgEax);
        w.bytes(kThirdArgImm32), w.u32(faults[i].size);
        w.call(reinterpret_cast<const void*>(L.memoryFault));
        w.u8(0xcc);
    }
    used_ = (size_t(w.at - base_) + 15) & ~size_t(15);
#if defined(RELUVA_JIT_WINDOWS)
    FlushInstructionCache(GetCurrentProcess(), code, size_t(w.at - code));
#endif
    return code;
}

void Jit::reset() {
    used_ = bodyStart_;
    ++generation_;
}

} // namespace reluva::x86
