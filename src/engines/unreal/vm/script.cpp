// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/vm/script.h"

#include "common/fs.h"

namespace reluva::unreal {

namespace {

// Decodes bytecode expressions. Positions are counted twice: in the file
// (the reader) and as the engine lays the code out in memory, where object
// references and names take 4 bytes each whatever their compact size on
// disk; jumps and the stored script size use the second.
class Decoder {
public:
    Decoder(const Package &package, Reader &r, Code &code) : _package(package), _r(r), _code(code) {}

    uint32_t at() const { return _mem; }
    bool failed() const { return !_error.empty() || !_r.ok(); }
    const std::string &error() const { return _error; }

    // One expression; its index in _code.exprs, or -1.
    int expr() {
        if (failed())
            return -1;
        const uint32_t start = _mem;
        const uint8_t t = u8();
        const int index = int(_code.exprs.size());
        _code.exprs.push_back({});
        _code.exprs.back().token = t;
        _code.exprs.back().offset = start;
        auto self = [&]() -> Expr & { return _code.exprs[size_t(index)]; };
        auto arg = [&] {
            const int a = expr();
            self().args.push_back(a);
        };
        // Function arguments, up to EndFunctionParms (kept, so arguments
        // left out are told from the end).
        auto params = [&] {
            for (int n = 0; n < 64 && !failed(); ++n) {
                const int a = expr();
                self().args.push_back(a);
                if (a < 0 || _code.exprs[size_t(a)].token == token::kEndFunctionParms)
                    return;
            }
            fail("a call's arguments don't end");
        };
        using namespace token;
        if (t >= kFirstNative || (t >= kExtendedNative && t < kFirstNative)) {
            self().native = t >= kFirstNative ? t : (t - kExtendedNative) * 256 + u8();
            params();
            return index;
        }
        if (t >= kFirstConversion && t <= kLastConversion) {
            arg();
            return index;
        }
        switch (t) {
        case kLocalVariable:
        case kInstanceVariable:
        case kDefaultVariable:
        case kObjectConst:
        case kNativeParm:
            self().ref = ref();
            break;
        case kNameConst:
            self().ref = name();
            break;
        case kReturn:
        case kGotoLabel:
        case kEatString:
        case kBoolVariable:
            arg();
            break;
        case kSwitch:
            self().size = u8();
            arg();
            break;
        case kJump:
            self().value = u16();
            break;
        case kJumpIfNot:
            self().value = u16();
            arg();
            break;
        case kAssert:
            self().value = u16();
            arg();
            break;
        case kCase:
            self().value = u16();
            if (self().value != 0xffff)
                arg();
            break;
        case kStop:
        case kNothing:
        case kEndFunctionParms:
        case kSelf:
        case kIntZero:
        case kIntOne:
        case kTrue:
        case kFalse:
        case kNoObject:
        case kIteratorPop:
        case kIteratorNext:
            break;
        case kLabelTable:
            for (int n = 0; n < 1024 && !failed(); ++n) {
                const int32_t label = name();
                const uint32_t offset = u32();
                if (fs::toLower(_package.name(label)) == "none")
                    break;
                self().labels.push_back({label, offset});
            }
            break;
        case kLet:
        case kLetBool:
        case kDynArrayElement:
        case kArrayElement:
            arg();
            arg();
            break;
        case kNew:
            for (int k = 0; k < 4; ++k)
                arg();
            break;
        case kContext:
        case kClassContext:
            arg();
            self().value = u16();
            self().size = u8();
            arg();
            break;
        case kMetaCast:
        case kDynamicCast:
        case kStructMember:
            self().ref = ref();
            arg();
            break;
        case kSkip:
            self().value = u16();
            arg();
            break;
        case kVirtualFunction:
        case kGlobalFunction:
            self().ref = name();
            params();
            break;
        case kFinalFunction:
            self().ref = ref();
            params();
            break;
        case kIntConst:
            self().value = int32_t(u32());
            break;
        case kFloatConst:
            self().f[0] = f32();
            break;
        case kStringConst: {
            std::string s;
            for (uint8_t c; (c = u8()) != 0 && !failed();)
                s += char(c);
            self().text = std::move(s);
            break;
        }
        case kUnicodeStringConst: {
            std::string s;
            for (uint16_t c; (c = u16()) != 0 && !failed();)
                s += c < 0x80 ? char(c) : '?';
            self().text = std::move(s);
            break;
        }
        case kRotationConst:
            for (int32_t &v : self().rot)
                v = int32_t(u32());
            break;
        case kVectorConst:
            for (float &v : self().f)
                v = f32();
            break;
        case kByteConst:
        case kIntConstByte:
            self().value = u8();
            break;
        case kIterator:
            arg();
            self().value = u16();
            break;
        case kStructCmpEq:
        case kStructCmpNe:
            self().ref = ref();
            arg();
            arg();
            break;
        default: {
            char buf[64];
            std::snprintf(buf, sizeof buf, "unknown token 0x%02x at %u", t, start);
            fail(buf);
            return -1;
        }
        }
        return failed() ? -1 : index;
    }

    void fail(const std::string &why) {
        if (_error.empty())
            _error = why;
    }

private:
    uint8_t u8() {
        _mem += 1;
        return _r.u8();
    }
    uint16_t u16() {
        _mem += 2;
        return _r.u16();
    }
    uint32_t u32() {
        _mem += 4;
        return _r.u32();
    }
    float f32() {
        _mem += 4;
        return _r.f32();
    }
    int32_t ref() {
        _mem += 4;
        return _r.index();
    }
    int32_t name() {
        _mem += 4;
        const int32_t n = _r.index();
        if (n < 0 || size_t(n) >= _package.names().size())
            fail("name " + std::to_string(n) + " out of range");
        return n;
    }

