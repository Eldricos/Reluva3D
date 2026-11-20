// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/vm/vm.h"

#include "engines/unreal/vm/script.h"

#include "common/fs.h"

#include <algorithm>
#include <optional>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace reluva::unreal {

void Fields::release() {
    delete _v;
}

namespace {

constexpr int kMaxDepth = 250;                  // nested calls before a script is stopped
constexpr uint64_t kMaxStatements = 2000000;    // in one run of a function
constexpr uint64_t kMaxStateStatements = 20000; // in one tick of state code
constexpr float kPi = 3.14159265358979f;

std::string lower(std::string_view s) {
    return fs::toLower(std::string(s));
}

VType typeOfProperty(std::string_view cls) {
    const std::string c = lower(cls);
    if (c == "byteproperty")
        return VType::kByte;
    if (c == "intproperty")
        return VType::kInt;
    if (c == "boolproperty")
        return VType::kBool;
    if (c == "floatproperty")
        return VType::kFloat;
    if (c == "nameproperty")
        return VType::kName;
    if (c == "strproperty" || c == "stringproperty")
        return VType::kString;
    if (c == "objectproperty" || c == "classproperty")
        return VType::kObject;
    if (c == "structproperty")
        return VType::kStruct;
    return VType::kNone; // dynamic arrays, maps: not supported
}

} // namespace

// --- Values --------------------------------------------------------------------------

Value Value::byteValue(int v) {
    Value x;
    x.type = VType::kByte;
    x.i = v & 0xff;
    return x;
}
Value Value::intValue(int32_t v) {
    Value x;
    x.type = VType::kInt;
    x.i = v;
    return x;
}
Value Value::boolValue(bool v) {
    Value x;
    x.type = VType::kBool;
    x.i = v ? 1 : 0;
    return x;
}
Value Value::floatValue(float v) {
    Value x;
    x.type = VType::kFloat;
    x.f = v;
    return x;
}
Value Value::nameValue(NameId v) {
    Value x;
    x.type = VType::kName;
    x.i = v;
    return x;
}
Value Value::stringValue(std::string v) {
    Value x;
    x.type = VType::kString;
    x.s = std::move(v);
    return x;
}
Value Value::objectValue(VmObject *v) {
    Value x;
    x.type = VType::kObject;
    x.o = v;
    return x;
}

int32_t Value::asIntSlow() const {
    switch (type) {
    case VType::kFloat:
        return int32_t(f);
    case VType::kByte:
    case VType::kInt:
    case VType::kBool:
    case VType::kName:
        return i;
    case VType::kString:
        return std::atoi(s.c_str());
    default:
        return 0;
    }
}
float Value::asFloatSlow() const {
    switch (type) {
    case VType::kFloat:
        return f;
    case VType::kByte:
    case VType::kInt:
    case VType::kBool:
        return float(i);
    case VType::kString:
        return float(std::atof(s.c_str()));
    default:
        return 0;
    }
}
bool Value::asBoolSlow() const {
    switch (type) {
    case VType::kFloat:
        return f != 0;
    case VType::kObject:
        return asObject() != nullptr;
    case VType::kString:
        return !s.empty() && lower(s) != "false" && s != "0";
    case VType::kNone:
    case VType::kStruct:
        return false;
    default:
        return i != 0;
    }
}

Value VmProperty::zero() const {
    Value v;
    v.type = type;
    if (type == VType::kStruct && structType) {
        v.st = structType;
        v.fields = structType->defaults;
    }
    return v;
}

VmProperty *VmStruct::findProperty(NameId name) const {
    // The most derived wins: a subclass can declare a property of the
    // same name again (The Fallen redeclares SoundVolume and others).
    if (_indexed != properties.size()) {
        _byName.clear();
        for (VmProperty *p : properties)
            _byName[p->nameId] = p;
        _indexed = properties.size();
    }
    auto it = _byName.find(name);
    return it != _byName.end() ? it->second : nullptr;
}

bool VmClass::isA(const VmClass *other) const {
    for (const VmClass *c = this; c; c = c->parent())
        if (c == other)
            return true;
    return false;
}

Value *VmObject::find(Vm &vm, std::string_view property) {
    if (!cls)
        return nullptr;
    VmProperty *p = cls->findProperty(vm.name(property));
    return p && size_t(p->slot) < values.size() ? &values[size_t(p->slot)] : nullptr;
}

Value *VmObject::findLiteral(Vm &vm, const char *property, size_t length) {
    if (!cls)
        return nullptr;
    const Vm::LiteralKey key{cls, property};
    auto it = vm.literalSlots.find(key);
    if (it == vm.literalSlots.end()) {
        VmProperty *p = cls->findProperty(vm.name(std::string_view(property, length)));
        it = vm.literalSlots.emplace(key, p ? p->slot : -1).first;
    }
    return it->second >= 0 && size_t(it->second) < values.size() ? &values[size_t(it->second)] : nullptr;
}

const std::string &NativeCall::s(size_t k) const {
    static const std::string kEmpty;
    return k < args.size() ? args[k].s : kEmpty;
}

// --- The VM: names and loading ---------------------------------------------------------

Vm::Vm(const PackageSet &packages) : _packages(packages) {
    name("None"); // id 0
    registerCoreNatives(*this);
}

Vm::~Vm() = default;

NameId Vm::name(std::string_view text) {
    // Lower-cased on the stack: names are looked up many times a tick.
    char buffer[128];
    std::string key;
    std::string_view lowered;
    if (text.size() <= sizeof buffer) {
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            buffer[i] = c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;
        }
        lowered = std::string_view(buffer, text.size());
    } else {
        key = lower(text);
        lowered = key;
    }
    auto it = _nameIds.find(lowered);
    if (it != _nameIds.end())
        return it->second;
    key = std::string(lowered);
    const NameId id = NameId(_names.size());
    _names.emplace_back(text);
    _nameIds.emplace(std::move(key), id);
    return id;
}

const std::string &Vm::nameText(NameId id) const {
    static const std::string kNone = "None";
    return id >= 0 && size_t(id) < _names.size() ? _names[size_t(id)] : kNone;
}

void Vm::reportError(const std::string &what) {
    int &n = stats.errors[what];
    if (n++ == 0 && logger)
        logger("Script error: " + what);
}

VmClass *Vm::findClass(std::string_view qualified) {
    const size_t dot = qualified.rfind('.');
    const std::string pkgName(dot == std::string_view::npos ? std::string_view() : qualified.substr(0, dot));
    const std::string cls = lower(dot == std::string_view::npos ? qualified : qualified.substr(dot + 1));
    for (const std::string &n : _packages.names()) {
        if (!pkgName.empty() && lower(n) != lower(pkgName))
            continue;
        const Package *p = _packages.get(n);
        if (!p)
            continue;
        for (size_t i = 0; i < p->exports().size(); ++i)
            if (p->exports()[i].classRef == 0 && lower(p->objectName(int32_t(i) + 1)) == cls)
                return classAt(*p, int(i));
    }
    return nullptr;
}

VmObject *Vm::findObject(std::string_view path, const VmClass *of) {
    // "Package.Name" or "Package.Group.Name"; with `of`, the one of that
    // class (a package can name a class and a mesh alike: PLYR.DS9_Sisko).
    const size_t dot = path.find('.');
    if (dot == std::string_view::npos)
        return nullptr;
    const Package *p = _packages.get(std::string(path.substr(0, dot)));
    if (!p)
        return nullptr;
    const std::string rest = lower(path.substr(dot + 1));
    for (size_t i = 0; i < p->exports().size(); ++i)
        if (lower(p->path(int32_t(i) + 1)) == rest) {
            VmObject *o = objectAt(*p, int32_t(i) + 1);
            if (!of || (o && (o->asClass ? lower(of->name) == "class" : o->cls && o->cls->isA(of))))
                return o;
        }
    return nullptr;
}

// As UnrealScript's string() of an object gives it: its path, from its
// package down ("M01_KiraL1A.LevelInfo0", "Engine.Actor", "DS9_Objectives.M01_KL1_LibKey"):
// a package's object by its place in it, a class by its package, anything
// else by its Outer's path. A level's actors take the level's package for
// their outer, as the game's scripts read it (GetMissionLevel takes the
// mission's number from string(Level)).
std::string Vm::pathName(const VmObject *o) {
    if (!o)
        return "None";
    if (o->asClass)
        return o->asClass->package ? o->asClass->package->name() + "." + o->asClass->name : o->asClass->name;
    if (o->package && o->exportIndex >= 0)
        return o->package->name() + "." + o->package->path(o->exportIndex + 1);
    const Value *outerValue = const_cast<VmObject *>(o)->find(*this, "Outer");
    const VmObject *outer = outerValue ? outerValue->asObject() : nullptr;
    if (!outer || outer == o)
        return nameText(o->name);
    if (outer->package && outer->exportIndex >= 0 && outer->cls && lower(outer->cls->name) == "level")
        return outer->package->name() + "." + nameText(o->name);
    return pathName(outer) + "." + nameText(o->name);
}

std::string Vm::objectName(const VmObject *o) const {
    if (!o)
        return "None";
    if (o->asClass)
        return o->asClass->name;
    return nameText(o->name);
}

VmClass *Vm::classAt(const Package &package, int exportIndex) {
    VmStruct *s = structAt(package, exportIndex);
    return s && s->kind == VmStruct::Kind::kClass ? static_cast<VmClass *>(s) : nullptr;
}

