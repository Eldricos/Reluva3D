// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// A Quake III virtual machine program (.qvm): game code compiled to
// bytecode, run by an interpreter, which reaches the engine only through
// numbered system calls. See docs/engines/idtech3/formats/qvm.md.
class Qvm {
public:
    // The engine's side: system call `number` with the VM's argument words
    // (args[0] is the first argument; there are at least 16). It returns
    // the call's result. A handler may read and write the VM's memory and
    // call back into it.
    using SystemCall = std::function<int32_t(Qvm &vm, int number, const int32_t *args)>;

    static constexpr int kMainArgs = 13; // vmMain's command and twelve arguments

    Error load(const std::vector<uint8_t> &file);
    void setSystemCall(SystemCall handler) { _syscall = std::move(handler); }
    // Stops a call (with an error) after this many instructions; 0 = never.
    void setInstructionLimit(uint64_t limit) { _limit = limit; }

    // Calls vmMain(command, args...); fails on any fault in the program
    // (bad address, bad jump, stack overflow, division by zero...), naming
    // the instruction.
    Error call(int32_t command, const std::vector<int32_t> &args, int32_t &result);
    // A function of the program other than vmMain, by its first
    // instruction (for diagnosis: reluva-tool's -vmcall).
    Error callAt(int32_t instruction, const std::vector<int32_t> &args, int32_t &result);

    // From a system call: ends the run once the call returns, with this
    // error from call() (the game code's own fatal errors, or memory a
    // call can't touch). A nested call() made since also fails.
    void abort(std::string message) {
        if (_abort.empty())
            _abort = std::move(message);
    }

    // Whether a system call may write [address, address + size): inside
    // memory and clear of the code's jump tables (see protectedRanges()).
    // If not, the run is aborted, naming the system call, and false
    // returned. writeInt and writeString check this themselves.
    bool checkWrite(int64_t address, int64_t size);
    // The tables `switch` statements jump through (instruction numbers in
    // the data), found when loading: the program never changes them, so a
    // write there is a fault, caught where it happens rather than when a
    // switch later jumps somewhere absurd. [first, end) byte ranges.
    const std::vector<std::pair<uint32_t, uint32_t>> &protectedRanges() const { return _protected; }

    // The VM's memory, for system calls: addresses are offsets into it.
    std::vector<uint8_t> &memory() { return _data; }
    bool valid(int64_t address, int64_t size) const {
        return address >= 0 && size >= 0 && address + size <= int64_t(_data.size());
    }
    // A NUL-terminated string at `address` (cut at the end of memory or at
    // `max` bytes); empty if the address is outside memory.
    std::string string(int32_t address, size_t max = 4096) const;
    int32_t readInt(int32_t address) const;
    void writeInt(int32_t address, int32_t value);
    // Copies `text` into a buffer of `size` bytes at `address`, cut to fit
    // and NUL-terminated; false (and nothing written) if the buffer isn't
    // inside memory or `size` is 0.
    bool writeString(int32_t address, std::string_view text, int32_t size);
    // A float passed in an argument word.
    static float asFloat(int32_t word);
    static int32_t fromFloat(float value);

    size_t instructionCount() const { return _code.size(); }
    uint64_t executed() const { return _executed; } // by the last call(), including nested ones

    struct Instruction {
        uint8_t op;
        int32_t operand;
    };
    const std::vector<Instruction> &code() const { return _code; }
    // The initialised words end at dataLength(); the literal block
    // (strings) runs from litStart() to litEnd(); zeros follow.
    uint32_t dataLength() const { return _dataLength; }
    uint32_t litStart() const { return _litStart; }
    uint32_t litEnd() const { return _litEnd; }

private:
    Error run(int32_t &result, int32_t entry = 0);

    std::vector<Instruction> _code;
    std::vector<uint8_t> _data; // a power of two in size; the stack is at the top
    uint32_t _dataLength = 0, _litStart = 0, _litEnd = 0;
    int32_t _programStack = 0; // the stack pointer between calls
    int32_t _stackBottom = 0;  // the stack may not grow below this
    SystemCall _syscall;
    uint64_t _limit = 0, _executed = 0;
    int _depth = 0; // nested call() through system calls
    std::string _abort;
    std::vector<std::pair<uint32_t, uint32_t>> _protected; // sorted, not overlapping
    uint32_t _protectedEnd = 0;                            // nothing protected from here up
    int _currentSyscall = -1;                              // the system call running, for messages

    void findJumpTables();
    // The protected range overlapping [address, address + size), or nullptr.
    const std::pair<uint32_t, uint32_t> *protectedOverlap(int64_t address, int64_t size) const;
};

// The opcodes (docs/engines/idtech3/formats/qvm.md), in their numbering.
namespace qvmop {
enum : uint8_t {
    kUndef,
    kIgnore,
    kBreak,
    kEnter,
    kLeave,
    kCall,
    kPush,
    kPop,
    kConst,
    kLocal,
    kJump,
    kEq,
    kNe,
    kLti,
    kLei,
    kGti,
    kGei,
    kLtu,
    kLeu,
    kGtu,
    kGeu,
    kEqf,
    kNef,
    kLtf,
    kLef,
    kGtf,
    kGef,
    kLoad1,
    kLoad2,
    kLoad4,
    kStore1,
    kStore2,
    kStore4,
    kArg,
    kBlockCopy,
    kSex8,
    kSex16,
    kNegi,
    kAdd,
    kSub,
    kDivi,
    kDivu,
    kModi,
    kModu,
    kMuli,
    kMulu,
    kBand,
    kBor,
    kBxor,
    kBcom,
    kLsh,
    kRshi,
    kRshu,
    kNegf,
    kAddf,
    kSubf,
    kDivf,
    kMulf,
    kCvif,
    kCvfi,
    kCount
};
// Bytes of operand after each opcode: 4, 1 (Arg) or 0.
int operandSize(uint8_t op);
const char *name(uint8_t op);
} // namespace qvmop

} // namespace reluva::idtech3
