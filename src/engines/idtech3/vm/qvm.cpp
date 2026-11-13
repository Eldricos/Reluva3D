// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The QVM format and how the original runs it (the call and stack
// conventions, each opcode's effect) were read from the GPL Quake III Arena
// source (qcommon/vm.c, vm_interpreted.c, vm_local.h, qfiles.h) and are
// described in docs/engines/idtech3/formats/qvm.md. This is new code written from
// that description.

#include "engines/idtech3/vm/qvm.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr int32_t kMagic = 0x12721444;
constexpr int32_t kMagic2 = 0x12721445;
constexpr int32_t kStackSize = 0x10000; // the program stack, at the top of the data
constexpr int kOpStackSize = 1024;
constexpr int kSyscallArgs = 16;
constexpr int kMaxDepth = 64; // nested call()s through system calls

int32_t le32(const uint8_t *p) {
    return static_cast<int32_t>(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
}

} // namespace

int qvmop::operandSize(uint8_t op) {
    switch (op) {
    case kEnter:
    case kLeave:
    case kConst:
    case kLocal:
    case kBlockCopy:
    case kEq:
    case kNe:
    case kLti:
    case kLei:
    case kGti:
    case kGei:
    case kLtu:
    case kLeu:
    case kGtu:
    case kGeu:
    case kEqf:
    case kNef:
    case kLtf:
    case kLef:
    case kGtf:
    case kGef:
        return 4;
    case kArg:
        return 1;
    default:
        return 0;
    }
}

const char *qvmop::name(uint8_t op) {
    static const char *const kNames[] = {
        "UNDEF",  "IGNORE", "BREAK",  "ENTER", "LEAVE",      "CALL", "PUSH",  "POP",   "CONST", "LOCAL",
        "JUMP",   "EQ",     "NE",     "LTI",   "LEI",        "GTI",  "GEI",   "LTU",   "LEU",   "GTU",
        "GEU",    "EQF",    "NEF",    "LTF",   "LEF",        "GTF",  "GEF",   "LOAD1", "LOAD2", "LOAD4",
        "STORE1", "STORE2", "STORE4", "ARG",   "BLOCK_COPY", "SEX8", "SEX16", "NEGI",  "ADD",   "SUB",
        "DIVI",   "DIVU",   "MODI",   "MODU",  "MULI",       "MULU", "BAND",  "BOR",   "BXOR",  "BCOM",
        "LSH",    "RSHI",   "RSHU",   "NEGF",  "ADDF",       "SUBF", "DIVF",  "MULF",  "CVIF",  "CVFI",
    };
    return op < kCount ? kNames[op] : "?";
}

float Qvm::asFloat(int32_t word) {
    return std::bit_cast<float>(word);
}
int32_t Qvm::fromFloat(float value) {
    return std::bit_cast<int32_t>(value);
}

std::string Qvm::string(int32_t address, size_t max) const {
    std::string s;
    if (!valid(address, 0))
        return s;
    for (size_t i = size_t(address); i < _data.size() && s.size() < max && _data[i]; ++i)
        s += char(_data[i]);
    return s;
}

int32_t Qvm::readInt(int32_t address) const {
    if (!valid(address, 4))
        return 0;
    int32_t v;
    std::memcpy(&v, &_data[size_t(address)], 4);
    return v;
}

void Qvm::writeInt(int32_t address, int32_t value) {
    if (valid(address, 4) && (uint32_t(address) >= _protectedEnd || checkWrite(address, 4)))
        std::memcpy(&_data[size_t(address)], &value, 4);
}

const std::pair<uint32_t, uint32_t> *Qvm::protectedOverlap(int64_t address, int64_t size) const {
    if (address >= int64_t(_protectedEnd) || size <= 0)
        return nullptr;
    // The first range ending after address, if it starts before the write ends.
    auto it = std::upper_bound(_protected.begin(), _protected.end(), uint32_t(std::max<int64_t>(address, 0)),
                               [](uint32_t a, const std::pair<uint32_t, uint32_t> &r) { return a < r.second; });
    return it != _protected.end() && int64_t(it->first) < address + size ? &*it : nullptr;
}

