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
#include <intrin.h>
#elif defined(__linux__) && defined(__x86_64__)
#define RELUVA_JIT_LINUX 1
#include <cpuid.h>
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

bool Jit::flagsOps() {
#if defined(RELUVA_JIT_WINDOWS)
    int r[4];
    __cpuid(r, int(0x80000000));
    if (unsigned(r[0]) < 0x80000001u)
        return false;
    __cpuid(r, int(0x80000001));
    return r[2] & 1;
#elif defined(RELUVA_JIT_LINUX)
    unsigned a, b, c, d;
    if (!__get_cpuid(0x80000001u, &a, &b, &c, &d))
        return false;
    return c & 1;
#else
    return false;
#endif
}

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
    // The dispatch (see jit.h).
    w.at = base_ + ((size_t(w.at - base_) + 15) & ~size_t(15));
    dispatch_ = w.at;
    {
        const Layout& L = layout_;
        uint8_t* exits[16];
        int n = 0;
        auto exitIf = [&](uint8_t cc) { w.u8(0x0f), w.u8(cc), exits[n++] = w.rel32(); };
        w.rbx({0x8b}, kEax, L.eip);                      // mov eax, [eip]
        w.rbx({0x3b}, kEax, L.runReturn), exitIf(0x84);  // cmp eax, [return]; je exit
        w.rbx({0x3b}, kEax, L.runNative), exitIf(0x83);  // cmp eax, [natives]; jae exit
        w.rbx({0x80}, 7, L.stop), w.u8(0), exitIf(0x85); // cmp byte [stop], 0; jne exit
        w.u8(0x89), w.u8(0xc1);                          // mov ecx, eax
        w.u8(0xc1), w.u8(0xe9), w.u8(15);                // shr ecx, 15
        w.u8(0x31), w.u8(0xc1);                          // xor ecx, eax
        w.u8(0x81), w.u8(0xe1), w.u32(L.blockMask);      // and ecx, mask
        w.u8(0x48), w.u8(0x69), w.u8(0xc9), w.u32(L.blockSize); // imul rcx, rcx, size
        w.u8(0x48), w.u8(0xba), w.u64(reinterpret_cast<uint64_t>(L.blocks)); // mov rdx, blocks
        w.u8(0x48), w.u8(0x01), w.u8(0xd1);              // add rcx, rdx
        auto rcxAt = [&](std::initializer_list<uint8_t> op, int r, size_t disp) { // op [rcx + disp32]
            for (uint8_t b : op)
                w.u8(b);
            w.u8(uint8_t(0x81 | r << 3));
            w.u32(uint32_t(disp));
        };
        rcxAt({0x39}, kEax, L.blockStart), exitIf(0x85); // cmp [start], eax; jne exit
        w.rbx({0x8b}, kEdx, L.epoch);                    // mov edx, [codeEpoch]
        rcxAt({0x39}, kEdx, L.blockEpoch), exitIf(0x85); // cmp [epoch], edx; jne exit
        w.u8(0x48), w.u8(0xba), w.u64(reinterpret_cast<uint64_t>(&generation_)); // mov rdx, &generation
        w.u8(0x8b), w.u8(0x12);                          // mov edx, [rdx]
        rcxAt({0x39}, kEdx, L.blockGeneration), exitIf(0x85); // cmp [its generation], edx; jne exit
        rcxAt({0x48, 0x8b}, kEdx, L.blockJit);           // mov rdx, [jit]
        w.u8(0x48), w.u8(0x85), w.u8(0xd2), exitIf(0x84); // test rdx, rdx; jz exit
        rcxAt({0x44, 0x0f, 0xb6}, 0, L.blockCount);      // movzx r8d, byte [count]
        w.rbx({0x4c, 0x03}, 0, L.executed);              // add r8, [executed]
        w.rbx({0x4c, 0x3b}, 0, L.runEnd), exitIf(0x87);  // cmp r8, [end]; ja exit
        w.rbx({0x4c, 0x89}, 0, L.executed);              // mov [executed], r8
        w.rbx({0x48, 0x89}, kEcx, L.countingBlock);      // mov [counting block], rcx
        w.rbx({0x89}, kEax, L.countingStart);            // mov [counting start], eax
        w.u8(0xff), w.u8(0xe2);                          // jmp rdx
        for (int i = 0; i < n; ++i)
            Emitter::patch(exits[i], w.at);
        w.bytes(kEpilogue); // back to runCached
    }
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
    const size_t most = size_t(count) * 320 + 64;
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
    // Whether the flags an instruction sets are read: by a later one in the
    // block before one sets them all (moves and the like leave them), or
    // after the block. INC and DEC keep CF, so they read it; ADC and SBB
    // read it; a handler may read any.
    auto flagsRead = [&](int i) {
        for (int j = i + 1; j < count; ++j) {
            const Op& n = ops[j];
            switch (n.kind) {
            case Kind::MovRegRm:
            case Kind::MovRmReg:
            case Kind::MovRmImm:
            case Kind::MovRegImm:
            case Kind::Lea:
            case Kind::PushReg:
            case Kind::PushImm:
            case Kind::PopReg:
            case Kind::FLoad:
            case Kind::FLoadInt:
            case Kind::FLoadSt:
            case Kind::FStore:
            case Kind::FStoreSt:
            case Kind::FArith:
            case Kind::FArithSt0:
            case Kind::FArithSti:
            case Kind::Fxch:
                continue;
            case Kind::Alu:
                if (n.alu != 2 && n.alu != 3 && n.alu <= 8)
                    return false;
                return true;
            default:
                return true;
            }
        }
        return true;
    };
    // r8d <- the register (zero-extended).
    auto loadR8 = [&](int r, int size) {
        if (size == 4)
            w.rbx({0x44, 0x8b}, 0, regAt(r));
        else
            w.rbx({0x44, 0x0f, 0xb6}, 0, reg8At(r));
    };
    // The flags the host's last operation set, into eflags: those in
    // `take` of LAHF's (SF ZF AF PF CF), OF if `of`, the rest of `clear`
    // cleared. eax and ecx go.
    auto mergeFlags = [&](uint32_t take, uint32_t clear) {
        w.u8(0x41), w.u8(0x0f), w.u8(0x90), w.u8(0xc1); // seto r9b
        w.u8(0x9f);                                     // lahf
        w.u8(0x0f), w.u8(0xb6), w.u8(0xcc);             // movzx ecx, ah
        w.u8(0x81), w.u8(0xe1), w.u32(take);            // and ecx, take
        w.u8(0x45), w.u8(0x0f), w.u8(0xb6), w.u8(0xc9); // movzx r9d, r9b
        w.u8(0x41), w.u8(0xc1), w.u8(0xe1), w.u8(11);   // shl r9d, 11
        w.u8(0x44), w.u8(0x09), w.u8(0xc9);             // or ecx, r9d
        w.rbx({0x8b}, kEax, L.eflags);                  // mov eax, [eflags]
        w.u8(0x25), w.u32(~clear);                      // and eax, ~clear
        w.u8(0x09), w.u8(0xc8);                         // or eax, ecx
        w.rbx({0x89}, kEax, L.eflags);                  // mov [eflags], eax
    };
    // eip <- what the instruction makes it, from edx.
    auto pushValue = [&](const Op& o) { // Cpu::push of edx: ESP down, then the store
        w.rbx({0x8b}, kEax, regAt(4));
        w.u8(0x83), w.u8(0xe8), w.u8(4);
        w.rbx({0x89}, kEax, regAt(4));
        store(o, kEdx, 4);
    };
    // The x87 unit. st(i) is regs[(top + i) & 7]: its index into ecx (st(0))
    // or edx (st(i)), and xmm0 or xmm1 to or from [rbx + index*8 + regs].
    auto st0Index = [&] { w.rbx({0x0f, 0xb6}, kEcx, L.fpuTop); }; // movzx ecx, byte [top]
    auto stIndex = [&](int i) { // edx <- (ecx + i) & 7
        w.u8(0x8d), w.u8(0x51), w.u8(uint8_t(i)); // lea edx, [rcx + i]
        w.u8(0x83), w.u8(0xe2), w.u8(7);          // and edx, 7
    };
    // movsd: `op` 0x10 load, 0x11 store; xmm `x`; index register `idx` (ecx 1, edx 2).
    auto fpuReg = [&](uint8_t op, int x, int idx) {
        w.u8(0xf2), w.u8(0x0f), w.u8(op), w.u8(uint8_t(0x84 | x << 3)), w.u8(uint8_t(0xc3 | idx << 3));
        w.u32(uint32_t(L.fpuRegs));
    };
    auto round = [&] { // Cpu::fpuRound on xmm0: through float when precision control is 0
        w.u8(0x66), w.u8(0xf7), w.u8(0x83), w.u32(uint32_t(L.fpuControl)), w.u8(0x00), w.u8(0x03); // test word [control], 0x300
        w.u8(0x75), w.u8(8);                                                 // jnz past
        w.u8(0xf2), w.u8(0x0f), w.u8(0x5a), w.u8(0xc0);                      // cvtsd2ss xmm0, xmm0
        w.u8(0xf3), w.u8(0x0f), w.u8(0x5a), w.u8(0xc0);                      // cvtss2sd xmm0, xmm0
    };
    // xmm0 <- xmm0 op xmm1, for arith's R (0 ADD, 1 MUL, 4 SUB, 5 SUBR, 6 DIV, 7 DIVR).
    auto fpuOp = [&](int r) {
        static constexpr uint8_t kOp[8] = {0x58, 0x59, 0, 0, 0x5c, 0x5c, 0x5e, 0x5e};
        if (r == 5 || r == 7) { // y op x
            w.u8(0xf2), w.u8(0x0f), w.u8(kOp[r]), w.u8(0xc8); // op xmm1, xmm0
            w.u8(0x66), w.u8(0x0f), w.u8(0x28), w.u8(0xc1);   // movapd xmm0, xmm1
        } else
            w.u8(0xf2), w.u8(0x0f), w.u8(kOp[r]), w.u8(0xc1); // op xmm0, xmm1
        round();
    };
    // xmm `x` <- the memory operand as a double (Cpu's loadReal: a double
    // as two 32-bit loads, or a float widened).
    auto loadRealTo = [&](const Op& o, int x) {
        address(o);
        load(o, kEdx, 4);
        w.u8(0x66), w.u8(0x0f), w.u8(0x6e), w.u8(uint8_t(0xc2 | x << 3)); // movd xmm(x), edx
        if (o.size == 8) {
            w.u8(0x83), w.u8(0xc0), w.u8(4); // add eax, 4
            load(o, kEdx, 4);
            w.u8(0x66), w.u8(0x0f), w.u8(0x6e), w.u8(0xd2);                   // movd xmm2, edx
            w.u8(0x66), w.u8(0x0f), w.u8(0x62), w.u8(uint8_t(0xc2 | x << 3)); // punpckldq xmm(x), xmm2
        } else
            w.u8(0xf3), w.u8(0x0f), w.u8(0x5a), w.u8(uint8_t(0xc0 | x << 3 | x)); // cvtss2sd
    };
    auto push = [&] { // Cpu::fpuPush of xmm0: top down, the register, its tag cleared
        st0Index();
        w.u8(0xff), w.u8(0xc9);           // dec ecx
        w.u8(0x83), w.u8(0xe1), w.u8(7);  // and ecx, 7
        w.rbx({0x88}, kEcx, L.fpuTop);    // mov [top], cl
        fpuReg(0x11, 0, kEcx);
        w.rbx({0x0f, 0xb6}, kEax, L.fpuTags); // movzx eax, byte [tags]
        w.u8(0x0f), w.u8(0xb3), w.u8(0xc8);   // btr eax, ecx
        w.rbx({0x88}, kEax, L.fpuTags);
    };
    auto pop = [&] { // Cpu::fpuPop (ecx st(0)'s index): its tag set, top up
        w.rbx({0x0f, 0xb6}, kEax, L.fpuTags);
        w.u8(0x0f), w.u8(0xab), w.u8(0xc8); // bts eax, ecx
        w.rbx({0x88}, kEax, L.fpuTags);
        w.u8(0xff), w.u8(0xc1);             // inc ecx
        w.u8(0x83), w.u8(0xe1), w.u8(7);
        w.rbx({0x88}, kEcx, L.fpuTop);
    };
    bool eipSet = false; // by the last: a handler sets it
    for (int i = 0; i < count; ++i) {
        const Op& o = ops[i];
        eipSet = false;
        switch (o.kind) {
        case Kind::Alu: {
            const int size = o.size;
            const bool test = o.alu == 8, incDec = o.alu >= 9;
            const bool writes = o.alu != 7 && !test;
            const bool memA = o.mem && (o.form == 0 || o.form == 3);
            // The first operand into edx (its address kept in r10d), the
            // second into r8d or the immediate.
            if (memA) {
                address(o);
                load(o, kEdx, size);
                w.u8(0x41), w.u8(0x89), w.u8(0xc2); // mov r10d, eax
            } else if (o.form == 2)
                loadReg(kEdx, 0, size);
            else
                loadReg(kEdx, o.form == 1 ? o.reg : o.rm, size);
            if (o.form == 0)
                loadR8(o.reg, size);
            else if (o.form == 1) {
                if (o.mem) {
                    address(o);
                    check(o, size);
                    if (size == 4)
                        w.u8(0x45), w.u8(0x8b), w.u8(0x04), w.u8(0x04); // mov r8d, [r12 + rax]
                    else
                        w.u8(0x45), w.u8(0x0f), w.u8(0xb6), w.u8(0x04), w.u8(0x04); // movzx r8d, byte [r12 + rax]
                } else
                    loadR8(o.rm, size);
            }
            if (o.alu == 2 || o.alu == 3)
                w.rbx({0x0f, 0xba}, 4, L.eflags), w.u8(0); // bt dword [eflags], 0: CF in
            if (incDec) {
                w.u8(size == 4 ? 0xff : 0xfe), w.u8(o.alu == 9 ? 0xc2 : 0xca); // inc/dec edx or dl
            } else if (o.form >= 2) {
                if (test)
                    w.u8(size == 4 ? 0xf7 : 0xf6), w.u8(0xc2); // test edx, imm
                else
                    w.u8(size == 4 ? 0x81 : 0x80), w.u8(uint8_t(0xc2 | o.alu << 3)); // op edx, imm
                if (size == 4)
                    w.u32(o.imm);
                else
                    w.u8(uint8_t(o.imm));
            } else {
                const uint8_t op = test ? 0x84 : uint8_t(o.alu << 3);
                w.u8(0x44), w.u8(uint8_t(op | (size == 4 ? 1 : 0))), w.u8(0xc2); // op edx, r8d (dl, r8b)
            }
            if (flagsRead(i)) {
                if (incDec)
                    mergeFlags(0xd4, 0x8d4); // CF kept
                else if (o.alu == 1 || o.alu == 4 || o.alu == 6 || test)
                    mergeFlags(0xc5, 0x8d5); // AF cleared, as the interpreter has it
                else
                    mergeFlags(0xd5, 0x8d5);
            }
            if (writes) {
                if (memA) {
                    w.u8(0x44), w.u8(0x89), w.u8(0xd0); // mov eax, r10d
                    store(o, kEdx, size);
                } else
                    storeReg(o.form == 1 ? o.reg : o.form == 2 ? 0 : o.rm, kEdx, size);
            }
            break;
        }
        case Kind::FLoad:
            loadRealTo(o, 0);
            push();
            break;
        case Kind::FLoadInt:
            address(o);
            load(o, kEdx, 4);
            w.u8(0xf2), w.u8(0x0f), w.u8(0x2a), w.u8(0xc2); // cvtsi2sd xmm0, edx
            push();
            break;
        case Kind::FLoadSt:
            st0Index(), stIndex(o.rm), fpuReg(0x10, 0, kEdx); // st(i), before the push
            push();
            break;
        case Kind::FStore:
            // storeReal of st(0), popped first if it's to be (the address
            // doesn't depend on it, and neither faults).
            st0Index(), fpuReg(0x10, 0, kEcx);
            if (o.cc)
                pop();
            if (o.size == 8)
                w.u8(0x66), w.u8(0x48), w.u8(0x0f), w.u8(0x7e), w.u8(0xc2); // movq rdx, xmm0
            else {
                w.u8(0xf2), w.u8(0x0f), w.u8(0x5a), w.u8(0xc0); // cvtsd2ss xmm0, xmm0
                w.u8(0x66), w.u8(0x0f), w.u8(0x7e), w.u8(0xc2); // movd edx, xmm0
            }
            address(o);
            store(o, kEdx, 4);
            if (o.size == 8) {
                w.u8(0x48), w.u8(0xc1), w.u8(0xea), w.u8(32); // shr rdx, 32
                w.u8(0x83), w.u8(0xc0), w.u8(4);              // add eax, 4
                store(o, kEdx, 4);
            }
            break;
        case Kind::FStoreSt:
            st0Index(), fpuReg(0x10, 0, kEcx);
            stIndex(o.rm), fpuReg(0x11, 0, kEdx);
            if (o.cc)
                pop();
            break;
        case Kind::Fxch:
            st0Index(), stIndex(o.rm);
            fpuReg(0x10, 0, kEcx), fpuReg(0x10, 1, kEdx);
            fpuReg(0x11, 1, kEcx), fpuReg(0x11, 0, kEdx);
            break;
        case Kind::FArith:
            loadRealTo(o, 1);
            st0Index(), fpuReg(0x10, 0, kEcx);
            fpuOp(o.alu);
            fpuReg(0x11, 0, kEcx);
            break;
        case Kind::FArithSt0:
            st0Index(), stIndex(o.rm);
            fpuReg(0x10, 0, kEcx), fpuReg(0x10, 1, kEdx);
            fpuOp(o.alu);
            fpuReg(0x11, 0, kEcx);
            break;
        case Kind::FArithSti: {
            static constexpr int kSwap[8] = {0, 1, 2, 3, 5, 4, 7, 6};
            st0Index(), stIndex(o.rm);
            fpuReg(0x10, 0, kEdx), fpuReg(0x10, 1, kEcx); // st(i) op st(0)
            fpuOp(kSwap[o.alu]);
            fpuReg(0x11, 0, kEdx);
            if (o.cc)
                pop();
            break;
        }
        case Kind::Jcc: {
            // condition<cc> on eflags: test it, then eip one way or the other.
            w.rbx({0x8b}, kEax, L.eflags); // mov eax, [eflags]
            static constexpr uint32_t kMask[6] = {0x800, 0x1, 0x40, 0x41, 0x80, 0x4};
            const int c = o.cc >> 1;
            if (c < 6) {
                w.u8(0xa9), w.u32(kMask[c]); // test eax, mask
            } else {
                w.u8(0x89), w.u8(0xc1);           // mov ecx, eax
                w.u8(0xc1), w.u8(0xe9), w.u8(4);  // shr ecx, 4: OF to SF's place
                w.u8(0x31), w.u8(0xc1);           // xor ecx, eax: SF != OF
                if (c == 7) {
                    w.u8(0xd1), w.u8(0xe0);      // shl eax, 1: ZF to SF's place
                    w.u8(0x09), w.u8(0xc1);      // or ecx, eax
                }
                w.u8(0xf7), w.u8(0xc1), w.u32(0x80); // test ecx, 0x80
            }
            const uint32_t after = o.start + o.length;
            w.u8(0xba), w.u32(after);                                // mov edx, after
            w.u8(0x41), w.u8(0xb8), w.u32(after + o.imm);            // mov r8d, target
            w.u8(0x41), w.u8(0x0f), w.u8((o.cc & 1) ? 0x44 : 0x45), w.u8(0xd0); // cmovz/cmovnz edx, r8d
            w.rbx({0x89}, kEdx, L.eip);
            storeImm32(L.start, o.start);
            eipSet = true;
            break;
        }
        case Kind::Jmp:
            storeImm32(L.eip, o.start + o.length + o.imm);
            storeImm32(L.start, o.start);
            eipSet = true;
            break;
        case Kind::CallRel:
            w.u8(0xba), w.u32(o.start + o.length); // the return address
            pushValue(o);
            storeImm32(L.eip, o.start + o.length + o.imm);
            storeImm32(L.start, o.start);
            eipSet = true;
            break;
        case Kind::JmpRm:
        case Kind::CallRm:
            if (o.mem) {
                address(o);
                load(o, kEdx, 4);
            } else
                loadReg(kEdx, o.rm, 4);
            if (o.kind == Kind::CallRm) {
                w.u8(0x41), w.u8(0x89), w.u8(0xd1);     // mov r9d, edx: the target
                w.u8(0xba), w.u32(o.start + o.length); // the return address
                pushValue(o);
                w.rbx({0x44, 0x89}, 1, L.eip); // mov [eip], r9d
            } else
                w.rbx({0x89}, kEdx, L.eip);
            storeImm32(L.start, o.start);
            eipSet = true;
            break;
        case Kind::Ret:
            // Cpu::pop, then ESP past the immediate.
            w.rbx({0x8b}, kEax, regAt(4));
            load(o, kEdx, 4);
            w.rbx({0x81}, 0, regAt(4)), w.u32(4 + o.imm); // add dword [esp], 4 + imm
            w.rbx({0x89}, kEdx, L.eip);
            storeImm32(L.start, o.start);
            eipSet = true;
            break;
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
    // On to the next block (a jump within the buffer: unwinding from the
    // call before doesn't take it for an epilogue).
    w.u8(0xe9);
    Emitter::patch(w.rel32(), dispatch_);
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
