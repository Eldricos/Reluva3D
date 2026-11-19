// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/vm.h"

#include <algorithm>

#include "engines/stgenerations/debug.h"
#include "common/serializer.h"
#include "engines/stgenerations/formats/opcodes.h"

namespace reluva::stgenerations {

namespace {

// Handler key, as built by the original's registration method (0x455F28).
uint16_t handlerKey(uint16_t event, int16_t state) {
    return (uint16_t)(((event & 0xFF) << 8) | ((uint16_t)state & 0xFF));
}

// Guards against runaway scripts; real blocks are a few hundred instructions at most.
const uint32_t kMaxInstructionsPerBlock = 100000;
// Guards against events that keep triggering state changes.
const int kMaxEventDepth = 64;

} // namespace

ScriptVM::ScriptVM(const ScriptFile &script, ScriptHost *host) : _script(script), _host(host) {
    reset();
}

void ScriptVM::reset() {
    _instances.clear();
    _instances.resize(_script.getObjects().size());
    for (size_t i = 0; i < _instances.size(); i++) {
        ScriptInstance &instance = _instances[i];
        instance.object = &_script.getObjects()[i];
        for (const ScriptState &state : instance.object->states) {
            for (const ScriptBlock &block : state.blocks)
                instance.handlers[handlerKey(block.event, state.id)] = &block;
        }
    }
    _globals.assign(kGlobalCount, 0);
    _stack.clear();
}

int ScriptVM::findInstance(uint16_t id) const {
    for (size_t i = 0; i < _instances.size(); i++) {
        if (_instances[i].object->id == id)
            return (int)i;
    }
    return -1;
}

void ScriptVM::sync(Serializer &s) {
    s.syncCount((uint32_t)_instances.size());
    for (ScriptInstance &instance : _instances) {
        s.syncAsSint16(instance.state);
        s.syncAsBool(instance.enabled);
        s.syncAsSint16(instance.pendingState);
        s.syncVector(instance.locals, [](Serializer &ser, int16_t &v) { ser.syncAsSint16(v); });
        int32_t parent = -1;
        if (instance.parent)
            parent = (int32_t)(instance.parent - _instances.data());
        s.syncAsSint32(parent);
        if (s.isLoading())
            instance.parent = parent >= 0 && parent < (int32_t)_instances.size() ? &_instances[parent] : nullptr;
    }
    // Globals: only the ones set.
    uint32_t count = 0;
    if (s.isSaving()) {
        for (int16_t v : _globals)
            count += v != 0;
    }
    s.syncAsUint32(count);
    if (s.isLoading())
        std::fill(_globals.begin(), _globals.end(), 0);
    int index = 0;
    for (uint32_t i = 0; i < count && !s.err(); i++) {
        if (s.isSaving()) {
            while (_globals[index] == 0)
                index++;
        }
        uint16_t at = (uint16_t)index;
        s.syncAsUint16(at);
        int16_t value = s.isSaving() ? _globals[index] : 0;
        s.syncAsSint16(value);
        if (s.isLoading() && at < _globals.size())
            _globals[at] = value;
        index++;
    }
}

int16_t ScriptVM::getGlobal(int index) const {
    return index >= 0 && index < kGlobalCount ? _globals[index] : 0;
}

void ScriptVM::setGlobal(int index, int16_t value) {
    if (index >= 0 && index < kGlobalCount)
        _globals[index] = value;
    else
        log::warning("Script: global %d out of range", index);
}

void ScriptVM::push(int16_t value) {
    if (_stack.size() >= kMaxStack) {
        log::warning("Script: stack overflow");
        return;
    }
    _stack.push_back(value);
}

int16_t ScriptVM::pop() {
    if (_stack.empty()) {
        log::warning("Script: stack underflow");
        return 0;
    }
    int16_t value = _stack.back();
    _stack.pop_back();
    return value;
}

uint16_t ScriptVM::word(uint32_t ip) const {
    const std::vector<uint16_t> &code = _script.getCode();
    return ip < code.size() ? code[ip] : (uint16_t)OP_END;
}

int16_t ScriptVM::sendEvent(ScriptInstance &self, uint16_t event, const std::vector<int16_t> &args) {
    if (!self.enabled)
        return 0;
    if (_depth >= kMaxEventDepth) {
        log::warning("Script: event %u to object %u nested too deeply", event, self.object->id);
        return 0;
    }

    if (self.state != -1) {
        for (ScriptInstance *owner = &self; owner; owner = owner->parent) {
            auto it = owner->handlers.find(handlerKey(event, self.state));
            if (it == owner->handlers.end())
                continue;

            const ScriptBlock *block = it->second;
            _depth++;
            int16_t result = execute(self, block->codeStart, args);
            if (self.pendingState != -1) {
                int16_t state = self.pendingState;
                self.pendingState = -1;
                setState(self, state);
            } else if (block->nextState != -1) {
                setState(self, block->nextState);
            }
            _depth--;
            return result;
        }
    }

    if (_host)
        _host->unhandledEvent(*this, self, event, args);
    return 0;
}

void ScriptVM::setState(ScriptInstance &self, int16_t state) {
    if (self.state == state)
        return;
    sendEvent(self, kEventLeaveState);
    self.state = state;
    sendEvent(self, kEventEnterState);
}

uint32_t ScriptVM::skipConditional(uint32_t ip) const {
    int depth = 1;
    while (true) {
        uint16_t op = word(ip);
        if (op >= OP_IF_EQ && op <= OP_IF_LE) {
            depth++;
            ip++;
        } else if (op == OP_ENDIF) {
            ip++;
            if (--depth <= 0)
                return ip;
        } else if (op == OP_ELSE) {
            ip++;
            if (depth <= 1)
                return ip;
        } else if (op == OP_END) {
            return ip;
        } else {
            ip += getInstructionLength(op);
        }
    }
}

void ScriptVM::callNative(ScriptInstance &self, uint16_t op) {
    if (_host) {
        _host->callNative(*this, self, op);
        return;
    }

    // No host: keep the stack balanced so execution can continue.
    const OpcodeInfo *info = getOpcodeInfo(op);
    if (_stubbed.insert(op).second)
        debugC(1, "scripts", "Script: opcode %02x (%s) not implemented", op, info ? info->name : "?");
    if (info) {
        for (int i = 0; i < info->pops; i++)
            pop();
        for (int i = 0; i < info->pushes; i++)
            push(0);
    }
}

int16_t ScriptVM::execute(ScriptInstance &self, uint32_t ip, const std::vector<int16_t> &args) {
    // Each block runs on a fresh stack, with the event arguments pushed last
    // to first so the first argument is on top.
    std::vector<int16_t> savedStack;
    savedStack.swap(_stack);
    for (size_t i = args.size(); i-- > 0;)
        push(args[i]);

    int16_t result = 0;
    uint32_t executed = 0;
    bool running = true;

    while (running) {
        if (++executed > kMaxInstructionsPerBlock) {
            log::warning("Script: object %u ran too long", self.object->id);
            break;
        }
        _instructionCount++;

        uint16_t op = word(ip++);
        switch (op) {
        case OP_END:
            running = false;
            break;
        case OP_NOP:
        case OP_ENDIF:
            break;
        case OP_PUSH:
            push((int16_t)word(ip++));
            break;
        case OP_POP:
            pop();
            break;
        case OP_PUSH_SELF:
            push((int16_t)self.object->id);
            break;
        case OP_PUSH_BLOCK:
            for (int i = 0; i < 7; i++)
                push((int16_t)word(ip++));
            break;
        case OP_PUSH32:
            push((int16_t)word(ip++));
            push((int16_t)word(ip++));
            break;
        case OP_SET_RESULT:
            result = pop();
            break;
        case OP_GOTO_STATE:
            self.pendingState = pop();
            break;
        case OP_CASE: {
            int16_t c = pop();
            int16_t b = pop();
            int16_t a = pop();
            push(a);
            if (a == c) {
                self.pendingState = b;
                running = false;
            }
            break;
        }
        case OP_IF_EQ:
        case OP_IF_NE:
        case OP_IF_GT:
        case OP_IF_GE:
        case OP_IF_LT:
        case OP_IF_LE: {
            int16_t b = pop();
            int16_t a = pop();
            push(a);
            bool pass = op == OP_IF_EQ   ? a == b
                        : op == OP_IF_NE ? a != b
                        : op == OP_IF_GT ? a > b
                        : op == OP_IF_GE ? a >= b
                        : op == OP_IF_LT ? a < b
                                         : a <= b;
            if (!pass)
                ip = skipConditional(ip);
            break;
        }
        case OP_ELSE:
            ip = skipConditional(ip);
            break;
        case OP_RETURN:
            running = false;
            break;
        case OP_ALLOC_LOCALS: {
            int16_t count = pop();
            if (count > 0 && (size_t)count > self.locals.size())
                self.locals.resize(count, 0);
            break;
        }
        case OP_GET_LOCAL: {
            int16_t index = pop();
            push(index >= 0 && (size_t)index < self.locals.size() ? self.locals[index] : 0);
            break;
        }
        case OP_SET_LOCAL: {
            int16_t index = pop();
            int16_t value = pop();
            if (index >= 0 && (size_t)index < self.locals.size())
                self.locals[index] = value;
            else
                log::warning("Script: object %u local %d out of range", self.object->id, index);
            break;
        }
        case OP_GET_GLOBAL:
            push(getGlobal(pop()));
            break;
        case OP_SET_GLOBAL: {
            int16_t index = pop();
            int16_t value = pop();
            setGlobal(index, value);
            break;
        }
        case OP_INC:
            push((int16_t)(pop() + 1));
            break;
        case OP_DEC:
            push((int16_t)(pop() - 1));
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_SIGN_DIFF:
        case OP_AND:
        case OP_OR:
        case OP_XOR: {
            // The original computes "top op below".
            int top = pop();
            int below = pop();
            int value = 0;
            switch (op) {
            case OP_ADD:
                value = top + below;
                break;
            case OP_SUB:
                value = top - below;
                break;
            case OP_MUL:
                value = top * below;
                break;
            case OP_DIV:
                if (below == 0) {
                    log::warning("Script: object %u divides by zero", self.object->id);
                    value = 0;
                } else {
                    value = top / below;
                }
                break;
            case OP_SIGN_DIFF:
                value = top > below ? 1 : top < below ? -1 : 0;
                break;
            case OP_AND:
                value = top & below;
                break;
            case OP_OR:
                value = top | below;
                break;
            default:
                value = top ^ below;
                break;
            }
            push((int16_t)value);
            break;
        }
        case OP_NOT:
            push((int16_t)~pop());
            break;
        case OP_HIGH_BITS:
            push((int16_t)(pop() & 0xE000));
            break;
        case OP_LOW_BITS:
            push((int16_t)(pop() & 0x1FFF));
            break;
        default:
            if (!getOpcodeInfo(op)) {
                log::warning("Script: object %u has unknown opcode %04x", self.object->id, op);
                running = false;
                break;
            }
            callNative(self, op);
            break;
        }
    }

    _stack.swap(savedStack);
    return result;
}

} // namespace reluva::stgenerations
