// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// UnrealScript: compiled code read and shown as text, and run in the VM.
//
//   reluva-tests unreal.unit.script

#include "tests/engines/unreal/support.h"

using namespace reluva;
using namespace reluva::unreal;
using namespace reluva::unreal::test_data;

namespace {

void testScript() {
    // An IntProperty "Count" and a function using it:
    //   Count = 5; if (true) { Lamp(0); native 0x9a(0, 1); } return;
    PackageWriter w;
    const int32_t intProp = w.addImport("Core", "Class", 0, "IntProperty");
    const int32_t function = w.addImport("Core", "Class", 0, "Function");
    Bytes prop;
    endProps(prop, w);
    prop.index(0), prop.index(0);           // super, next
    prop.i32(1), prop.u32(propflag::kEdit); // array size, flags
    prop.index(w.name("Lamp"));             // category
    const int32_t count = w.addExport(intProp, 0, "Count", prop.b);
    Bytes code;
    code.u8(token::kLet), code.u8(token::kLocalVariable), code.index(count), code.u8(token::kIntConst), code.i32(5);
    code.u8(token::kJumpIfNot), code.u16(26), code.u8(token::kTrue);
    code.u8(token::kVirtualFunction), code.index(w.name("Lamp")), code.u8(token::kIntZero),
        code.u8(token::kEndFunctionParms);
    code.u8(0x9a), code.u8(token::kIntZero), code.u8(token::kIntOne), code.u8(token::kEndFunctionParms);
    code.u8(token::kReturn), code.u8(token::kNothing);
    Bytes fn;
    endProps(fn, w);
    fn.index(0), fn.index(0), fn.index(0), fn.index(count); // super, next, script text, children
    fn.index(w.name("Lamp")), fn.i32(10), fn.i32(100);      // friendly name, line, text position
    fn.i32(28);                                             // script size in memory
    fn.b.insert(fn.b.end(), code.b.begin(), code.b.end());
    fn.u16(0), fn.u8(0), fn.u32(funcflag::kDefined | funcflag::kEvent); // native, precedence, flags
    w.addExport(function, 0, "Light", fn.b);
    Package p;
    CHECK(!p.openMemory(w.build(), "Script"));
    ScriptObject o;
    const Error err = readScriptObject(p, 0, o);
    CHECK(!err && o.kind == ScriptObject::Kind::kProperty && o.arrayDim == 1 && o.propertyFlags == propflag::kEdit);
    CHECK(!readScriptObject(p, 1, o) && o.kind == ScriptObject::Kind::kFunction && o.trailing == 0);
    CHECK(o.children == count && o.line == 10 && o.functionFlags == (funcflag::kDefined | funcflag::kEvent));
    const Code &c = o.code;
    CHECK(c.size == 28 && c.statements.size() == 5);
    if (c.statements.size() == 5) {
        const Expr &let = c.exprs[size_t(c.statements[0])];
        CHECK(let.token == token::kLet && let.args.size() == 2 && let.offset == 0);
        CHECK(c.exprs[size_t(let.args[0])].ref == count && c.exprs[size_t(let.args[1])].value == 5);
        const Expr &jump = c.exprs[size_t(c.statements[1])];
        CHECK(jump.offset == 11 && jump.value == 26); // the reference counts 4 bytes, as in memory
        const Expr &call = c.exprs[size_t(c.statements[2])];
        CHECK(call.token == token::kVirtualFunction && p.name(call.ref) == "Lamp" && call.args.size() == 2);
        const Expr &native = c.exprs[size_t(c.statements[3])];
        CHECK(native.native == 0x9a && native.offset == 22 && native.args.size() == 3);
        CHECK(c.exprs[size_t(c.statements[4])].offset == 26);
    }
    // A size that the tokens don't end at is refused.
    Reader r(code.b);
    Code bad;
    CHECK(decodeCode(p, r, 27, bad));
    // As text: the variable a local (it isn't a parameter), the jump by
    // its offset, the unknown native by number.
    PackageSet set;
    set.addMemory("Script", ".u", w.build());
    const Package *sp = set.get("Script");
    CHECK(sp != nullptr);
    if (!sp)
        return;
    const NativeTable natives(set);
    const std::string text = scriptText(set, *sp, 1, natives);
    CHECK(text.find("event Light()") != std::string::npos);
    CHECK(text.find("local int Count;") != std::string::npos);
    CHECK(text.find("0x0000: Count = 5;") != std::string::npos);
    CHECK(text.find("0x000B: if (!(true)) jump 0x001A") != std::string::npos);
    CHECK(text.find("0x000F: Lamp(0);") != std::string::npos);
    CHECK(text.find("0x0016: native154(0, 1);") != std::string::npos);
    CHECK(text.find("0x001A: return;") != std::string::npos);
}

void testVm() {
    // A class Counter: int Count, int Total (default 10); function
    // SetCount(int N) { Count = N; }; native function Wait(float S); and
    //   state Running { Begin: Count = 1; Wait(1); Count = 2; stop; }
    PackageWriter w;
    const int32_t classClass = 0;
    (void)classClass;
    const int32_t intProp = w.addImport("Core", "Class", 0, "IntProperty");
    const int32_t floatProp = w.addImport("Core", "Class", 0, "FloatProperty");
    const int32_t function = w.addImport("Core", "Class", 0, "Function");
    const int32_t state = w.addImport("Core", "Class", 0, "State");
    // Export references, in the order they're added below.
    const int32_t counter = 1;
    const int32_t count = 2;
    const int32_t total = 3;
    const int32_t setCount = 4;
    const int32_t n = 5;
    const int32_t wait = 6;
    const int32_t s = 7;
    const int32_t running = 8;
    auto property = [&](int32_t next, uint32_t flags) {
        Bytes b;
        endProps(b, w);
        b.index(0), b.index(next), b.i32(1), b.u32(flags), b.index(0);
        return b.b;
    };
    auto structPart = [&](Bytes &b, int32_t next, int32_t children, const char *name, const Bytes &code, int32_t size) {
        b.index(0), b.index(next), b.index(0), b.index(children), b.index(w.name(name));
        b.i32(0), b.i32(0), b.i32(size);
        b.b.insert(b.b.end(), code.b.begin(), code.b.end());
    };
    Bytes cls;
    {
        Bytes none;
        structPart(cls, 0, count, "Counter", none, 0);
        cls.u64(0), cls.u64(0), cls.u16(0xffff), cls.i32(0), cls.i32(0);
        for (int i = 0; i < 16; ++i)
            cls.u8(0);
        cls.index(0), cls.index(0), cls.index(0), cls.index(w.name("System"));
        tag(cls, w, "Total", PropertyType::kInt, 4);
        cls.i32(10);
        endProps(cls, w);
    }
    CHECK(w.addExport(0, 0, "Counter", cls.b) == counter);
    w.addExport(intProp, counter, "Count", property(total, 0));
    w.addExport(intProp, counter, "Total", property(setCount, 0));
    Bytes setCode; // Count = N; return;
    setCode.u8(token::kLet), setCode.u8(token::kInstanceVariable), setCode.index(count),
        setCode.u8(token::kLocalVariable), setCode.index(n);
    setCode.u8(token::kReturn), setCode.u8(token::kNothing);
    Bytes fn;
    endProps(fn, w);
    structPart(fn, wait, n, "SetCount", setCode, 13);
    fn.u16(0), fn.u8(0), fn.u32(funcflag::kDefined);
    w.addExport(function, counter, "SetCount", fn.b);
    w.addExport(intProp, setCount, "N", property(0, propflag::kParm));
    Bytes waitFn;
    endProps(waitFn, w);
    structPart(waitFn, running, s, "Wait", Bytes{}, 0);
    waitFn.u16(0), waitFn.u8(0), waitFn.u32(funcflag::kNative | funcflag::kLatent);
    w.addExport(function, counter, "Wait", waitFn.b);
    w.addExport(floatProp, wait, "S", property(0, propflag::kParm));
    Bytes stateCode; // Count = 1; Wait(1.0); Count = 2; stop; labels
    stateCode.u8(token::kLet), stateCode.u8(token::kInstanceVariable), stateCode.index(count),
        stateCode.u8(token::kIntOne);
    stateCode.u8(token::kFinalFunction), stateCode.index(wait), stateCode.u8(token::kFloatConst), stateCode.f32(1),
        stateCode.u8(token::kEndFunctionParms);
    stateCode.u8(token::kLet), stateCode.u8(token::kInstanceVariable), stateCode.index(count),
        stateCode.u8(token::kIntConstByte), stateCode.u8(2);
    stateCode.u8(token::kStop);
    stateCode.u8(token::kLabelTable), stateCode.index(w.name("Begin")), stateCode.u32(0),
        stateCode.index(w.name("None")), stateCode.u32(0);
    Bytes st;
    endProps(st, w);
    structPart(st, 0, 0, "Running", stateCode, 7 + 11 + 8 + 1 + 17);
    st.u64(0), st.u64(0), st.u16(27), st.i32(0);
    w.addExport(state, counter, "Running", st.b);

    PackageSet set;
    set.addMemory("Counting", ".u", w.build());
    const Package *p = set.get("Counting");
    CHECK(p != nullptr);
    if (!p)
        return;
    Vm vm(set);
    VmClass *c = vm.classAt(*p, counter - 1);
    CHECK(c && c->slots == 2 && c->functions.size() == 2 && c->states.size() == 1);
    if (!c)
        return;
    VmObject *o = vm.create(c);
    CHECK(o->find(vm, "Total") && o->find(vm, "Total")->asInt() == 10 && o->find(vm, "Count")->asInt() == 0);
    vm.callEvent(o, "SetCount", {Value::intValue(5)});
    CHECK(o->find(vm, "Count")->asInt() == 5);
    // Rotator * Rotator (The Fallen's): the second turned on in the first's
    // frame; none leaves the first as it is.
    if (const NativeFn *times = vm.findNative("Object.Multiply_RotatorRotator")) {
        auto multiply = [&](std::array<int32_t, 3> a, std::array<int32_t, 3> b) {
            NativeCall call{vm,
                            nullptr,
                            nullptr,
                            {vm.rotatorValue(a[0], a[1], a[2]), vm.rotatorValue(b[0], b[1], b[2])},
                            {true, true},
                            {},
                            -1,
                            {},
                            {}};
            (*times)(call);
            return Vm::rotatorOf(call.result);
        };
        auto near = [](std::array<int32_t, 3> a, std::array<int32_t, 3> b) {
            for (size_t k = 0; k < 3; ++k)
                if (std::abs(int(int16_t(uint16_t((a[k] - b[k]) & 0xffff)))) > 2)
                    return false;
            return true;
        };
        CHECK(near(multiply({1000, 16384, 0}, {0, 0, 0}), {1000, 16384, 0}));
        CHECK(near(multiply({0, 16384, 0}, {5000, 0, 0}), {5000, 16384, 0}));
        CHECK(near(multiply({0, 0, 0}, {0, 8000, 0}), {0, 8000, 0}));
    } else {
        CHECK(false);
    }
    vm.registerNative("Counter.Wait", [](NativeCall &call) { call.latentSeconds = call.f(0); });
    vm.gotoState(o, vm.name("Running"));
    CHECK(o->state && o->state->name == "Running" && o->codeAt == 0);
    vm.tick(o, 0.1f);
    CHECK(o->find(vm, "Count")->asInt() == 1 && o->latent);
    vm.tick(o, 0.5f);
    CHECK(o->find(vm, "Count")->asInt() == 1);
    vm.tick(o, 0.6f);
    CHECK(o->find(vm, "Count")->asInt() == 2 && o->codeAt == -1);
    CHECK(vm.stats.errors.empty() && vm.stats.missingNatives.empty());
    // A class with no script (as Viewport is), made up under Counter:
    // found by name, a kind of Counter, its objects with Counter's values
    // and their built-in name.
    VmClass *made = vm.defineIntrinsic("Tally", c);
    CHECK(made && made->isA(c) && vm.intrinsic("tally") == made && made->object && made->object->asClass == made);
    VmObject *t = vm.create(made);
    CHECK(t->find(vm, "Total") && t->find(vm, "Total")->asInt() == 10 && vm.nameText(t->name) == "Tally0");
    (void)total;
    (void)setCount;
}

} // namespace

static const reluva::test::Suite kSuite("unreal.unit.script",
                                        {
                                            {"script", [](const std::filesystem::path &) { testScript(); }},
                                            {"vm", [](const std::filesystem::path &) { testVm(); }},
                                        });