bool Qvm::checkWrite(int64_t address, int64_t size) {
    const std::string who = _currentSyscall >= 0 ? "system call " + std::to_string(_currentSyscall) : "the engine";
    if (!valid(address, size)) {
        abort(who + " wrote outside the program's memory, at " + std::to_string(address));
        return false;
    }
    if (const auto *r = protectedOverlap(address, size)) {
        abort(who + " wrote " + std::to_string(size) + " bytes at " + std::to_string(address) +
              ", over the jump table at " + std::to_string(r->first) + "-" + std::to_string(r->second));
        return false;
    }
    return true;
}

void Qvm::findJumpTables() {
    // A switch compiles to: (index) CONST 2, LSH, CONST base, ADD, LOAD4,
    // JUMP, after range checks CONST lo, LTI and CONST hi, GTI on the
    // index. The compiler folds lo into the constant (base = table - 4 *
    // lo), so the table, instruction numbers for lo..hi, starts at base +
    // 4 * lo.
    using namespace qvmop;
    _protected.clear();
    const auto count = int32_t(_code.size());
    for (size_t i = 0; i + 3 < _code.size(); ++i) {
        if (_code[i].op != kConst || _code[i + 1].op != kAdd || _code[i + 2].op != kLoad4 || _code[i + 3].op != kJump)
            continue;
        int64_t lo = 0;
        int64_t hi = -1;
        bool haveLo = false;
        bool haveHi = false;
        for (size_t j = i > 16 ? i - 16 : 0; j < i; ++j) {
            if (_code[j].op == kConst && j + 1 < i && _code[j + 1].op == kLti) {
                lo = _code[j].operand;
                haveLo = true;
            }
            if (_code[j].op == kConst && j + 1 < i && _code[j + 1].op == kGti) {
                hi = _code[j].operand;
                haveHi = true;
            }
        }
        const int64_t table = int64_t(_code[i].operand) + 4 * lo;
        const int64_t entries = hi - lo + 1;
        // Anything unexpected: leave it unprotected rather than guess.
        if (!haveLo || !haveHi || entries <= 0 || entries > 4096 || table < 0 || table + 4 * entries > int64_t(_litEnd))
            continue;
        bool plausible = true;
        for (int64_t e = 0; e < entries && plausible; ++e) {
            const int32_t target = readInt(int32_t(table + 4 * e));
            plausible = target >= 0 && target < count;
        }
        if (plausible)
            _protected.emplace_back(uint32_t(table), uint32_t(table + 4 * entries));
    }
    std::sort(_protected.begin(), _protected.end());
    std::vector<std::pair<uint32_t, uint32_t>> merged;
    for (const auto &r : _protected) {
        if (!merged.empty() && r.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, r.second);
        else
            merged.push_back(r);
    }
    _protected = std::move(merged);
    _protectedEnd = _protected.empty() ? 0 : _protected.back().second;
}

bool Qvm::writeString(int32_t address, std::string_view text, int32_t size) {
    if (size <= 0 || !valid(address, size))
        return false;
    if (!checkWrite(address, int64_t(std::min(text.size() + 1, size_t(size)))))
        return false;
    const size_t n = std::min(text.size(), size_t(size) - 1);
    std::memcpy(&_data[size_t(address)], text.data(), n);
    _data[size_t(address) + n] = 0;
    return true;
}

