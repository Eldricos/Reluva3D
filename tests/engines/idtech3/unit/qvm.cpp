// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: The QVM interpreter and console variables.

#include "common/fs.h"
#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/formats/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/server/server_world.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/formats/lexer.h"
#include "engines/idtech3/formats/marks.h"
#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/render/model_cache.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"
#include "engines/idtech3/render/patch.h"
#include "engines/idtech3/formats/player_animation.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/vm/qvm.h"
#include "engines/idtech3/client/server_browser.h"
#include "backends/networking/sockets/socket_net.h"
#include "engines/idtech3/formats/shader.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/audio/sound_system.h"
#include "engines/idtech3/audio/sounds.h"
#include "engines/idtech3/client/page.h"
#include "audio/wav.h"
#include "common/md4.h"
#include "common/zip.h"
#include "engines/registry.h"
#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm/vm_services.h"
#include "tests/engines/idtech3/fixtures.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <memory>
#include <thread>
#include <chrono>
#include <set>
#include <string>

using namespace reluva;
using namespace reluva::idtech3;
namespace q3 = reluva::idtech3;
using namespace reluva::idtech3::fixtures;

// Builds a QVM from instructions, initialised data and literals.
struct QvmAsm {
    std::vector<std::pair<uint8_t, int32_t>> code;
    std::vector<uint8_t> data;
    std::vector<uint8_t> lit;
    int32_t bss = 0;
    int add(uint8_t op, int32_t operand = 0) {
        code.emplace_back(op, operand);
        return int(code.size()) - 1;
    }
    std::vector<uint8_t> build(int32_t magic = 0x12721444) const {
        std::vector<uint8_t> bytes;
        for (const auto &[op, v] : code) {
            bytes.push_back(op);
            const int n = qvmop::operandSize(op);
            for (int i = 0; i < n; ++i)
                bytes.push_back(uint8_t(uint32_t(v) >> (8 * i)));
        }
        while (bytes.size() % 4)
            bytes.push_back(0);
        std::vector<uint8_t> out;
        auto i32 = [&](int32_t x) {
            for (int i = 0; i < 4; ++i)
                out.push_back(uint8_t(uint32_t(x) >> (8 * i)));
        };
        const int32_t header = 32;
        i32(magic);
        i32(int32_t(code.size()));
        i32(header);
        i32(int32_t(bytes.size()));
        i32(header + int32_t(bytes.size()));
        i32(int32_t(data.size()));
        i32(int32_t(lit.size()));
        i32(bss);
        out.insert(out.end(), bytes.begin(), bytes.end());
        out.insert(out.end(), data.begin(), data.end());
        out.insert(out.end(), lit.begin(), lit.end());
        return out;
    }
};

