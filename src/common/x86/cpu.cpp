// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/x86/cpu.h"

#include <cstdio>
#include <cstring>

namespace reluva::x86 {

namespace {

uint32_t mask(int size) { return size == 4 ? 0xffffffffu : size == 2 ? 0xffffu : 0xffu; }
uint32_t signBit(int size) { return size == 4 ? 0x80000000u : size == 2 ? 0x8000u : 0x80u; }
int32_t signExtend(uint32_t v, int size) {
    return size == 4 ? int32_t(v) : size == 2 ? int32_t(int16_t(v)) : int32_t(int8_t(v));
}
bool parity(uint32_t v) {
    v &= 0xff;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return !(v & 1);
}
std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08x", v);
    return buf;
}

// step() is inlined into runUntil, so that its frame is set up once a run;
// Visual Studio doesn't know the GNU attribute.
#if defined(_MSC_VER)
#define RELUVA_FORCE_INLINE __forceinline
#else
#define RELUVA_FORCE_INLINE [[gnu::always_inline]] inline
#endif

// Case labels for runs of opcodes.
#define RELUVA_CASE6(b) \
    case (b):           \
    case (b) + 1:       \
    case (b) + 2:       \
    case (b) + 3:       \
    case (b) + 4:       \
    case (b) + 5
#define RELUVA_CASE8(b) \
    RELUVA_CASE6(b):    \
    case (b) + 6:       \
    case (b) + 7

// The bytes that are prefixes: operand size, REP and REPNE, the segment
// overrides, LOCK and address size.
constexpr std::array<bool, 256> kPrefix = [] {
    std::array<bool, 256> t{};
    for (int b : {0x66, 0xf3, 0xf2, 0x64, 0x26, 0x2e, 0x36, 0x3e, 0x65, 0xf0, 0x67})
        t[size_t(b)] = true;
    return t;
}();

// Calls end by returning here: a HLT just below the native functions.
constexpr uint32_t kReturnSlots = 1;

} // namespace

Cpu::Cpu(uint32_t memorySize) : modrmCache_(kModrmCacheSize), memory_(memorySize, 0), memoryLimit_(memorySize - kPageSize) {
    nativeBase_ = memorySize - kMaxNatives * kNativeStride;
    std::memset(&memory_[nativeBase_ - kNativeStride * kReturnSlots], 0xf4, kMaxNatives * kNativeStride + kNativeStride);
}

uint16_t Cpu::read16(uint32_t a) const { return valid(a, 2) ? uint16_t(memory_[a] | memory_[a + 1] << 8) : 0; }
uint32_t Cpu::read32(uint32_t a) const {
    if (!valid(a, 4))
        return 0;
    uint32_t v;
    std::memcpy(&v, &memory_[a], 4);
    return v;
}
void Cpu::write16(uint32_t a, uint16_t v) {
    if (valid(a, 2)) {
        memory_[a] = uint8_t(v);
        memory_[a + 1] = uint8_t(v >> 8);
    }
}
void Cpu::write32(uint32_t a, uint32_t v) {
    if (valid(a, 4))
        std::memcpy(&memory_[a], &v, 4);
}

std::string Cpu::string(uint32_t address, size_t max) const {
    std::string s;
    while (s.size() < max && valid(address, 1) && memory_[address])
        s.push_back(char(memory_[address++]));
    return s;
}

bool Cpu::writeString(uint32_t address, std::string_view text, uint32_t size) {
    if (!size || !valid(address, size))
        return false;
    const size_t n = std::min<size_t>(text.size(), size - 1);
    std::memcpy(&memory_[address], text.data(), n);
    memory_[address + n] = 0;
    return true;
}

uint32_t Cpu::addNative(std::string name, Native fn) {
    if (natives_.size() >= kMaxNatives)
        return 0;
    natives_.push_back({std::move(name), std::move(fn)});
    return nativeBase_ + uint32_t(natives_.size() - 1) * kNativeStride;
}

const std::string* Cpu::nativeName(uint32_t address) const {
    if (address < nativeBase_ || (address - nativeBase_) % kNativeStride)
        return nullptr;
    const uint32_t i = (address - nativeBase_) / kNativeStride;
    return i < natives_.size() ? &natives_[i].name : nullptr;
}

void Cpu::fault(const std::string& message) {
    throw Fault{message + " at " + hex(instructionStart_)};
}

void Cpu::outside(uint32_t a, uint32_t size) {
    fault("memory access at " + hex(a) + " (" + std::to_string(size) + " bytes) outside memory");
}

uint32_t Cpu::loadOutside(uint32_t a, int size) {
    if (readableFirstPage && a < kPageSize && a + uint32_t(size) <= kPageSize)
        return 0;
    outside(a, uint32_t(size));
}

void Cpu::pushSegment(int s) {
    if (d_.opSize16) {
        reg[ESP] -= 2;
        store(reg[ESP], sreg[size_t(s)], 2);
    } else {
        push(sreg[size_t(s)]);
    }
}

void Cpu::popSegment(int s) {
    if (d_.opSize16) {
        sreg[size_t(s)] = uint16_t(load(reg[ESP], 2));
        reg[ESP] += 2;
    } else {
        sreg[size_t(s)] = uint16_t(pop());
    }
}


uint32_t Cpu::readReg(int size, int r) const {
    if (size == 4)
        return reg[size_t(r)];
    if (size == 2)
        return reg[size_t(r)] & 0xffff;
    return r < 4 ? reg[size_t(r)] & 0xff : (reg[size_t(r - 4)] >> 8) & 0xff;
}