    const Package &_package;
    Reader &_r;
    Code &_code;
    uint32_t _mem = 0;
    std::string _error;
};

} // namespace

Error decodeCode(const Package &package, Reader &r, uint32_t scriptSize, Code &out) {
    out = {};
    out.size = scriptSize;
    if (scriptSize > 0x100000)
        return Error::fail("a script of " + std::to_string(scriptSize) + " bytes");
    Decoder d(package, r, out);
    while (d.at() < scriptSize && !d.failed())
        out.statements.push_back(d.expr());
    if (d.failed())
        return Error::fail(d.error().empty() ? "the code runs past the end" : d.error());
    if (d.at() != scriptSize)
        return Error::fail("the code ends at " + std::to_string(d.at()) + ", not " + std::to_string(scriptSize));
    return Error::success();
}

bool isScriptClass(std::string_view className) {
    const std::string c = fs::toLower(className);
    return c == "const" || c == "enum" || c == "struct" || c == "function" || c == "state" || c == "class" ||
           (c.size() > 8 && c.compare(c.size() - 8, 8, "property") == 0);
}

Error readScriptObject(const Package &package, int index, ScriptObject &out) {
    out = {};
    if (index < 0 || size_t(index) >= package.exports().size())
        return Error::fail("no export " + std::to_string(index));
    const Export &e = package.exports()[size_t(index)];
    const std::string what = package.path(index + 1);
    out.className = e.classRef == 0 ? "Class" : package.objectName(e.classRef);
    const std::string cls = fs::toLower(out.className);
    if (!isScriptClass(cls))
        return Error::fail(what + ": a " + out.className + ", not a script object");
    std::vector<uint8_t> data;
    if (Error err = package.readExport(index, data))
        return err;
    Reader r(data, size_t(e.serialOffset));
    // Classes start with their fields; the rest with their own (empty)
    // properties.
    if (e.classRef != 0) {
        PropertyList props;
        if (Error err = beginObject(package, index, r, props))
            return err;
    }
    out.super = r.index();
    out.next = r.index();
    auto done = [&]() -> Error {
        if (!r.ok())
            return Error::fail(what + ": runs past the end");
        out.trailing = r.remaining();
        return Error::success();
    };
    if (cls == "const") {
        out.kind = ScriptObject::Kind::kConst;
        out.constValue = r.string(package.version());
        return done();
    }
    if (cls == "enum") {
        out.kind = ScriptObject::Kind::kEnum;
        const int32_t n = r.index();
        if (n < 0 || n > 256)
            return Error::fail(what + ": " + std::to_string(n) + " names");
        for (int32_t i = 0; i < n; ++i)
            out.enumNames.push_back(r.index());
        return done();
    }
    if (cls.size() > 8 && cls.compare(cls.size() - 8, 8, "property") == 0) {
        out.kind = ScriptObject::Kind::kProperty;
        out.arrayDim = r.i32();
        out.propertyFlags = r.u32();
        out.category = r.index();
        if (out.propertyFlags & propflag::kNet)
            out.repOffset = r.u16();
        if (cls == "byteproperty" || cls == "objectproperty" || cls == "structproperty" || cls == "arrayproperty" ||
            cls == "fixedarrayproperty" || cls == "classproperty")
            out.propertyRef = r.index();
        if (cls == "classproperty")
            out.metaClass = r.index();
        if (cls == "fixedarrayproperty")
            out.fixedCount = r.i32();
        return done();
    }
    // Structs, functions, states and classes.
    out.kind = cls == "function" ? ScriptObject::Kind::kFunction
               : cls == "state"  ? ScriptObject::Kind::kState
               : cls == "class"  ? ScriptObject::Kind::kClass
                                 : ScriptObject::Kind::kStruct;
    out.scriptText = r.index();
    out.children = r.index();
    out.friendlyName = r.index();
    out.line = r.i32();
    out.textPos = r.i32();
    const uint32_t scriptSize = r.u32();
    if (!r.ok())
        return Error::fail(what + ": runs past the end");
    if (Error err = decodeCode(package, r, scriptSize, out.code))
        return Error::fail(what + ": " + err.message);
    if (out.kind == ScriptObject::Kind::kFunction) {
        out.native = r.u16();
        out.operatorPrecedence = r.u8();
        out.functionFlags = r.u32();
        if (out.functionFlags & funcflag::kNet)
            out.repOffset = r.u16();
        return done();
    }
    if (out.kind == ScriptObject::Kind::kStruct)
        return done();
    out.probeMask = r.u64();
    out.ignoreMask = r.u64();
    out.labelTableOffset = r.u16();
    out.stateFlags = r.u32();
    if (out.kind == ScriptObject::Kind::kState)
        return done();
    out.classFlags = r.u32();
    r.bytes(out.guid.data(), out.guid.size());
    const int32_t deps = r.index();
    if (deps < 0 || deps > 4096)
        return Error::fail(what + ": " + std::to_string(deps) + " dependencies");
    for (int32_t i = 0; i < deps && r.ok(); ++i) {
        ScriptObject::Dependency d;
        d.cls = r.index();
        d.deep = r.u32();
        d.scriptTextCrc = r.u32();
        out.dependencies.push_back(d);
    }
    const int32_t imports = r.index();
    if (imports < 0 || imports > 4096)
        return Error::fail(what + ": " + std::to_string(imports) + " package imports");
    for (int32_t i = 0; i < imports && r.ok(); ++i)
        out.packageImports.push_back(r.index());
    out.within = r.index();
    out.configName = r.index();
    if (!r.ok())
        return Error::fail(what + ": runs past the end");
    if (Error err = readProperties(package, r, out.defaults))
        return Error::fail(what + ": its defaults: " + err.message);
    return done();
}

} // namespace reluva::unreal