static void testQvm() {
    using namespace qvmop;
    int32_t r = 0;
    // vmMain(command, a, b) = command + a * b; arguments sit at
    // sp + frame + 8 after ENTER frame.
    {
        QvmAsm a;
        a.add(kEnter, 8);
        a.add(kLocal, 16), a.add(kLoad4);
        a.add(kLocal, 20), a.add(kLoad4), a.add(kLocal, 24), a.add(kLoad4), a.add(kMuli), a.add(kAdd);
        a.add(kLeave, 8);
        Qvm vm;
        CHECK(!vm.load(a.build()));
        CHECK(!vm.call(3, {4, 5}, r) && r == 23);
        CHECK(!vm.call(-7, {-2, 3}, r) && r == -13);
    }
    // Recursion: factorial through calls, frames and ARG.
    QvmAsm f;
    f.add(kEnter, 16);
    f.add(kLocal, 24), f.add(kLoad4), f.add(kArg, 8), f.add(kConst, 0), f.add(kCall), f.add(kLeave, 16);
    const int fact = f.add(kEnter, 16);
    f.code[4].second = fact;
    f.add(kLocal, 24), f.add(kLoad4), f.add(kConst, 1);
    const int gt = f.add(kGti, 0);
    f.add(kConst, 1), f.add(kLeave, 16);
    f.code[size_t(gt)].second = int32_t(f.code.size());
    f.add(kLocal, 24), f.add(kLoad4);
    f.add(kLocal, 24), f.add(kLoad4), f.add(kConst, 1), f.add(kSub), f.add(kArg, 8), f.add(kConst, fact), f.add(kCall);
    f.add(kMuli), f.add(kLeave, 16);
    Qvm vm;
    CHECK(!vm.load(f.build()));
    CHECK(!vm.call(5, {}, r) && r == 120);
    CHECK(!vm.call(10, {}, r) && r == 3628800);

    // A system call with a string and a number, and one that calls back
    // into the VM while it's running.
    {
        QvmAsm a;
        a.lit = {'h', 'i', 0, 0};
        a.data = {0, 0, 0, 0};
        a.add(kEnter, 16);
        a.add(kConst, 4), a.add(kArg, 8), a.add(kConst, 42), a.add(kArg, 12), a.add(kConst, -2), a.add(kCall);
        a.add(kConst, 1), a.add(kAdd);
        a.add(kLocal, 24), a.add(kLoad4), a.add(kArg, 8), a.add(kConst, -6), a.add(kCall), a.add(kAdd);
        a.add(kLeave, 16);
        Qvm s;
        CHECK(!s.load(a.build()));
        Qvm inner;
        CHECK(!inner.load(f.build()));
        std::string seen;
        int calls = 0;
        s.setSystemCall([&](Qvm &v, int number, const int32_t *args) -> int32_t {
            ++calls;
            if (number == 1) {
                seen = v.string(args[0]) + "/" + std::to_string(args[1]);
                return 100;
            }
            if (number == 5) {
                // Back into a VM from a system call: here another one, and
                // the running one keeps its state.
                int32_t out = 0;
                return inner.call(args[0], {}, out) ? -1 : out;
            }
            return 0;
        });
        CHECK(!s.call(4, {}, r) && r == 101 + 24 && seen == "hi/42" && calls == 2);
        CHECK(s.executed() > 10);
    }

    // Memory: bytes and halfwords sign-extended, block copy, initialised data.
    {
        QvmAsm a;
        a.data = {0x80, 0xff, 0x34, 0x12, 1, 2, 3, 4};
        a.bss = 64;
        a.add(kEnter, 8);
        a.add(kConst, 0), a.add(kLoad1), a.add(kSex8);               // -128
        a.add(kConst, 0), a.add(kLoad2), a.add(kSex16), a.add(kAdd); // -128 + -128
        a.add(kConst, 16), a.add(kConst, 4), a.add(kBlockCopy, 4);
        a.add(kConst, 16), a.add(kLoad4), a.add(kAdd); // + 0x04030201
        a.add(kConst, 20), a.add(kConst, 0x55), a.add(kStore1);
        a.add(kConst, 20), a.add(kLoad1), a.add(kAdd); // + 0x55
        a.add(kLeave, 8);
        Qvm m;
        CHECK(!m.load(a.build()));
        CHECK(!m.call(0, {}, r) && r == -256 + 0x04030201 + 0x55);
        CHECK(m.readInt(16) == 0x04030201 && m.memory()[20] == 0x55 && m.readInt(0) == int32_t(0x1234ff80));
    }

    // Floats, unsigned comparison, division edge cases.
    {
        QvmAsm a;
        a.add(kEnter, 8);
        a.add(kConst, Qvm::fromFloat(1.5f)), a.add(kConst, Qvm::fromFloat(2.25f)), a.add(kMulf);
        a.add(kConst, 3), a.add(kCvif), a.add(kAddf), a.add(kNegf), a.add(kCvfi); // -(3.375 + 3) -> -6
        a.add(kConst, -1), a.add(kConst, 1);
        const int b = a.add(kLtu, 0); // 0xffffffff < 1 unsigned: false
        a.add(kConst, 1000), a.add(kAdd);
        a.code[size_t(b)].second = int32_t(a.code.size());
        a.add(kConst, INT32_MIN), a.add(kConst, -1), a.add(kDivi), a.add(kConst, INT32_MIN), a.add(kSub), a.add(kAdd);
        a.add(kConst, -7), a.add(kConst, 2), a.add(kModi), a.add(kAdd);  // -1
        a.add(kConst, -8), a.add(kConst, 1), a.add(kRshi), a.add(kAdd);  // -4
        a.add(kConst, -8), a.add(kConst, 28), a.add(kRshu), a.add(kAdd); // 15
        a.add(kLeave, 8);
        Qvm m;
        CHECK(!m.load(a.build()));
        CHECK(!m.call(0, {}, r) && r == -6 + 1000 + 0 - 1 - 4 + 15);
    }

    // Faults are errors that name the instruction, not crashes.
    auto runs = [&](std::vector<std::pair<uint8_t, int32_t>> code, uint64_t limit = 0) {
        QvmAsm a;
        a.code = std::move(code);
        Qvm m;
        if (m.load(a.build()))
            return std::string("load");
        m.setInstructionLimit(limit);
        Error err = m.call(0, {}, r);
        return err ? err.message : std::string();
    };
    CHECK(runs({{kEnter, 8}, {kConst, -100}, {kLoad4, 0}, {kLeave, 8}}).find("reads outside memory") !=
          std::string::npos);
    CHECK(runs({{kEnter, 8}, {kConst, 1 << 29}, {kConst, 1}, {kStore4, 0}, {kLeave, 8}}).find("writes outside") !=
          std::string::npos);
    CHECK(runs({{kEnter, 8}, {kConst, 1}, {kConst, 0}, {kDivi, 0}, {kLeave, 8}}).find("division by zero") !=
          std::string::npos);
    CHECK(runs({{kEnter, 8}, {kConst, 99}, {kJump, 0}}).find("jumps to 99") != std::string::npos);
    CHECK(runs({{kEnter, 8}, {kConst, 1}, {kJump, 0}}, 1000).find("instruction limit") != std::string::npos);
    CHECK(runs({{kEnter, 0x20000}, {kLeave, 0x20000}}).find("stack overflow") != std::string::npos);
    CHECK(runs({{kEnter, 8}, {kAdd, 0}, {kLeave, 8}}).find("operand stack") != std::string::npos);
    CHECK(runs({{kEnter, 8}, {kConst, 5}, {kCall, 0}, {kLeave, 8}}).find("calls 5") != std::string::npos);
    CHECK(runs({{kEnter, 8}, {kEq, 50}, {kLeave, 8}}) == "load"); // branch target checked on load
    // Damaged files.
    Qvm bad;
    CHECK(bad.load({1, 2, 3}));
    auto file = f.build();
    file[0] = 0;
    CHECK(bad.load(file));
    file = f.build();
    file[32] = 200; // an unknown opcode
    CHECK(bad.load(file));
    file = f.build();
    file[4] = 100; // more instructions than the code holds
    CHECK(bad.load(file));
    CHECK(!bad.load(f.build(0x12721445))); // the second version's magic
}

