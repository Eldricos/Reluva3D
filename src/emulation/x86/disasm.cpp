// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A table-driven 32-bit x86 disassembler, after the Intel manuals' opcode
// maps (volume 2, appendix A). Operands are written as capstone writes them
// in Intel syntax, so listings read the same as those made with it.

#include "emulation/x86/disasm.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace reluva::x86 {

namespace {

// An opcode: its mnemonic ("cwde|cbw": with 32- and 16-bit operands) and
// its operands, comma separated:
//   Eb Ew Ev Ed  register or memory (ModRM rm): 8, 16, 16/32, 32 bits
//   Gb Gw Gv Gd  register (ModRM reg)
//   M Mp         memory only: no size (lea), far pointer
//   Ib Iw Iz Is  immediate: 8, 16, 16/32 bits; 8 bits sign-extended
//   Jb Jz        relative target
//   Ob Ov        memory at an absolute address (moffs)
//   Zb Zv        register in the opcode's low three bits
//   Sw Cd Dd     segment, control and debug register (ModRM reg)
//   Ap           far address (selector:offset)
//   al ax eAX cl dx 1 es cs ss ds fs gs: themselves
struct Opcode {
    const char *name;
    const char *operands;
};

constexpr Opcode kNone{nullptr, ""};

// The one-byte map. Group opcodes (80-83, C0, C1, D0-D3, F6, F7, FE, FF,
// 8F, C6, C7) take their mnemonic from the ModRM reg field (kGroups).
const Opcode kOneByte[256] = {
    // 00
    {"add", "Eb,Gb"},
    {"add", "Ev,Gv"},
    {"add", "Gb,Eb"},
    {"add", "Gv,Ev"},
    {"add", "al,Ib"},
    {"add", "eAX,Iz"},
    {"push", "es"},
    {"pop", "es"},
    {"or", "Eb,Gb"},
    {"or", "Ev,Gv"},
    {"or", "Gb,Eb"},
    {"or", "Gv,Ev"},
    {"or", "al,Ib"},
    {"or", "eAX,Iz"},
    {"push", "cs"},
    kNone,
    // 10
    {"adc", "Eb,Gb"},
    {"adc", "Ev,Gv"},
    {"adc", "Gb,Eb"},
    {"adc", "Gv,Ev"},
    {"adc", "al,Ib"},
    {"adc", "eAX,Iz"},
    {"push", "ss"},
    {"pop", "ss"},
    {"sbb", "Eb,Gb"},
    {"sbb", "Ev,Gv"},
    {"sbb", "Gb,Eb"},
    {"sbb", "Gv,Ev"},
    {"sbb", "al,Ib"},
    {"sbb", "eAX,Iz"},
    {"push", "ds"},
    {"pop", "ds"},
    // 20
    {"and", "Eb,Gb"},
    {"and", "Ev,Gv"},
    {"and", "Gb,Eb"},
    {"and", "Gv,Ev"},
    {"and", "al,Ib"},
    {"and", "eAX,Iz"},
    kNone,
    {"daa", ""},
    {"sub", "Eb,Gb"},
    {"sub", "Ev,Gv"},
    {"sub", "Gb,Eb"},
    {"sub", "Gv,Ev"},
    {"sub", "al,Ib"},
    {"sub", "eAX,Iz"},
    kNone,
    {"das", ""},
    // 30
    {"xor", "Eb,Gb"},
    {"xor", "Ev,Gv"},
    {"xor", "Gb,Eb"},
    {"xor", "Gv,Ev"},
    {"xor", "al,Ib"},
    {"xor", "eAX,Iz"},
    kNone,
    {"aaa", ""},
    {"cmp", "Eb,Gb"},
    {"cmp", "Ev,Gv"},
    {"cmp", "Gb,Eb"},
    {"cmp", "Gv,Ev"},
    {"cmp", "al,Ib"},
    {"cmp", "eAX,Iz"},
    kNone,
    {"aas", ""},
    // 40
    {"inc", "Zv"},
    {"inc", "Zv"},
    {"inc", "Zv"},
    {"inc", "Zv"},
    {"inc", "Zv"},
    {"inc", "Zv"},
    {"inc", "Zv"},
    {"inc", "Zv"},
    {"dec", "Zv"},
    {"dec", "Zv"},
    {"dec", "Zv"},
    {"dec", "Zv"},
    {"dec", "Zv"},
    {"dec", "Zv"},
    {"dec", "Zv"},
    {"dec", "Zv"},
    // 50
    {"push", "Zv"},
    {"push", "Zv"},
    {"push", "Zv"},
    {"push", "Zv"},
    {"push", "Zv"},
    {"push", "Zv"},
    {"push", "Zv"},
    {"push", "Zv"},
    {"pop", "Zv"},
    {"pop", "Zv"},
    {"pop", "Zv"},
    {"pop", "Zv"},
    {"pop", "Zv"},
    {"pop", "Zv"},
    {"pop", "Zv"},
    {"pop", "Zv"},
    // 60
    {"pushal|pushaw", ""},
    {"popal|popaw", ""},
    {"bound", "Gv,Mp"},
    {"arpl", "Ew,Gw"},
    kNone,
    kNone,
    kNone,
    kNone,
    {"push", "Iz"},
    {"imul", "Gv,Ev,Iz"},
    {"push", "Is"},
    {"imul", "Gv,Ev,Is"},
    kNone,
    kNone,
    kNone,
    kNone,
    // 70
    {"jo", "Jb"},
    {"jno", "Jb"},
    {"jb", "Jb"},
    {"jae", "Jb"},
    {"je", "Jb"},
    {"jne", "Jb"},
    {"jbe", "Jb"},
    {"ja", "Jb"},
    {"js", "Jb"},
    {"jns", "Jb"},
    {"jp", "Jb"},
    {"jnp", "Jb"},
    {"jl", "Jb"},
    {"jge", "Jb"},
    {"jle", "Jb"},
    {"jg", "Jb"},
    // 80
    {"", "Eb,Ib"},
    {"", "Ev,Iz"},
    {"", "Eb,Ib"},
    {"", "Ev,Is"},
    {"test", "Eb,Gb"},
    {"test", "Ev,Gv"},
    {"xchg", "Eb,Gb"},
    {"xchg", "Ev,Gv"},
    {"mov", "Eb,Gb"},
    {"mov", "Ev,Gv"},
    {"mov", "Gb,Eb"},
    {"mov", "Gv,Ev"},
    {"mov", "Ev,Sw"},
    {"lea", "Gv,M"},
    {"mov", "Sw,Ew"},
    {"", "Ev"},
    // 90
    {"nop", ""},
    {"xchg", "Zv,eAX"},
    {"xchg", "Zv,eAX"},
    {"xchg", "Zv,eAX"},
    {"xchg", "Zv,eAX"},
    {"xchg", "Zv,eAX"},
    {"xchg", "Zv,eAX"},
    {"xchg", "Zv,eAX"},
    {"cwde|cbw", ""},
    {"cdq|cwd", ""},
    {"lcall", "Ap"},
    {"wait", ""},
    {"pushfd|pushf", ""},
    {"popfd|popf", ""},
    {"sahf", ""},
    {"lahf", ""},
    // A0 (string instructions: stringOperands)
    {"mov", "al,Ob"},
    {"mov", "eAX,Ov"},
    {"mov", "Ob,al"},
    {"mov", "Ov,eAX"},
    {"movsb", ""},
    {"movsd|movsw", ""},
    {"cmpsb", ""},
    {"cmpsd|cmpsw", ""},
    {"test", "al,Ib"},
    {"test", "eAX,Iz"},
    {"stosb", ""},
    {"stosd|stosw", ""},
    {"lodsb", ""},
    {"lodsd|lodsw", ""},
    {"scasb", ""},
    {"scasd|scasw", ""},
    // B0
    {"mov", "Zb,Ib"},
    {"mov", "Zb,Ib"},
    {"mov", "Zb,Ib"},
    {"mov", "Zb,Ib"},
    {"mov", "Zb,Ib"},
    {"mov", "Zb,Ib"},
    {"mov", "Zb,Ib"},
    {"mov", "Zb,Ib"},
    {"mov", "Zv,Iz"},
    {"mov", "Zv,Iz"},
    {"mov", "Zv,Iz"},
    {"mov", "Zv,Iz"},
    {"mov", "Zv,Iz"},
    {"mov", "Zv,Iz"},
    {"mov", "Zv,Iz"},
    {"mov", "Zv,Iz"},
    // C0
    {"", "Eb,Ib"},
    {"", "Ev,Ib"},
    {"ret", "Iw"},
    {"ret", ""},
    {"les", "Gv,Mp"},
    {"lds", "Gv,Mp"},
    {"", "Eb,Ib"},
    {"", "Ev,Iz"},
    {"enter", "Iw,Ib"},
    {"leave", ""},
    {"retf", "Iw"},
    {"retf", ""},
    {"int3", ""},
    {"int", "Ib"},
    {"into", ""},
    {"iretd|iret", ""},
    // D0 (D8-DF: the FPU, fpu())
    {"", "Eb,1"},
    {"", "Ev,1"},
    {"", "Eb,cl"},
    {"", "Ev,cl"},
    {"aam", "Ib"},
    {"aad", "Ib"},
    {"salc", ""},
    {"xlatb", ""},
    kNone,
    kNone,
    kNone,
    kNone,
    kNone,
    kNone,
    kNone,
    kNone,
    // E0
    {"loopne", "Jb"},
    {"loope", "Jb"},
    {"loop", "Jb"},
    {"jecxz|jcxz", "Jb"},
    {"in", "al,Ib"},
    {"in", "eAX,Ib"},
    {"out", "Ib,al"},
    {"out", "Ib,eAX"},
    {"call", "Jz"},
    {"jmp", "Jz"},
    {"ljmp", "Ap"},
    {"jmp", "Jb"},
    {"in", "al,dx"},
    {"in", "eAX,dx"},
    {"out", "dx,al"},
    {"out", "dx,eAX"},
    // F0
    kNone,
    {"int1", ""},
    kNone,
    kNone,
    {"hlt", ""},
    {"cmc", ""},
    {"", "Eb"},
    {"", "Ev"},
    {"clc", ""},
    {"stc", ""},
    {"cli", ""},
    {"sti", ""},
    {"cld", ""},
    {"std", ""},
    {"", "Eb"},
    {"", "Ev"},
};

// The groups' mnemonics by ModRM reg; "" is not an instruction. An entry
// with operands of its own replaces the opcode's.
struct Group {
    uint8_t opcode;
    Opcode by[8];
};

const Opcode kArith[8] = {{"add", nullptr}, {"or", nullptr},  {"adc", nullptr}, {"sbb", nullptr},
                          {"and", nullptr}, {"sub", nullptr}, {"xor", nullptr}, {"cmp", nullptr}};
const Opcode kShift[8] = {{"rol", nullptr}, {"ror", nullptr}, {"rcl", nullptr}, {"rcr", nullptr},
                          {"shl", nullptr}, {"shr", nullptr}, {"sal", nullptr}, {"sar", nullptr}};

const Group kGroups[] = {
    {0x8f, {{"pop", nullptr}, {}, {}, {}, {}, {}, {}, {}}},
    {0xc6, {{"mov", nullptr}, {}, {}, {}, {}, {}, {}, {}}},
    {0xc7, {{"mov", nullptr}, {}, {}, {}, {}, {}, {}, {}}},
    {0xf6,
     {{"test", "Eb,Ib"},
      {"test", "Eb,Ib"},
      {"not", "Eb"},
      {"neg", "Eb"},
      {"mul", "Eb"},
      {"imul", "Eb"},
      {"div", "Eb"},
      {"idiv", "Eb"}}},
    {0xf7,
     {{"test", "Ev,Iz"},
      {"test", "Ev,Iz"},
      {"not", "Ev"},
      {"neg", "Ev"},
      {"mul", "Ev"},
      {"imul", "Ev"},
      {"div", "Ev"},
      {"idiv", "Ev"}}},
    {0xfe, {{"inc", nullptr}, {"dec", nullptr}, {}, {}, {}, {}, {}, {}}},
    {0xff,
     {{"inc", "Ev"},
      {"dec", "Ev"},
      {"call", "Ev"},
      {"lcall", "Mp"},
      {"jmp", "Ev"},
      {"ljmp", "Mp"},
      {"push", "Ev"},
      {}}},
};

// The 0x0F map.
const Opcode *twoByte(uint8_t op) {
    static const std::array<Opcode, 256> kTable = [] {
        std::array<Opcode, 256> t{};
        auto set = [&](int o, const char *n, const char *ops) { t[size_t(o)] = {n, ops}; };
        set(0x00, "", "Ew"); // group 6
        set(0x01, "", "M");  // group 7
        set(0x02, "lar", "Gv,Ew");
        set(0x03, "lsl", "Gv,Ew");
        set(0x06, "clts", "");
        set(0x08, "invd", "");
        set(0x09, "wbinvd", "");
        set(0x0b, "ud2", "");
        set(0x20, "mov", "Rd,Cd");
        set(0x21, "mov", "Rd,Dd");
        set(0x22, "mov", "Cd,Rd");
        set(0x23, "mov", "Dd,Rd");
        set(0x30, "wrmsr", "");
        set(0x31, "rdtsc", "");
        set(0x32, "rdmsr", "");
        set(0x33, "rdpmc", "");
        static const char *const kCc[16] = {"o", "no", "b", "ae", "e", "ne", "be", "a",
                                            "s", "ns", "p", "np", "l", "ge", "le", "g"};
        static std::string names[3][16];
        for (int i = 0; i < 16; ++i) {
            names[0][i] = std::string("cmov") + kCc[i];
            names[1][i] = std::string("j") + kCc[i];
            names[2][i] = std::string("set") + kCc[i];
            set(0x40 + i, names[0][i].c_str(), "Gv,Ev");
            set(0x80 + i, names[1][i].c_str(), "Jz");
            set(0x90 + i, names[2][i].c_str(), "Eb");
        }
        set(0xa0, "push", "fs");
        set(0xa1, "pop", "fs");
        set(0xa2, "cpuid", "");
        set(0xa3, "bt", "Ev,Gv");
        set(0xa4, "shld", "Ev,Gv,Ib");
        set(0xa5, "shld", "Ev,Gv,cl");
        set(0xa8, "push", "gs");
        set(0xa9, "pop", "gs");
        set(0xab, "bts", "Ev,Gv");
        set(0xac, "shrd", "Ev,Gv,Ib");
        set(0xad, "shrd", "Ev,Gv,cl");
        set(0xaf, "imul", "Gv,Ev");
        set(0xb0, "cmpxchg", "Eb,Gb");
        set(0xb1, "cmpxchg", "Ev,Gv");
        set(0xb2, "lss", "Gv,Mp");
        set(0xb3, "btr", "Ev,Gv");
        set(0xb4, "lfs", "Gv,Mp");
        set(0xb5, "lgs", "Gv,Mp");
        set(0xb6, "movzx", "Gv,Eb");
        set(0xb7, "movzx", "Gv,Ew");
        set(0xba, "", "Ev,Ib"); // group 8
        set(0xbb, "btc", "Ev,Gv");
        set(0xbc, "bsf", "Gv,Ev");
        set(0xbd, "bsr", "Gv,Ev");
        set(0xbe, "movsx", "Gv,Eb");
        set(0xbf, "movsx", "Gv,Ew");
        set(0xc0, "xadd", "Eb,Gb");
        set(0xc1, "xadd", "Ev,Gv");
        for (int i = 0; i < 8; ++i)
            set(0xc8 + i, "bswap", "Zd");
        return t;
    }();
    return &kTable[op];
}

const Opcode kGroup6[8] = {{"sldt", nullptr},
                           {"str", nullptr},
                           {"lldt", nullptr},
                           {"ltr", nullptr},
                           {"verr", nullptr},
                           {"verw", nullptr},
                           {},
                           {}};
const Opcode kGroup7[8] = {{"sgdt", nullptr}, {"sidt", nullptr},  {"lgdt", nullptr},
                           {"lidt", nullptr}, {"smsw", "Ew"},     {},
                           {"lmsw", "Ew"},    {"invlpg", nullptr}};
const Opcode kGroup8[8] = {{}, {}, {}, {}, {"bt", nullptr}, {"bts", nullptr}, {"btr", nullptr}, {"btc", nullptr}};

const char *const kReg8[8] = {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};
const char *const kReg16[8] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
const char *const kReg32[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
const char *const kSeg[8] = {"es", "cs", "ss", "ds", "fs", "gs", "", ""};

// Numbers as capstone writes them: up to 9 in decimal, else hex.
std::string number(int64_t v) {
    char buf[32];
    const bool negative = v < 0;
    const uint64_t m = negative ? uint64_t(-v) : uint64_t(v);
    if (m <= 9)
        std::snprintf(buf, sizeof buf, "%s%llu", negative ? "-" : "", static_cast<unsigned long long>(m));
    else
        std::snprintf(buf, sizeof buf, "%s0x%llx", negative ? "-" : "", static_cast<unsigned long long>(m));
    return buf;
}

std::string hexAddress(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

const char *sizeName(int bytes) {
    switch (bytes) {
    case 1:
        return "byte ptr ";
    case 2:
        return "word ptr ";
    case 4:
        return "dword ptr ";
    case 6:
        return "fword ptr ";
    case 8:
        return "qword ptr ";
    case 10:
        return "xword ptr ";
    default:
        return "";
    }
}

// Reads the instruction's bytes, failing once they run out.
class Reader {
public:
    Reader(const uint8_t *code, size_t size) : _code(code), _size(size) {}
    bool ok() const { return _ok; }
    size_t pos() const { return _pos; }
    uint8_t peek() {
        if (_pos >= _size) {
            _ok = false;
            return 0;
        }
        return _code[_pos];
    }
    uint32_t get(int bytes) {
        uint32_t v = 0;
        for (int i = 0; i < bytes; ++i) {
            if (_pos >= _size || _pos >= 15) {
                _ok = false;
                return 0;
            }
            v |= uint32_t(_code[_pos++]) << (8 * i);
        }
        return v;
    }

private:
    const uint8_t *_code;
    size_t _size;
    size_t _pos = 0;
    bool _ok = true;
};

struct Decoder {
    Decoder(const uint8_t *code, size_t size, uint32_t at) : in(code, size), address(at) {}

    Reader in;
    uint32_t address;
    bool opsize16 = false;
    bool addrsize16 = false;
    int segment = -1; // a segment override, or -1
    bool rep = false;
    bool repne = false;
    bool lock = false;
    bool haveModrm = false;
    uint8_t modrm = 0;
    Instruction out;

    int mod() const { return modrm >> 6; }
    int reg() const { return (modrm >> 3) & 7; }
    int rm() const { return modrm & 7; }

    void readModrm() {
        if (!haveModrm) {
            modrm = uint8_t(in.get(1));
            haveModrm = true;
        }
    }

    std::string segmentPrefix() const { return segment >= 0 ? std::string(kSeg[segment]) + ":" : std::string(); }

    // The ModRM memory operand, `bytes` wide (0: no size, as for lea).
    std::string memory(int bytes) {
        std::string inside;
        auto add = [&](const std::string &part) {
            if (!inside.empty())
                inside += " + ";
            inside += part;
        };
        int64_t disp = 0;
        bool haveDisp = false;
        if (addrsize16) {
            static const char *const kBase16[8] = {"bx + si", "bx + di", "bp + si", "bp + di", "si", "di", "bp", "bx"};
            if (mod() == 0 && rm() == 6) {
                disp = in.get(2);
                haveDisp = true;
            } else {
                add(kBase16[rm()]);
                if (mod() == 1) {
                    disp = int8_t(in.get(1));
                    haveDisp = true;
                } else if (mod() == 2) {
                    disp = int16_t(in.get(2));
                    haveDisp = true;
                }
            }
        } else {
            int base = rm();
            bool noBase = false;
            if (rm() == 4) {
                const uint8_t sib = uint8_t(in.get(1));
                const int scale = 1 << (sib >> 6);
                const int index = (sib >> 3) & 7;
                base = sib & 7;
                noBase = base == 5 && mod() == 0;
                if (!noBase)
                    add(kReg32[base]);
                if (index != 4)
                    add(std::string(kReg32[index]) + (scale > 1 ? "*" + std::to_string(scale) : ""));
                if (noBase) {
                    disp = int32_t(in.get(4));
                    haveDisp = true;
                }
            } else if (mod() == 0 && rm() == 5) {
                disp = in.get(4);
                haveDisp = true;
                noBase = true;
            } else {
                add(kReg32[base]);
            }
            if (mod() == 1) {
                disp = int8_t(in.get(1));
                haveDisp = true;
            } else if (mod() == 2) {
                disp = int32_t(in.get(4));
                haveDisp = true;
            }
        }
        if (inside.empty())
            inside = hexAddress(uint32_t(disp));
        else if (haveDisp && disp != 0)
            inside += (disp < 0 ? " - " : " + ") + number(disp < 0 ? -disp : disp);
        return std::string(sizeName(bytes)) + segmentPrefix() + "[" + inside + "]";
    }

    std::string regOrMemory(int bytes) {
        readModrm();
        if (mod() == 3)
            return bytes == 1 ? kReg8[rm()] : bytes == 2 ? kReg16[rm()] : kReg32[rm()];
        return memory(bytes);
    }

    // One operand from its code; false when it can't be (a register where
    // memory must be).
    bool operand(std::string_view code, std::string &text, const char *mnemonic, uint8_t opcode) {
        const int v = opsize16 ? 2 : 4;
        auto reg = [&](int bytes, int n) { return bytes == 1 ? kReg8[n] : bytes == 2 ? kReg16[n] : kReg32[n]; };
        if (code == "Eb")
            text = regOrMemory(1);
        else if (code == "Ew")
            text = regOrMemory(2);
        else if (code == "Ev")
            text = regOrMemory(v);
        else if (code == "Ed")
            text = regOrMemory(4);
        else if (code == "Gb" || code == "Gw" || code == "Gv" || code == "Gd") {
            readModrm();
            text = reg(code == "Gb" ? 1 : code == "Gw" ? 2 : code == "Gv" ? v : 4, this->reg());
        } else if (code == "Rd") {
            readModrm();
            text = kReg32[rm()];
        } else if (code == "Cd" || code == "Dd") {
            readModrm();
            text = (code == "Cd" ? "cr" : "dr") + std::to_string(this->reg());
        } else if (code == "Sw") {
            readModrm();
            if (this->reg() > 5)
                return false;
            text = kSeg[this->reg()];
        } else if (code == "M" || code == "Mp") {
            readModrm();
            if (mod() == 3)
                return false;
            text = memory(code == "M" ? 0 : opsize16 ? 4 : 6);
        } else if (code == "Ib") {
            text = number(in.get(1));
        } else if (code == "Iw") {
            text = number(in.get(2));
        } else if (code == "Iz") {
            const uint32_t i = in.get(v);
            text = number(i);
        } else if (code == "Is") {
            // Sign-extended: negative for arithmetic, the full mask for the
            // logical operations (as capstone shows them).
            const int32_t i = int8_t(in.get(1));
            const std::string_view m = mnemonic;
            if (i < 0 && (m == "and" || m == "or" || m == "xor"))
                text = number(opsize16 ? uint16_t(i) : uint32_t(i));
            else
                text = number(i);
        } else if (code == "Jb" || code == "Jz") {
            const int32_t rel = code == "Jb" ? int8_t(in.get(1)) : opsize16 ? int16_t(in.get(2)) : int32_t(in.get(4));
            out.hasTarget = true;
            out.target = address + uint32_t(in.pos()) + uint32_t(rel);
            if (opsize16 && code == "Jz")
                out.target &= 0xffff;
            text = hexAddress(out.target);
        } else if (code == "Ob" || code == "Ov") {
            const uint32_t a = addrsize16 ? in.get(2) : in.get(4);
            text = std::string(sizeName(code == "Ob" ? 1 : v)) + segmentPrefix() + "[" + hexAddress(a) + "]";
        } else if (code == "Zb") {
            text = kReg8[opcode & 7];
        } else if (code == "Zv") {
            text = reg(v, opcode & 7);
        } else if (code == "Zd") {
            text = kReg32[opcode & 7];
        } else if (code == "Ap") {
            const uint32_t offset = in.get(v);
            const uint32_t selector = in.get(2);
            text = hexAddress(selector) + ":" + hexAddress(offset);
        } else if (code == "eAX") {
            text = opsize16 ? "ax" : "eax";
        } else {
            text = std::string(code); // al ax cl dx 1 and the segments
        }
        return true;
    }

    // Operands from a list of codes.
    bool operands(const char *list, const char *mnemonic, uint8_t opcode) {
        std::string_view codes = list ? list : "";
        while (!codes.empty()) {
            const size_t comma = codes.find(',');
            std::string text;
            if (!operand(codes.substr(0, comma), text, mnemonic, opcode))
                return false;
            if (!out.operands.empty())
                out.operands += ", ";
            out.operands += text;
            if (comma == std::string_view::npos)
                break;
            codes.remove_prefix(comma + 1);
        }
        return true;
    }

    // The name for the operand size: "cwde|cbw".
    std::string pickName(const char *name) const {
        const char *bar = std::strchr(name, '|');
        if (!bar)
            return name;
        return opsize16 ? std::string(bar + 1) : std::string(name, size_t(bar - name));
    }

    // String instructions, with their implicit operands as capstone shows them.
    bool stringInstruction(uint8_t op) {
        const int bytes = (op & 1) ? (opsize16 ? 2 : 4) : 1;
        const std::string size = sizeName(bytes);
        const std::string di = addrsize16 ? "di" : "edi";
        const std::string si = addrsize16 ? "si" : "esi";
        const std::string dst = size + "es:[" + di + "]";
        const std::string src = size + (segment >= 0 ? segmentPrefix() : "") + "[" + si + "]";
        const char *acc = bytes == 1 ? "al" : bytes == 2 ? "ax" : "eax";
        switch (op & 0xfe) {
        case 0xa4:
            out.operands = dst + ", " + src;
            break;
        case 0xa6:
            out.operands = src + ", " + dst;
            break;
        case 0xaa:
            out.operands = dst + ", " + acc;
            break;
        case 0xac:
            out.operands = std::string(acc) + ", " + src;
            break;
        case 0xae:
            out.operands = std::string(acc) + ", " + dst;
            break;
        case 0x6c:
            out.operands = dst + ", dx";
            break;
        case 0x6e:
            out.operands = "dx, " + src;
            break;
        default:
            return false;
        }
        const bool compares = (op & 0xfe) == 0xa6 || (op & 0xfe) == 0xae;
        if (rep)
            out.mnemonic = (compares ? "repe " : "rep ") + out.mnemonic;
        else if (repne)
            out.mnemonic = (compares ? "repne " : "repne ") + out.mnemonic;
        return true;
    }

    bool fpu(uint8_t op) {
        readModrm();
        const int row = op - 0xd8;
        if (mod() != 3) {
            struct Mem {
                const char *name;
                int bytes;
            };
            static const Mem kTable[8][8] = {
                {{"fadd", 4},
                 {"fmul", 4},
                 {"fcom", 4},
                 {"fcomp", 4},
                 {"fsub", 4},
                 {"fsubr", 4},
                 {"fdiv", 4},
                 {"fdivr", 4}},
                {{"fld", 4},
                 {nullptr, 0},
                 {"fst", 4},
                 {"fstp", 4},
                 {"fldenv", 0},
                 {"fldcw", 2},
                 {"fnstenv", 0},
                 {"fnstcw", 2}},
                {{"fiadd", 4},
                 {"fimul", 4},
                 {"ficom", 4},
                 {"ficomp", 4},
                 {"fisub", 4},
                 {"fisubr", 4},
                 {"fidiv", 4},
                 {"fidivr", 4}},
                {{"fild", 4},
                 {"fisttp", 4},
                 {"fist", 4},
                 {"fistp", 4},
                 {nullptr, 0},
                 {"fld", 10},
                 {nullptr, 0},
                 {"fstp", 10}},
                {{"fadd", 8},
                 {"fmul", 8},
                 {"fcom", 8},
                 {"fcomp", 8},
                 {"fsub", 8},
                 {"fsubr", 8},
                 {"fdiv", 8},
                 {"fdivr", 8}},
                {{"fld", 8},
                 {"fisttp", 8},
                 {"fst", 8},
                 {"fstp", 8},
                 {"frstor", 0},
                 {nullptr, 0},
                 {"fnsave", 0},
                 {"fnstsw", 2}},
                {{"fiadd", 2},
                 {"fimul", 2},
                 {"ficom", 2},
                 {"ficomp", 2},
                 {"fisub", 2},
                 {"fisubr", 2},
                 {"fidiv", 2},
                 {"fidivr", 2}},
                {{"fild", 2},
                 {"fisttp", 2},
                 {"fist", 2},
                 {"fistp", 2},
                 {"fbld", 10},
                 {"fild", 8},
                 {"fbstp", 10},
                 {"fistp", 8}},
            };
            const Mem &m = kTable[row][reg()];
            if (!m.name)
                return false;
            out.mnemonic = m.name;
            out.operands = memory(m.bytes);
            return true;
        }
        const std::string sti = "st(" + std::to_string(rm()) + ")";
        auto both = [&](const char *name, bool stFirst) {
            out.mnemonic = name;
            out.operands = stFirst ? "st(0), " + sti : sti + ", st(0)";
            return true;
        };
        auto one = [&](const char *name) {
            out.mnemonic = name;
            out.operands = sti;
            return true;
        };
        auto none = [&](const char *name) {
            if (!name)
                return false;
            out.mnemonic = name;
            return true;
        };
        switch (row) {
        case 0: {
            static const char *const kN[8] = {"fadd", "fmul", "fcom", "fcomp", "fsub", "fsubr", "fdiv", "fdivr"};
            return reg() == 2 || reg() == 3 ? one(kN[reg()]) : both(kN[reg()], true);
        }
        case 1: {
            if (reg() == 0)
                return one("fld");
            if (reg() == 1)
                return one("fxch");
            if (reg() == 3)
                return one("fstp1");
            switch (modrm) {
            case 0xd0:
                return none("fnop");
            case 0xe0:
                return none("fchs");
            case 0xe1:
                return none("fabs");
            case 0xe4:
                return none("ftst");
            case 0xe5:
                return none("fxam");
            case 0xe8:
                return none("fld1");
            case 0xe9:
                return none("fldl2t");
            case 0xea:
                return none("fldl2e");
            case 0xeb:
                return none("fldpi");
            case 0xec:
                return none("fldlg2");
            case 0xed:
                return none("fldln2");
            case 0xee:
                return none("fldz");
            default:
                break;
            }
            if (reg() == 6) {
                static const char *const kN[8] = {"f2xm1",   "fyl2x",  "fptan",   "fpatan",
                                                  "fxtract", "fprem1", "fdecstp", "fincstp"};
                return none(kN[rm()]);
            }
            if (reg() == 7) {
                static const char *const kN[8] = {"fprem",   "fyl2xp1", "fsqrt", "fsincos",
                                                  "frndint", "fscale",  "fsin",  "fcos"};
                return none(kN[rm()]);
            }
            return false;
        }
        case 2: {
            static const char *const kN[4] = {"fcmovb", "fcmove", "fcmovbe", "fcmovu"};
            if (reg() < 4)
                return both(kN[reg()], true);
            return modrm == 0xe9 ? none("fucompp") : false;
        }
        case 3: {
            static const char *const kN[4] = {"fcmovnb", "fcmovne", "fcmovnbe", "fcmovnu"};
            if (reg() < 4)
                return both(kN[reg()], true);
            if (modrm == 0xe2)
                return none("fnclex");
            if (modrm == 0xe3)
                return none("fninit");
            if (reg() == 5)
                return both("fucomi", true);
            if (reg() == 6)
                return both("fcomi", true);
            return false;
        }
        case 4: {
            static const char *const kN[8] = {"fadd", "fmul", "fcom", "fcomp", "fsubr", "fsub", "fdivr", "fdiv"};
            return reg() == 2 || reg() == 3 ? one(kN[reg()]) : both(kN[reg()], false);
        }
        case 5: {
            static const char *const kN[8] = {"ffree", "fxch", "fst", "fstp", "fucom", "fucomp", nullptr, nullptr};
            return kN[reg()] ? one(kN[reg()]) : false;
        }
        case 6: {
            if (modrm == 0xd9)
                return none("fcompp");
            static const char *const kN[8] = {"faddp", "fmulp", nullptr, nullptr, "fsubrp", "fsubp", "fdivrp", "fdivp"};
            return kN[reg()] ? both(kN[reg()], false) : false;
        }
        default: {
            if (modrm == 0xe0) {
                out.mnemonic = "fnstsw";
                out.operands = "ax";
                return true;
            }
            if (reg() == 5)
                return both("fucomip", true);
            if (reg() == 6)
                return both("fcomip", true);
            if (reg() == 0)
                return one("ffreep");
            return false;
        }
        }
    }

    bool decode() {
        // Prefixes.
        for (;;) {
            const uint8_t p = in.peek();
            if (!in.ok())
                return false;
            if (p == 0x66)
                opsize16 = true;
            else if (p == 0x67)
                addrsize16 = true;
            else if (p == 0xf0)
                lock = true;
            else if (p == 0xf2)
                repne = true, rep = false;
            else if (p == 0xf3)
                rep = true, repne = false;
            else if (p == 0x26 || p == 0x2e || p == 0x36 || p == 0x3e)
                segment = (p >> 3) & 3;
            else if (p == 0x64 || p == 0x65)
                segment = p - 0x60;
            else
                break;
            in.get(1);
        }
        const uint8_t op = uint8_t(in.get(1));
        if (!in.ok())
            return false;
        if (op >= 0xd8 && op <= 0xdf)
            return fpu(op);
        const Opcode *entry;
        Opcode chosen;
        if (op == 0x0f) {
            const uint8_t op2 = uint8_t(in.get(1));
            entry = twoByte(op2);
            if (!entry->name)
                return false;
            chosen = *entry;
            const Opcode *group = op2 == 0x00 ? kGroup6 : op2 == 0x01 ? kGroup7 : op2 == 0xba ? kGroup8 : nullptr;
            if (group) {
                readModrm();
                const Opcode &g = group[reg()];
                if (!g.name)
                    return false;
                chosen.name = g.name;
                if (g.operands)
                    chosen.operands = g.operands;
            }
            out.mnemonic = pickName(chosen.name);
            if (!operands(chosen.operands, chosen.name, op2))
                return false;
            if (lock)
                out.mnemonic = "lock " + out.mnemonic;
            return true;
        }
        entry = &kOneByte[op];
        if (!entry->name)
            return false;
        chosen = *entry;
        if (*chosen.name == '\0') {
            // A group: the mnemonic from ModRM reg.
            readModrm();
            const Opcode *by = nullptr;
            if ((op >= 0x80 && op <= 0x83))
                by = kArith;
            else if (op == 0xc0 || op == 0xc1 || (op >= 0xd0 && op <= 0xd3))
                by = kShift;
            else
                for (const Group &g : kGroups)
                    if (g.opcode == op)
                        by = g.by;
            if (!by || !by[reg()].name || !*by[reg()].name)
                return false;
            chosen.name = by[reg()].name;
            if (by[reg()].operands)
                chosen.operands = by[reg()].operands;
            if (op == 0xff && (reg() == 3 || reg() == 5) && mod() == 3)
                return false;
        }
        out.mnemonic = pickName(chosen.name);
        if ((op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf) || (op >= 0x6c && op <= 0x6f)) {
            if (op >= 0x6c && op <= 0x6f)
                out.mnemonic = op == 0x6c   ? "insb"
                               : op == 0x6d ? (opsize16 ? "insw" : "insd")
                               : op == 0x6e ? "outsb"
                                            : (opsize16 ? "outsw" : "outsd");
            return stringInstruction(op);
        }
        if (!operands(chosen.operands, chosen.name, op))
            return false;
        if (op == 0xe3 && addrsize16)
            out.mnemonic = "jcxz";
        if (lock)
            out.mnemonic = "lock " + out.mnemonic;
        return true;
    }
};

} // namespace

Instruction disassemble(const uint8_t *code, size_t size, uint32_t address) {
    Decoder d(code, size, address);
    d.out.address = address;
    if (d.decode() && d.in.ok()) {
        d.out.valid = true;
        d.out.size = int(d.in.pos());
        return d.out;
    }
    Instruction bad;
    bad.address = address;
    bad.size = 1;
    bad.mnemonic = "db";
    if (size)
        bad.operands = number(code[0]);
    return bad;
}

} // namespace reluva::x86
