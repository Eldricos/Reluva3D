// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

namespace reluva::stgenerations {

enum Opcode {
    OP_END = 0x00,
    OP_NOP = 0x01,
    OP_PUSH = 0x02,
    OP_POP = 0x03,
    OP_PUSH_SELF = 0x04,
    OP_PUSH_BLOCK = 0x05,
    OP_PUSH32 = 0x06,
    OP_SET_RESULT = 0x07,
    OP_GOTO_STATE = 0x08,
    OP_SEND_SIGNAL = 0x09,
    OP_CASE = 0x0A,
    OP_IF_EQ = 0x0B,
    OP_IF_NE = 0x0C,
    OP_IF_GT = 0x0D,
    OP_IF_GE = 0x0E,
    OP_IF_LT = 0x0F,
    OP_IF_LE = 0x10,
    OP_ENDIF = 0x11,
    OP_ELSE = 0x12,
    OP_RETURN = 0x13,
    OP_ALLOC_LOCALS = 0x14,
    OP_GAME_INIT = 0x15,
    OP_GET_LOCAL = 0x16,
    OP_GET_GLOBAL = 0x17,
    OP_SET_LOCAL = 0x18,
    OP_SET_GLOBAL = 0x19,
    OP_INC = 0x1A,
    OP_DEC = 0x1B,
    OP_ADD = 0x1C,
    OP_SUB = 0x1D,
    OP_MUL = 0x1E,
    OP_DIV = 0x1F,
    OP_SIGN_DIFF = 0x20,
    OP_AND = 0x21,
    OP_OR = 0x22,
    OP_XOR = 0x23,
    OP_NOT = 0x24,
    OP_HIGH_BITS = 0x25,
    OP_LOW_BITS = 0x26,

    // The first opcode that calls into the game rather than the VM.
    OP_FIRST_NATIVE = 0x27,

    kOpcodeCount = 0xF5
};

struct OpcodeInfo {
    const char* name;
    // Values popped and pushed, in 16-bit stack slots.
    uint8_t pops;
    uint8_t pushes;
    // Length in words, including the opcode.
    uint8_t length;
    const char* note;
};

// Information about an opcode, or nullptr if the interpreter has no case for it.
const OpcodeInfo* getOpcodeInfo(uint16_t op);

// Length of an instruction in words, as the original's scanner computes it.
inline int getInstructionLength(uint16_t op) {
    return op == OP_PUSH ? 2 : op == OP_PUSH_BLOCK ? 8 : op == OP_PUSH32 ? 3 : 1;
}

} // namespace reluva::stgenerations