VmStruct *Vm::structAt(const Package &package, int exportIndex) {
    const auto key = std::make_pair(&package, exportIndex);
    if (auto it = _structs.find(key); it != _structs.end())
        return it->second.get();
    if (exportIndex < 0 || size_t(exportIndex) >= package.exports().size())
        return nullptr;
    const Export &e = package.exports()[size_t(exportIndex)];
    if (e.classRef != 0) {
        // A function, state or struct: loaded with the class it's in.
        const std::string cls = lower(package.objectName(e.classRef));
        if (cls != "function" && cls != "state" && cls != "struct")
            return nullptr;
        if (cls == "struct") {
            ScriptObject so;
            if (Error err = readScriptObject(package, exportIndex, so)) {
                reportError(err.message);
                return nullptr;
            }
            auto owned = std::make_unique<VmStruct>();
            VmStruct *st = owned.get();
            st->kind = VmStruct::Kind::kStruct;
            st->name = package.objectName(exportIndex + 1);
            st->nameId = name(st->name);
            st->package = &package;
            st->exportIndex = exportIndex;
            _structs.emplace(key, std::move(owned));
            if (e.superRef) {
                const ObjectRef sup = _packages.resolve(package, e.superRef);
                if (sup)
                    st->super = structAt(*sup.package, sup.exportIndex);
                if (st->super) {
                    st->properties = st->super->properties;
                    st->slots = st->super->slots;
                    st->defaults = st->super->defaults;
                }
            }
            loadStruct(*st, so);
            if (lower(package.name()) == "core")
                _coreStructs.emplace(st->nameId, st);
            return st;
        }
        // Up the outers to the class, then look again.
        int32_t outer = e.package;
        for (int guard = 0; guard < 16 && outer > 0; ++guard) {
            const Export &o = package.exports()[size_t(outer - 1)];
            if (o.classRef == 0) {
                classAt(package, outer - 1);
                break;
            }
            outer = o.package;
        }
        auto it = _structs.find(key);
        return it == _structs.end() ? nullptr : it->second.get();
    }

    // A class.
    ScriptObject so;
    if (Error err = readScriptObject(package, exportIndex, so)) {
        reportError(err.message);
        return nullptr;
    }
    auto owned = std::make_unique<VmClass>();
    VmClass *c = owned.get();
    c->kind = VmStruct::Kind::kClass;
    c->name = package.objectName(exportIndex + 1);
    c->nameId = name(c->name);
    c->package = &package;
    c->exportIndex = exportIndex;
    c->owner = c;
    c->classFlags = so.classFlags;
    if (so.configName)
        c->configName = package.name(so.configName);
    _structs.emplace(key, std::move(owned));
    if (e.superRef) {
        const ObjectRef sup = _packages.resolve(package, e.superRef);
        VmClass *parent = sup ? classAt(*sup.package, sup.exportIndex) : nullptr;
        c->super = parent;
        if (parent) {
            if (c->configName.empty())
                c->configName = parent->configName;
            c->properties = parent->properties;
            c->slots = parent->slots;
            c->defaults = parent->defaults;
        }
    }
    loadStruct(*c, so);
    // Its defaults over its parent's.
    for (const Property &p : so.defaults.items) {
        VmProperty *prop = c->findProperty(name(p.name));
        if (!prop) {
            reportError(c->name + "'s defaults: no property " + p.name);
            continue;
        }
        const int at = prop->slot + p.arrayIndex;
        if (p.arrayIndex < 0 || p.arrayIndex >= prop->arrayDim)
            continue;
        c->defaults[size_t(at)] = valueOf(*prop, package, p);
    }
    applyLocalized(*c);
    applyConfig(*c);
    classObject(c);
    return c;
}

// The settings the player's and the game's .ini files hold for a class's
// config properties, over its defaults: a config property from the class's
// own section ([Engine.HUD]), a globalconfig one from the section of the
// class that declares it; User.ini first, then DefUser.ini, then the
// game's own .ini (DS9.ini). An array's elements as Key[n]; structs aren't
// read.
void Vm::applyConfig(VmClass &c) {
    if (!c.package)
        return;
    const std::string own = c.package->name() + "." + c.name;
    for (const VmProperty *prop : c.properties) {
        if (!(prop->flags & (propflag::kConfig | propflag::kGlobalConfig)) || prop->type == VType::kStruct)
            continue;
        std::string section = own;
        if (prop->flags & propflag::kGlobalConfig)
            for (const VmClass *up = &c; up; up = up->parent())
                if (std::find(up->properties.begin(), up->properties.end(), prop) != up->properties.end() &&
                    up->package)
                    section = up->package->name() + "." + up->name;
        for (int k = 0; k < prop->arrayDim; ++k) {
            const std::string key = prop->arrayDim > 1 ? prop->name + "[" + std::to_string(k) + "]" : prop->name;
            if (const std::string *text = configSetting(section, key))
                setFromText(*prop, c.defaults[size_t(prop->slot + k)], *text);
        }
    }
}

// A property's value from its text as the .ini files and console commands
// write it ("True", "0.5", an enum's name, an object's path); structs and
// arrays are left as they are.
void Vm::setFromText(const VmProperty &prop, Value &v, const std::string &text) {
    switch (prop.type) {
    case VType::kBool:
        v = Value::boolValue(lower(text) == "true" || text == "1");
        break;
    case VType::kInt:
        v = Value::intValue(std::atoi(text.c_str()));
        break;
    case VType::kFloat:
        v = Value::floatValue(std::strtof(text.c_str(), nullptr));
        break;
    case VType::kName:
        v = Value::nameValue(name(text));
        break;
    case VType::kString:
        v = Value::stringValue(text);
        break;
    case VType::kByte: {
        int b = std::atoi(text.c_str());
        if (prop.enumNames)
            for (size_t e = 0; e < prop.enumNames->size(); ++e)
                if (lower(nameText((*prop.enumNames)[e])) == lower(text))
                    b = int(e);
        v = Value::byteValue(b);
        break;
    }
    case VType::kObject:
        if (VmObject *o = findObject(text))
            v = Value::objectValue(o);
        break;
    default:
        break;
    }
}

// A setting changed while running (a console command's "set"): it comes
// before the .ini files' for as long as this VM lasts.
void Vm::setConfig(const std::string &section, const std::string &key, const std::string &value) {
    _configSet[lower(section)][lower(key)] = value;
}

// A class's config file, as the engine names them: "System" is the game's
// own .ini (DS9.ini), anything else that name's (User.ini). Written in the
// config folder, so the game's install is left as it is.
std::filesystem::path Vm::configFile(const std::string &configName) const {
    if (_configFolder.empty())
        return {};
    if ((configName.empty() || lower(configName) == "system") && !_packages.iniFile().empty())
        return _configFolder / _packages.iniFile().filename();
    if (configName.empty())
        return _configFolder / "System.ini";
    return _configFolder / (configName + ".ini");
}

// A value as SaveConfig writes it: a float with six places, a byte of an
// enum by the enum's name, the rest as scripts see them.
std::string Vm::configText(const VmProperty &prop, const Value &v) const {
    if (v.type == VType::kFloat) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%f", double(v.f));
        return buf;
    }
    if (v.type == VType::kByte && prop.enumNames && size_t(v.i) < prop.enumNames->size())
        return nameText((*prop.enumNames)[size_t(v.i)]);
    return toString(v);
}

void Vm::saveConfig(const VmClass &cls, const std::vector<Value> &values) {
    const std::filesystem::path file = configFile(cls.configName);
    if (file.empty() || !cls.package)
        return;
    std::vector<std::array<std::string, 3>> settings; // section, key, value
    for (const VmProperty *prop : cls.properties) {
        if (!(prop->flags & (propflag::kConfig | propflag::kGlobalConfig)) || prop->type == VType::kStruct)
            continue;
        // As applyConfig reads it: the class's own section, or a
        // globalconfig property's declaring class's.
        std::string section = cls.package->name() + "." + cls.name;
        if (prop->flags & propflag::kGlobalConfig)
            for (const VmClass *up = &cls; up; up = up->parent())
                if (std::find(up->properties.begin(), up->properties.end(), prop) != up->properties.end() &&
                    up->package)
                    section = up->package->name() + "." + up->name;
        for (int k = 0; k < prop->arrayDim; ++k) {
            const size_t slot = size_t(prop->slot + k);
            if (slot >= values.size())
                continue;
            const std::string key = prop->arrayDim > 1 ? prop->name + "[" + std::to_string(k) + "]" : prop->name;
            settings.push_back({section, key, configText(*prop, values[slot])});
        }
    }
    writeSettings(file, settings);
}

void Vm::saveSetting(const std::string &configName, const std::string &section, const std::string &key,
                     const std::string &value) {
    if (const std::filesystem::path file = configFile(configName); !file.empty())
        writeSettings(file, {{section, key, value}});
}

// Settings (section, key, value) into an .ini: each put in place of its
// key's line in its section, else at the section's end, else in a section
// of its own; the rest of the file kept as it is.
void Vm::writeSettings(const std::filesystem::path &file, const std::vector<std::array<std::string, 3>> &settings) {
    if (settings.empty())
        return;
    std::vector<std::string> lines;
    if (FILE *f = std::fopen(file.string().c_str(), "rb")) {
        std::string text;
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
            text.append(buf, n);
        std::fclose(f);
        for (size_t at = 0; at < text.size();) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(at, end - at);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
            at = end + 1;
        }
    }
    auto sectionAt = [&](size_t i) {
        const std::string &l = lines[i];
        return l.size() >= 2 && l.front() == '[' && l.back() == ']' ? lower(l.substr(1, l.size() - 2)) : std::string();
    };
    for (const auto &[section, key, value] : settings) {
        _configSet[lower(section)][lower(key)] = value;
        const std::string line = key + "=" + value;
        size_t i = 0;
        while (i < lines.size() && sectionAt(i) != lower(section))
            ++i;
        if (i == lines.size()) {
            if (!lines.empty() && !lines.back().empty())
                lines.emplace_back();
            lines.push_back("[" + section + "]");
            lines.push_back(line);
            continue;
        }
        size_t last = i;
        bool done = false;
        for (size_t j = i + 1;
             j < lines.size() && sectionAt(j).empty() && !(lines[j].size() >= 2 && lines[j][0] == '['); ++j) {
            const size_t eq = lines[j].find('=');
            if (eq != std::string::npos && lower(lines[j].substr(0, eq)) == lower(key)) {
                lines[j] = line;
                done = true;
                break;
            }
            if (!lines[j].empty())
                last = j;
        }
        if (!done)
            lines.insert(lines.begin() + std::ptrdiff_t(last + 1), line);
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (FILE *f = std::fopen(file.string().c_str(), "wb")) {
        for (const std::string &l : lines) {
            std::fputs(l.c_str(), f);
            std::fputs("\r\n", f);
        }
        std::fclose(f);
    }
    _iniFiles.erase(file.string());
}