Error Qvm::load(const std::vector<uint8_t> &file) {
    _code.clear();
    _data.clear();
    if (file.size() < 32)
        return Error::fail("Not a QVM (too small)");
    const int32_t magic = le32(&file[0]);
    if (magic != kMagic && magic != kMagic2)
        return Error::fail("Not a QVM (wrong magic number)");
    const int32_t count = le32(&file[4]);
    const int32_t codeOffset = le32(&file[8]);
    const int32_t codeLength = le32(&file[12]);
    const int32_t dataOffset = le32(&file[16]);
    const int32_t dataLength = le32(&file[20]);
    const int32_t litLength = le32(&file[24]);
    const int32_t bssLength = le32(&file[28]);
    auto inFile = [&](int64_t offset, int64_t size) {
        return offset >= 0 && size >= 0 && uint64_t(offset + size) <= file.size();
    };
    if (count <= 0 || !inFile(codeOffset, codeLength) || !inFile(dataOffset, int64_t(dataLength) + litLength) ||
        dataLength < 0 || litLength < 0 || bssLength < 0 || (dataLength & 3))
        return Error::fail("QVM header gives sizes outside the file");

    // Code: an opcode byte, then 4 or 1 operand bytes (little-endian).
    size_t at = size_t(codeOffset);
    const size_t end = at + size_t(codeLength);
    _code.reserve(size_t(count));
    for (int32_t i = 0; i < count; ++i) {
        if (at >= end)
            return Error::fail("QVM code ends after " + std::to_string(i) + " of " + std::to_string(count) +
                               " instructions");
        const uint8_t op = file[at++];
        if (op >= qvmop::kCount)
            return Error::fail("QVM instruction " + std::to_string(i) + " has unknown opcode " + std::to_string(op));
        int32_t operand = 0;
        const int size = qvmop::operandSize(op);
        if (at + size_t(size) > end)
            return Error::fail("QVM instruction " + std::to_string(i) + " runs past the code");
        if (size == 4)
            operand = le32(&file[at]);
        else if (size == 1)
            operand = file[at];
        at += size_t(size);
        _code.push_back({op, operand});
    }
    // Branch targets are instruction numbers.
    for (size_t i = 0; i < _code.size(); ++i) {
        const Instruction &in = _code[i];
        if (in.op >= qvmop::kEq && in.op <= qvmop::kGef && (in.operand < 0 || in.operand >= count))
            return Error::fail("QVM instruction " + std::to_string(i) + " branches to " + std::to_string(in.operand) +
                               " (of " + std::to_string(count) + ")");
    }

    // Data: initialised words, literals (bytes, e.g. strings), then zeros;
    // the whole rounded up to a power of two, the stack at the top.
    const uint64_t needed = uint64_t(dataLength) + uint64_t(litLength) + uint64_t(bssLength);
    uint64_t size = uint64_t(kStackSize) * 2; // room for the stack even if bss doesn't count it
    while (size < needed)
        size <<= 1;
    if (size > (uint64_t(1) << 30))
        return Error::fail("QVM data too large");
    _data.assign(size_t(size), 0);
    std::memcpy(_data.data(), &file[size_t(dataOffset)], size_t(dataLength) + size_t(litLength));
    // Words are little-endian in the file; the interpreter uses the host's order.
    if constexpr (std::endian::native == std::endian::big)
        for (size_t i = 0; i + 3 < size_t(dataLength); i += 4)
            std::swap(_data[i], _data[i + 3]), std::swap(_data[i + 1], _data[i + 2]);
    _dataLength = uint32_t(dataLength);
    _litStart = uint32_t(dataLength);
    _litEnd = uint32_t(dataLength + litLength);
    findJumpTables();
    _programStack = int32_t(size);
    _stackBottom = _programStack - kStackSize;
    return Error::success();
}

Error Qvm::call(int32_t command, const std::vector<int32_t> &args, int32_t &result) {
    std::vector<int32_t> all{command};
    all.insert(all.end(), args.begin(), args.end());
    return callAt(0, all, result);
}

Error Qvm::callAt(int32_t instruction, const std::vector<int32_t> &args, int32_t &result) {
    result = 0;
    if (_code.empty())
        return Error::fail("No QVM loaded");
    if (_depth >= kMaxDepth)
        return Error::fail("QVM calls nested too deeply");
    if (_depth == 0) {
        _executed = 0;
        _abort.clear();
    }
    if (!_abort.empty())
        return Error::fail(_abort);
    const int32_t saved = _programStack;
    // vmMain's arguments, with -1 as the return address that ends the run.
    int32_t sp = _programStack - (8 + 4 * kMainArgs);
    if (sp < _stackBottom)
        return Error::fail("QVM stack overflow on entry");
    for (int i = 0; i < kMainArgs; ++i)
        writeInt(sp + 8 + 4 * i, i < int(args.size()) ? args[size_t(i)] : 0);
    writeInt(sp + 4, 0);
    writeInt(sp, -1);
    _programStack = sp;
    if (instruction < 0 || instruction >= int32_t(_code.size()))
        return Error::fail("No instruction " + std::to_string(instruction));
    ++_depth;
    Error err = run(result, instruction);
    --_depth;
    _programStack = saved;
    return err;
}

