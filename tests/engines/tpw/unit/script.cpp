// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Theme Park World engine's script machine, running scripts built
// here (docs/engines/tpw/games/simcoaster/systems/scripts.md).
//
//   reluva-tests tpw.unit.script

#include "engines/tpw/formats/rse.h"
#include "engines/tpw/script/machine.h"
#include "engines/tpw/script/simulated_host.h"

#include "tests/support/test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::tpw;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t kOp = 0x80000000;
constexpr uint32_t kStr = 0x10000000;
constexpr uint32_t kAddr = 0x20000000;
constexpr uint32_t kVar = 0x40000000;

uint32_t num(int v) {
    return uint32_t(uint16_t(int16_t(v)));
}

void put32(Bytes &b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}

// A compiled script of `code`, with `variables` variables named V0...,
// the strings given back to back, a stack of 4 and a time slice of
// `slice`.
Script build(const std::vector<uint32_t> &code, int variables, const std::string &strings = "", uint32_t slice = 50) {
    Bytes b = {'R', 'S', 'S', 'E', 'Q', 0x0f, 0x01, 0x00};
    for (uint32_t v : {uint32_t(variables), 4u, slice, 0u, 0u, 0u})
        put32(b, v);
    for (char c : std::string("Pad Pad Pad Pad "))
        b.push_back(uint8_t(c));
    put32(b, uint32_t(code.size()));
    for (uint32_t w : code)
        put32(b, w);
    put32(b, uint32_t(strings.size()) + 1);
    b.insert(b.end(), strings.begin(), strings.end());
    b.push_back(0);
    for (int i = 0; i < variables; ++i) {
        const std::string name = "V" + std::to_string(i);
        put32(b, uint32_t(name.size()) + 1);
        b.insert(b.end(), name.begin(), name.end());
        b.push_back(0);
    }
    Script script;
    CHECK(!script.parse(b));
    return script;
}

// A host that answers each instruction for the game with the next of
// `answers`, and records them.
struct Host : ScriptHost {
    uint32_t time = 0;
    std::vector<int> answers;
    std::vector<uint16_t> asked;
    uint32_t now() const override { return time; }
    int random(int below) override { return below - 1; }
    Error loadScript(const ScriptMachine &, const std::string &name, Script &) override {
        return Error::fail("no " + name);
    }
    HostResult perform(ScriptMachine &, const Script::Instruction &ins, const std::vector<int> &) override {
        asked.push_back(ins.opcode);
        HostResult r;
        if (!answers.empty()) {
            r.value = answers.front();
            answers.erase(answers.begin());
        }
        return r;
    }
};

// Arithmetic with the result first, flags, the branches on them, and a
// result given a number for its place.
void testArithmetic() {
    // V0 = 10; V1 = V0 - 3; V2 = V1 * 2 ... a countdown of V3 from 3 by
    // ADD -1, BRANCH_PV, adding to V4 each time; then END.
    const Script script = build({kOp | 3,  kVar | 0,   num(10),          // 0 COPY V0 10
                                 kOp | 5,  kVar | 1,   kVar | 0, num(3), // 3 SUB V1 V0 3
                                 kOp | 48, kVar | 2,   kVar | 1, num(2), // 7 MULT V2 V1 2
                                 kOp | 49, kVar | 5,   kVar | 2, num(4), // 11 DIV V5 V2 4
                                 kOp | 50, num(0),     kVar | 2, num(7), // 15 MOD 0 V2 7 (zero)
                                 kOp | 3,  kVar | 3,   num(3),           // 19 COPY V3 3
                                 kOp | 47, kVar | 4,   num(1),           // 22 ADD V4 1
                                 kOp | 47, kVar | 3,   num(-1),          // 25 ADD V3 -1
                                 kOp | 35, kAddr | 22,                   // 28 BRANCH_PV 22
                                 kOp | 52},                              // 30 END
                                6);
    Host host;
    ScriptMachine m(script, host);
    m.tick();
    CHECK(m.state() == ScriptMachine::State::kEnded);
    CHECK(m.variable(0) == 10 && m.variable(1) == 7 && m.variable(2) == 14 && m.variable(5) == 3);
    // Three times round: V3 3, 2, 1 before each ADD -1.
    CHECK(m.variable(3) == 0 && m.variable(4) == 3 && m.zero() && !m.negative());
}