// A text file of [Section] and Key=Value lines (an .ini, an .int), sections
// and keys in lower case; read once. Empty if it isn't there.
const Vm::IniFile &Vm::iniFileAt(const std::filesystem::path &file) {
    const std::string fileKey = file.string();
    auto it = _iniFiles.find(fileKey);
    if (it != _iniFiles.end())
        return it->second;
    IniFile parsed;
    if (FILE *f = std::fopen(fileKey.c_str(), "rb")) {
        std::string text;
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
            text.append(buf, n);
        std::fclose(f);
        std::map<std::string, std::string> *current = nullptr;
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
                current = &parsed[lower(line.substr(1, line.size() - 2))];
                continue;
            }
            const size_t eq = line.find('=');
            if (current && eq != std::string::npos)
                current->emplace(lower(line.substr(0, eq)), line.substr(eq + 1));
        }
    }
    return _iniFiles.emplace(fileKey, std::move(parsed)).first->second;
}

// A setting from the .ini files, in the order applyConfig says. One set
// while running comes first.
const std::string *Vm::configSetting(const std::string &section, const std::string &key) {
    if (auto sec = _configSet.find(lower(section)); sec != _configSet.end())
        if (auto value = sec->second.find(lower(key)); value != sec->second.end())
            return &value->second;
    // Those written (SaveConfig) before the game's own.
    const std::filesystem::path system = _packages.iniFile().parent_path();
    std::vector<std::filesystem::path> files;
    if (!_configFolder.empty())
        files = {configFile("User"), configFile("System")};
    for (const std::filesystem::path &f : {system / "User.ini", system / "DefUser.ini", _packages.iniFile()})
        files.push_back(f);
    for (const std::filesystem::path &file : files) {
        const IniFile &ini = iniFileAt(file);
        auto sec = ini.find(lower(section));
        if (sec == ini.end())
            continue;
        auto value = sec->second.find(lower(key));
        if (value != sec->second.end())
            return &value->second;
    }
    return nullptr;
}

// A localized text: `key` of [`section`] in `package`.int, else in
// Classes.int (where The Fallen keeps its classes'), each looked for in the
// int folder beside System, then in System (the engine's own: Engine.int,
// SoftDrv.int). Surrounding quotes are dropped. Null if it's nowhere.
std::optional<std::string> Vm::localized(const std::string &package, const std::string &section, const std::string &key,
                                         bool classesToo) {
    const std::filesystem::path system = _packages.iniFile().parent_path();
    std::vector<std::string> files{package + ".int"};
    if (classesToo)
        files.push_back("Classes.int");
    for (const std::string &file : files)
        for (const std::filesystem::path &folder :
             {system.parent_path() / "int", system.parent_path() / "Int", system}) {
            const IniFile &ini = iniFileAt(folder / file);
            auto sec = ini.find(lower(section));
            if (sec == ini.end())
                continue;
            auto value = sec->second.find(lower(key));
            if (value == sec->second.end())
                continue;
            std::string v = value->second;
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                v = v.substr(1, v.size() - 2);
            return v;
        }
    return std::nullopt;
}

// A class's localized properties (the text players read: menu items,
// messages): its section, by its name alone ([DS9_Hud]), as localized()
// finds it. What a parent's section set is inherited with its defaults.
void Vm::applyLocalized(VmClass &c) {
    if (!c.package)
        return;
    for (const VmProperty *prop : c.properties) {
        if (!(prop->flags & propflag::kLocalized) || prop->type == VType::kStruct)
            continue;
        for (int k = 0; k < prop->arrayDim; ++k) {
            const std::string key = prop->arrayDim > 1 ? prop->name + "[" + std::to_string(k) + "]" : prop->name;
            if (const auto value = localized(c.package->name(), c.name, key))
                setFromText(*prop, c.defaults[size_t(prop->slot + k)], *value);
        }
    }
}

// The Fallen's actors name their own strings: Actor's TextInfo is the
// section and FileName the .int it's in, with or without ".int"
// (CTVE_TricorderInfo1: [WarpCore] in M01_WorfL1.int). The file is often
// named for the map before it was split in two (M01_KiraL1.int for both
// M01_KiraL1A and B), and the map's own name when FileName is empty.
int Vm::applyLocalizedActor(VmObject *a, const std::string &map) {
    const Value *textInfo = a ? a->find(*this, "TextInfo") : nullptr;
    if (!textInfo || textInfo->s.empty())
        return 0;
    const std::string section = textInfo->s;
    const Value *fileName = a->find(*this, "FileName");
    std::string file = fileName && !fileName->s.empty() ? fileName->s : map;
    if (file.size() > 4 && lower(file.substr(file.size() - 4)) == ".int")
        file.resize(file.size() - 4);
    int set = 0;
    for (const VmClass *c = a->cls; c; c = c->parent())
        for (const VmProperty *prop : c->properties) {
            if (!(prop->flags & propflag::kLocalized) || prop->type == VType::kStruct)
                continue;
            Value *v = a->find(*this, prop->name);
            for (int k = 0; v && k < prop->arrayDim && size_t(prop->slot + k) < a->values.size(); ++k) {
                const std::string key = prop->arrayDim > 1 ? prop->name + "[" + std::to_string(k) + "]" : prop->name;
                if (const auto value = localized(file, section, key, false)) {
                    setFromText(*prop, v[k], *value);
                    ++set;
                }
            }
        }
    return set;
}

void Vm::loadStruct(VmStruct &s, const ScriptObject &so) {
    const Package &package = *s.package;
    for (int32_t ref = so.children, guard = 0; ref > 0 && guard < 100000; ++guard) {
        const int index = ref - 1;
        ScriptObject child;
        if (Error err = readScriptObject(package, index, child)) {
            reportError(err.message);
            break;
        }
        switch (child.kind) {
        case ScriptObject::Kind::kProperty:
            addProperty(s, package, index, child);
            break;
        case ScriptObject::Kind::kFunction: {
            VmFunction *f = loadFunction(package, index, &s);
            if (f && (s.kind == VmStruct::Kind::kClass || s.kind == VmStruct::Kind::kState))
                static_cast<VmState &>(s).functions[f->nameId] = f;
            break;
        }
        case ScriptObject::Kind::kState:
            if (s.kind == VmStruct::Kind::kClass) {
                auto &cls = static_cast<VmClass &>(s);
                if (VmState *st = loadState(package, index, &cls))
                    cls.states[st->nameId] = st;
            }
            break;
        case ScriptObject::Kind::kStruct:
            structAt(package, index);
            break;
        default:
            break;
        }
        ref = child.next;
    }
    s.code = so.code;
    prepareCode(s);
    if (s.kind == VmStruct::Kind::kClass || s.kind == VmStruct::Kind::kState) {
        auto &st = static_cast<VmState &>(s);
        st.stateFlags = so.stateFlags;
        st.probeMask = so.probeMask;
        st.ignoreMask = so.ignoreMask;
        for (const Expr &x : s.code.exprs)
            for (const auto &[label, offset] : x.labels)
                st.labels[name(package.name(label))] = offset;
    }
}

void Vm::prepareCode(VmStruct &s) {
    s.statementAt.clear();
    for (size_t i = 0; i < s.code.statements.size(); ++i)
        if (s.code.statements[i] >= 0)
            s.statementAt[s.code.exprs[size_t(s.code.statements[i])].offset] = int(i);
    s.resolved.assign(s.code.exprs.size(), nullptr);
    s.calls.assign(s.code.exprs.size(), {});
}

void Vm::addProperty(VmStruct &s, const Package &package, int index, const ScriptObject &so) {
    _propertyStore.emplace_back();
    VmProperty &p = _propertyStore.back();
    p.name = package.objectName(index + 1);
    p.nameId = name(p.name);
    p.type = typeOfProperty(so.className);
    p.isClass = lower(so.className) == "classproperty";
    p.arrayDim = std::max(1, so.arrayDim);
    p.flags = so.propertyFlags;
    p.slot = s.slots;
    const std::string cls = lower(so.className);
    if (cls == "structproperty" && so.propertyRef) {
        const ObjectRef r = _packages.resolve(package, so.propertyRef);
        if (r)
            p.structType = structAt(*r.package, r.exportIndex);
        if (!p.structType)
            p.type = VType::kNone;
    }
    if (cls == "byteproperty" && so.propertyRef) {
        const ObjectRef r = _packages.resolve(package, so.propertyRef);
        ScriptObject en;
        if (r && !readScriptObject(*r.package, r.exportIndex, en)) {
            _enumStore.emplace_back();
            for (int32_t n : en.enumNames)
                _enumStore.back().push_back(name(r.package->name(n)));
            p.enumNames = &_enumStore.back();
        }
    }
    s.slots += p.arrayDim;
    s.properties.push_back(&p);
    for (int k = 0; k < p.arrayDim; ++k)
        s.defaults.push_back(p.zero());
    _properties[{&package, index}] = &p;
}

VmFunction *Vm::loadFunction(const Package &package, int index, VmStruct *outer) {
    const auto key = std::make_pair(&package, index);
    if (auto it = _structs.find(key); it != _structs.end())
        return static_cast<VmFunction *>(it->second.get());
    ScriptObject so;
    if (Error err = readScriptObject(package, index, so)) {
        reportError(err.message);
        return nullptr;
    }
    auto owned = std::make_unique<VmFunction>();
    VmFunction *f = owned.get();
    f->kind = VmStruct::Kind::kFunction;
    f->name = package.objectName(index + 1);
    f->nameId = name(f->name);
    f->package = &package;
    f->exportIndex = index;
    f->flags = so.functionFlags;
    f->native = so.native;
    f->symbol = package.name(so.friendlyName);
    f->outer = outer;
    const VmClass *owner = outer->kind == VmStruct::Kind::kClass   ? static_cast<VmClass *>(outer)
                           : outer->kind == VmStruct::Kind::kState ? static_cast<VmState *>(outer)->owner
                                                                   : nullptr;
    f->qualified = (owner ? owner->name : outer->name) + "." + f->name;
    _structs.emplace(key, std::move(owned));
    loadStruct(*f, so);
    for (VmProperty *p : f->properties) {
        if (p->flags & propflag::kReturnParm)
            f->returnValue = p;
        else if (p->flags & propflag::kParm)
            f->params.push_back(p);
    }
    if (f->native)
        _nativeFunctions[f->native] = f;
    return f;
}

