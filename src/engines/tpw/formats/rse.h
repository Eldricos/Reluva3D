// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A compiled ride or shop script (`.RSE`, "RSSEQ"), as SimCoaster's
 * `Data/levels/arabian/rides/wateride/Wateride.RSE`: code for the game's
 * script machine, its strings and its variables' names. See
 * docs/engines/tpw/games/simcoaster/formats/rse.md.
 */
class Script {
public:
    /** What an operand is, by its flags. */
    enum class OperandKind : uint8_t {
        kNumber,   // a signed 16-bit value
        kString,   // a byte offset into the script's strings
        kAddress,  // a word of the code, for branches
        kVariable, // a variable's number
    };

    /** An instruction's operand. */
    struct Operand {
        OperandKind kind = OperandKind::kNumber;
        int value = 0;
    };

    /** An instruction: its opcode and operands, and where it starts in the code, in words. */
    struct Instruction {
        uint16_t opcode = 0;
        size_t address = 0;
        std::vector<Operand> operands;
    };

    /**
     * Reads the script. Fails without "RSSEQ", on code or strings past the
     * end, or on an opcode whose operands aren't known.
     */
    Error parse(const std::vector<uint8_t> &data);

    /** The header's sizes, as the source's directives set them (`#setstack`...). */
    ///@{
    uint32_t stackSize() const { return _stackSize; }
    uint32_t timeSlice() const { return _timeSlice; } // 50 in all of SimCoaster's
    uint32_t limboSize() const { return _limboSize; }
    uint32_t bounceSize() const { return _bounceSize; }
    uint32_t walkSize() const { return _walkSize; }
    ///@}
    const std::vector<Instruction> &instructions() const { return _instructions; }
    /** The string at a byte offset of the script's strings; empty past their end. */
    std::string string(int offset) const;
    /** The variables' names, by number. */
    const std::vector<std::string> &variables() const { return _variables; }

    /** An opcode's name in the script sources' language ("WAITANIM"), or nullptr for one not known. */
    static const char *opcodeName(uint16_t opcode);
    /** How many operands an opcode takes; -1 for one not known. */
    static int operandCount(uint16_t opcode);

    /**
     * The script as source, in the language of the sources that ship with
     * the game (the `.rss` files in `Data/TestScript/`): its directives, its
     * variables, then its code, a label at each branch's target.
     */
    std::string disassemble() const;

private:
    uint32_t _stackSize = 0;
    uint32_t _timeSlice = 0;
    uint32_t _limboSize = 0;
    uint32_t _bounceSize = 0;
    uint32_t _walkSize = 0;
    std::vector<Instruction> _instructions;
    std::vector<uint8_t> _strings;
    std::vector<std::string> _variables;
};

} // namespace reluva::tpw