void Cpu::writeReg(int size, int r, uint32_t v) {
    if (size == 4)
        reg[size_t(r)] = v;
    else if (size == 2)
        reg[size_t(r)] = (reg[size_t(r)] & 0xffff0000u) | (v & 0xffff);
    else if (r < 4)
        reg[size_t(r)] = (reg[size_t(r)] & 0xffffff00u) | (v & 0xff);
    else
        reg[size_t(r - 4)] = (reg[size_t(r - 4)] & 0xffff00ffu) | (v & 0xff) << 8;
}

void Cpu::decodeModrmSlow(ModrmEntry& e) {
    const uint32_t start = eip;
    const uint32_t m = fetch8();
    e = ModrmEntry{};
    e.mod = uint8_t(m >> 6);
    e.regField = uint8_t((m >> 3) & 7);
    e.rm = uint8_t(m & 7);
    if (e.mod != 3) {
        if (e.rm == 4) {
            const uint32_t sib = fetch8();
            const uint32_t index = (sib >> 3) & 7, base = sib & 7;
            e.scale = uint8_t(sib >> 6);
            if (index != 4)
                e.index = uint8_t(index);
            if (base == 5 && e.mod == 0)
                e.displacement = fetch32();
            else
                e.base = uint8_t(base);
        } else if (e.rm == 5 && e.mod == 0) {
            e.displacement = fetch32();
        } else {
            e.base = e.rm;
        }
        if (e.mod == 1)
            e.displacement += uint32_t(int32_t(int8_t(fetch8())));
        else if (e.mod == 2)
            e.displacement += fetch32();
    }
    e.length = uint8_t(eip - start);
    if (valid(start, 8)) {
        std::memcpy(&e.bytes, memory_.data() + start, 8);
        e.bytes &= ~0ull >> (64 - 8 * e.length);
        e.address = start;
    }
}

void Cpu::decodeModrm() {
    ModrmEntry& e = modrmCache_[eip & (kModrmCacheSize - 1)];
    uint64_t bytes = 0;
    if (e.address != eip || !validSmall(eip, 8) ||
        (std::memcpy(&bytes, memory_.data() + eip, 8), (bytes & (~0ull >> (64 - 8 * e.length))) != e.bytes))
        decodeModrmSlow(e);
    else
        eip += e.length;
    d_.hasModrm = true;
    d_.mod = e.mod;
    d_.regField = e.regField;
    d_.rm = e.rm;
    d_.memoryOperand = e.mod != 3;
    if (!d_.memoryOperand)
        return;
    uint32_t a = e.displacement + d_.segment;
    if (e.base < 8)
        a += reg[e.base];
    if (e.index < 8)
        a += reg[e.index] << e.scale;
    d_.address = a;
}

uint32_t Cpu::readRm(int size) {
    return d_.memoryOperand ? load(d_.address, size) : readReg(size, d_.rm);
}

void Cpu::writeRm(int size, uint32_t v) {
    if (d_.memoryOperand)
        store(d_.address, v, size);
    else
        writeReg(size, d_.rm, v);
}

void Cpu::setLogicFlags(uint32_t r, int size) {
    r &= mask(size);
    eflags &= ~(CF | OF | AF | ZF | SF | PF);
    if (!r)
        eflags |= ZF;
    if (r & signBit(size))
        eflags |= SF;
    if (parity(r))
        eflags |= PF;
}

uint32_t Cpu::alu(int op, uint32_t a, uint32_t b, int size) {
    const uint32_t m = mask(size), sign = signBit(size);
    a &= m;
    b &= m;
    uint32_t r = 0;
    uint32_t flags = eflags & ~(CF | OF | AF | ZF | SF | PF);
    const uint32_t carryIn = (eflags & CF) ? 1 : 0;
    switch (op) {
    case 0: // ADD
    case 2: { // ADC
        const uint64_t wide = uint64_t(a) + b + (op == 2 ? carryIn : 0);
        r = uint32_t(wide) & m;
        if (wide > m)
            flags |= CF;
        if ((a ^ r) & (b ^ r) & sign)
            flags |= OF;
        if ((a ^ b ^ r) & 0x10)
            flags |= AF;
        break;
    }
    case 3: // SBB
    case 5: // SUB
    case 7: { // CMP
        const uint64_t subtrahend = uint64_t(b) + (op == 3 ? carryIn : 0);
        r = uint32_t(uint64_t(a) - subtrahend) & m;
        if (uint64_t(a) < subtrahend)
            flags |= CF;
        if ((a ^ b) & (a ^ r) & sign)
            flags |= OF;
        if ((a ^ b ^ r) & 0x10)
            flags |= AF;
        break;
    }
    case 1: r = a | b; break;
    case 4: r = a & b; break;
    case 6: r = a ^ b; break;
    }
    if (!r)
        flags |= ZF;
    if (r & sign)
        flags |= SF;
    if (parity(r))
        flags |= PF;
    eflags = flags;
    return r;
}