// tests/engines/idtech3/data/qvm/test_vm.c compiled by the Quake III compiler: every case
// must give what the same program gives compiled natively.
static void testCompiledQvm() {
    Qvm vm;
    Error err = vm.load(std::vector<uint8_t>(std::begin(kTestVmQvm), std::end(kTestVmQvm)));
    CHECK(!err);
    if (err)
        return;
    std::string printed;
    vm.setSystemCall([&](Qvm &v, int number, const int32_t *args) -> int32_t {
        switch (number) {
        case 0:
            printed = v.string(args[0]);
            return int32_t(printed.size()) * 3;
        case 1:
            return args[0] * 2 + args[1];
        case 2: {
            int32_t out = 0;
            return v.call(args[0], {args[1]}, out) ? -999 : out;
        }
        case 3:
            return Qvm::fromFloat(Qvm::asFloat(args[0]) / 2.0f);
        default:
            return 0;
        }
    });
    for (const QvmCase &c : kQvmCases) {
        int32_t r = 0;
        err = vm.call(c.command, {c.arg0, c.arg1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 100}, r);
        if (err || r != c.expected)
            std::fprintf(stderr, "case %d(%d, %d): got %d, expected %d %s\n", c.command, c.arg0, c.arg1, r, c.expected,
                         err ? err.message.c_str() : "");
        CHECK(!err && r == c.expected);
    }
    // Case 7 builds a string and prints it.
    int32_t r = 0;
    CHECK(!vm.call(7, {42, -17}, r) && printed == "value 42 and -17");
}

