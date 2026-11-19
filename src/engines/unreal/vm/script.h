// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// UnrealScript as packages store it, compiled: classes and what they're
// made of (properties, functions, states, structs, enums, constants: the
// UField subclasses, chained through their Next references), and the
// functions' and states' bytecode, decoded into expression trees. See
// docs/formats/unreal/script.md.

#include "engines/unreal/formats/package.h"
#include "engines/unreal/formats/properties.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::unreal {

// Property flags (a property's PropertyFlags).
namespace propflag {
constexpr uint32_t kEdit = 0x1, kConst = 0x2, kInput = 0x4, kExportObject = 0x8, kOptionalParm = 0x10, kNet = 0x20,
                   kConstRef = 0x40, kParm = 0x80, kOutParm = 0x100, kSkipParm = 0x200, kReturnParm = 0x400,
                   kCoerceParm = 0x800, kNative = 0x1000, kTransient = 0x2000, kConfig = 0x4000, kLocalized = 0x8000,
                   kTravel = 0x10000, kEditConst = 0x20000, kGlobalConfig = 0x40000;
} // namespace propflag

// Function flags (a function's FunctionFlags).
namespace funcflag {
constexpr uint32_t kFinal = 0x1, kDefined = 0x2, kIterator = 0x4, kLatent = 0x8, kPreOperator = 0x10, kSingular = 0x20,
                   kNet = 0x40, kNetReliable = 0x80, kSimulated = 0x100, kExec = 0x200, kNative = 0x400, kEvent = 0x800,
                   kOperator = 0x1000, kStatic = 0x2000;
} // namespace funcflag

// The bytecode's tokens (the first byte of each expression).
namespace token {
enum : uint8_t {
    kLocalVariable = 0x00,
    kInstanceVariable = 0x01,
    kDefaultVariable = 0x02,
    kReturn = 0x04,
    kSwitch = 0x05,
    kJump = 0x06,
    kJumpIfNot = 0x07,
    kStop = 0x08,
    kAssert = 0x09,
    kCase = 0x0a,
    kNothing = 0x0b,
    kLabelTable = 0x0c,
    kGotoLabel = 0x0d,
    kEatString = 0x0e,
    kLet = 0x0f,
    kDynArrayElement = 0x10,
    kNew = 0x11,
    kClassContext = 0x12,
    kMetaCast = 0x13,
    kLetBool = 0x14,
    kEndFunctionParms = 0x16,
    kSelf = 0x17,
    kSkip = 0x18,
    kContext = 0x19,
    kArrayElement = 0x1a,
    kVirtualFunction = 0x1b,
    kFinalFunction = 0x1c,
    kIntConst = 0x1d,
    kFloatConst = 0x1e,
    kStringConst = 0x1f,
    kObjectConst = 0x20,
    kNameConst = 0x21,
    kRotationConst = 0x22,
    kVectorConst = 0x23,
    kByteConst = 0x24,
    kIntZero = 0x25,
    kIntOne = 0x26,
    kTrue = 0x27,
    kFalse = 0x28,
    kNativeParm = 0x29,
    kNoObject = 0x2a,
    kIntConstByte = 0x2c,
    kBoolVariable = 0x2d,
    kDynamicCast = 0x2e,
    kIterator = 0x2f,
    kIteratorPop = 0x30,
    kIteratorNext = 0x31,
    kStructCmpEq = 0x32,
    kStructCmpNe = 0x33,
    kUnicodeStringConst = 0x34,
    kStructMember = 0x36,
    kGlobalFunction = 0x38,
    kFirstConversion = 0x39,
    kLastConversion = 0x5f, // each one expression to convert
    kExtendedNative = 0x60, // 0x60-0x6f: native (token - 0x60) * 256 + the next byte
    kFirstNative = 0x70,    // 0x70-0xff: native function `token`
};
} // namespace token

// One expression of bytecode, its operands, and its subexpressions (by
// index into Code::exprs).
struct Expr {
    uint8_t token = 0;
    uint32_t offset = 0;   // where it starts, counted as the engine does in memory (see script.md)
    int native = -1;       // native calls: the native function's number
    int32_t ref = 0;       // an object reference, or a name index (functions called by name, name constants)
    int32_t value = 0;     // ints, bytes, jump targets, skips, line numbers
    uint8_t size = 0;      // Context, ClassContext, Switch: a byte operand
    float f[3] = {};       // float, vector constants
    int32_t rot[3] = {};   // rotator constants
    std::string text;      // string constants
    std::vector<int> args; // subexpressions, in order
    std::vector<std::pair<int32_t, uint32_t>> labels; // LabelTable: name index, code offset
};

struct Code {
    std::vector<Expr> exprs;
    std::vector<int> statements; // the top-level expressions, in order
    uint32_t size = 0;           // in memory
};

// Decodes `scriptSize` bytes of bytecode (as the engine counts them in
// memory) from `r`; fails if the tokens don't end exactly there.
Error decodeCode(const Package &package, Reader &r, uint32_t scriptSize, Code &out);

// A UField subclass's export, read: which kind it is and what it stores.
struct ScriptObject {
    enum class Kind { kConst, kEnum, kProperty, kStruct, kFunction, kState, kClass } kind = Kind::kConst;
    std::string className; // the export's class ("IntProperty", "Function"...)
    int32_t super = 0, next = 0;

    // Const: its value as written in the source.
    std::string constValue;
    // Enum: its names (name indices).
    std::vector<int32_t> enumNames;

    // Property.
    int32_t arrayDim = 1;
    uint32_t propertyFlags = 0;
    int32_t category = 0; // name index
    uint16_t repOffset = 0;
    // Byte: its enum; Object, Class: its class; Struct: its struct; Array:
    // its inner property; FixedArray: its inner property.
    int32_t propertyRef = 0;
    int32_t metaClass = 0; // Class: the classes it holds are this or below
    int32_t fixedCount = 0;

    // Struct and up.
    int32_t scriptText = 0, children = 0, friendlyName = 0;
    int32_t line = 0, textPos = 0;
    Code code;

    // Function.
    uint16_t native = 0;
    uint8_t operatorPrecedence = 0;
    uint32_t functionFlags = 0;

    // State and Class.
    uint64_t probeMask = 0, ignoreMask = 0;
    uint16_t labelTableOffset = 0;
    uint32_t stateFlags = 0;

    // Class.
    uint32_t classFlags = 0;
    std::array<uint8_t, 16> guid{};
    struct Dependency {
        int32_t cls = 0;
        uint32_t deep = 0, scriptTextCrc = 0;
    };
    std::vector<Dependency> dependencies;
    std::vector<int32_t> packageImports; // name indices
    int32_t within = 0;
    int32_t configName = 0; // name index
    PropertyList defaults;  // the class's default properties

    size_t trailing = 0; // bytes left after what was read (none expected)
};

// Whether an export's class is one of the UField subclasses above.
bool isScriptClass(std::string_view className);

Error readScriptObject(const Package &package, int exportIndex, ScriptObject &out);

} // namespace reluva::unreal