uint32_t Cpu::shift(int op, uint32_t v, uint32_t count, int size) {
    const int bits = size * 8;
    const uint32_t m = mask(size), sign = signBit(size);
    v &= m;
    count &= 31;
    if (!count)
        return v;
    uint32_t r = v;
    bool cf = (eflags & CF) != 0;
    switch (op) {
    case 0: { // ROL
        const uint32_t n = count % uint32_t(bits);
        r = n ? ((v << n) | (v >> (uint32_t(bits) - n))) & m : v;
        cf = r & 1;
        eflags = (eflags & ~(CF | OF)) | (cf ? CF : 0);
        if (((r & sign) != 0) != cf)
            eflags |= OF;
        return r;
    }
    case 1: { // ROR
        const uint32_t n = count % uint32_t(bits);
        r = n ? ((v >> n) | (v << (uint32_t(bits) - n))) & m : v;
        cf = (r & sign) != 0;
        eflags = (eflags & ~(CF | OF)) | (cf ? CF : 0);
        if (((r & sign) != 0) != ((r & (sign >> 1)) != 0))
            eflags |= OF;
        return r;
    }
    case 2: // RCL
    case 3: { // RCR
        const uint32_t n = count % uint32_t(bits + 1);
        for (uint32_t i = 0; i < n; ++i) {
            if (op == 2) {
                const bool out = (r & sign) != 0;
                r = ((r << 1) | (cf ? 1 : 0)) & m;
                cf = out;
            } else {
                const bool out = r & 1;
                r = (r >> 1) | (cf ? sign : 0);
                cf = out;
            }
        }
        eflags = (eflags & ~(CF | OF)) | (cf ? CF : 0);
        if (op == 2 ? (((r & sign) != 0) != cf) : (((r & sign) != 0) != ((r & (sign >> 1)) != 0)))
            eflags |= OF;
        return r;
    }
    case 4: // SHL
    case 6:
        cf = count <= uint32_t(bits) ? ((v >> (uint32_t(bits) - count)) & 1) != 0 : false;
        r = count < 32 ? (v << count) & m : 0;
        setLogicFlags(r, size);
        if (cf)
            eflags |= CF;
        if (((r & sign) != 0) != cf)
            eflags |= OF;
        return r;
    case 5: // SHR
        cf = count <= uint32_t(bits) ? ((v >> (count - 1)) & 1) != 0 : false;
        r = count < 32 ? v >> count : 0;
        setLogicFlags(r, size);
        if (cf)
            eflags |= CF;
        if (v & sign)
            eflags |= OF;
        return r;
    case 7: { // SAR
        const int32_t s = signExtend(v, size);
        const uint32_t n = count >= uint32_t(bits) ? uint32_t(bits) - 1 : count;
        cf = ((s >> (n - 1)) & 1) != 0;
        if (count >= uint32_t(bits))
            cf = s < 0;
        r = uint32_t(s >> (count >= uint32_t(bits) ? bits - 1 : int(count))) & m;
        setLogicFlags(r, size);
        if (cf)
            eflags |= CF;
        return r;
    }
    }
    return r;
}

bool Cpu::condition(int cc) const {
    const bool cf = eflags & CF, zf = eflags & ZF, sf = eflags & SF, of = eflags & OF, pf = eflags & PF;
    bool r = false;
    switch (cc >> 1) {
    case 0: r = of; break;
    case 1: r = cf; break;
    case 2: r = zf; break;
    case 3: r = cf || zf; break;
    case 4: r = sf; break;
    case 5: r = pf; break;
    case 6: r = sf != of; break;
    case 7: r = zf || sf != of; break;
    }
    return (cc & 1) ? !r : r;
}

void Cpu::group3(int size) {
    const uint32_t m = mask(size);
    const uint32_t v = readRm(size);
    switch (d_.regField) {
    case 0:
    case 1: { // TEST r/m, imm
        const uint32_t imm = size == 1 ? fetch8() : size == 2 ? fetch16() : fetch32();
        setLogicFlags(v & imm, size);
        break;
    }
    case 2: writeRm(size, ~v & m); break; // NOT
    case 3: { // NEG
        const uint32_t r = alu(5, 0, v, size);
        if (v & m)
            eflags |= CF;
        else
            eflags &= ~CF;
        writeRm(size, r);
        break;
    }
    case 4: // MUL
    case 5: { // IMUL
        uint64_t product;
        bool overflow;
        if (d_.regField == 4) {
            product = uint64_t(readReg(size, EAX)) * v;
            overflow = (product >> (size * 8)) != 0;
        } else {
            const int64_t p = int64_t(signExtend(readReg(size, EAX), size)) * signExtend(v, size);
            product = uint64_t(p);
            overflow = p != int64_t(signExtend(uint32_t(p), size));
        }
        if (size == 1) {
            writeReg(2, EAX, uint32_t(product) & 0xffff);
        } else if (size == 2) {
            writeReg(2, EAX, uint32_t(product) & 0xffff);
            writeReg(2, EDX, uint32_t(product >> 16) & 0xffff);
        } else {
            reg[EAX] = uint32_t(product);
            reg[EDX] = uint32_t(product >> 32);
        }
        eflags &= ~(CF | OF);
        if (overflow)
            eflags |= CF | OF;
        break;
    }
    case 6: // DIV
    case 7: { // IDIV
        if (!(v & m))
            fault("division by zero");
        if (size == 1) {
            const uint32_t ax = reg[EAX] & 0xffff;
            if (d_.regField == 6) {
                const uint32_t q = ax / v, rem = ax % v;
                if (q > 0xff)
                    fault("division overflow");
                writeReg(2, EAX, (rem << 8) | q);
            } else {
                const int32_t n = int16_t(ax), dv = int8_t(v);
                const int32_t q = n / dv, rem = n % dv;
                if (q < -128 || q > 127)
                    fault("division overflow");
                writeReg(2, EAX, (uint32_t(rem & 0xff) << 8) | uint32_t(q & 0xff));
            }
        } else if (size == 2) {
            const uint32_t n = (reg[EDX] & 0xffff) << 16 | (reg[EAX] & 0xffff);
            if (d_.regField == 6) {
                const uint32_t q = n / v, rem = n % v;
                if (q > 0xffff)
                    fault("division overflow");
                writeReg(2, EAX, q);
                writeReg(2, EDX, rem);
            } else {
                const int32_t sn = int32_t(n), dv = int16_t(v);
                const int32_t q = sn / dv, rem = sn % dv;
                if (q < -32768 || q > 32767)
                    fault("division overflow");
                writeReg(2, EAX, uint32_t(q));
                writeReg(2, EDX, uint32_t(rem));
            }
        } else {
            const uint64_t n = uint64_t(reg[EDX]) << 32 | reg[EAX];
            if (d_.regField == 6) {
                const uint64_t q = n / v, rem = n % v;
                if (q > 0xffffffffu)
                    fault("division overflow");
                reg[EAX] = uint32_t(q);
                reg[EDX] = uint32_t(rem);
            } else {
                const int64_t sn = int64_t(n), dv = int32_t(v);
                if (dv == -1 && sn == INT64_MIN)
                    fault("division overflow");
                const int64_t q = sn / dv, rem = sn % dv;
                if (q < INT32_MIN || q > INT32_MAX)
                    fault("division overflow");
                reg[EAX] = uint32_t(q);
                reg[EDX] = uint32_t(rem);
            }
        }
        break;
    }
    }
}