static void testCvars() {
    CvarStore cvars;
    const CvarStore::Cvar &a = cvars.create("Sensitivity", "5.5", CvarStore::kArchive);
    CHECK(a.handle == 1 && a.modificationCount == 1);
    CHECK(cvars.value("sensitivity") == 5.5f && cvars.integer("SENSITIVITY") == 5);
    cvars.create("sensitivity", "1", CvarStore::kCheat); // exists: keeps its value, gains the flag
    CHECK(cvars.string("sensitivity") == "5.5" &&
          cvars.find("sensitivity")->flags == (CvarStore::kArchive | CvarStore::kCheat));
    cvars.set("sensitivity", "3");
    CHECK(cvars.find("sensitivity")->modificationCount == 2);
    cvars.set("sensitivity", "3"); // unchanged: not counted
    CHECK(cvars.find("sensitivity")->modificationCount == 2);
    cvars.create("version", "1.0", CvarStore::kReadOnly);
    cvars.set("version", "2.0");
    CHECK(cvars.string("version") == "1.0");
    cvars.set("version", "2.0", true);
    CHECK(cvars.string("version") == "2.0");
    // A Latch variable (g_gametype, the Holomatch modes) keeps its value
    // until a level starts; forcing sets it at once; setting it back
    // forgets the wait.
    CvarStore latch;
    latch.create("g_pModAssimilation", "0", CvarStore::kServerInfo | CvarStore::kLatch);
    latch.set("g_pModAssimilation", "1");
    CHECK(latch.integer("g_pModAssimilation") == 0 && latch.hasLatched("g_pModAssimilation") &&
          latch.pending("g_pModAssimilation") == "1");
    latch.applyLatched();
    CHECK(latch.integer("g_pModAssimilation") == 1 && !latch.hasLatched("g_pModAssimilation"));
    latch.set("g_pModAssimilation", "0");
    latch.set("g_pModAssimilation", "1");
    CHECK(!latch.hasLatched("g_pModAssimilation"));
    latch.set("g_pModAssimilation", "0", true);
    CHECK(latch.integer("g_pModAssimilation") == 0 && !latch.hasLatched("g_pModAssimilation"));
    cvars.set("made_up", "x"); // created by the user, then claimed by the code with its default
    CHECK(cvars.find("made_up")->flags == CvarStore::kUserCreated);
    cvars.create("made_up", "y", 0);
    CHECK(cvars.find("made_up")->flags == 0 && cvars.find("made_up")->defaultValue == "y" &&
          cvars.string("made_up") == "x");
    CHECK(cvars.byHandle(2)->name == "version" && !cvars.byHandle(0) && !cvars.byHandle(4));
}

// The tables switch statements jump through are found and kept from
// writes: a system call writing over one stops the run, named.
static void testJumpTableGuard() {
    Qvm vm;
    CHECK(!vm.load(std::vector<uint8_t>(std::begin(kFakeUiQvm), std::end(kFakeUiQvm))));
    CHECK(!vm.protectedRanges().empty());
    if (vm.protectedRanges().empty())
        return;
    // vmMain's (commands 0..33) and test command 32's (cases 100..107):
    // exactly their entries, nothing before them.
    // (They may sit side by side, as one range.)
    size_t bytes = 0;
    for (const auto &[first, end] : vm.protectedRanges())
        bytes += end - first;
    CHECK(bytes == 4 * (34 + 8));
    const auto table = vm.protectedRanges().front();
    vm.setSystemCall([&](Qvm &v, int number, const int32_t *) -> int32_t {
        if (number == 1) // Print, during Init
            v.writeString(int32_t(table.first), "-75", 4);
        return 0;
    });
    int32_t r = 0;
    const Error err = vm.call(1, {}, r);
    CHECK(err && err.message.find("system call 1") != std::string::npos &&
          err.message.find("jump table") != std::string::npos);
    CHECK(vm.readInt(int32_t(table.first)) >= 0 && vm.readInt(int32_t(table.first)) < int32_t(vm.code().size()));
    vm.setSystemCall(nullptr);
    CHECK(!vm.call(0, {}, r) && r == 4); // the table intact, it still works
    const int expected[8] = {1, 7, 3, 9, 2, 8, 4, 6};
    for (int i = 0; i < 8; ++i)
        CHECK(!vm.call(32, {100 + i}, r) && r == expected[i]);
    CHECK(!vm.call(32, {99}, r) && r == 0);
}

static const reluva::test::Suite
    kSuite("idtech3.unit.qvm", {
                                   {"qvm", [](const std::filesystem::path &) { testQvm(); }},
                                   {"compiled qvm", [](const std::filesystem::path &) { testCompiledQvm(); }},
                                   {"jump table guard", [](const std::filesystem::path &) { testJumpTableGuard(); }},
                                   {"cvars", [](const std::filesystem::path &) { testCvars(); }},
                               });
