// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Structured exceptions: RaiseException dispatching to the handlers the
// program registered at fs:[0], and RtlUnwind unwinding to a frame, as
// Microsoft documents them (EXCEPTION_RECORD, the x86 CONTEXT, the
// handlers' dispositions, the unwinding flags). Visual C++'s throw and
// catch, and __try and __except, are built on them.
//
// A handler is called as if the stand-in had called it, but without a
// nested Cpu::call: the stand-in points the program's stack and eip at
// the handler, with a trampoline (a native function) as its return
// address, which carries on from its answer. A handler that catches never
// returns: it jumps to its catch's continuation itself, leaving nothing
// behind on the host's side.

#include "emulation/x86/win32.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

namespace reluva::x86 {

namespace {

// EXCEPTION_RECORD (80 bytes) and the x86 CONTEXT (716 bytes): the
// offsets of their fields.
constexpr uint32_t kRecordSize = 80;
constexpr uint32_t kContextSize = 716;
constexpr uint32_t kCtxFlags = 0;
constexpr uint32_t kCtxEdi = 156;
constexpr uint32_t kCtxEsi = 160;
constexpr uint32_t kCtxEbx = 164;
constexpr uint32_t kCtxEdx = 168;
constexpr uint32_t kCtxEcx = 172;
constexpr uint32_t kCtxEax = 176;
constexpr uint32_t kCtxEbp = 180;
constexpr uint32_t kCtxEip = 184;
constexpr uint32_t kCtxEflags = 192;
constexpr uint32_t kCtxEsp = 196;
constexpr uint32_t kContextFull = 0x10007; // CONTEXT_i386 | CONTROL | INTEGER | SEGMENTS

// A dispatch's state, below the stack of the code that raised or unwound:
// the record, the context, then the handler being run and, for an unwind,
// where to go back to.
constexpr uint32_t kStRecord = 0;
constexpr uint32_t kStContext = kStRecord + kRecordSize;
constexpr uint32_t kStRegistration = kStContext + kContextSize; // the handler's registration
constexpr uint32_t kStDispatcher = kStRegistration + 4;         // the handler's DispatcherContext
constexpr uint32_t kStTarget = kStDispatcher + 4;               // an unwind's target frame
constexpr uint32_t kStReturnEip = kStTarget + 4;                // and where RtlUnwind returns to
constexpr uint32_t kStReturnEsp = kStReturnEip + 4;
constexpr uint32_t kStReturnValue = kStReturnEsp + 4;
constexpr uint32_t kStRecordUsed = kStReturnValue + 4; // the record an unwind passes its handlers
constexpr uint32_t kStateSize = (kStRecordUsed + 4 + 15) & ~15u;

constexpr uint32_t kEndOfChain = 0xffffffffu;
constexpr uint32_t kNonContinuable = 0x1; // EXCEPTION_NONCONTINUABLE
constexpr uint32_t kUnwinding = 0x2;      // EXCEPTION_UNWINDING
constexpr uint32_t kExitUnwind = 0x4;     // EXCEPTION_EXIT_UNWIND
constexpr uint32_t kStatusUnwind = 0xc0000027u;
constexpr uint32_t kAccessViolation = 0xc0000005u; // EXCEPTION_ACCESS_VIOLATION

// The handler's dispositions (EXCEPTION_DISPOSITION).
constexpr uint32_t kContinueExecution = 0;

// The registers as they are, in a CONTEXT, with the given eip and esp.
void saveContext(Cpu &c, uint32_t ctx, uint32_t eip, uint32_t esp) {
    for (uint32_t i = 0; i < kContextSize; i += 4)
        c.write32(ctx + i, 0);
    c.write32(ctx + kCtxFlags, kContextFull);
    c.write32(ctx + kCtxEdi, c.reg[Cpu::EDI]);
    c.write32(ctx + kCtxEsi, c.reg[Cpu::ESI]);
    c.write32(ctx + kCtxEbx, c.reg[Cpu::EBX]);
    c.write32(ctx + kCtxEdx, c.reg[Cpu::EDX]);
    c.write32(ctx + kCtxEcx, c.reg[Cpu::ECX]);
    c.write32(ctx + kCtxEax, c.reg[Cpu::EAX]);
    c.write32(ctx + kCtxEbp, c.reg[Cpu::EBP]);
    c.write32(ctx + kCtxEip, eip);
    c.write32(ctx + kCtxEflags, c.eflags);
    c.write32(ctx + kCtxEsp, esp);
}

// Where the program goes on when the native function running returns:
// at `eip`, with the stack at `esp`. (After a native function the
// processor pops eip from the stack: it's put there.)
void continueAt(Cpu &c, uint32_t eip, uint32_t esp) {
    c.reg[Cpu::ESP] = esp - 4;
    c.write32(esp - 4, eip);
}

} // namespace

std::string Win32Process::describeException(Cpu &c, uint32_t record) const {
    char buf[96];
    std::snprintf(buf, sizeof buf, "exception 0x%08x at 0x%08x", c.read32(record), c.read32(record + 12));
    return buf;
}

void Win32Process::addExceptionStandIns() {
    const char *k = "KERNEL32.dll";
    Cpu &cpu = _cpu;

    // Calls the handler of `registration` for the dispatch whose state is
    // at `state`, coming back to `trampoline`: handler(record, frame,
    // context, dispatcher context), cdecl, with the state's address above
    // its arguments for the trampoline.
    auto callHandler = [](Cpu &c, uint32_t state, uint32_t registration, uint32_t record, uint32_t trampoline) {
        c.write32(state + kStRegistration, registration);
        c.write32(state + kStDispatcher, 0);
        const uint32_t esp = state - 24;
        const uint32_t frame[6] = {trampoline, record, registration, state + kStContext, state + kStDispatcher, state};
        for (uint32_t i = 0; i < 6; ++i)
            c.write32(esp + 4 * i, frame[i]);
        continueAt(c, c.read32(registration + 4), esp);
    };

    // The search: each handler from fs:[0] on until one goes on with the
    // program or catches (a catch jumps away itself).
    auto search = std::make_shared<std::function<void(Cpu &, uint32_t, uint32_t)>>();
    const uint32_t afterSearch = cpu.addNative("exception dispatch: after a handler", [this, search](Cpu &c) {
        // Here by the handler's ret: the stack holds its arguments, then the state.
        const uint32_t state = c.read32(c.reg[Cpu::ESP] + 16);
        const uint32_t record = state + kStRecord;
        if (c.reg[Cpu::EAX] == kContinueExecution) {
            if (c.read32(record + 4) & kNonContinuable) {
                c.abort("a handler went on from a noncontinuable " + describeException(c, record));
                return 0u;
            }
            const uint32_t ctx = state + kStContext;
            c.reg[Cpu::EDI] = c.read32(ctx + kCtxEdi);
            c.reg[Cpu::ESI] = c.read32(ctx + kCtxEsi);
            c.reg[Cpu::EBX] = c.read32(ctx + kCtxEbx);
            c.reg[Cpu::EDX] = c.read32(ctx + kCtxEdx);
            c.reg[Cpu::ECX] = c.read32(ctx + kCtxEcx);
            c.reg[Cpu::EAX] = c.read32(ctx + kCtxEax);
            c.reg[Cpu::EBP] = c.read32(ctx + kCtxEbp);
            c.eflags = c.read32(ctx + kCtxEflags);
            continueAt(c, c.read32(ctx + kCtxEip), c.read32(ctx + kCtxEsp));
            return 0u;
        }
        // Searching on (or nested, or collided, which go on the same way).
        (*search)(c, state, c.read32(c.read32(state + kStRegistration)));
        return 0u;
    });
    *search = [this, callHandler, afterSearch](Cpu &c, uint32_t state, uint32_t registration) {
        if (registration == kEndOfChain || registration == 0 || !c.valid(registration, 8)) {
            c.abort("the program raised " + describeException(c, state + kStRecord) + ", which nothing handled");
            return;
        }
        callHandler(c, state, registration, state + kStRecord, afterSearch);
    };

    // RaiseException(code, flags, argument count, arguments). It and
    // RtlUnwind set where the program goes on themselves, so they pop
    // nothing as they return (provide, not stdcall).
    provide(k, "RaiseException", [this, search](Cpu &c) {
        const uint32_t returnEip = c.read32(c.reg[Cpu::ESP]);
        const uint32_t returnEsp = c.reg[Cpu::ESP] + 4 + 16;
        const uint32_t state = (c.reg[Cpu::ESP] - kStateSize) & ~15u;
        const uint32_t record = state + kStRecord;
        for (uint32_t i = 0; i < kRecordSize; i += 4)
            c.write32(record + i, 0);
        c.write32(record, c.arg(0));
        c.write32(record + 4, c.arg(1) & kNonContinuable);
        c.write32(record + 12, returnEip);
        const uint32_t count = std::min(c.arg(2), 15u);
        c.write32(record + 16, count);
        for (uint32_t i = 0; i < count && c.arg(3); ++i)
            c.write32(record + 20 + 4 * i, c.read32(c.arg(3) + 4 * i));
        saveContext(c, state + kStContext, returnEip, returnEsp);
        if (exceptionRaised)
            exceptionRaised(c, record);
        (*search)(c, state, c.read32(c.fsBase));
        return 0u;
    });

    // A fault in the program's code (a read through a null pointer, say)
    // raised as Windows raises it: EXCEPTION_ACCESS_VIOLATION at the
    // instruction, its two arguments whether it wrote (0: whether it read
    // or wrote isn't told by the processor here) and the address. Visual
    // C++'s catch (...) catches it. Nothing on the chain: the run ends.
    cpu.memoryFaulted = [this, search](Cpu &c, uint32_t address) {
        const uint32_t registration = c.read32(c.fsBase);
        if (registration == kEndOfChain || registration == 0 || !c.valid(registration, 8))
            return false;
        const uint32_t esp = c.reg[Cpu::ESP];
        const uint32_t state = (esp - kStateSize) & ~15u;
        const uint32_t record = state + kStRecord;
        for (uint32_t i = 0; i < kRecordSize; i += 4)
            c.write32(record + i, 0);
        c.write32(record, kAccessViolation);
        c.write32(record + 12, c.eip);
        c.write32(record + 16, 2);
        c.write32(record + 24, address);
        saveContext(c, state + kStContext, c.eip, esp);
        if (exceptionRaised)
            exceptionRaised(c, record);
        (*search)(c, state, registration);
        // The search left the handler's address for a native's return:
        // there's no native here, so it's gone to at once.
        c.eip = c.read32(c.reg[Cpu::ESP]);
        c.reg[Cpu::ESP] += 4;
        return true;
    };

    // RtlUnwind(target frame, target eip, record, return value): each
    // handler from fs:[0] up to the target frame, called to unwind and
    // then taken off the chain; then back to the caller with the return
    // value in eax. (Visual C++'s callers give as the target eip the
    // instruction after their call, so it's returned to as usual.)
    auto unwind = std::make_shared<std::function<void(Cpu &, uint32_t, uint32_t)>>();
    const uint32_t afterUnwind = cpu.addNative("exception dispatch: after unwinding a frame", [unwind](Cpu &c) {
        const uint32_t state = c.read32(c.reg[Cpu::ESP] + 16);
        const uint32_t next = c.read32(c.read32(state + kStRegistration));
        c.write32(c.fsBase, next); // the frame's taken off the chain
        (*unwind)(c, state, next);
        return 0u;
    });
    *unwind = [callHandler, afterUnwind](Cpu &c, uint32_t state, uint32_t registration) {
        const uint32_t target = c.read32(state + kStTarget);
        if (registration == target || registration == kEndOfChain || registration == 0 || !c.valid(registration, 8)) {
            c.reg[Cpu::EAX] = c.read32(state + kStReturnValue);
            continueAt(c, c.read32(state + kStReturnEip), c.read32(state + kStReturnEsp));
            return;
        }
        callHandler(c, state, registration, c.read32(state + kStRecordUsed), afterUnwind);
    };
    provide(k, "RtlUnwind", [unwind](Cpu &c) {
        const uint32_t returnEip = c.read32(c.reg[Cpu::ESP]);
        const uint32_t returnEsp = c.reg[Cpu::ESP] + 4 + 16;
        const uint32_t target = c.arg(0);
        uint32_t record = c.arg(2);
        const uint32_t returnValue = c.arg(3);
        const uint32_t state = (c.reg[Cpu::ESP] - kStateSize) & ~15u;
        if (!record) { // a record of its own: STATUS_UNWIND
            record = state + kStRecord;
            for (uint32_t i = 0; i < kRecordSize; i += 4)
                c.write32(record + i, 0);
            c.write32(record, kStatusUnwind);
            c.write32(record + 12, returnEip);
        }
        c.write32(record + 4, c.read32(record + 4) | kUnwinding | (target ? 0 : kExitUnwind));
        saveContext(c, state + kStContext, returnEip, returnEsp);
        c.write32(state + kStTarget, target ? target : kEndOfChain);
        c.write32(state + kStReturnEip, returnEip);
        c.write32(state + kStReturnEsp, returnEsp);
        c.write32(state + kStReturnValue, returnValue);
        c.write32(state + kStRecordUsed, record);
        (*unwind)(c, state, c.read32(c.fsBase));
        return 0u;
    });
}

} // namespace reluva::x86