void Cpu::stringOp(uint8_t op) {
    const int size = (op & 1) ? (d_.opSize16 ? 2 : 4) : 1;
    const uint32_t step = (eflags & DF) ? uint32_t(-size) : uint32_t(size);
    const bool repeat = d_.rep || d_.repne;
    const bool compare = op == 0xa6 || op == 0xa7 || op == 0xae || op == 0xaf;
    // rep movs and rep stos forward, a run at a time.
    if (repeat && !(eflags & DF) && (op == 0xa4 || op == 0xa5) && reg[ECX]) {
        const uint64_t bytes = uint64_t(reg[ECX]) * uint32_t(size);
        const uint32_t src = reg[ESI] + d_.segment, dst = reg[EDI];
        if (bytes <= 0xffffffffu && (uint64_t(dst) + bytes <= src || dst < src)) {
            uint8_t* to = at(dst, uint32_t(bytes));
            const uint8_t* from = at(src, uint32_t(bytes));
            std::memmove(to, from, size_t(bytes));
            reg[ESI] += uint32_t(bytes);
            reg[EDI] += uint32_t(bytes);
            reg[ECX] = 0;
            return;
        }
    }
    if (repeat && !(eflags & DF) && (op == 0xaa || op == 0xab) && reg[ECX]) {
        const uint64_t bytes = uint64_t(reg[ECX]) * uint32_t(size);
        if (bytes <= 0xffffffffu) {
            uint8_t* to = at(reg[EDI], uint32_t(bytes));
            const uint32_t v = readReg(size, EAX);
            if (size == 1)
                std::memset(to, int(v & 0xff), size_t(bytes));
            else
                for (uint64_t i = 0; i < bytes; i += uint32_t(size))
                    std::memcpy(to + i, &v, size_t(size));
            reg[EDI] += uint32_t(bytes);
            reg[ECX] = 0;
            return;
        }
    }
    while (!repeat || reg[ECX]) {
        switch (op) {
        case 0xa4:
        case 0xa5: // MOVS
            store(reg[EDI], load(reg[ESI] + d_.segment, size), size);
            reg[ESI] += step;
            reg[EDI] += step;
            break;
        case 0xa6:
        case 0xa7: // CMPS
            alu(7, load(reg[ESI] + d_.segment, size), load(reg[EDI], size), size);
            reg[ESI] += step;
            reg[EDI] += step;
            break;
        case 0xaa:
        case 0xab: // STOS
            store(reg[EDI], readReg(size, EAX), size);
            reg[EDI] += step;
            break;
        case 0xac:
        case 0xad: // LODS
            writeReg(size, EAX, load(reg[ESI] + d_.segment, size));
            reg[ESI] += step;
            break;
        case 0xae:
        case 0xaf: // SCAS
            alu(7, readReg(size, EAX), load(reg[EDI], size), size);
            reg[EDI] += step;
            break;
        }
        if (!repeat)
            break;
        --reg[ECX];
        if (compare && (d_.rep ? !(eflags & ZF) : (eflags & ZF) != 0))
            break;
    }
}

