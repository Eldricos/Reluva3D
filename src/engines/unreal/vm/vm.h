// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The UnrealScript virtual machine: classes loaded from their compiled
// script (script.h) into a runtime form, objects made from them with
// their properties' values, and an interpreter for the bytecode, with
// states, state code that waits (latent functions), timers and native
// functions written in C++ (vm_natives_*.cpp). See docs/engines/unreal/games/ds9fallen/systems/script-vm.md.
//
// Values are kept by property, not in raw memory as the original does: an
// object holds one Value per property element, a struct value its fields'
// Values. The layout is Reluva's own; scripts only see properties.

#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/vm/script.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reluva::unreal {

class ScriptLevel;
class Vm;
struct VmClass;
struct VmFunction;
struct VmObject;
struct NativeCall;
using NativeFn = std::function<void(NativeCall &)>;
struct VmState;
struct VmStruct;

/** A name, interned for the whole VM: names compare without case. */
using NameId = int32_t;

/** What a Value holds. */
enum class VType : uint8_t { kNone, kByte, kInt, kBool, kFloat, kName, kString, kObject, kStruct };

/** A value as scripts handle it: of a property, a variable, an expression. */
struct Value {
    VType type = VType::kNone;
    int32_t i = 0;                // Byte, Int, Bool (0 or 1), Name
    float f = 0;                  // Float
    VmObject *o = nullptr;        // Object; a class object for class values
    std::string s;                // String
    const VmStruct *st = nullptr; // Struct: which
    std::vector<Value> fields;    // Struct: one per field slot

    Value() = default;
    /**
     * Copied, the string and fields only if there are any: values are
     * copied all the time, and most are numbers.
     */
    Value(const Value &v) : type(v.type), i(v.i), f(v.f), o(v.o), st(v.st) {
        if (!v.s.empty())
            s = v.s;
        if (!v.fields.empty())
            fields = v.fields;
    }
    /** Copied as the constructor copies. */
    Value &operator=(const Value &v) {
        if (this == &v)
            return *this;
        type = v.type, i = v.i, f = v.f, o = v.o, st = v.st;
        if (v.s.empty())
            s.clear();
        else
            s = v.s;
        if (v.fields.empty())
            fields.clear();
        else
            fields = v.fields;
        return *this;
    }
    /** Moved as they are. */
    ///@{
    Value(Value &&) noexcept = default;
    Value &operator=(Value &&) noexcept = default;
    ///@}

    /** A value of that type (a byte kept to its low 8 bits). */
    ///@{
    static Value byteValue(int v);
    static Value intValue(int32_t v);
    static Value boolValue(bool v);
    static Value floatValue(float v);
    static Value nameValue(NameId v);
    static Value stringValue(std::string v);
    static Value objectValue(VmObject *v);
    ///@}

    /**
     * Read as a type, converting as UnrealScript does (None reads as 0,
     * false, "" or None; so does a deleted object). Inline for numbers and
     * objects (read all the time); a string's reading, and the rest, out
     * of line. (Byte to Name takes in Float, so it's tested first.)
     */
    ///@{
    int32_t asInt() const {
        return type == VType::kFloat ? int32_t(f) : (type >= VType::kByte && type <= VType::kName) ? i : asIntSlow();
    }
    float asFloat() const {
        return type == VType::kFloat ? f : (type >= VType::kByte && type <= VType::kBool) ? float(i) : asFloatSlow();
    }
    bool asBool() const {
        return type == VType::kFloat ? f != 0 : (type >= VType::kByte && type <= VType::kName) ? i != 0 : asBoolSlow();
    }
    inline VmObject *asObject() const;
    ///@}
    /** The out-of-line readings: a string's number, or 0, false for the rest. */
    ///@{
    int32_t asIntSlow() const;
    float asFloatSlow() const;
    bool asBoolSlow() const;
    ///@}
};

/** A property of a class or struct, or a function's parameter or local. */
struct VmProperty {
    std::string name;
    NameId nameId = 0;
    VType type = VType::kNone;
    bool isClass = false;           // a ClassProperty: holds classes
    VmClass *objectClass = nullptr; // Object: the class of what it holds; Class: the meta class
    const VmStruct *structType = nullptr;
    const std::vector<NameId> *enumNames = nullptr; // Byte with an enum
    int arrayDim = 1;
    uint32_t flags = 0; // propflag::
    int slot = 0;       // its first element's index in its owner's values