VmState *Vm::loadState(const Package &package, int index, VmClass *owner) {
    const auto key = std::make_pair(&package, index);
    if (auto it = _structs.find(key); it != _structs.end())
        return static_cast<VmState *>(it->second.get());
    ScriptObject so;
    if (Error err = readScriptObject(package, index, so)) {
        reportError(err.message);
        return nullptr;
    }
    auto owned = std::make_unique<VmState>();
    VmState *st = owned.get();
    st->kind = VmStruct::Kind::kState;
    st->name = package.objectName(index + 1);
    st->nameId = name(st->name);
    st->package = &package;
    st->exportIndex = index;
    st->owner = owner;
    _structs.emplace(key, std::move(owned));
    const Export &e = package.exports()[size_t(index)];
    if (e.superRef) {
        const ObjectRef sup = _packages.resolve(package, e.superRef);
        VmStruct *s = sup ? structAt(*sup.package, sup.exportIndex) : nullptr;
        if (s && s->kind == VmStruct::Kind::kState)
            st->super = s;
    }
    loadStruct(*st, so);
    return st;
}

VmProperty *Vm::propertyAt(const Package &package, int exportIndex) {
    const auto key = std::make_pair(&package, exportIndex);
    if (auto it = _properties.find(key); it != _properties.end())
        return it->second;
    // Load what it's in: up its outers to a struct or class.
    if (exportIndex < 0 || size_t(exportIndex) >= package.exports().size())
        return nullptr;
    const int32_t outer = package.exports()[size_t(exportIndex)].package;
    if (outer > 0)
        structAt(package, outer - 1);
    auto it = _properties.find(key);
    return it == _properties.end() ? nullptr : it->second;
}

VmFunction *Vm::nativeFunction(int number) {
    auto it = _nativeFunctions.find(number);
    if (it != _nativeFunctions.end())
        return it->second;
    if (!_nativesScanned)
        scanNatives();
    it = _nativeFunctions.find(number);
    return it == _nativeFunctions.end() ? nullptr : it->second;
}

// Loads every class declaring numbered natives (Core, Engine...), once.
void Vm::scanNatives() {
    _nativesScanned = true;
    for (const std::string &n : _packages.names()) {
        const Package *p = _packages.get(n);
        if (!p)
            continue;
        for (size_t i = 0; i < p->exports().size(); ++i) {
            const Export &e = p->exports()[i];
            if (e.classRef == 0 || lower(p->objectName(e.classRef)) != "function")
                continue;
            ScriptObject so;
            if (readScriptObject(*p, int(i), so) || so.native == 0 || _nativeFunctions.count(so.native))
                continue;
            structAt(*p, int(i));
        }
    }
}

VmObject *Vm::classObject(VmClass *cls) {
    if (cls->object)
        return cls->object;
    auto o = std::make_unique<VmObject>();
    o->asClass = cls;
    o->name = cls->nameId;
    o->package = cls->package;
    o->exportIndex = cls->exportIndex;
    cls->object = o.get();
    _objects.push_back(std::move(o));
    return cls->object;
}

VmObject *Vm::objectAt(const Package &package, int32_t ref) {
    if (ref == 0)
        return nullptr;
    const ObjectRef r = _packages.resolve(package, ref);
    if (!r)
        return nullptr;
    const auto key = std::make_pair(r.package, r.exportIndex);
    if (auto it = _exportObjects.find(key); it != _exportObjects.end())
        return it->second;
    const Export &e = r.package->exports()[size_t(r.exportIndex)];
    if (e.classRef == 0) {
        VmClass *c = classAt(*r.package, r.exportIndex);
        return c ? classObject(c) : nullptr;
    }
    // An object the VM doesn't run: its class's defaults, and where it is.
    const ObjectRef cref = _packages.resolve(*r.package, e.classRef);
    VmClass *cls = cref ? classAt(*cref.package, cref.exportIndex) : nullptr;
    if (!cls) // the engine's own class, with no script
        cls = nativeClass(r.package->objectName(e.classRef));
    auto o = std::make_unique<VmObject>();
    o->cls = cls;
    if (cls)
        o->values = cls->defaults;
    o->name = name(r.package->objectName(r.exportIndex + 1));
    o->package = r.package;
    o->exportIndex = r.exportIndex;
    VmObject *out = o.get();
    _objects.push_back(std::move(o));
    _exportObjects[key] = out;
    // Its outer, if that's an object of its package (a saved game's
    // effect pools are their actors').
    if (e.package > 0)
        if (VmObject *outer = objectAt(*r.package, e.package))
            if (Value *v = out->find(*this, "Outer"))
                v->o = outer;
    // Its own properties over the defaults: scripts read them (The Fallen's
    // HUD textures keep where they go on the screen in theirs). Textures
    // and anything small; meshes and sounds aren't read whole for it.
    bool texture = false;
    for (const VmClass *c = cls; c && !texture; c = c->parent())
        texture = lower(c->name) == "texture";
    if (cls && (texture || e.serialSize < 65536)) {
        std::vector<uint8_t> data;
        PropertyList props;
        if (!r.package->readExport(r.exportIndex, data)) {
            Reader rd(data, size_t(e.serialOffset));
            if (!beginObject(*r.package, r.exportIndex, rd, props))
                applyProperties(out, *r.package, props);
        }
    }
    return out;
}

VmObject *Vm::create(VmClass *cls, NameId objName, VmObject *outer) {
    auto o = std::make_unique<VmObject>();
    o->cls = cls;
    o->values = cls->defaults;
    o->name = objName ? objName : name(cls->name + std::to_string(_nameCounts[cls]++));
    // Object's native properties, which scripts read.
    if (Value *v = o->find(*this, "Outer"))
        v->o = outer;
    if (Value *v = o->find(*this, "Name"))
        v->i = o->name;
    if (Value *v = o->find(*this, "Class"))
        v->o = classObject(cls);
    for (const VmClass *c = cls; c; c = c->parent())
        if (lower(c->name) == "actor")
            o->isActor = true;
    VmObject *out = o.get();
    _objects.push_back(std::move(o));
    return out;
}

void Vm::adopt(const Package &package, int exportIndex, VmObject *object) {
    _exportObjects[{&package, exportIndex}] = object;
    object->package = &package;
    object->exportIndex = exportIndex;
}

void Vm::applyProperties(VmObject *object, const Package &package, const PropertyList &props) {
    if (!object->cls)
        return;
    for (const Property &p : props.items) {
        VmProperty *prop = object->cls->findProperty(name(p.name));
        if (!prop) {
            reportError(object->cls->name + ": no property " + p.name);
            continue;
        }
        if (p.arrayIndex < 0 || p.arrayIndex >= prop->arrayDim)
            continue;
        object->values[size_t(prop->slot + p.arrayIndex)] = valueOf(*prop, package, p);
    }
}

Value Vm::valueOf(const VmProperty &p, const Package &package, const Property &data) {
    if (p.type == VType::kBool)
        return Value::boolValue(data.boolValue);
    Reader r(data.data);
    Value v = readValue(p, package, r);
    if (!r.ok() || r.remaining() != 0)
        reportError("the value of " + p.name + " (" + std::to_string(data.data.size()) +
                    " bytes) doesn't read as its type");
    return v;
}

// A value as properties and struct fields store it.
Value Vm::readValue(const VmProperty &p, const Package &package, Reader &r) {
    switch (p.type) {
    case VType::kByte:
        return Value::byteValue(r.u8());
    case VType::kInt:
        return Value::intValue(r.i32());
    case VType::kBool:
        return Value::boolValue(r.u8() != 0);
    case VType::kFloat:
        return Value::floatValue(r.f32());
    case VType::kName: {
        const int32_t n = r.index();
        return Value::nameValue(n >= 0 && size_t(n) < package.names().size() ? name(package.name(n)) : 0);
    }
    case VType::kString:
        return Value::stringValue(r.string(package.version()));
    case VType::kObject:
        return Value::objectValue(objectAt(package, r.index()));
    case VType::kStruct:
        return p.structType ? readStruct(*p.structType, package, r) : Value();
    default:
        return Value();
    }
}

Value Vm::readStruct(const VmStruct &st, const Package &package, Reader &r) {
    Value v;
    v.type = VType::kStruct;
    v.st = &st;
    v.fields = st.defaults;
    for (const VmProperty *p : st.properties)
        for (int k = 0; k < p->arrayDim; ++k)
            v.fields[size_t(p->slot + k)] = readValue(*p, package, r);
    return v;
}

// --- Values, converted -----------------------------------------------------------------

std::string Vm::toString(const Value &v) const {
    char buf[96];
    switch (v.type) {
    case VType::kByte:
    case VType::kInt:
        return std::to_string(v.i);
    case VType::kBool:
        return v.i ? "True" : "False";
    case VType::kFloat:
        std::snprintf(buf, sizeof buf, "%.2f", double(v.f));
        return buf;
    case VType::kName:
        return nameText(v.i);
    case VType::kString:
        return v.s;
    case VType::kObject:
        return const_cast<Vm *>(this)->pathName(v.asObject());
    case VType::kStruct:
        if (v.fields.size() == 3 && v.fields[0].type == VType::kFloat) {
            std::snprintf(buf, sizeof buf, "%.2f,%.2f,%.2f", double(v.fields[0].f), double(v.fields[1].f),
                          double(v.fields[2].f));
            return buf;
        }
        if (v.fields.size() == 3 && v.fields[0].type == VType::kInt) {
            std::snprintf(buf, sizeof buf, "%d,%d,%d", v.fields[0].i, v.fields[1].i, v.fields[2].i);
            return buf;
        }
        return v.st ? v.st->name : "struct";
    default:
        return "";
    }
}

Value Vm::convert(const Value &v, const VmProperty &to) const {
    switch (to.type) {
    case VType::kByte:
        return Value::byteValue(v.asInt());
    case VType::kInt:
        return Value::intValue(v.asInt());
    case VType::kBool:
        return Value::boolValue(v.asBool());
    case VType::kFloat:
        return Value::floatValue(v.asFloat());
    case VType::kName:
        if (v.type == VType::kName)
            return v;
        if (v.type == VType::kString)
            return Value::nameValue(const_cast<Vm *>(this)->name(v.s));
        return Value::nameValue(0);
    case VType::kString:
        return Value::stringValue(toString(v));
    case VType::kObject:
        return Value::objectValue(v.asObject());
    case VType::kStruct:
        if (v.type == VType::kStruct)
            return v;
        return to.zero();
    default:
        return v;
    }
}

