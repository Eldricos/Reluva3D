// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/text.h"
#include "engines/stgenerations/formats/script.h"

namespace reluva::stgenerations {

/**
 * The instruction at `ip` (a word index into `code`) as text, e.g. "PUSH 32"
 * or "NATIVE_E2"; "??? 00f5" for an opcode the interpreter has no case for.
 */
std::string disassembleInstruction(const std::vector<uint16_t> &code, uint32_t ip);

/**
 * The whole program as an indented listing: objects, states, event
 * blocks and their instructions, with if/else nesting shown.
 */
std::string disassembleScript(const ScriptFile &script);

} // namespace reluva::stgenerations