// A turn ends at ENDSLICE or after the time slice's instructions, a
// critical section or not; WAIT sleeps for its milliseconds.
void testTurns() {
    const Script script = build({kOp | 37, kStr | 0,         // 0 NAME "Turns"
                                 kOp | 1,                    // 2 CRIT_LOCK
                                 kOp | 47, kVar | 0, num(1), // 3 ADD V0 1
                                 kOp | 31, kAddr | 3},       // 6 BRANCH 3
                                1, "Turns", 10);
    Host host;
    ScriptMachine m(script, host);
    m.tick();
    // NAME, CRIT_LOCK, then four rounds of ADD and BRANCH: ten in all.
    CHECK(m.name() == "Turns" && m.locked() && m.variable(0) == 4);
    m.tick();
    CHECK(m.variable(0) == 9);

    const Script waits = build({kOp | 44, num(500),         // 0 WAIT 500
                                kOp | 47, kVar | 0, num(1), // 2 ADD V0 1
                                kOp | 6,                    // 5 ENDSLICE
                                kOp | 31, kAddr | 0},       // 6 BRANCH 0
                               1);
    ScriptMachine w(waits, host);
    w.tick();
    CHECK(w.state() == ScriptMachine::State::kWaiting && w.wakeTime() == 500 && w.variable(0) == 0);
    host.time = 499;
    w.tick();
    CHECK(w.variable(0) == 0);
    host.time = 500;
    w.tick();
    CHECK(w.variable(0) == 1 && w.state() == ScriptMachine::State::kRunning);
}

// JSR and RETURN; PUSH to a full stack, a branch to no instruction, and a
// child that can't be read stop the script, saying why.
void testCallsAndFailures() {
    const Script calls = build({kOp | 29, kAddr | 3,       // 0 JSR 3
                                kOp | 52,                  // 2 END
                                kOp | 3, kVar | 0, num(7), // 3 COPY V0 7
                                kOp | 30},                 // 6 RETURN
                               1);
    Host host;
    ScriptMachine c(calls, host);
    c.tick();
    CHECK(c.state() == ScriptMachine::State::kEnded && c.variable(0) == 7);

    const Script full = build({kOp | 40, num(1), kOp | 31, kAddr | 0}, 0);
    ScriptMachine f(full, host);
    f.tick();
    CHECK(f.state() == ScriptMachine::State::kFailed && f.error().find("stack") != std::string::npos);

    const Script lost = build({kOp | 31, kAddr | 1, kOp | 0}, 0);
    ScriptMachine l(lost, host);
    l.tick();
    CHECK(l.state() == ScriptMachine::State::kFailed);

    const Script child = build({kOp | 63, kStr | 0}, 0, "Missing.rse");
    ScriptMachine m(child, host);
    m.tick();
    CHECK(m.state() == ScriptMachine::State::kFailed && m.error().find("Missing.rse") != std::string::npos);
}

// The game's instructions go to the host; the answers of those that set
// flags do, for the branch after them.
void testHost() {
    const Script script = build({kOp | 53, num(10), num(0), // 0 TOUR 10 0: a car in the station?
                                 kOp | 32, kAddr | 9,       // 3 BRANCH_Z 9
                                 kOp | 3, kVar | 0, num(1), // 5 COPY V0 1
                                 kOp | 52,                  // 8 END
                                 kOp | 17, num(0), num(0),  // 9 WAITANIM 0 0
                                 kOp | 52},                 // 12 END
                                1);
    Host host;
    host.answers = {1};
    ScriptMachine yes(script, host);
    yes.tick();
    CHECK(yes.variable(0) == 1 && host.asked == std::vector<uint16_t>{53});
    host.answers = {0};
    host.asked.clear();
    ScriptMachine no(script, host);
    no.tick();
    CHECK(no.variable(0) == 0 && (host.asked == std::vector<uint16_t>{53, 17}));
}

// The stand-in for the park: animations take a second; a guest in the
// queue goes into limbo and comes out when its time is up.
void testSimulatedHost() {
    const Script script = build({kOp | 17, num(0), num(0),      // 0 WAITANIM 0 0
                                 kOp | 38, kVar | 0,            // 3 TEST VAR_LETMEON
                                 kOp | 32, kAddr | 3,           // 5 BRANCH_Z 3
                                 kOp | 58, kVar | 0, num(2000), // 7 LIMBO VAR_LETMEON 2000
                                 kOp | 3, kVar | 0, num(0),     // 10 COPY VAR_LETMEON 0
                                 kOp | 59, kVar | 1,            // 13 UNLIMBO VAR_LETMEOFF
                                 kOp | 32, kAddr | 13,          // 15 BRANCH_Z 13
                                 kOp | 52},                     // 17 END
                                2);
    SearchSet files;
    SimulatedHost host(files, "");
    host.setGuests(true);
    ScriptMachine m(script, host);
    runFor(m, host, 1000);
    CHECK(m.state() == ScriptMachine::State::kWaiting);
    runFor(m, host, 5000);
    // Guest 1 went in at a second and came out two seconds later.
    CHECK(m.state() == ScriptMachine::State::kEnded && m.variable(1) == 1);
    CHECK(!host.trace().empty() && host.trace()[0].find("WAITANIM") != std::string::npos);
}

} // namespace

static const reluva::test::Suite kSuite("tpw.unit.script",
                                        {
                                            {"arithmetic", [](const std::filesystem::path &) { testArithmetic(); }},
                                            {"turns", [](const std::filesystem::path &) { testTurns(); }},
                                            {"calls", [](const std::filesystem::path &) { testCallsAndFailures(); }},
                                            {"host", [](const std::filesystem::path &) { testHost(); }},
                                            {"stand-in", [](const std::filesystem::path &) { testSimulatedHost(); }},
                                        });