    /** Its type's empty value (a struct's: its defaults). */
    Value zero() const;
};

/**
 * A struct, and what's built on one: a function, a state, a class. Its
 * properties' values are kept by slot.
 */
struct VmStruct {
    /** Which it is (and so which subclass). */
    enum class Kind { kStruct, kFunction, kState, kClass } kind = Kind::kStruct;
    std::string name;
    NameId nameId = 0;
    const Package *package = nullptr; // where it's defined; its code's references are into this
    int exportIndex = -1;
    VmStruct *super = nullptr;
    std::vector<VmProperty *> properties; // inherited ones first, in slot order
    int slots = 0;
    std::vector<Value> defaults; // one per slot
    Code code;
    std::unordered_map<uint32_t, int> statementAt; // code offset -> statement index
    /**
     * What each expression's reference resolved to (a property, function,
     * class...), filled in as the code runs.
     */
    mutable std::vector<const void *> resolved;

    virtual ~VmStruct() = default;
    /** Its property of that name, inherited or its own (the most derived); nullptr if none. */
    VmProperty *findProperty(NameId name) const;

private:
    // findProperty's index by name, the most derived of each; built again
    // when properties are added.
    mutable std::unordered_map<NameId, VmProperty *> _byName;
    mutable size_t _indexed = 0;
};

/** A function: its parameters, and its code or its native. */
struct VmFunction : VmStruct {
    uint32_t flags = 0; // funcflag::
    int native = 0;
    std::string symbol;        // an operator's ("+=")
    VmStruct *outer = nullptr; // the class or state it's in
    std::vector<VmProperty *> params;
    VmProperty *returnValue = nullptr;
    std::string qualified; // "Class.Name", for natives and messages
    // Its native's code, found once (again if natives are registered
    // after: the VM's _nativeGeneration).
    mutable const NativeFn *nativeFn = nullptr;
    mutable int nativeGeneration = -1;
    // Found once: whether it's && or || (their right side may be left
    // unevaluated: 1 and, 2 or, 0 neither) and has out parameters.
    mutable int8_t logicOp = -1;
    mutable bool hasOut = false;
    /**
     * Found once: a script function that only returns, with nothing to
     * give back (Actor's C3DPreRender, its empty events): not run.
     */
    mutable int8_t doesNothing = -1;
};

/** A state: its own functions and its state code. */
struct VmState : VmStruct {
    VmClass *owner = nullptr;
    std::map<NameId, VmFunction *> functions; // its own
    std::map<NameId, uint32_t> labels;        // state code labels
    uint32_t stateFlags = 0;
    // the events it (or its class) has, and those it doesn't ignore
    uint64_t probeMask = 0;
    uint64_t ignoreMask = ~0ull;
};

/** A class: a state (its code outside states) with states of its own. */
struct VmClass : VmState {
    /** The class it extends; null for Object. */
    VmClass *parent() const { return static_cast<VmClass *>(super); }
    std::map<NameId, VmState *> states; // its own
    uint32_t classFlags = 0;
    VmObject *object = nullptr; // the class as an object (class values point here)
    /** Whether it's `other` or a subclass of it. */
    bool isA(const VmClass *other) const;
};

/** An object: its class, its properties' values, and its state. */
struct VmObject {
    VmClass *cls = nullptr;
    std::vector<Value> values; // one per slot of cls
    NameId name = 0;
    VmClass *asClass = nullptr; // this object is a class
    // An object loaded from a package that the VM doesn't run (a texture,
    // a sound, a mesh): where it is.
    const Package *package = nullptr;
    int exportIndex = -1;
    bool deleted = false;
    bool isActor = false;

    // Its state and state code.
    VmState *state = nullptr;
    VmState *codeState = nullptr; // whose state code runs (its state's, or a parent state's it inherits)
    int codeAt = -1;              // the next statement of that code to run; -1 none
    float sleeping = 0;           // seconds a latent Sleep has left
    bool latent = false;          // waiting in a latent function
    int stateSerial = 0;          // counts state changes, so running state code sees one
    std::set<NameId> disabled;    // events Disable()d (Tick, Timer...)
    float timerRate = 0;
    float timerCount = 0;
    bool timerLoop = false;