void Cpu::twoByte() {
    const uint8_t op = uint8_t(fetch8());
    const int osz = d_.opSize16 ? 2 : 4;
    if (op >= 0x80 && op <= 0x8f) { // Jcc rel32
        const uint32_t rel = fetch32();
        if (condition(op & 15))
            eip += rel;
        return;
    }
    if (op >= 0x90 && op <= 0x9f) { // SETcc
        decodeModrm();
        writeRm(1, condition(op & 15) ? 1 : 0);
        return;
    }
    if (op >= 0x40 && op <= 0x4f) { // CMOVcc
        decodeModrm();
        const uint32_t v = readRm(osz);
        if (condition(op & 15))
            writeReg(osz, d_.regField, v);
        return;
    }
    if (op >= 0xc8) { // BSWAP
        uint32_t& r = reg[op & 7];
        r = (r >> 24) | ((r >> 8) & 0xff00) | ((r << 8) & 0xff0000) | (r << 24);
        return;
    }
    switch (op) {
    case 0x1f: decodeModrm(); return; // NOP r/m
    case 0xa0: pushSegment(4); return; // PUSH FS
    case 0xa1: popSegment(4); return;  // POP FS
    case 0xa8: pushSegment(5); return; // PUSH GS
    case 0xa9: popSegment(5); return;  // POP GS
    case 0x31: // RDTSC
        reg[EAX] = uint32_t(executed_);
        reg[EDX] = uint32_t(executed_ >> 32);
        return;
    case 0xa2: { // CPUID: a Pentium with an FPU, a time stamp counter and CMPXCHG8B
        const uint32_t leaf = reg[EAX];
        if (leaf == 0) {
            reg[EAX] = 1;
            reg[EBX] = 0x756e6547; // "GenuineIntel"
            reg[EDX] = 0x49656e69;
            reg[ECX] = 0x6c65746e;
        } else {
            reg[EAX] = 0x543; // family 5
            reg[EBX] = reg[ECX] = 0;
            reg[EDX] = 0x1 | 0x10 | 0x100;
        }
        return;
    }
    case 0xa3: // BT
    case 0xab: // BTS
    case 0xb3: // BTR
    case 0xbb: // BTC
    case 0xba: { // BT/BTS/BTR/BTC r/m, imm8
        decodeModrm();
        uint32_t bit;
        int kind;
        if (op == 0xba) {
            bit = fetch8() & uint32_t(osz * 8 - 1);
            kind = d_.regField - 4;
            if (kind < 0)
                fault("undefined instruction 0f ba /" + std::to_string(d_.regField));
        } else {
            const int32_t offset = signExtend(readReg(osz, d_.regField), osz);
            kind = op == 0xa3 ? 0 : op == 0xab ? 1 : op == 0xb3 ? 2 : 3;
            if (d_.memoryOperand) {
                d_.address += uint32_t((offset >> (osz == 4 ? 5 : 4)) * osz);
                bit = uint32_t(offset) & uint32_t(osz * 8 - 1);
            } else {
                bit = uint32_t(offset) & uint32_t(osz * 8 - 1);
            }
        }
        const uint32_t v = readRm(osz);
        eflags = (v >> bit) & 1 ? eflags | CF : eflags & ~CF;
        if (kind == 1)
            writeRm(osz, v | (1u << bit));
        else if (kind == 2)
            writeRm(osz, v & ~(1u << bit));
        else if (kind == 3)
            writeRm(osz, v ^ (1u << bit));
        return;
    }
    case 0xa4: // SHLD r/m, r, imm8
    case 0xa5: // SHLD r/m, r, CL
    case 0xac: // SHRD r/m, r, imm8
    case 0xad: { // SHRD r/m, r, CL
        decodeModrm();
        const uint32_t count = ((op & 1) ? reg[ECX] : fetch8()) & 31;
        if (!count)
            return;
        const uint32_t dst = readRm(osz), src = readReg(osz, d_.regField);
        const int bits = osz * 8;
        uint64_t r;
        bool cf;
        if (op < 0xac) {
            const uint64_t joined = (uint64_t(dst) << bits) | src;
            r = (joined << count) >> bits;
            cf = (dst >> (bits - int(count))) & 1;
        } else {
            const uint64_t joined = (uint64_t(src) << bits) | dst;
            r = joined >> count;
            cf = (dst >> (count - 1)) & 1;
        }
        const uint32_t result = uint32_t(r) & mask(osz);
        setLogicFlags(result, osz);
        if (cf)
            eflags |= CF;
        if (((result ^ dst) & signBit(osz)) != 0)
            eflags |= OF;
        writeRm(osz, result);
        return;
    }
    case 0xaf: { // IMUL r, r/m
        decodeModrm();
        const int64_t p = int64_t(signExtend(readReg(osz, d_.regField), osz)) * signExtend(readRm(osz), osz);
        writeReg(osz, d_.regField, uint32_t(p));
        eflags &= ~(CF | OF);
        if (p != int64_t(signExtend(uint32_t(p), osz)))
            eflags |= CF | OF;
        return;
    }
    case 0xb0: // CMPXCHG r/m8, r8
    case 0xb1: { // CMPXCHG r/m, r
        const int size = op == 0xb0 ? 1 : osz;
        decodeModrm();
        const uint32_t dst = readRm(size), acc = readReg(size, EAX);
        alu(7, acc, dst, size);
        if (eflags & ZF)
            writeRm(size, readReg(size, d_.regField));
        else
            writeReg(size, EAX, dst);
        return;
    }
    case 0xb6: // MOVZX r, r/m8
    case 0xb7: // MOVZX r, r/m16
    case 0xbe: // MOVSX r, r/m8
    case 0xbf: { // MOVSX r, r/m16
        const int from = (op & 1) ? 2 : 1;
        decodeModrm();
        const uint32_t v = readRm(from);
        writeReg(osz, d_.regField, op < 0xbe ? v : uint32_t(signExtend(v, from)));
        return;
    }
    case 0xbc: // BSF
    case 0xbd: { // BSR
        decodeModrm();
        const uint32_t v = readRm(osz) & mask(osz);
        if (!v) {
            eflags |= ZF;
            return;
        }
        eflags &= ~ZF;
        uint32_t i = 0;
        if (op == 0xbc)
            while (!((v >> i) & 1))
                ++i;
        else
            for (i = uint32_t(osz * 8 - 1); !((v >> i) & 1); --i)
                ;
        writeReg(osz, d_.regField, i);
        return;
    }
    case 0xc0: // XADD r/m8, r8
    case 0xc1: { // XADD r/m, r
        const int size = op == 0xc0 ? 1 : osz;
        decodeModrm();
        const uint32_t dst = readRm(size), src = readReg(size, d_.regField);
        const uint32_t sum = alu(0, dst, src, size);
        writeReg(size, d_.regField, dst);
        writeRm(size, sum);
        return;
    }
    case 0x0b: fault("UD2");
    default: fault("instruction 0f " + std::to_string(op) + " not implemented");
    }
}

void Cpu::events() {
    if (sliceLength_ && executed_ >= nextSlice_) {
        nextSlice_ = executed_ + sliceLength_;
        if (!slice_(*this))
            fault(stopMessage);
        instructionStart_ = eip;
    }
    // Another thread may have run meanwhile, with a limit of its own.
    updateNextEvent();
    if (limit_ && executed_ > limitAt_)
        fault("instruction limit reached");
}

