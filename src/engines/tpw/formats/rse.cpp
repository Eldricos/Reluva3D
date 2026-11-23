// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's 288 scripts', worked out from them and set
// beside the five sources that ship with them (Data/TestScript/*.rss):
// docs/engines/tpw/games/simcoaster/formats/rse.md.

#include "engines/tpw/formats/rse.h"

#include "common/text.h"

#include <cstring>
#include <set>

namespace reluva::tpw {

namespace {

constexpr size_t kCodeCount = 0x30;
constexpr uint32_t kOpcodeFlag = 0x8000;
constexpr uint32_t kStringFlag = 0x1000;
constexpr uint32_t kAddressFlag = 0x2000;
constexpr uint32_t kVariableFlag = 0x4000;

struct OpcodeInfo {
    const char *name;
    int operands; // -1: not known
};

// By opcode. The names and operand counts the shipped sources use are
// confirmed by their compiled scripts; the rest are OpenTPW's
// (opentpw.gu3.me/formats/rsse-vm-instructions), their operand counts
// checked against every script that uses them. CRIT_UNLOCK, missing there,
// is the sources'.
constexpr OpcodeInfo kOpcodes[] = {
    {"NOP", 0},
    {"CRIT_LOCK", 0},
    {"CRIT_UNLOCK", 0},
    {"COPY", 2},
    {"SETLV", -1},
    {"SUB", 3},
    {"ENDSLICE", 0},
    {"GETTIME", 1},
    {"ADDOBJ", 4},
    {"ADDOBJ_EXT", 5},
    {"KILLOBJ", 1},
    {"FADEOBJ", 1},
    {"SETOBJPARAM", 3},
    {"EVENT", 3},
    {"EVENT_EXT", 4},
    {"FLUSHANIM", 0},
    {"TRIGANIM", 3},
    {"WAITANIM", 2},
    {"LOOPANIM", 2},
    {"TRIGWAITANIM", 3},
    {"GETANIM", 1},
    {"TRIGANIMSPEED", 4},
    {"FLUSHANIM_CH", 1},
    {"TRIGANIM_CH", 4},
    {"WAITANIM_CH", 3},
    {"LOOPANIM_CH", 3},
    {"TRIGWAITANIM_CH", 4},
    {"GETANIM_CH", 2},
    {"RAND", 2},
    {"JSR", 1},
    {"RETURN", 0},
    {"BRANCH", 1},
    {"BRANCH_Z", 1},
    {"BRANCH_NZ", 1},
    {"BRANCH_NV", 1},
    {"BRANCH_PV", 1},
    {"DBGMSG", 1},
    {"NAME", 1},
    {"TEST", 1},
    {"CMP", 2},
    {"PUSH", 1},
    {"POP", 0},
    {"HUSH", 1},
    {"HOP", 1},
    {"WAIT", 1},
    {"WAITABS", 1},
    {"WAIT4ANIM", 0},
    {"ADD", 2},
    {"MULT", 3},
    {"DIV", 3},
    {"MOD", 3},
    {"TURBO", 1},
    {"END", 0},
    {"TOUR", 2},
    {"BUMP", 2},
    {"COAST", 2},
    {"ADDHEAD", 1},
    {"DELHEAD", 1},
    {"LIMBO", 2},
    {"UNLIMBO", 1},
    {"FORCEUNLIMBO", 1},
    {"INLIMBO", 1},
    {"LIMBOSPACE", 1},
    {"SPAWNCHILD", 1},
    {"SPAWNSOUND", 1},
    {"REMOVECHILD", 0},
    {"SETVARINCHILD", 2},
    {"GETVARINCHILD", 2},
    {"SETVARINPARENT", 2},
    {"GETVARINPARENT", 2},
    {"BOUNCESETNODE", 1},
    {"BOUNCESETBASE", 1},
    {"BOUNCE", 2},
    {"UNBOUNCE", 1},
    {"FORCEUNBOUNCE", 1},
    {"BOUNCING", 1},
    {"WALKON", 7},
    {"WALKOFF", 1},
    {"WALKGET", 1},
    {"WALKST_FLOAT", 3},
    {"WALKFLOATSTAT", 1},
    {"WALKFLOATSTOP", 0},
    {"ENABLELIGHT", 1},
    {"DISABLELIGHT", 1},
    {"SETLIGHT", 1},
    {"COLOURLIGHT", 4},
    {"STARTSCREAM", 2},
    {"STOPSCREAM", 0},
    {"SINGLESCREAM", 2},
    {"SCREAMLEVEL", 1},
    {"FINDSCRIPTRAND", 2},
    {"GETREMOTEVAR", 3},
    {"SETREMOTEVAR", 3},
    {"REPAIREFFECT", 1},
    {"GETCUSTPTCLCODE", 2},
    {"SETTIMER", 1},
    {"GETTIMER", 1},
    {"YEAR", 1},
    {"MONTH", 1},
    {"DAY", 1},
    {"HOUR", 1},
    {"MIN", 1},
    {"SEC", 1},
    {"SETREVERB", 1},
    {"DIPMUSIC", 1},
    {"SPARK", 4},
};

uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

} // namespace

const char *Script::opcodeName(uint16_t opcode) {
    return opcode < std::size(kOpcodes) ? kOpcodes[opcode].name : nullptr;
}

int Script::operandCount(uint16_t opcode) {
    return opcode < std::size(kOpcodes) ? kOpcodes[opcode].operands : -1;
}

Error Script::parse(const std::vector<uint8_t> &data) {
    *this = {};
    if (data.size() < kCodeCount + 4 || std::memcmp(data.data(), "RSSEQ", 5) != 0)
        return Error::fail("not a script (RSSEQ)");
    const size_t variables = le32(data, 0x08);
    _stackSize = le32(data, 0x0c);
    _timeSlice = le32(data, 0x10);
    _limboSize = le32(data, 0x14);
    _bounceSize = le32(data, 0x18);
    _walkSize = le32(data, 0x1c);
    const size_t words = le32(data, kCodeCount);
    if (words > (data.size() - kCodeCount - 4) / 4)
        return Error::fail("the script's code runs past the end");
    // Each word: a 16-bit value, then 16 bits of flags.
    const size_t code = kCodeCount + 4;
    for (size_t w = 0; w < words;) {
        const uint32_t word = le32(data, code + 4 * w);
        if ((word >> 16) != kOpcodeFlag)
            return Error::fail(text::format("word %zu of the script's code isn't an instruction", w));
        Instruction ins;
        ins.opcode = uint16_t(word);
        ins.address = w;
        const int count = operandCount(ins.opcode);
        if (count < 0)
            return Error::fail(text::format("opcode %u at word %zu isn't known", ins.opcode, w));
        if (w + 1 + size_t(count) > words)
            return Error::fail("the script's last instruction is cut short");
        for (int i = 0; i < count; ++i) {
            const uint32_t op = le32(data, code + 4 * (w + 1 + size_t(i)));
            const uint32_t flags = op >> 16;
            Operand o;
            o.value = int(uint16_t(op));
            if (flags == 0)
                o.value = int16_t(uint16_t(op));
            else if (flags == kStringFlag)
                o.kind = OperandKind::kString;
            else if (flags == kAddressFlag)
                o.kind = OperandKind::kAddress;
            else if (flags == kVariableFlag)
                o.kind = OperandKind::kVariable;
            else
                return Error::fail(text::format("an operand at word %zu has flags %04x", w + 1 + size_t(i), flags));
            ins.operands.push_back(o);
        }
        w += 1 + size_t(count);
        _instructions.push_back(std::move(ins));
    }
    // Then entries of a length and that many bytes: the strings, back to
    // back, then each variable's name.
    size_t at = code + 4 * words;
    for (size_t entry = 0; entry < 1 + variables; ++entry) {
        if (at + 4 > data.size() || le32(data, at) > data.size() - at - 4)
            return Error::fail("the script's strings run past the end");
        const size_t length = le32(data, at);
        const auto first = data.begin() + long(at + 4);
        if (entry == 0) {
            _strings.assign(first, first + long(length));
        } else {
            std::string name(first, first + long(length));
            name.resize(std::strlen(name.c_str()));
            _variables.push_back(std::move(name));
        }
        at += 4 + length;
    }
    return Error::success();
}

std::string Script::string(int offset) const {
    if (offset < 0 || size_t(offset) >= _strings.size())
        return {};
    const char *start = reinterpret_cast<const char *>(_strings.data()) + offset;
    return std::string(start, strnlen(start, _strings.size() - size_t(offset)));
}

std::string Script::disassemble() const {
    std::string out;
    if (_stackSize)
        out += text::format("#setstack %u\n", _stackSize);
    if (_limboSize)
        out += text::format("#setlimbo %u\n", _limboSize);
    if (_bounceSize)
        out += text::format("#setbounce %u\n", _bounceSize);
    if (_walkSize)
        out += text::format("#setwalk %u\n", _walkSize);
    if (_timeSlice != 50)
        out += text::format("; time slice %u\n", _timeSlice);
    for (size_t i = 0; i < _variables.size(); ++i)
        out += text::format("variable %s\t\t; %zu\n", _variables[i].c_str(), i);
    if (!out.empty())
        out += "\n";
    std::set<size_t> targets;
    for (const Instruction &ins : _instructions)
        for (const Operand &o : ins.operands)
            if (o.kind == OperandKind::kAddress)
                targets.insert(size_t(o.value));
    for (const Instruction &ins : _instructions) {
        std::string line = targets.count(ins.address) ? text::format(".a%zu", ins.address) : std::string();
        line += "\t";
        line += opcodeName(ins.opcode);
        for (const Operand &o : ins.operands) {
            line += "\t";
            switch (o.kind) {
            case OperandKind::kNumber:
                line += std::to_string(o.value);
                break;
            case OperandKind::kString:
                line += "\"" + string(o.value) + "\"";
                break;
            case OperandKind::kAddress:
                line += text::format("a%d", o.value);
                break;
            case OperandKind::kVariable:
                line +=
                    size_t(o.value) < _variables.size() ? _variables[size_t(o.value)] : text::format("var%d", o.value);
                break;
            }
        }
        out += line + "\n";
    }
    return out;
}

} // namespace reluva::tpw
