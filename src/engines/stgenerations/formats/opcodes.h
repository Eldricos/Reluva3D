// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

namespace reluva::stgenerations {

enum Opcode {
    kOpEnd = 0x00,
    kOpNop = 0x01,
    kOpPush = 0x02,
    kOpPop = 0x03,
    kOpPushSelf = 0x04,
    kOpPushBlock = 0x05,
    kOpPush32 = 0x06,
    kOpSetResult = 0x07,
    kOpGotoState = 0x08,
    kOpSendSignal = 0x09,
    kOpCase = 0x0A,
    kOpIfEq = 0x0B,
    kOpIfNe = 0x0C,
    kOpIfGt = 0x0D,
    kOpIfGe = 0x0E,
    kOpIfLt = 0x0F,
    kOpIfLe = 0x10,
    kOpEndif = 0x11,
    kOpElse = 0x12,
    kOpReturn = 0x13,
    kOpAllocLocals = 0x14,
    kOpGameInit = 0x15,
    kOpGetLocal = 0x16,
    kOpGetGlobal = 0x17,
    kOpSetLocal = 0x18,
    kOpSetGlobal = 0x19,
    kOpInc = 0x1A,
    kOpDec = 0x1B,
    kOpAdd = 0x1C,
    kOpSub = 0x1D,
    kOpMul = 0x1E,
    kOpDiv = 0x1F,
    kOpSignDiff = 0x20,
    kOpAnd = 0x21,
    kOpOr = 0x22,
    kOpXor = 0x23,
    kOpNot = 0x24,
    kOpHighBits = 0x25,
    kOpLowBits = 0x26,

    // The first opcode that calls into the game rather than the VM.
    kOpFirstNative = 0x27,

    kOpcodeCount = 0xF5
};

struct OpcodeInfo {
    const char *name;
    // Values popped and pushed, in 16-bit stack slots.
    uint8_t pops;
    uint8_t pushes;
    // Length in words, including the opcode.
    uint8_t length;
    const char *note;
};

// Information about an opcode, or nullptr if the interpreter has no case for it.
const OpcodeInfo *getOpcodeInfo(uint16_t op);

// Length of an instruction in words, as the original's scanner computes it.
inline int getInstructionLength(uint16_t op) {
    return op == kOpPush ? 2 : op == kOpPushBlock ? 8 : op == kOpPush32 ? 3 : 1;
}

} // namespace reluva::stgenerations