void Vm::assign(Value &dst, const Value &src) const {
    switch (dst.type) {
    case VType::kByte:
        dst.i = src.asInt() & 0xff;
        break;
    case VType::kInt:
        dst.i = src.asInt();
        break;
    case VType::kBool:
        dst.i = src.asBool() ? 1 : 0;
        break;
    case VType::kFloat:
        dst.f = src.asFloat();
        break;
    case VType::kName:
        if (src.type == VType::kName)
            dst.i = src.i;
        else if (src.type == VType::kString)
            dst.i = const_cast<Vm *>(this)->name(src.s);
        else
            dst.i = 0;
        break;
    case VType::kString:
        dst.s = toString(src);
        break;
    case VType::kObject:
        dst.o = src.asObject();
        break;
    case VType::kStruct:
        if (src.type == VType::kStruct && (!dst.st || src.fields.size() == dst.fields.size())) {
            dst.fields = src.fields;
            dst.st = src.st;
        }
        break;
    case VType::kNone:
        dst = src;
        break;
    }
}

bool Vm::equal(const Value &a, const Value &b) const {
    const VType t = a.type != VType::kNone ? a.type : b.type;
    switch (t) {
    case VType::kFloat:
        return a.asFloat() == b.asFloat();
    case VType::kString:
        return toString(a) == toString(b);
    case VType::kObject:
        return a.asObject() == b.asObject();
    case VType::kStruct:
        if (a.fields.size() != b.fields.size())
            return false;
        for (size_t k = 0; k < a.fields.size(); ++k)
            if (!equal(a.fields[k], b.fields[k]))
                return false;
        return true;
    default:
        return a.asInt() == b.asInt();
    }
}

bool Vm::isA(const VmObject *o, std::string_view className) {
    if (!o || !o->cls)
        return false;
    // The class's name interned once for each text it's asked by (mostly
    // the same literals, every tick), checked to be the same text.
    IsACache &c = _isACache[(reinterpret_cast<uintptr_t>(className.data()) >> 3) % _isACache.size()];
    if (c.text != className) {
        c.text = className;
        c.name = name(className);
    }
    const NameId n = c.name;
    for (const VmClass *c = o->cls; c; c = c->parent())
        if (c->nameId == n)
            return true;
    return false;
}

const VmStruct *Vm::coreStruct(std::string_view structName) {
    const NameId n = name(structName);
    if (auto it = _coreStructs.find(n); it != _coreStructs.end())
        return it->second;
    findClass("Core.Object"); // loads Object's structs
    auto it = _coreStructs.find(n);
    return it == _coreStructs.end() ? nullptr : it->second;
}

Value Vm::vectorValue(float x, float y, float z) {
    Value v;
    v.type = VType::kStruct;
    if (!_vectorStruct)
        _vectorStruct = coreStruct("Vector");
    v.st = _vectorStruct;
    v.fields = {Value::floatValue(x), Value::floatValue(y), Value::floatValue(z)};
    return v;
}

void Vm::setVector(Value &v, float x, float y, float z) {
    if (!_vectorStruct)
        _vectorStruct = coreStruct("Vector");
    if (v.type == VType::kStruct && v.st == _vectorStruct && v.fields.size() == 3) {
        v.fields[0].f = x, v.fields[1].f = y, v.fields[2].f = z;
        return;
    }
    v = vectorValue(x, y, z);
}

Value Vm::rotatorValue(int32_t pitch, int32_t yaw, int32_t roll) {
    Value v;
    v.type = VType::kStruct;
    if (!_rotatorStruct)
        _rotatorStruct = coreStruct("Rotator");
    v.st = _rotatorStruct;
    v.fields = {Value::intValue(pitch), Value::intValue(yaw), Value::intValue(roll)};
    return v;
}

bool Vm::callerSimulated() const {
    if (_running.empty() || _running.back()->kind != VmStruct::Kind::kFunction)
        return false;
    return (static_cast<const VmFunction *>(_running.back())->flags & funcflag::kSimulated) != 0;
}

std::string Vm::where() const {
    if (_running.empty())
        return "";
    const VmStruct *s = _running.back();
    if (s->kind == VmStruct::Kind::kFunction)
        return " in " + static_cast<const VmFunction *>(s)->qualified;
    if (s->kind == VmStruct::Kind::kState)
        return " in " + static_cast<const VmState *>(s)->owner->name + "." + s->name + " (state code)";
    return " in " + s->name;
}

VmClass *Vm::defineIntrinsic(std::string_view className, VmClass *parent) {
    const NameId n = name(className);
    if (auto it = _intrinsics.find(n); it != _intrinsics.end())
        return it->second.get();
    auto c = std::make_unique<VmClass>();
    c->kind = VmStruct::Kind::kClass;
    c->name = std::string(className);
    c->nameId = n;
    c->owner = c.get();
    c->super = parent;
    if (parent) {
        c->package = parent->package;
        c->properties = parent->properties;
        c->slots = parent->slots;
        c->defaults = parent->defaults;
    }
    VmClass *out = c.get();
    _intrinsics.emplace(n, std::move(c));
    classObject(out);
    return out;
}

// A class with no script (the engine's own: Mesh, LodMesh, Sound, Font...),
// made as an intrinsic under its parent, so objects of it have a class
// that casts and IsA see (the HUD loads a talker's mesh with
// Mesh(DynamicLoadObject(...))). The parents are UE1's: LodMesh is a Mesh,
// Mesh and Model are Primitives; the rest are Objects.
VmClass *Vm::nativeClass(std::string_view className) {
    if (VmClass *c = intrinsic(className))
        return c;
    static const std::map<std::string, std::string> kParents = {
        {"lodmesh", "Mesh"},
        {"mesh", "Primitive"},
        {"model", "Primitive"},
        {"primitive", "Object"},
    };
    auto it = kParents.find(lower(className));
    VmClass *parent = it != kParents.end() ? nativeClass(it->second) : findClass("Object");
    return defineIntrinsic(className, parent);
}

VmClass *Vm::intrinsic(std::string_view className) {
    auto it = _intrinsics.find(name(className));
    return it == _intrinsics.end() ? nullptr : it->second.get();
}

const NativeFn *Vm::findNative(std::string_view qualifiedName) const {
    auto it = _natives.find(lower(qualifiedName));
    return it == _natives.end() ? nullptr : &it->second;
}

void Vm::registerNative(std::string_view qualifiedName, NativeFn fn) {
    _natives[lower(qualifiedName)] = std::move(fn);
    ++_nativeGeneration;
}

// --- The interpreter -----------------------------------------------------------------------

class Interpreter {
public:
    explicit Interpreter(Vm &vm) : _vm(vm) {}

    struct Iteration {
        std::vector<Value> items;
        std::vector<Value> items2; // its second out parameter's, item by item (ListActors' entry)
        size_t next = 0;
        Value *out = nullptr;
        Value *out2 = nullptr;
        int body = 0;
    };
    struct Frame {
        const VmStruct *owner = nullptr; // the function or state whose code runs
        VmObject *self = nullptr;
        std::vector<Value> *locals = nullptr;
        Value result;
        std::vector<Iteration> iterations;
        Value switchValue;
        // Values that aren't variables, for references to them; made when
        // first needed (most frames have none).
        std::unique_ptr<std::deque<Value>> temps;
        uint64_t statements = 0;
    };
    enum class Flow { kReturn, kStop, kWait, kRestart };

    // Runs statements from `pc`; on Wait, `pc` is the statement to resume at.
    Flow run(Frame &f, int &pc) {
        _vm._running.push_back(f.owner);
        const Flow flow = runStatements(f, pc);
        _vm._running.pop_back();
        return flow;
    }

    Flow runStatements(Frame &f, int &pc) {
        const Code &code = f.owner->code;
        const int stateSerial = f.self ? f.self->stateSerial : 0;
        const bool stateCode = f.owner->kind == VmStruct::Kind::kState || f.owner->kind == VmStruct::Kind::kClass;
        while (pc >= 0 && size_t(pc) < code.statements.size()) {
            if (++f.statements > (stateCode ? kMaxStateStatements : kMaxStatements)) {
                _vm.reportError("a loop that doesn't end in " + where(f));
                return Flow::kReturn;
            }
            ++_vm.stats.statements;
            if (f.temps)
                f.temps->clear();
            const int si = code.statements[size_t(pc)];
            if (si < 0)
                return Flow::kReturn;
            const Expr &x = code.exprs[size_t(si)];
            switch (x.token) {
            case token::kJump:
                pc = at(f, uint32_t(x.value));
                continue;
            case token::kJumpIfNot:
                pc = eval(f, arg(x, 0), f.self).asBool() ? pc + 1 : at(f, uint32_t(x.value));
                continue;
            case token::kReturn:
                if (!x.args.empty() && x.args[0] >= 0 && code.exprs[size_t(x.args[0])].token != token::kNothing)
                    f.result = eval(f, x.args[0], f.self);
                return Flow::kReturn;
            case token::kStop:
                return Flow::kStop;
            // The switch finds its case: each label in turn, to the first
            // that matches or the default. A label reached by falling
            // through from the case above is passed over, as the engine
            // does ("case 38: case 40: ..." share their code).
            case token::kSwitch: {
                f.switchValue = eval(f, arg(x, 0), f.self);
                ++pc;
                for (int guard = 0; guard < 10000 && pc >= 0 && size_t(pc) < code.statements.size(); ++guard) {
                    const int ci = code.statements[size_t(pc)];
                    if (ci < 0 || code.exprs[size_t(ci)].token != token::kCase)
                        break;
                    const Expr &c = code.exprs[size_t(ci)];
                    if (c.value == 0xffff || _vm.equal(f.switchValue, eval(f, arg(c, 0), f.self))) {
                        ++pc;
                        break;
                    }
                    pc = at(f, uint32_t(c.value));
                }
                continue;
            }
            case token::kCase:
                ++pc;
                continue;
            case token::kIterator: {
                Iteration it;
                it.body = pc + 1;
                iterate(f, arg(x, 0), it);
                const bool empty = it.items.empty() || !it.out;
                if (!empty) {
                    _vm.assign(*it.out, it.items[0]);
                    if (it.out2 && !it.items2.empty())
                        _vm.assign(*it.out2, it.items2[0]);
                }
                it.next = 1;
                f.iterations.push_back(std::move(it));
                pc = empty ? at(f, uint32_t(x.value)) : pc + 1;
                continue;
            }
            case token::kIteratorNext:
                if (!f.iterations.empty()) {
                    Iteration &it = f.iterations.back();
                    // Destroyed actors drop out of the list as it goes.
                    while (it.next < it.items.size() && it.items[it.next].type == VType::kObject &&
                           !it.items[it.next].asObject())
                        ++it.next;
                    if (it.next < it.items.size()) {
                        if (it.out2 && it.next < it.items2.size())
                            _vm.assign(*it.out2, it.items2[it.next]);
                        _vm.assign(*it.out, it.items[it.next++]);
                        pc = it.body;
                        continue;
                    }
                }
                ++pc;
                continue;
            case token::kIteratorPop:
                if (!f.iterations.empty())
                    f.iterations.pop_back();
                ++pc;
                continue;
            case token::kGotoLabel: {
                const NameId label = eval(f, arg(x, 0), f.self).i;
                const int to = labelAt(f.owner, label);
                if (to < 0) {
                    // The script's own mistake (DS9_GrigariSentryDrone goes
                    // to 'DetermineAction', a function): reported, and the
                    // code carries on.
                    _vm.reportError("goto a label that isn't there, " + _vm.nameText(label) + ", in " + where(f));
                    ++pc;
                    continue;
                }
                pc = to;
                continue;
            }
            case token::kNothing:
            case token::kLabelTable:
            case token::kEndFunctionParms:
                ++pc;
                continue;
            case token::kAssert:
                if (!eval(f, arg(x, 0), f.self).asBool())
                    _vm.reportError("an assertion failed in " + where(f) + " (line " + std::to_string(x.value) + ")");
                ++pc;
                continue;
            default:
                eval(f, si, f.self);
                ++pc;
                if (_vm._pendingLatent >= 0) {
                    const float wait = _vm._pendingLatent;
                    _vm._pendingLatent = -1;
                    if (stateCode && f.self) {
                        f.self->sleeping = wait;
                        f.self->latent = true;
                        return Flow::kWait;
                    }
                }
                if (stateCode && f.self && f.self->stateSerial != stateSerial)
                    return Flow::kRestart; // GotoState from its own code
                continue;
            }
        }
        return Flow::kReturn;
    }