RELUVA_FORCE_INLINE void Cpu::step() {
    instructionStart_ = eip;
    if (eip >= nativeBase_ - kNativeStride * kReturnSlots) {
        callNative(eip);
        return;
    }
    if (++executed_ >= nextEvent_) [[unlikely]]
        events();
    if (trace) [[unlikely]]
        trace(*this);
    d_ = Decoded{};
    uint8_t op = uint8_t(fetch8());
    while (kPrefix[op]) [[unlikely]] {
        if (op == 0x66)
            d_.opSize16 = true;
        else if (op == 0xf3)
            d_.rep = true;
        else if (op == 0xf2)
            d_.repne = true;
        else if (op == 0x64)
            d_.segment = fsBase;
        else if (op == 0x67)
            fault("16-bit addressing not implemented");
        // else flat segments; LOCK means nothing here
        op = uint8_t(fetch8());
    }
    const int osz = d_.opSize16 ? 2 : 4;

    switch (op) {
    // The eight ALU operations, in their six forms each.
    RELUVA_CASE6(0x00):
    RELUVA_CASE6(0x08):
    RELUVA_CASE6(0x10):
    RELUVA_CASE6(0x18):
    RELUVA_CASE6(0x20):
    RELUVA_CASE6(0x28):
    RELUVA_CASE6(0x30):
    RELUVA_CASE6(0x38): {
        const int aluOp = op >> 3;
        int size = (op & 1) ? osz : 1;
        uint32_t a, b;
        switch (op & 7) {
        case 0:
        case 1: // r/m, r
            decodeModrm();
            a = readRm(size);
            b = readReg(size, d_.regField);
            break;
        case 2:
        case 3: // r, r/m
            decodeModrm();
            a = readReg(size, d_.regField);
            b = readRm(size);
            break;
        default: // AL/eAX, imm
            a = readReg(size, EAX);
            b = size == 1 ? fetch8() : size == 2 ? fetch16() : fetch32();
            break;
        }
        const uint32_t r = alu(aluOp, a, b, size);
        if (aluOp == 7)
            return;
        if ((op & 7) < 2)
            writeRm(size, r);
        else if ((op & 7) < 4)
            writeReg(size, d_.regField, r);
        else
            writeReg(size, EAX, r);
        return;
    }
    RELUVA_CASE8(0x40):
    RELUVA_CASE8(0x48): { // INC/DEC r
        const int r = op & 7;
        const uint32_t cf = eflags & CF;
        writeReg(osz, r, alu(op < 0x48 ? 0 : 5, readReg(osz, r), 1, osz));
        eflags = (eflags & ~CF) | cf;
        return;
    }
    RELUVA_CASE8(0x50): push(reg[op & 7]); return;
    RELUVA_CASE8(0x58): reg[op & 7] = pop(); return;
    RELUVA_CASE8(0x70):
    RELUVA_CASE8(0x78): { // Jcc rel8
        const uint32_t rel = uint32_t(int32_t(int8_t(fetch8())));
        if (condition(op & 15))
            eip += rel;
        return;
    }
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97: { // XCHG eAX, r
        const uint32_t t = readReg(osz, EAX);
        writeReg(osz, EAX, readReg(osz, op & 7));
        writeReg(osz, op & 7, t);
        return;
    }
    RELUVA_CASE8(0xb0): writeReg(1, op & 7, fetch8()); return; // MOV r8, imm8
    RELUVA_CASE8(0xb8): writeReg(osz, op & 7, osz == 2 ? fetch16() : fetch32()); return; // MOV r, imm
    RELUVA_CASE8(0xd8): x87(op); return;
    case 0x0f: twoByte(); return;
    // Segment registers: pushed and popped as selectors (the model is flat).
    case 0x06: pushSegment(0); return; // PUSH ES
    case 0x07: popSegment(0); return;  // POP ES
    case 0x0e: pushSegment(1); return; // PUSH CS
    case 0x16: pushSegment(2); return; // PUSH SS
    case 0x17: popSegment(2); return;  // POP SS
    case 0x1e: pushSegment(3); return; // PUSH DS
    case 0x1f: popSegment(3); return;  // POP DS
    case 0x8c: // MOV r/m16, Sreg
        decodeModrm();
        if (d_.regField > 5)
            fault("MOV from segment register " + std::to_string(d_.regField));
        if (d_.memoryOperand)
            store(d_.address, sreg[d_.regField], 2);
        else
            writeReg(osz, d_.rm, sreg[d_.regField]);
        return;
    case 0x8e: // MOV Sreg, r/m16
        decodeModrm();
        if (d_.regField > 5 || d_.regField == 1)
            fault("MOV to segment register " + std::to_string(d_.regField));
        sreg[d_.regField] = uint16_t(readRm(2));
        return;
    case 0x27: { // DAA
        const uint32_t al = reg[EAX] & 0xff, cf = eflags & CF;
        uint32_t r = al;
        bool newCf = false;
        if ((al & 0xf) > 9 || (eflags & AF)) {
            r += 6;
            eflags |= AF;
        } else {
            eflags &= ~AF;
        }
        if (al > 0x99 || cf) {
            r += 0x60;
            newCf = true;
        }
        setLogicFlags(r & 0xff, 1);
        if (newCf)
            eflags |= CF;
        writeReg(1, EAX, r);
        return;
    }
    case 0x60: { // PUSHAD
        const uint32_t sp = reg[ESP];
        for (int r = 0; r < 8; ++r)
            push(r == ESP ? sp : reg[size_t(r)]);
        return;
    }
    case 0x61: // POPAD
        for (int r = 7; r >= 0; --r) {
            const uint32_t v = pop();
            if (r != ESP)
                reg[size_t(r)] = v;
        }
        return;
    case 0x68: push(fetch32()); return;
    case 0x6a: push(uint32_t(int32_t(int8_t(fetch8())))); return;
    case 0x69: // IMUL r, r/m, imm
    case 0x6b: {
        decodeModrm();
        const int32_t imm = op == 0x6b ? int8_t(fetch8()) : signExtend(osz == 2 ? fetch16() : fetch32(), osz);
        const int64_t p = int64_t(signExtend(readRm(osz), osz)) * imm;
        writeReg(osz, d_.regField, uint32_t(p));
        eflags &= ~(CF | OF);
        if (p != int64_t(signExtend(uint32_t(p), osz)))
            eflags |= CF | OF;
        return;
    }
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83: { // ALU r/m, imm
        const int size = (op == 0x81 || op == 0x83) ? osz : 1;
        decodeModrm();
        const uint32_t imm = op == 0x81 ? (osz == 2 ? fetch16() : fetch32()) : op == 0x83 ? uint32_t(int32_t(int8_t(fetch8())))
                                                                                         : fetch8();
        const uint32_t r = alu(d_.regField, readRm(size), imm, size);
        if (d_.regField != 7)
            writeRm(size, r);
        return;
    }
    case 0x84:
    case 0x85: { // TEST r/m, r
        const int size = op == 0x84 ? 1 : osz;
        decodeModrm();
        setLogicFlags(readRm(size) & readReg(size, d_.regField), size);
        return;
    }
    case 0x86:
    case 0x87: { // XCHG r/m, r
        const int size = op == 0x86 ? 1 : osz;
        decodeModrm();
        const uint32_t t = readRm(size);
        writeRm(size, readReg(size, d_.regField));
        writeReg(size, d_.regField, t);
        return;
    }
    case 0x88: decodeModrm(); writeRm(1, readReg(1, d_.regField)); return;
    case 0x89: decodeModrm(); writeRm(osz, readReg(osz, d_.regField)); return;
    case 0x8a: decodeModrm(); writeReg(1, d_.regField, readRm(1)); return;
    case 0x8b: decodeModrm(); writeReg(osz, d_.regField, readRm(osz)); return;
    case 0x8d: // LEA (the offset, without a segment's base)
        decodeModrm();
        if (!d_.memoryOperand)
            fault("LEA with a register operand");
        writeReg(osz, d_.regField, d_.address - d_.segment);
        return;
    case 0x8f: { // POP r/m
        const uint32_t v = pop();
        decodeModrm();
        writeRm(4, v);
        return;
    }
    case 0x90: return; // NOP
    case 0x98: // CWDE / CBW
        if (osz == 2)
            writeReg(2, EAX, uint32_t(int32_t(int8_t(reg[EAX]))));
        else
            reg[EAX] = uint32_t(int32_t(int16_t(reg[EAX])));
        return;
    case 0x99: // CDQ / CWD
        if (osz == 2)
            writeReg(2, EDX, (reg[EAX] & 0x8000) ? 0xffff : 0);
        else
            reg[EDX] = (reg[EAX] & 0x80000000u) ? 0xffffffffu : 0;
        return;
    case 0x9b: return; // FWAIT
    case 0x9c: push(eflags); return; // PUSHFD
    case 0x9d: // POPFD: the flags a program may change (ID and AC too, which
               // tell it the processor has CPUID)
        eflags = (pop() & 0x00240fd5u) | 0x2;
        return;
    case 0x9e: eflags = (eflags & ~0xd5u) | (((reg[EAX] >> 8) & 0xd5u)); return; // SAHF
    case 0x9f: writeReg(1, 4, (eflags & 0xd5u) | 0x2); return;                  // LAHF
    case 0xa0: writeReg(1, EAX, load(fetch32() + d_.segment, 1)); return;
    case 0xa1: writeReg(osz, EAX, load(fetch32() + d_.segment, osz)); return;
    case 0xa2: store(fetch32() + d_.segment, readReg(1, EAX), 1); return;
    case 0xa3: store(fetch32() + d_.segment, readReg(osz, EAX), osz); return;
    case 0xa4:
    case 0xa5:
    case 0xa6:
    case 0xa7:
    case 0xaa:
    case 0xab:
    case 0xac:
    case 0xad:
    case 0xae:
    case 0xaf: stringOp(op); return;
    case 0xa8: setLogicFlags(readReg(1, EAX) & fetch8(), 1); return;
    case 0xa9: setLogicFlags(readReg(osz, EAX) & (osz == 2 ? fetch16() : fetch32()), osz); return;
    case 0xc0:
    case 0xc1:
    case 0xd0:
    case 0xd1:
    case 0xd2:
    case 0xd3: { // shifts and rotates
        const int size = (op & 1) ? osz : 1;
        decodeModrm();
        const uint32_t count = op <= 0xc1 ? fetch8() : op <= 0xd1 ? 1 : reg[ECX] & 0xff;
        writeRm(size, shift(d_.regField, readRm(size), count, size));
        return;
    }
    case 0xc2: { // RET imm16
        const uint32_t n = fetch16();
        eip = pop();
        reg[ESP] += n;
        return;
    }
    case 0xc3: eip = pop(); return;
    case 0xc6: decodeModrm(); writeRm(1, fetch8()); return;
    case 0xc7: decodeModrm(); writeRm(osz, osz == 2 ? fetch16() : fetch32()); return;
    case 0xc8: { // ENTER (nesting level 0)
        const uint32_t size = fetch16(), level = fetch8();
        if (level)
            fault("ENTER with a nesting level");
        push(reg[EBP]);
        reg[EBP] = reg[ESP];
        reg[ESP] -= size;
        return;
    }
    case 0xc9: // LEAVE
        reg[ESP] = reg[EBP];
        reg[EBP] = pop();
        return;
    case 0xcc: fault("breakpoint (INT3)");
    case 0xcd: fault("software interrupt " + std::to_string(fetch8()));
    case 0xd7: writeReg(1, EAX, load(reg[EBX] + (reg[EAX] & 0xff) + d_.segment, 1)); return; // XLAT
    case 0xe0:
    case 0xe1:
    case 0xe2: { // LOOPNE, LOOPE, LOOP
        const uint32_t rel = uint32_t(int32_t(int8_t(fetch8())));
        --reg[ECX];
        const bool zf = (eflags & ZF) != 0;
        if (reg[ECX] && (op == 0xe2 || (op == 0xe1 ? zf : !zf)))
            eip += rel;
        return;
    }
    case 0xe3: { // JECXZ
        const uint32_t rel = uint32_t(int32_t(int8_t(fetch8())));
        if (!reg[ECX])
            eip += rel;
        return;
    }
    case 0xe8: { // CALL rel32
        const uint32_t rel = fetch32();
        push(eip);
        eip += rel;
        return;
    }
    case 0xe9: eip += fetch32(); return;
    case 0xeb: eip += uint32_t(int32_t(int8_t(fetch8()))); return;
    case 0xf4: fault("HLT");
    case 0xf5: eflags ^= CF; return;
    case 0xf6: decodeModrm(); group3(1); return;
    case 0xf7: decodeModrm(); group3(osz); return;
    case 0xf8: eflags &= ~CF; return;
    case 0xf9: eflags |= CF; return;
    case 0xfa:
    case 0xfb: return; // CLI, STI: nothing to do
    case 0xfc: eflags &= ~DF; return;
    case 0xfd: eflags |= DF; return;
    case 0xfe: { // INC/DEC r/m8
        decodeModrm();
        if (d_.regField > 1)
            fault("undefined instruction fe /" + std::to_string(d_.regField));
        const uint32_t cf = eflags & CF;
        writeRm(1, alu(d_.regField ? 5 : 0, readRm(1), 1, 1));
        eflags = (eflags & ~CF) | cf;
        return;
    }
    case 0xff: {
        decodeModrm();
        switch (d_.regField) {
        case 0:
        case 1: { // INC/DEC r/m
            const uint32_t cf = eflags & CF;
            writeRm(osz, alu(d_.regField ? 5 : 0, readRm(osz), 1, osz));
            eflags = (eflags & ~CF) | cf;
            return;
        }
        case 2: { // CALL r/m
            const uint32_t target = readRm(4);
            push(eip);
            eip = target;
            return;
        }
        case 4: eip = readRm(4); return; // JMP r/m
        case 6: push(readRm(4)); return; // PUSH r/m
        default: fault("far call or jump not implemented");
        }
    }
    default: {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%02x", op);
        fault(std::string("instruction ") + buf + " not implemented");
    }
    }
}