    /** Its value of the property of that name (any case); nullptr if it has none. */
    Value *find(Vm &vm, std::string_view property);
    /**
     * A property named by a string literal: its slot found once per class
     * and remembered by the literal's address (the per-frame readings).
     */
    template<size_t N>
    Value *find(Vm &vm, const char (&property)[N]) {
        return findLiteral(vm, property, N - 1);
    }
    /** find() for a string literal: `property` must stay where it is for the VM's life. */
    Value *findLiteral(Vm &vm, const char *property, size_t length);
};

inline VmObject *Value::asObject() const {
    return type == VType::kObject && o && !o->deleted ? o : nullptr;
}

/**
 * A native function's call: its arguments (by parameter, converted to
 * their types; out parameters are written back after), what it returns.
 */
struct NativeCall {
    Vm &vm;
    VmObject *self;
    const VmFunction *function;
    std::vector<Value> args;
    std::vector<bool> given; // whether each argument was given (optional ones)
    Value result;
    float latentSeconds = -1;  // >= 0: the call waits this long (a latent function)
    std::vector<Value> items;  // an iterator's results, put in its first out parameter in turn
    std::vector<Value> items2; // and its second's, item by item, if it has one

    /** Argument `k` read as a type; 0, false, "" or nullptr past the last. */
    ///@{
    int32_t i(size_t k) const { return k < args.size() ? args[k].asInt() : 0; }
    float f(size_t k) const { return k < args.size() ? args[k].asFloat() : 0; }
    bool b(size_t k) const { return k < args.size() && args[k].asBool(); }
    const std::string &s(size_t k) const;
    VmObject *o(size_t k) const { return k < args.size() ? args[k].asObject() : nullptr; }
    ///@}
};
/**
 * The UnrealScript VM over a game's packages: the classes it loads, the
 * objects it makes, and the code it runs.
 */
class Vm {
public:
    /** Core's natives registered; classes load as they're asked for. `packages` must outlive it. */
    explicit Vm(const PackageSet &packages);
    ~Vm();
    Vm(const Vm &) = delete;
    Vm &operator=(const Vm &) = delete;

    /** The packages it loads from. */
    const PackageSet &packages() const { return _packages; }

    /** A name's id, added if it's new; any case finds the same one ("None" is 0). */
    NameId name(std::string_view text);
    /** A name's text, as first spelled; "None" for an id that isn't one. */
    const std::string &nameText(NameId id) const;

    /**
     * A class by "Package.Class" or by name alone (the first package
     * defining it); nullptr if not found or it doesn't load.
     */
    VmClass *findClass(std::string_view name);
    /** The class, function, state, struct or property an export is, loaded once; nullptr if it's none. */
    ///@{
    VmClass *classAt(const Package &package, int exportIndex);
    VmStruct *structAt(const Package &package, int exportIndex);
    VmProperty *propertyAt(const Package &package, int exportIndex);
    ///@}
    /**
     * The object a reference in `package` names: a class, an object made
     * for a level (registered with adopt()), or one standing for a
     * package's object the VM doesn't run (a texture...).
     */
    VmObject *objectAt(const Package &package, int32_t ref);
    /**
     * An object of a class, its properties its class's defaults; its name
     * the class's and a number if none is given. Its Outer, Name and Class
     * (Object's own properties) are set.
     */
    VmObject *create(VmClass *cls, NameId name = 0, VmObject *outer = nullptr);
    /** Registers an object as the one a package's export is (level actors). */
    void adopt(const Package &package, int exportIndex, VmObject *object);
    /** Sets an object's properties from tagged properties read in `package`. */
    void applyProperties(VmObject *object, const Package &package, const PropertyList &props);
    /** A tagged property's value as a property's type. */
    Value valueOf(const VmProperty &p, const Package &package, const Property &data);
    /** Every object it has made, deleted ones too. */
    const std::vector<std::unique_ptr<VmObject>> &objects() const { return _objects; }

    /**
     * Calls a function by name on an object (an event the engine sends):
     * its state's version first, then its class's. Returns its result, or
     * an empty value if it has none.
     */
    Value callEvent(VmObject *self, std::string_view function, std::vector<Value> args = {});
    /**
     * The function a call by name on an object runs: its state's (unless
     * `global`), else its class's or a parent's; for a class object, only
     * the class's or a parent's. nullptr if there's none.
     */
    VmFunction *findFunction(VmObject *self, NameId name, bool global = false);
    /**
     * Runs a function on an object, its arguments converted to its
     * parameters' types (those not given their empty values); `outs`, by
     * parameter, get the out parameters' values back. Returns its result.
     */
    Value invoke(VmFunction *f, VmObject *self, std::vector<Value> args, std::vector<Value *> outs = {});