    Value eval(Frame &f, int i, VmObject *ctx) {
        if (i < 0)
            return Value();
        const Code &code = f.owner->code;
        const Expr &x = code.exprs[size_t(i)];
        using namespace token;
        if (x.native >= 0) {
            // (Its function, found by its number once.)
            const void *&fn = f.owner->resolved[size_t(i)];
            if (!fn)
                fn = _vm.nativeFunction(x.native);
            return call(f, x, ctx, static_cast<VmFunction *>(const_cast<void *>(fn)));
        }
        if (x.token >= kFirstConversion && x.token <= kLastConversion)
            return conversion(x.token, eval(f, arg(x, 0), ctx));
        switch (x.token) {
        case kLocalVariable:
        case kInstanceVariable:
        case kDefaultVariable:
        case kArrayElement:
        case kStructMember:
        case kBoolVariable: {
            Value *v = ref(f, i, ctx);
            return v ? *v : Value();
        }
        case kContext:
        case kClassContext: {
            VmObject *o = eval(f, arg(x, 0), ctx).asObject();
            if (!o) {
                accessedNone(f, x.offset);
                return Value();
            }
            return eval(f, arg(x, 1), o);
        }
        case kLet:
        case kLetBool: {
            const Value v = eval(f, arg(x, 1), ctx);
            if (Value *d = ref(f, arg(x, 0), ctx))
                _vm.assign(*d, v);
            return Value();
        }
        case kVirtualFunction:
        case kGlobalFunction: {
            VmObject *target = ctx ? ctx : f.self;
            VmStruct::CallCache &cache = f.owner->calls[size_t(i)];
            if (cache.name < 0)
                cache.name = _vm.name(f.owner->package->name(x.ref));
            const NameId n = cache.name;
            VmFunction *fn = nullptr;
            if (target) {
                // A class's own (a static call) or its state's: as found before
                // for the same class and state.
                const VmClass *cls = target->asClass ? target->asClass : target->cls;
                const VmStruct *state = target->asClass || x.token == kGlobalFunction ? nullptr : target->state;
                if (cache.fn && cache.cls == cls && cache.state == state) {
                    fn = cache.fn;
                } else {
                    fn = _vm.findFunction(target, n, x.token == kGlobalFunction);
                    cache.cls = cls;
                    cache.state = state;
                    cache.fn = fn;
                }
            }
            if (!fn) {
                _vm.reportError("no function " + _vm.nameText(n) + " for " +
                                (target && target->cls ? target->cls->name : std::string("None")) + " (in " + where(f) +
                                ")");
                return Value();
            }
            return call(f, x, target, fn);
        }
        case kFinalFunction:
            return call(f, x, ctx ? ctx : f.self, function(f, i, x.ref));
        case kIntConst:
        case kIntConstByte:
            return Value::intValue(x.value);
        case kByteConst:
            return Value::byteValue(x.value);
        case kIntZero:
            return Value::intValue(0);
        case kIntOne:
            return Value::intValue(1);
        case kTrue:
            return Value::boolValue(true);
        case kFalse:
            return Value::boolValue(false);
        case kFloatConst:
            return Value::floatValue(x.f[0]);
        case kStringConst:
        case kUnicodeStringConst:
            return Value::stringValue(x.text);
        case kNameConst:
            return Value::nameValue(_vm.name(f.owner->package->name(x.ref)));
        case kObjectConst:
            return Value::objectValue(_vm.objectAt(*f.owner->package, x.ref));
        case kVectorConst:
            return _vm.vectorValue(x.f[0], x.f[1], x.f[2]);
        case kRotationConst:
            return _vm.rotatorValue(x.rot[0], x.rot[1], x.rot[2]);
        case kSelf:
            return Value::objectValue(f.self);
        case kNoObject:
            return Value::objectValue(nullptr);
        case kNothing:
            return Value();
        case kSkip:
        case kEatString:
            return eval(f, arg(x, 0), ctx);
        case kDynamicCast: {
            VmObject *o = eval(f, arg(x, 0), ctx).asObject();
            VmClass *to = classRef(f, i, x.ref);
            if (!o || !to)
                return Value::objectValue(nullptr);
            if (o->asClass) // casting a class to Class or Object
                return Value::objectValue(lowerName(to) == "class" || lowerName(to) == "object" ? o : nullptr);
            return Value::objectValue(o->cls && o->cls->isA(to) ? o : nullptr);
        }
        case kMetaCast: {
            VmObject *o = eval(f, arg(x, 0), ctx).asObject();
            VmClass *to = classRef(f, i, x.ref);
            return Value::objectValue(o && o->asClass && to && o->asClass->isA(to) ? o : nullptr);
        }
        case kStructCmpEq:
        case kStructCmpNe: {
            const bool same = _vm.equal(eval(f, arg(x, 0), ctx), eval(f, arg(x, 1), ctx));
            return Value::boolValue(x.token == kStructCmpEq ? same : !same);
        }
        case kNew: {
            VmObject *clsObj = eval(f, arg(x, 3), ctx).asObject();
            if (!clsObj || !clsObj->asClass) {
                _vm.reportError("new without a class in " + where(f));
                return Value::objectValue(nullptr);
            }
            VmObject *outer = eval(f, arg(x, 0), ctx).asObject();
            const Value objName = eval(f, arg(x, 1), ctx);
            return Value::objectValue(
                _vm.create(clsObj->asClass, objName.type == VType::kName ? objName.i : 0, outer ? outer : f.self));
        }
        default:
            _vm.reportError("token 0x" + hex(x.token) + " can't be evaluated (in " + where(f) + ")");
            return Value();
        }
    }

    // A reference to a variable, or to a temporary holding the value.
    Value *ref(Frame &f, int i, VmObject *ctx) {
        if (i < 0)
            return temp(f, Value());
        const Expr &x = f.owner->code.exprs[size_t(i)];
        using namespace token;
        switch (x.token) {
        case kLocalVariable: {
            VmProperty *p = property(f, i, x.ref);
            if (!p || !f.locals || size_t(p->slot) >= f.locals->size()) {
                _vm.reportError("a local variable that isn't there, in " + where(f));
                return temp(f, Value());
            }
            return &(*f.locals)[size_t(p->slot)];
        }
        case kInstanceVariable:
        case kDefaultVariable: {
            VmProperty *p = property(f, i, x.ref);
            VmObject *o = ctx ? ctx : f.self;
            if (!p || !o) {
                _vm.reportError("a variable that isn't there, in " + where(f));
                return temp(f, Value());
            }
            std::vector<Value> *values = &o->values;
            if (x.token == kDefaultVariable || o->asClass) {
                VmClass *c = o->asClass ? o->asClass : o->cls;
                values = c ? &c->defaults : nullptr;
            }
            if (!values || size_t(p->slot) >= values->size()) {
                _vm.reportError("variable " + p->name + " isn't in " + (o->cls ? o->cls->name : _vm.objectName(o)) +
                                " (in " + where(f) + ")");
                return temp(f, p->zero());
            }
            return &(*values)[size_t(p->slot)];
        }
        case kBoolVariable:
            return ref(f, arg(x, 0), ctx);
        case kContext:
        case kClassContext: {
            VmObject *o = eval(f, arg(x, 0), ctx).asObject();
            if (!o) {
                accessedNone(f, x.offset);
                return temp(f, Value());
            }
            return ref(f, arg(x, 1), o);
        }
        case kArrayElement: {
            const int index = eval(f, arg(x, 0), f.self).asInt();
            const int arrayExpr = arg(x, 1);
            Value *base = ref(f, arrayExpr, ctx);
            const int dim = arrayDim(f, arrayExpr);
            if (index < 0 || index >= dim) {
                _vm.reportError("an array index out of range (" + std::to_string(index) + " of " + std::to_string(dim) +
                                ") in " + where(f));
                return temp(f, base ? *base : Value());
            }
            return base + index;
        }
        case kStructMember: {
            Value *base = ref(f, arg(x, 0), ctx);
            VmProperty *p = property(f, i, x.ref);
            if (base && base->type == VType::kNone) // after None was accessed, already counted
                return temp(f, p ? p->zero() : Value());
            if (!base || !p || size_t(p->slot) >= base->fields.size()) {
                _vm.reportError("a struct member that isn't there, in " + where(f));
                return temp(f, p ? p->zero() : Value());
            }
            return &base->fields[size_t(p->slot)];
        }
        default:
            return temp(f, eval(f, i, ctx));
        }
    }