Error Qvm::run(int32_t &result, int32_t entry) {
    using namespace qvmop;
    std::array<int32_t, kOpStackSize> stack{};
    int top = 0; // stack[top - 1] is the top; stack[0] is never used
    int32_t sp = _programStack;
    int32_t pc = entry;
    const int32_t count = int32_t(_code.size());
    auto fail = [&](int32_t at, const std::string &what) {
        return Error::fail("QVM instruction " + std::to_string(at) + " (" + name(_code[size_t(at)].op) + "): " + what);
    };

    while (true) {
        if (pc < 0 || pc >= count)
            return Error::fail("QVM jumped to " + std::to_string(pc) + " (of " + std::to_string(count) + ")");
        if (_limit && ++_executed > _limit)
            return fail(pc, "instruction limit reached");
        if (!_limit)
            ++_executed;
        const Instruction in = _code[size_t(pc)];
        const int32_t at = pc++;
        auto need = [&](int n) { return top >= n; };
        auto push = [&](int32_t v) {
            if (top + 1 >= kOpStackSize)
                return false;
            stack[size_t(++top)] = v;
            return true;
        };
        // Two operands: a below, b on top; both popped, the result pushed.
        auto binary = [&](auto f) -> bool {
            if (!need(2))
                return false;
            const int32_t b = stack[size_t(top--)];
            stack[size_t(top)] = f(stack[size_t(top)], b);
            return true;
        };
        auto fbinary = [&](auto f) -> bool {
            return binary([&](int32_t a, int32_t b) { return fromFloat(f(asFloat(a), asFloat(b))); });
        };
        auto branch = [&](auto f) -> bool {
            if (!need(2))
                return false;
            const int32_t b = stack[size_t(top--)];
            const int32_t a = stack[size_t(top--)];
            if (f(a, b))
                pc = in.operand;
            return true;
        };
        bool ok = true;
        switch (in.op) {
        case kUndef:
            return fail(at, "undefined instruction");
        case kBreak:
            return fail(at, "breakpoint");
        case kIgnore:
            break;
        case kEnter:
            sp -= in.operand;
            if (sp < _stackBottom || (sp & 3))
                return fail(at, "stack overflow");
            break;
        case kLeave: {
            sp += in.operand;
            if (!valid(sp, 4))
                return fail(at, "stack underflow");
            pc = readInt(sp);
            if (pc == -1) {
                result = top > 0 ? stack[size_t(top)] : 0;
                return Error::success();
            }
            break;
        }
        case kCall: {
            if (!need(1))
                return fail(at, "operand stack underflow");
            const int32_t target = stack[size_t(top--)];
            if (!valid(sp, 8))
                return fail(at, "bad stack");
            writeInt(sp, pc); // the return address
            if (target >= 0) {
                if (target >= count)
                    return fail(at, "calls " + std::to_string(target) + " (of " + std::to_string(count) + ")");
                pc = target;
                break;
            }
            // A system call: its arguments follow it on the stack.
            const int number = -1 - target;
            std::array<int32_t, kSyscallArgs> args{};
            for (int i = 0; i < kSyscallArgs; ++i)
                args[size_t(i)] = readInt(sp + 8 + 4 * i);
            writeInt(sp + 4, number);
            int32_t r = 0;
            if (_syscall) {
                const int32_t savedStack = _programStack;
                _programStack = sp - 4; // nested calls run below this frame
                const int savedSyscall = _currentSyscall;
                _currentSyscall = number;
                r = _syscall(*this, number, args.data());
                _currentSyscall = savedSyscall;
                _programStack = savedStack;
                if (!_abort.empty())
                    return Error::fail(_abort);
            }
            if (!push(r))
                return fail(at, "operand stack overflow");
            pc = readInt(sp);
            break;
        }
        case kPush:
            ok = push(0);
            break;
        case kPop:
            ok = need(1);
            if (ok)
                --top;
            break;
        case kConst:
            ok = push(in.operand);
            break;
        case kLocal:
            ok = push(sp + in.operand);
            break;
        case kJump: {
            if (!need(1))
                return fail(at, "operand stack underflow");
            const int32_t target = stack[size_t(top--)];
            if (target < 0 || target >= count)
                return fail(at, "jumps to " + std::to_string(target));
            pc = target;
            break;
        }
        case kEq:
            ok = branch([](int32_t a, int32_t b) { return a == b; });
            break;
        case kNe:
            ok = branch([](int32_t a, int32_t b) { return a != b; });
            break;
        case kLti:
            ok = branch([](int32_t a, int32_t b) { return a < b; });
            break;
        case kLei:
            ok = branch([](int32_t a, int32_t b) { return a <= b; });
            break;
        case kGti:
            ok = branch([](int32_t a, int32_t b) { return a > b; });
            break;
        case kGei:
            ok = branch([](int32_t a, int32_t b) { return a >= b; });
            break;
        case kLtu:
            ok = branch([](int32_t a, int32_t b) { return uint32_t(a) < uint32_t(b); });
            break;
        case kLeu:
            ok = branch([](int32_t a, int32_t b) { return uint32_t(a) <= uint32_t(b); });
            break;
        case kGtu:
            ok = branch([](int32_t a, int32_t b) { return uint32_t(a) > uint32_t(b); });
            break;
        case kGeu:
            ok = branch([](int32_t a, int32_t b) { return uint32_t(a) >= uint32_t(b); });
            break;
        case kEqf:
            ok = branch([](int32_t a, int32_t b) { return asFloat(a) == asFloat(b); });
            break;
        case kNef:
            ok = branch([](int32_t a, int32_t b) { return asFloat(a) != asFloat(b); });
            break;
        case kLtf:
            ok = branch([](int32_t a, int32_t b) { return asFloat(a) < asFloat(b); });
            break;
        case kLef:
            ok = branch([](int32_t a, int32_t b) { return asFloat(a) <= asFloat(b); });
            break;
        case kGtf:
            ok = branch([](int32_t a, int32_t b) { return asFloat(a) > asFloat(b); });
            break;
        case kGef:
            ok = branch([](int32_t a, int32_t b) { return asFloat(a) >= asFloat(b); });
            break;
        case kLoad1:
        case kLoad2:
        case kLoad4: {
            if (!need(1))
                return fail(at, "operand stack underflow");
            const int32_t address = stack[size_t(top)];
            const int size = in.op == kLoad1 ? 1 : in.op == kLoad2 ? 2 : 4;
            if (!valid(address, size))
                return fail(at, "reads outside memory at " + std::to_string(address));
            const uint8_t *p = &_data[size_t(address)];
            if (size == 1)
                stack[size_t(top)] = p[0];
            else if (size == 2) {
                uint16_t v;
                std::memcpy(&v, p, 2);
                stack[size_t(top)] = v;
            } else {
                std::memcpy(&stack[size_t(top)], p, 4);
            }
            break;
        }
        case kStore1:
        case kStore2:
        case kStore4: {
            if (!need(2))
                return fail(at, "operand stack underflow");
            const int32_t value = stack[size_t(top--)];
            const int32_t address = stack[size_t(top--)];
            const int size = in.op == kStore1 ? 1 : in.op == kStore2 ? 2 : 4;
            if (!valid(address, size))
                return fail(at, "writes outside memory at " + std::to_string(address));
            if (const auto *r = protectedOverlap(address, size))
                return fail(at, "writes at " + std::to_string(address) + ", over the jump table at " +
                                    std::to_string(r->first) + "-" + std::to_string(r->second));
            uint8_t *p = &_data[size_t(address)];
            if (size == 1)
                p[0] = uint8_t(value);
            else if (size == 2) {
                const uint16_t v = uint16_t(value);
                std::memcpy(p, &v, 2);
            } else {
                std::memcpy(p, &value, 4);
            }
            break;
        }
        case kArg: {
            if (!need(1))
                return fail(at, "operand stack underflow");
            const int32_t address = sp + in.operand;
            if (!valid(address, 4))
                return fail(at, "argument outside memory");
            writeInt(address, stack[size_t(top--)]);
            break;
        }
        case kBlockCopy: {
            if (!need(2))
                return fail(at, "operand stack underflow");
            const int32_t src = stack[size_t(top--)];
            const int32_t dest = stack[size_t(top--)];
            if (!valid(src, in.operand) || !valid(dest, in.operand))
                return fail(at, "copies outside memory");
            if (protectedOverlap(dest, in.operand))
                return fail(at, "copies over a jump table, at " + std::to_string(dest));
            std::memmove(&_data[size_t(dest)], &_data[size_t(src)], size_t(in.operand));
            break;
        }
        case kSex8:
            ok = need(1);
            if (ok)
                stack[size_t(top)] = int8_t(stack[size_t(top)]);
            break;
        case kSex16:
            ok = need(1);
            if (ok)
                stack[size_t(top)] = int16_t(stack[size_t(top)]);
            break;
        case kNegi:
            ok = need(1);
            if (ok)
                stack[size_t(top)] = int32_t(0u - uint32_t(stack[size_t(top)]));
            break;
        case kBcom:
            ok = need(1);
            if (ok)
                stack[size_t(top)] = ~stack[size_t(top)];
            break;
        case kAdd:
            ok = binary([](int32_t a, int32_t b) { return int32_t(uint32_t(a) + uint32_t(b)); });
            break;
        case kSub:
            ok = binary([](int32_t a, int32_t b) { return int32_t(uint32_t(a) - uint32_t(b)); });
            break;
        case kMuli:
        case kMulu:
            ok = binary([](int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); });
            break;
        case kDivi:
        case kDivu:
        case kModi:
        case kModu: {
            if (!need(2))
                return fail(at, "operand stack underflow");
            if (stack[size_t(top)] == 0)
                return fail(at, "division by zero");
            const bool div = in.op == kDivi || in.op == kDivu;
            if (in.op == kDivi || in.op == kModi)
                binary([div](int32_t a, int32_t b) {
                    if (a == INT32_MIN && b == -1) // overflows: wraps, as hardware does
                        return div ? a : 0;
                    return div ? a / b : a % b;
                });
            else
                binary([div](int32_t a, int32_t b) {
                    return int32_t(div ? uint32_t(a) / uint32_t(b) : uint32_t(a) % uint32_t(b));
                });
            break;
        }
        case kBand:
            ok = binary([](int32_t a, int32_t b) { return a & b; });
            break;
        case kBor:
            ok = binary([](int32_t a, int32_t b) { return a | b; });
            break;
        case kBxor:
            ok = binary([](int32_t a, int32_t b) { return a ^ b; });
            break;
        case kLsh:
            ok = binary([](int32_t a, int32_t b) { return int32_t(uint32_t(a) << (b & 31)); });
            break;
        case kRshi:
            ok = binary([](int32_t a, int32_t b) { return a >> (b & 31); });
            break;
        case kRshu:
            ok = binary([](int32_t a, int32_t b) { return int32_t(uint32_t(a) >> (b & 31)); });
            break;
        case kNegf:
            ok = need(1);
            if (ok)
                stack[size_t(top)] = fromFloat(-asFloat(stack[size_t(top)]));
            break;
        case kAddf:
            ok = fbinary([](float a, float b) { return a + b; });
            break;
        case kSubf:
            ok = fbinary([](float a, float b) { return a - b; });
            break;
        case kMulf:
            ok = fbinary([](float a, float b) { return a * b; });
            break;
        case kDivf:
            ok = fbinary([](float a, float b) { return a / b; });
            break;
        case kCvif:
            ok = need(1);
            if (ok)
                stack[size_t(top)] = fromFloat(float(stack[size_t(top)]));
            break;
        case kCvfi: {
            ok = need(1);
            if (ok) {
                // Towards zero; out-of-range values and NaN saturate instead
                // of being undefined.
                const float f = asFloat(stack[size_t(top)]);
                stack[size_t(top)] = std::isnan(f)         ? 0
                                     : f >= 2147483648.0f  ? INT32_MAX
                                     : f <= -2147483648.0f ? INT32_MIN
                                                           : int32_t(f);
            }
            break;
        }
        default:
            return fail(at, "unknown opcode");
        }
        if (!ok)
            return fail(at, "operand stack underflow or overflow");
    }
}

} // namespace reluva::idtech3