std::vector<std::pair<std::string, uint64_t>> Cpu::nativeCallCounts() const {
    std::vector<std::pair<std::string, uint64_t>> out;
    for (const NativeEntry& n : natives_)
        if (n.calls)
            out.push_back({n.name, n.calls});
    return out;
}

void Cpu::callNative(uint32_t address) {
    if (address < nativeBase_ || (address - nativeBase_) % kNativeStride)
        fault("jump into the middle of native code");
    const uint32_t i = (address - nativeBase_) / kNativeStride;
    if (i >= natives_.size())
        fault("call to an unused native slot");
    ++natives_[i].calls;
    if (nativeTrace)
        nativeTrace(*this, natives_[i].name);
    const uint32_t popBytes = natives_[i].fn(*this);
    if (!abort_.empty()) {
        std::string message = std::move(abort_);
        abort_.clear();
        throw Fault{message + " (in " + natives_[i].name + ")"};
    }
    eip = pop();
    reg[ESP] += popBytes;
}

Cpu::Context Cpu::saveContext() const {
    Context c;
    c.reg = reg;
    c.eip = eip;
    c.eflags = eflags;
    c.fsBase = fsBase;
    c.sreg = sreg;
    c.fpu = fpu;
    c.depth = depth_;
    c.limitAt = limitAt_;
    c.instructionStart = instructionStart_;
    c.abort = abort_;
    return c;
}