    // A call: its arguments evaluated in the caller (out ones by reference),
    // then the function, script or native.
    Value call(Frame &f, const Expr &x, VmObject *target, VmFunction *fn) {
        if (!fn) {
            _vm.reportError("a call to a function that isn't there (native " + std::to_string(x.native) + ") in " +
                            where(f));
            return Value();
        }
        if (fn->logicOp < 0) {
            fn->logicOp = fn->qualified == "Object.AndAnd_BoolBool" ? 1
                          : fn->qualified == "Object.OrOr_BoolBool" ? 2
                                                                    : 0;
            fn->hasOut = std::any_of(fn->params.begin(), fn->params.end(),
                                     [](const VmProperty *p) { return p->flags & propflag::kOutParm; });
        }
        const size_t depth = _vm._callNest++;
        struct Leave {
            size_t &nest;
            ~Leave() { --nest; }
        } leave{_vm._callNest};
        if (_vm._callSpace.size() <= depth)
            _vm._callSpace.resize(depth + 1);
        std::vector<Value> args = std::move(_vm._callSpace[depth].args);
        args.resize(fn->params.size());
        static const std::vector<Value *> kNoOuts;
        std::vector<Value *> outs;
        if (fn->hasOut)
            outs.assign(fn->params.size(), nullptr);
        std::vector<bool> given = std::move(_vm._callSpace[depth].given);
        given.assign(fn->params.size(), false);
        // && and || leave their right side unevaluated when the left decides.
        const bool andOp = fn->logicOp == 1;
        const bool orOp = fn->logicOp == 2;
        for (size_t k = 0; k < fn->params.size(); ++k) {
            const int a = k < x.args.size() ? x.args[k] : -1;
            const VmProperty &p = *fn->params[k];
            if (a < 0 || f.owner->code.exprs[size_t(a)].token == token::kEndFunctionParms ||
                f.owner->code.exprs[size_t(a)].token == token::kNothing) {
                args[k] = p.zero();
                continue;
            }
            if (k == 1 && (andOp || orOp)) {
                const bool left = args[0].asBool();
                if ((andOp && !left) || (orOp && left))
                    return Value::boolValue(left);
                return Value::boolValue(eval(f, a, f.self).asBool());
            }
            given[k] = true;
            if (p.flags & propflag::kOutParm) {
                outs[k] = ref(f, a, f.self);
                args[k] = *outs[k];
            } else {
                args[k] = _vm.convert(eval(f, a, f.self), p);
            }
        }
        Value r = invoke(fn, target, args, fn->hasOut ? outs : kNoOuts, given, nullptr);
        _vm._callSpace[depth].args = std::move(args);
        _vm._callSpace[depth].given = std::move(given);
        return r;
    }

    Value invoke(VmFunction *fn, VmObject *self, std::vector<Value> &args, const std::vector<Value *> &outs,
                 std::vector<bool> &given, std::vector<Value> *items, std::vector<Value> *items2 = nullptr) {
        if (_vm._depth >= kMaxDepth) {
            _vm.reportError("calls nested too deep, at " + fn->qualified);
            return fn->returnValue ? fn->returnValue->zero() : Value();
        }
        ++_vm.stats.calls;
        if (given.size() < args.size())
            given.resize(args.size(), true);
        Value result = fn->returnValue ? fn->returnValue->zero() : Value();
        if (fn->flags & funcflag::kNative) {
            ++_vm.stats.nativeCalls;
            if (fn->nativeGeneration != _vm._nativeGeneration) {
                auto it = _vm._natives.find(lower(fn->qualified));
                fn->nativeFn = it != _vm._natives.end() ? &it->second : nullptr;
                fn->nativeGeneration = _vm._nativeGeneration;
            }
            if (!fn->nativeFn) {
                ++_vm.stats.missingNatives[fn->qualified];
                // A latent one still waits a tick, so state code that
                // loops round it doesn't spin.
                if (fn->flags & funcflag::kLatent)
                    _vm._pendingLatent = 0;
                return result;
            }
            NativeCall c{_vm, self, fn, std::move(args), std::move(given), result, -1, {}, {}};
            ++_vm._depth;
            (*fn->nativeFn)(c);
            --_vm._depth;
            for (size_t k = 0; k < outs.size() && k < c.args.size(); ++k)
                if (outs[k])
                    _vm.assign(*outs[k], c.args[k]);
            if (items)
                *items = std::move(c.items);
            if (items2)
                *items2 = std::move(c.items2);
            if (c.latentSeconds >= 0)
                _vm._pendingLatent = c.latentSeconds;
            // (Their storage back to the caller, to be used again.)
            args = std::move(c.args);
            given = std::move(c.given);
            if (fn->returnValue)
                return _vm.convert(c.result, *fn->returnValue);
            return c.result;
        }
        Frame frame;
        frame.owner = fn;
        frame.self = self;
        const size_t depth = _vm._localNest++;
        struct Leave {
            size_t &nest;
            ~Leave() { --nest; }
        } leave{_vm._localNest};
        if (_vm._localSpace.size() <= depth)
            _vm._localSpace.resize(depth + 1);
        std::vector<Value> locals = std::move(_vm._localSpace[depth]);
        locals.assign(fn->defaults.begin(), fn->defaults.end());
        for (size_t k = 0; k < fn->params.size() && k < args.size(); ++k)
            _vm.assign(locals[size_t(fn->params[k]->slot)], args[k]);
        frame.locals = &locals;
        ++_vm._depth;
        int pc = 0;
        run(frame, pc);
        --_vm._depth;
        _vm._pendingLatent = -1; // latent functions only wait in state code
        for (size_t k = 0; k < outs.size() && k < fn->params.size(); ++k)
            if (outs[k])
                _vm.assign(*outs[k], locals[size_t(fn->params[k]->slot)]);
        _vm._localSpace[depth] = std::move(locals);
        if (fn->returnValue && frame.result.type != VType::kNone)
            return _vm.convert(frame.result, *fn->returnValue);
        return result;
    }

private:
    // foreach: the iterator function's results, and its out parameter.
    void iterate(Frame &f, int callExpr, Iteration &it) {
        if (callExpr < 0)
            return;
        const Expr &x = f.owner->code.exprs[size_t(callExpr)];
        VmObject *target = f.self;
        const Expr *call = &x;
        // Through a context (Level.AllActors...).
        if ((x.token == token::kContext || x.token == token::kClassContext) && x.args.size() >= 2) {
            target = eval(f, x.args[0], f.self).asObject();
            if (!target) {
                accessedNone(f, x.offset);
                return;
            }
            call = &f.owner->code.exprs[size_t(x.args[1])];
        }
        VmFunction *fn = nullptr;
        if (call->native >= 0)
            fn = _vm.nativeFunction(call->native);
        else if (call->token == token::kFinalFunction)
            fn = function(f, int(call - f.owner->code.exprs.data()), call->ref);
        else if (call->token == token::kVirtualFunction)
            fn = _vm.findFunction(target, _vm.name(f.owner->package->name(call->ref)));
        if (!fn) {
            _vm.reportError("foreach over an unknown function in " + where(f));
            return;
        }
        std::vector<Value> args(fn->params.size());
        std::vector<Value *> outs(fn->params.size(), nullptr);
        std::vector<bool> given(fn->params.size(), false);
        for (size_t k = 0; k < fn->params.size(); ++k) {
            const int a = k < call->args.size() ? call->args[k] : -1;
            const VmProperty &p = *fn->params[k];
            if (a < 0 || f.owner->code.exprs[size_t(a)].token == token::kEndFunctionParms ||
                f.owner->code.exprs[size_t(a)].token == token::kNothing) {
                args[k] = p.zero();
                continue;
            }
            given[k] = true;
            if (p.flags & propflag::kOutParm) {
                outs[k] = ref(f, a, f.self);
                args[k] = *outs[k];
                if (!it.out)
                    it.out = outs[k];
                else if (!it.out2)
                    it.out2 = outs[k];
            } else {
                args[k] = _vm.convert(eval(f, a, f.self), p);
            }
        }
        // The out parameters aren't written back: the loop sets the first.
        invoke(fn, target, args, {}, given, &it.items, &it.items2);
    }

