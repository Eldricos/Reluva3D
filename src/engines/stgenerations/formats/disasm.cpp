// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/disasm.h"

#include "engines/stgenerations/formats/opcodes.h"
#include "engines/stgenerations/game/vm.h"

namespace reluva::stgenerations {

std::string disassembleInstruction(const std::vector<uint16_t>& code, uint32_t ip) {
    auto word = [&](uint32_t i) -> uint16_t { return i < code.size() ? code[i] : 0; };
    uint16_t op = word(ip);
    const OpcodeInfo* info = getOpcodeInfo(op);
    if (!info)
        return text::format("??? %04x", op);

    std::string text = info->name;
    switch (op) {
    case OP_PUSH:
        text += text::format(" %d", (int16_t)word(ip + 1));
        break;
    case OP_PUSH32:
        text += text::format(" 0x%04x%04x", word(ip + 2), word(ip + 1));
        break;
    case OP_PUSH_BLOCK:
        for (int i = 1; i < 8; i++)
            text += text::format(" %04x", word(ip + i));
        break;
    default:
        break;
    }
    return text;
}

std::string disassembleScript(const ScriptFile& script) {
    const std::vector<uint16_t>& code = script.getCode();
    std::string out;
    const std::vector<ScriptObject>& objects = script.getObjects();

    for (size_t o = 0; o < objects.size(); o++) {
        const ScriptObject& object = objects[o];
        out += text::format("object %u: type %u, id 0x%04x\n", (unsigned)o, object.type, object.id);
        for (const ScriptState& state : object.states) {
            out += text::format("  state %u\n", state.id);
            for (const ScriptBlock& block : state.blocks) {
                const char* eventName = block.event == kEventEnterState ? " (enter)"
                                        : block.event == kEventLeaveState ? " (leave)" : "";
                out += text::format("    on event %u%s", block.event, eventName);
                if (block.nextState != -1)
                    out += text::format(", then state %d", block.nextState);
                out += "\n";

                int depth = 3;
                for (uint32_t ip = block.codeStart; ip < code.size() && code[ip] != OP_END;
                     ip += getInstructionLength(code[ip])) {
                    uint16_t op = code[ip];
                    if (op == OP_ENDIF || op == OP_ELSE)
                        depth--;
                    const OpcodeInfo* info = getOpcodeInfo(op);
                    std::string line = std::string(depth * 2, ' ') + disassembleInstruction(code, ip);
                    if (info && op >= OP_FIRST_NATIVE && info->note && *info->note)
                        line += std::string(std::max<int>(1, 40 - (int)line.size()), ' ') + "; " + info->note;
                    out += text::format("%5u %s\n", ip, line.c_str());
                    if ((op >= OP_IF_EQ && op <= OP_IF_LE) || op == OP_ELSE)
                        depth++;
                }
            }
        }
    }
    return out;
}

} // namespace reluva::stgenerations