    /**
     * States: sets an object's state (EndState, BeginState) and its state
     * code to the label (Begin by default).
     */
    void gotoState(VmObject *self, NameId state, NameId label = 0);
    /**
     * Runs an object's state code until it waits or stops, its timer, and
     * (unless disabled) its Tick event.
     */
    void tick(VmObject *self, float seconds, bool sendTick = true);

    /**
     * A class the engine defines in C++ only, with no script (Viewport):
     * made up under its parent so scripts can cast to it and objects of it
     * can be made. Found by name when a reference to it doesn't resolve.
     */
    VmClass *defineIntrinsic(std::string_view name, VmClass *parent);
    /** An intrinsic class defined already, or nullptr. */
    VmClass *intrinsic(std::string_view name);
    /**
     * A class the engine has with no script (Mesh, LodMesh, Sound...),
     * defined as an intrinsic if need be, under its parent as UE1 has it.
     */
    VmClass *nativeClass(std::string_view className);

    /**
     * A native function's code, by "Class.Function" (any case); registering
     * one again replaces it.
     */
    ///@{
    void registerNative(std::string_view qualifiedName, NativeFn fn);
    bool hasNative(std::string_view qualifiedName) const { return findNative(qualifiedName) != nullptr; }
    const NativeFn *findNative(std::string_view qualifiedName) const;
    ///@}

    /** Core's structs, and values of them. */
    const VmStruct *coreStruct(std::string_view name);
    /** A Vector value. */
    Value vectorValue(float x, float y, float z);
    /** A vector value set in place (no new value made, if it's a vector). */
    void setVector(Value &v, float x, float y, float z);
    /** A Rotator value. */
    Value rotatorValue(int32_t pitch, int32_t yaw, int32_t roll);
    /** A Vector's or Rotator's fields; zeros for a value with fewer than three. */
    ///@{
    static std::array<float, 3> vectorOf(const Value &v);
    static std::array<int32_t, 3> rotatorOf(const Value &v);
    ///@}
    /**
     * Finds an object by its path ("Package.Group.Name"), for
     * DynamicLoadObject.
     */
    VmObject *findObject(std::string_view path, const VmClass *of = nullptr);
    /** A class's config properties from the .ini files (Vm::applyConfig). */
    void applyConfig(VmClass &cls);
    /** A class's localized properties from the .int files (Vm::applyLocalized). */
    void applyLocalized(VmClass &cls);
    /** `classesToo`: Classes.int as well, where the package's own hasn't it. */
    std::optional<std::string> localized(const std::string &package, const std::string &section, const std::string &key,
                                         bool classesToo = true);
    /**
     * A level's actor's localized properties from the .int its FileName
     * names, in the section its TextInfo does (The Fallen's). How many
     * were set.
     */
    int applyLocalizedActor(VmObject *actor, const std::string &map);
    /**
     * A setting: one set while running first, then User.ini's,
     * DefUser.ini's and the game's .ini's. nullptr if none has it.
     */
    const std::string *configSetting(const std::string &section, const std::string &key);
    /**
     * Changes a setting for as long as the VM lasts (a console command's
     * "set"); the files aren't written.
     */
    void setConfig(const std::string &section, const std::string &key, const std::string &value);
    /**
     * A property's value from its text as the .ini files and console
     * commands write it ("True", "0.5", an enum's name, an object's path);
     * structs and arrays are left as they are.
     */
    void setFromText(const VmProperty &prop, Value &v, const std::string &text);
    /** An object's name or a class's, as scripts see it. */
    std::string objectName(const VmObject *o) const;
    /** What string() of an object gives: its path from its package down ("Package.Name"). */
    std::string pathName(const VmObject *o);

    /** Converts a value to a property's type (assignment). */
    Value convert(const Value &v, const VmProperty &to) const;
    /** Sets `dst` to `src` converted to `dst`'s type (a struct only from one of its size). */
    void assign(Value &dst, const Value &src) const;
    /** Whether two values are equal as UnrealScript's == has them (by the first's type, if it has one). */
    bool equal(const Value &a, const Value &b) const;
    /** A value as string() gives it (floats to two places). */
    std::string toString(const Value &v) const;
    /** Whether an object's class, or a parent of it, has that name. */
    bool isA(const VmObject *o, std::string_view className);