    Value conversion(uint8_t t, const Value &v) {
        switch (t) {
        case 0x39: { // rotator to vector: the direction it points
            const auto r = Vm::rotatorOf(v);
            const float p = float(r[0]) * 2 * kPi / 65536;
            const float y = float(r[1]) * 2 * kPi / 65536;
            return _vm.vectorValue(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p));
        }
        case 0x3a:
        case 0x41:
        case 0x44:
        case 0x46:
        case 0x4a:
            return Value::intValue(v.asInt());
        case 0x3b:
        case 0x3e:
        case 0x45:
        case 0x47:
        case 0x48:
        case 0x4b:
            return Value::boolValue(v.asBool());
        case 0x3c:
        case 0x3f:
        case 0x42:
        case 0x4c:
            return Value::floatValue(v.asFloat());
        case 0x3d:
        case 0x40:
        case 0x43:
        case 0x49:
            return Value::byteValue(v.asInt());
        case 0x4d: { // string to vector: "x,y,z"
            float a = 0;
            float b = 0;
            float c = 0;
            std::sscanf(v.s.c_str(), "%f,%f,%f", &a, &b, &c);
            return _vm.vectorValue(a, b, c);
        }
        case 0x4e: {
            int a = 0;
            int b = 0;
            int c = 0;
            std::sscanf(v.s.c_str(), "%d,%d,%d", &a, &b, &c);
            return _vm.rotatorValue(a, b, c);
        }
        case 0x4f: {
            const auto a = Vm::vectorOf(v);
            return Value::boolValue(a[0] != 0 || a[1] != 0 || a[2] != 0);
        }
        case 0x50: { // vector to rotator: pointing along it
            const auto a = Vm::vectorOf(v);
            const float yaw = std::atan2(a[1], a[0]);
            const float pitch = std::atan2(a[2], std::sqrt(a[0] * a[0] + a[1] * a[1]));
            return _vm.rotatorValue(int32_t(pitch * 32768 / kPi) & 0xffff, int32_t(yaw * 32768 / kPi) & 0xffff, 0);
        }
        case 0x51: {
            const auto r = Vm::rotatorOf(v);
            return Value::boolValue(r[0] != 0 || r[1] != 0 || r[2] != 0);
        }
        default:
            if (t >= 0x52 && t <= 0x59)
                return Value::stringValue(_vm.toString(v));
            return v;
        }
    }

    int arg(const Expr &x, size_t k) const { return k < x.args.size() ? x.args[k] : -1; }
    Value *temp(Frame &f, Value v) {
        if (!f.temps)
            f.temps = std::make_unique<std::deque<Value>>();
        f.temps->push_back(std::move(v));
        return &f.temps->back();
    }
    int at(Frame &f, uint32_t offset) {
        auto it = f.owner->statementAt.find(offset);
        if (it == f.owner->statementAt.end()) {
            _vm.reportError("a jump into the middle of a statement in " + where(f));
            return -1;
        }
        return it->second;
    }
    int labelAt(const VmStruct *owner, NameId label) {
        if (owner->kind != VmStruct::Kind::kState && owner->kind != VmStruct::Kind::kClass)
            return -1;
        const auto *st = static_cast<const VmState *>(owner);
        auto it = st->labels.find(label);
        if (it == st->labels.end())
            return -1;
        auto s = owner->statementAt.find(it->second);
        return s == owner->statementAt.end() ? -1 : s->second;
    }
    // `offset`: the context expression's, in the code as ue-script lists it.
    void accessedNone(Frame &f, uint32_t offset) {
        ++_vm.stats.accessedNone;
        const std::string at = where(f);
        _vm.stats.errors["Accessed None in " + at]++;
        char buf[16];
        std::snprintf(buf, sizeof buf, " at 0x%04x", offset);
        _vm.stats.noneAt[at + buf]++;
    }
    std::string where(const Frame &f) const {
        if (f.owner->kind == VmStruct::Kind::kFunction)
            return static_cast<const VmFunction *>(f.owner)->qualified;
        if (f.owner->kind == VmStruct::Kind::kState)
            return static_cast<const VmState *>(f.owner)->owner->name + "." + f.owner->name + " (state code)";
        return f.owner->name;
    }
    static std::string hex(uint8_t v) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%02x", v);
        return buf;
    }
    std::string lowerName(const VmClass *c) const { return lower(c->name); }

    int arrayDim(Frame &f, int i) {
        if (i < 0)
            return 1;
        const Expr &x = f.owner->code.exprs[size_t(i)];
        if (x.token == token::kLocalVariable || x.token == token::kInstanceVariable ||
            x.token == token::kDefaultVariable) {
            VmProperty *p = property(f, i, x.ref);
            return p ? p->arrayDim : 1;
        }
        if (x.token == token::kContext || x.token == token::kClassContext)
            return arrayDim(f, arg(x, 1));
        if (x.token == token::kStructMember) {
            VmProperty *p = property(f, i, x.ref);
            return p ? p->arrayDim : 1;
        }
        return 1;
    }

    // An expression's reference, resolved once.
    VmProperty *property(Frame &f, int i, int32_t ref) {
        const void *&cached = f.owner->resolved[size_t(i)];
        if (!cached) {
            const ObjectRef r = _vm.packages().resolve(*f.owner->package, ref);
            cached = r ? _vm.propertyAt(*r.package, r.exportIndex) : nullptr;
        }
        return static_cast<VmProperty *>(const_cast<void *>(cached));
    }
    VmFunction *function(Frame &f, int i, int32_t ref) {
        const void *&cached = f.owner->resolved[size_t(i)];
        if (!cached) {
            const ObjectRef r = _vm.packages().resolve(*f.owner->package, ref);
            VmStruct *s = r ? _vm.structAt(*r.package, r.exportIndex) : nullptr;
            cached = s && s->kind == VmStruct::Kind::kFunction ? s : nullptr;
        }
        return static_cast<VmFunction *>(const_cast<void *>(cached));
    }
    VmClass *classRef(Frame &f, int i, int32_t ref) {
        const void *&cached = f.owner->resolved[size_t(i)];
        if (!cached) {
            const ObjectRef r = _vm.packages().resolve(*f.owner->package, ref);
            cached = r ? _vm.classAt(*r.package, r.exportIndex) : _vm.nativeClass(f.owner->package->objectName(ref));
        }
        return static_cast<VmClass *>(const_cast<void *>(cached));
    }

    Vm &_vm;
};

// --- Calls, states, ticking -------------------------------------------------------------

VmFunction *Vm::findFunction(VmObject *self, NameId fname, bool global) {
    if (!self)
        return nullptr;
    if (self->asClass) {
        for (VmClass *c = self->asClass; c; c = c->parent())
            if (auto it = c->functions.find(fname); it != c->functions.end())
                return it->second;
        return nullptr;
    }
    if (!global)
        for (VmStruct *s = self->state; s && s->kind == VmStruct::Kind::kState; s = s->super) {
            auto *st = static_cast<VmState *>(s);
            if (auto it = st->functions.find(fname); it != st->functions.end())
                return it->second;
        }
    for (VmClass *c = self->cls; c; c = c->parent())
        if (auto it = c->functions.find(fname); it != c->functions.end())
            return it->second;
    return nullptr;
}

Value Vm::invoke(VmFunction *f, VmObject *self, std::vector<Value> args, std::vector<Value *> outs) {
    Interpreter in(*this);
    std::vector<Value> converted(f->params.size());
    std::vector<bool> given(f->params.size(), false);
    for (size_t k = 0; k < f->params.size(); ++k) {
        given[k] = k < args.size();
        converted[k] = k < args.size() ? convert(args[k], *f->params[k]) : f->params[k]->zero();
    }
    outs.resize(f->params.size(), nullptr);
    return in.invoke(f, self, converted, outs, given, nullptr);
}

Value Vm::callEvent(VmObject *self, std::string_view function, std::vector<Value> args) {
    if (!self || self->deleted || holdEvents)
        return Value();
    const NameId n = name(function);
    if (self->disabled.count(n))
        return Value();
    VmFunction *f = findFunction(self, n);
    if (!f)
        return Value();
    if (f->doesNothing < 0) {
        const Code &c = f->code;
        const bool empty = !f->native && !(f->flags & funcflag::kNative) && !f->returnValue && !c.statements.empty() &&
                           c.exprs[size_t(c.statements[0])].token == token::kReturn &&
                           (c.exprs[size_t(c.statements[0])].args.empty() ||
                            c.exprs[size_t(c.exprs[size_t(c.statements[0])].args[0])].token == token::kNothing);
        f->doesNothing = empty ? 1 : 0;
    }
    return f->doesNothing ? Value() : invoke(f, self, std::move(args));
}

VmState *Vm::findState(VmClass *cls, NameId stateName) const {
    const bool autoState = lower(nameText(stateName)) == "auto";
    for (VmClass *c = cls; c; c = c->parent()) {
        if (autoState) {
            for (const auto &[n, st] : c->states)
                if (st->stateFlags & 0x2) // auto
                    return st;
            continue;
        }
        if (auto it = c->states.find(stateName); it != c->states.end())
            return it->second;
    }
    return nullptr;
}

void Vm::gotoState(VmObject *self, NameId stateName, NameId label) {
    if (!self || !self->cls || self->deleted)
        return;
    VmState *next = stateName == 0 ? nullptr : findState(self->cls, stateName);
    if (stateName != 0 && !next && lower(nameText(stateName)) != "auto") {
        reportError(self->cls->name + " has no state " + nameText(stateName));
        return;
    }
    if (next != self->state) {
        if (self->state)
            callEvent(self, "EndState");
        self->state = next;
        ++self->stateSerial;
        if (next)
            callEvent(self, "BeginState");
    } else {
        ++self->stateSerial;
    }
    self->codeAt = -1;
    self->codeState = nullptr;
    self->latent = false;
    self->sleeping = 0;
    if (self->state) {
        const NameId want = label ? label : name("Begin");
        for (VmStruct *s = self->state; s && s->kind == VmStruct::Kind::kState; s = s->super) {
            auto *st = static_cast<VmState *>(s);
            if (auto it = st->labels.find(want); it != st->labels.end()) {
                if (auto a = s->statementAt.find(it->second); a != s->statementAt.end()) {
                    self->codeState = st; // the state that has the label
                    self->codeAt = a->second;
                }
                break;
            }
        }
    }
}

// An actor's script side of a tick, in the engine's order: its Tick event
// (unless `sendTick` is off: the player gets PlayerTick instead), its state
// code, then its timer, which goes off at most once a tick (what's left
// over a whole number of rates is kept).
void Vm::tick(VmObject *self, float seconds, bool sendTick) {
    if (!self || self->deleted || !self->cls)
        return;
    if (sendTick)
        callEvent(self, "Tick", {Value::floatValue(seconds)});
    if (self->deleted)
        return;
    bool run = true;
    if (self->latent) {
        self->sleeping -= seconds;
        if (self->sleeping > 0)
            run = false;
        else
            self->latent = false;
    }
    for (int restarts = 0; run && restarts < 16 && self->codeState && self->codeAt >= 0 && !self->deleted; ++restarts) {
        Interpreter in(*this);
        Interpreter::Frame f;
        f.owner = self->codeState;
        f.self = self;
        int pc = self->codeAt;
        const int serial = self->stateSerial;
        const Interpreter::Flow flow = in.run(f, pc);
        if (self->stateSerial != serial)
            continue; // it changed state: run the new state's code
        self->codeAt = flow == Interpreter::Flow::kWait ? pc : -1;
        break;
    }
    if (!self->deleted && self->timerRate > 0) {
        self->timerCount += seconds;
        if (self->timerCount >= self->timerRate) {
            // What's over the whole rates stays counted, looping or not.
            self->timerCount = std::fmod(self->timerCount, self->timerRate);
            if (!self->timerLoop)
                self->timerRate = 0;
            callEvent(self, "Timer");
        }
    }
}

} // namespace reluva::unreal