void Cpu::loadContext(const Context& c) {
    reg = c.reg;
    eip = c.eip;
    eflags = c.eflags;
    fsBase = c.fsBase;
    sreg = c.sreg;
    fpu = c.fpu;
    depth_ = c.depth;
    limitAt_ = c.limitAt;
    instructionStart_ = c.instructionStart;
    abort_ = c.abort;
    updateNextEvent();
}

void Cpu::runUntil(uint32_t returnAddress) {
    while (eip != returnAddress)
        step();
}

Error Cpu::call(uint32_t address, const std::vector<uint32_t>& args, uint32_t& result) {
    const uint32_t returnAddress = nativeBase_ - kNativeStride; // a HLT that's never run
    const uint32_t savedEsp = reg[ESP], savedEip = eip;
    if (depth_ == 0) {
        limitAt_ = executed_ + limit_;
        updateNextEvent();
        faultEip = 0;
    }
    ++depth_;
    try {
        for (auto it = args.rbegin(); it != args.rend(); ++it)
            push(*it);
        push(returnAddress);
        eip = address;
        runUntil(returnAddress);
    } catch (Fault& f) {
        if (faultEip == 0 || depth_ == 1) {
            faultReg = reg;
            faultEip = instructionStart_;
        }
        --depth_;
        reg[ESP] = savedEsp;
        eip = savedEip;
        if (depth_ > 0)
            throw;
        return Error::fail(f.message);
    }
    --depth_;
    result = reg[EAX];
    reg[ESP] = savedEsp;
    eip = savedEip;
    return Error::success();
}

} // namespace reluva::x86