    /** What happened, for diagnostics. */
    struct Stats {
        uint64_t calls = 0;
        uint64_t statements = 0;
        uint64_t nativeCalls = 0;
        int accessedNone = 0;
        std::map<std::string, int> missingNatives; // "Class.Function" -> calls
        std::map<std::string, int> errors;         // message -> times
        std::map<std::string, int> noneAt;         // Accessed None by function and code offset -> times
    };
    Stats stats;
    std::function<void(const std::string &)> logger; // Log() and warnings

    /** The level running, for the natives that need the world. */
    ScriptLevel *level = nullptr;
    /**
     * Events aren't passed to scripts while set: the engine's are dropped
     * until the level has begun play (LevelInfo.bBegunPlay).
     */
    bool holdEvents = false;
    /** Counts a script error in `stats`, logging it the first time. */
    void reportError(const std::string &what);
    /**
     * Whether the script function a native was called from is declared
     * simulated (not state code).
     */
    bool callerSimulated() const;
    /** Where scripts are running: " in Class.Function", for natives' errors. */
    std::string where() const;
    /** A state of a class or its parents, by name; null if none. */
    VmState *findState(VmClass *cls, NameId name) const;
    /**
     * How many objects of each class have been given made-up names
     * ("DS9_Hud0"...): a saved game's carry on from where it left off.
     */
    std::map<const VmClass *, int> &nameCounts() { return _nameCounts; }

private:
    friend class Interpreter;
    void loadStruct(VmStruct &s, const ScriptObject &so);
    VmFunction *loadFunction(const Package &package, int exportIndex, VmStruct *outer);
    VmState *loadState(const Package &package, int exportIndex, VmClass *owner);
    void addProperty(VmStruct &s, const Package &package, int exportIndex, const ScriptObject &so);
    void prepareCode(VmStruct &s);
    VmObject *classObject(VmClass *cls);
    VmFunction *nativeFunction(int number);
    void scanNatives();
    Value readStruct(const VmStruct &st, const Package &package, Reader &r);
    Value readValue(const VmProperty &p, const Package &package, Reader &r);
    float _pendingLatent = -1;
    std::vector<const VmStruct *> _running; // the code running, innermost last
    std::map<int, VmFunction *> _nativeFunctions;
    bool _nativesScanned = false;
    std::map<NameId, const VmStruct *> _coreStructs;
    std::map<const VmClass *, int> _nameCounts;
    std::map<NameId, std::unique_ptr<VmClass>> _intrinsics;

    const PackageSet &_packages;
    std::vector<std::string> _names;
    // By lower-case text, looked up by a string_view without a copy.
    struct NameHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };
    std::unordered_map<std::string, NameId, NameHash, std::equal_to<>> _nameIds;

public:
    /** VmObject::find's literals: (class, the literal's address) to a slot, -1 none. */
    struct LiteralKey {
        const VmClass *cls;
        const char *text;
        /** Same class and literal. */
        bool operator==(const LiteralKey &o) const { return cls == o.cls && text == o.text; }
    };
    /** LiteralKey's hash. */
    struct LiteralHash {
        /** The two addresses' hashes mixed. */
        size_t operator()(const LiteralKey &k) const {
            return std::hash<const void *>()(k.cls) * 31 ^ std::hash<const void *>()(k.text);
        }
    };
    std::unordered_map<LiteralKey, int, LiteralHash> literalSlots;

private:
    std::map<std::pair<const Package *, int>, std::unique_ptr<VmStruct>> _structs;
    using IniFile = std::map<std::string, std::map<std::string, std::string>>; // section, key (lower case): value
    std::map<std::string, IniFile> _iniFiles;
    IniFile _configSet; // settings changed while running (setConfig)
    const IniFile &iniFileAt(const std::filesystem::path &file);
    std::map<std::pair<const Package *, int>, VmProperty *> _properties;
    std::deque<VmProperty> _propertyStore;
    std::deque<std::vector<NameId>> _enumStore;
    std::map<std::pair<const Package *, int>, VmObject *> _exportObjects;
    std::vector<std::unique_ptr<VmObject>> _objects;

    std::map<std::string, NativeFn> _natives;
    int _nativeGeneration = 0;
    const VmStruct *_vectorStruct = nullptr;
    const VmStruct *_rotatorStruct = nullptr;
    int _depth = 0;
};

/** The natives of Core's Object (operators, maths, strings, states). */
void registerCoreNatives(Vm &vm);

} // namespace reluva::unreal
