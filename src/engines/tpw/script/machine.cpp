// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What each instruction does is worked out from the sources that ship
// with the game and from how the 288 scripts use them:
// docs/engines/tpw/games/simcoaster/systems/scripts.md.

#include "engines/tpw/script/machine.h"

#include "common/text.h"

namespace reluva::tpw {

namespace {

// The opcodes the machine runs itself.
enum Opcode : uint16_t {
    kNop = 0,
    kCritLock = 1,
    kCritUnlock = 2,
    kCopy = 3,
    kSub = 5,
    kEndSlice = 6,
    kGetTime = 7,
    kRand = 28,
    kJsr = 29,
    kReturn = 30,
    kBranch = 31,
    kBranchZero = 32,
    kBranchNotZero = 33,
    kBranchNegative = 34,
    kBranchPositive = 35,
    kName = 37,
    kTest = 38,
    kCmp = 39,
    kPush = 40,
    kPop = 41,
    kWait = 44,
    kWaitAbs = 45,
    kAdd = 47,
    kMult = 48,
    kDiv = 49,
    kMod = 50,
    kEnd = 52,
    kSpawnChild = 63,
    kSpawnSound = 64,
    kRemoveChild = 65,
    kSetVarInChild = 66,
    kGetVarInChild = 67,
    kSetVarInParent = 68,
    kGetVarInParent = 69,
};

// The game's instructions whose answer sets the flags: each is followed
// straight by a conditional branch somewhere in the scripts (TOUR 10,
// "check if car in station", WALKGET, INLIMBO...).
bool answerSetsFlags(uint16_t opcode) {
    switch (opcode) {
    case 27: // GETANIM_CH
    case 53: // TOUR
    case 54: // BUMP
    case 55: // COAST
    case 59: // UNLIMBO
    case 60: // FORCEUNLIMBO
    case 61: // INLIMBO
    case 62: // LIMBOSPACE
    case 75: // BOUNCING
    case 78: // WALKGET
    case 96: // GETTIMER
        return true;
    default:
        return false;
    }
}

constexpr size_t kNoInstruction = SIZE_MAX;

} // namespace

ScriptMachine::ScriptMachine(const Script &script, ScriptHost &host, ScriptMachine *parent)
    : _script(script), _host(host), _parent(parent) {
    _variables.assign(script.variables().size(), 0);
    size_t words = 0;
    for (const Script::Instruction &ins : script.instructions())
        words = ins.address + 1 + ins.operands.size();
    _byAddress.assign(words, kNoInstruction);
    for (size_t i = 0; i < script.instructions().size(); ++i)
        _byAddress[script.instructions()[i].address] = i;
    _start = host.now();
}

ScriptMachine::~ScriptMachine() = default;

size_t ScriptMachine::address() const {
    return _next < _script.instructions().size() ? _script.instructions()[_next].address : _byAddress.size();
}

void ScriptMachine::setVariable(size_t index, int value) {
    if (index < _variables.size())
        _variables[index] = value;
}

void ScriptMachine::fail(const std::string &why) {
    _state = State::kFailed;
    _error = text::format("at word %zu: %s", address(), why.c_str());
}

int ScriptMachine::value(const Script::Operand &o) const {
    return o.kind == Script::OperandKind::kVariable ? variable(size_t(o.value)) : o.value;
}

void ScriptMachine::store(const Script::Operand &o, int v) {
    // A result given a number for its place is dropped: "MOD 0 VAR_CAPACITY
    // 9" keeps only the flags.
    if (o.kind == Script::OperandKind::kVariable)
        setVariable(size_t(o.value), v);
}

void ScriptMachine::setFlags(int v) {
    _zero = v == 0;
    _negative = v < 0;
}

void ScriptMachine::jump(int address) {
    if (address < 0 || size_t(address) >= _byAddress.size() || _byAddress[size_t(address)] == kNoInstruction) {
        fail(text::format("a branch to word %d, where no instruction starts", address));
        return;
    }
    _next = _byAddress[size_t(address)];
}

void ScriptMachine::tick() {
    if (_state == State::kWaiting && int32_t(_host.now() - _wakeTime) >= 0)
        _state = State::kRunning;
    // The time slice counts instructions (inferred: 50 in every script),
    // inside a critical section too: the shops spin in one until the game
    // takes a guest off, which it couldn't if the section kept its turn.
    const int slice = _script.timeSlice() ? int(_script.timeSlice()) : 1;
    int ran = 0;
    _yield = false;
    while (_state == State::kRunning && !_yield) {
        if (ran >= slice)
            break;
        ++ran;
        step();
    }
    if (_child)
        _child->tick();
}

void ScriptMachine::step() {
    const auto &code = _script.instructions();
    if (_next >= code.size()) {
        _state = State::kEnded;
        return;
    }
    const Script::Instruction &ins = code[_next++];
    const auto &ops = ins.operands;
    auto v = [&](size_t i) { return value(ops[i]); };
    switch (ins.opcode) {
    case kNop:
        break;
    case kCritLock:
        _locked = true;
        break;
    case kCritUnlock:
        _locked = false;
        break;
    case kCopy:
        store(ops[0], v(1));
        break;
    case kSub: {
        // SUB result a b: result = a - b. The bus's "SUB VAR_TEMP VAR_TEMP
        // 3000" has a number last, so the result goes first.
        const int r = v(1) - v(2);
        store(ops[0], r);
        setFlags(r);
        break;
    }
    case kEndSlice:
        _yield = true;
        break;
    case kGetTime:
        store(ops[0], int(_host.now() - _start));
        break;
    case kRand:
        store(ops[0], _host.random(v(1)));
        break;
    case kJsr:
        _returns.push_back(_next);
        jump(ops[0].value);
        break;
    case kReturn:
        if (_returns.empty()) {
            fail("RETURN without a JSR");
            break;
        }
        _next = _returns.back();
        _returns.pop_back();
        break;
    case kBranch:
        jump(ops[0].value);
        break;
    case kBranchZero:
        if (_zero)
            jump(ops[0].value);
        break;
    case kBranchNotZero:
        if (!_zero)
            jump(ops[0].value);
        break;
    case kBranchNegative:
        if (_negative)
            jump(ops[0].value);
        break;
    case kBranchPositive:
        // Above zero, by the countdown in test.rss (inferred: or not below).
        if (!_negative && !_zero)
            jump(ops[0].value);
        break;
    case kName:
        _name = _script.string(ops[0].value);
        break;
    case kTest:
        setFlags(v(0));
        break;
    case kCmp:
        // Bitwise AND, OpenTPW's page says (inferred).
        setFlags(v(0) & v(1));
        break;
    case kPush:
        if (_stack.size() >= _script.stackSize()) {
            fail("the stack is full");
            break;
        }
        _stack.push_back(v(0));
        break;
    case kPop:
        if (_stack.empty()) {
            fail("POP from an empty stack");
            break;
        }
        _stack.pop_back();
        break;
    case kWait:
        _wakeTime = _host.now() + uint32_t(v(0));
        _state = State::kWaiting;
        _yield = true;
        break;
    case kWaitAbs:
        // Until a time from the script's start (guessed from the name).
        _wakeTime = _start + uint32_t(v(0));
        if (int32_t(_host.now() - _wakeTime) < 0)
            _state = State::kWaiting;
        _yield = true;
        break;
    case kAdd: {
        const int r = v(0) + v(1);
        store(ops[0], r);
        setFlags(r);
        break;
    }
    case kMult:
    case kDiv:
    case kMod: {
        // As SUB: "DIV VAR_TEMP VAR_CAPACITY 9", "MOD 0 VAR_CAPACITY 9".
        const int a = v(1);
        const int b = v(2);
        if (ins.opcode != kMult && b == 0) {
            fail("a division by 0");
            break;
        }
        const int r = ins.opcode == kMult ? a * b : ins.opcode == kDiv ? a / b : a % b;
        store(ops[0], r);
        setFlags(r);
        break;
    }
    case kEnd:
        _state = State::kEnded;
        break;
    case kSpawnChild:
    case kSpawnSound: {
        _child.reset();
        auto script = std::make_unique<Script>();
        if (Error err = _host.loadScript(*this, _script.string(ops[0].value), *script)) {
            fail(err.message);
            break;
        }
        _childScript = std::move(script);
        _child = std::make_unique<ScriptMachine>(*_childScript, _host, this);
        break;
    }
    case kRemoveChild:
        _child.reset();
        _childScript.reset();
        break;
    case kSetVarInChild:
        if (_child)
            _child->setVariable(size_t(v(0)), v(1));
        break;
    case kGetVarInChild:
        store(ops[0], _child ? _child->variable(size_t(v(1))) : 0);
        break;
    case kSetVarInParent:
        if (_parent)
            _parent->setVariable(size_t(v(0)), v(1));
        break;
    case kGetVarInParent:
        store(ops[0], _parent ? _parent->variable(size_t(v(1))) : 0);
        break;
    default: {
        std::vector<int> values;
        for (const Script::Operand &o : ops)
            values.push_back(value(o));
        const HostResult r = _host.perform(*this, ins, values);
        if (answerSetsFlags(ins.opcode))
            setFlags(r.value);
        if (r.waitMs) {
            _wakeTime = _host.now() + r.waitMs;
            _state = State::kWaiting;
            _yield = true;
        }
        break;
    }
    }
}

} // namespace reluva::tpw
