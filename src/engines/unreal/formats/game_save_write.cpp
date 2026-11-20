// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A level saved as the original game saves it: a package (.dsa) that the
// original loads, and so does Reluva. It's written over the map's own
// tables, those exports the game never loads left out and the rest numbered
// again: the parts the level hasn't changed (its BSP, the movers' brushes,
// the lights' surfaces) are the map's data, their references renumbered;
// what the level has (its actors, their state, the objects they made) is
// written from the running level. See
// docs/formats/unreal/save.md.

#include "engines/unreal/formats/model.h"
#include "engines/unreal/vm/vm_level.h"

#include "engines/unreal/formats/game_save.h"
#include "engines/unreal/formats/package_writer.h"
#include "engines/unreal/vm/script.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <set>
#include <sstream>
#include <functional>

namespace reluva::unreal {

namespace {

constexpr uint32_t kClassTransient = 0x8;
constexpr uint32_t kActorFlags = 0x00070001 | objflag::kHasStack;
constexpr uint32_t kObjectFlags = 0x00070000;

// An object reference in the map's numbering to the save's (0: left out).
using Renumber = std::function<int32_t(int32_t)>;

// A copy of an export's data with its object references renumbered: the
// bytes between them as they are, each reference written again.
class RefCopy {
public:
    explicit RefCopy(const std::vector<uint8_t> &data) : r(data), _data(data) {}
    Reader r;
    int32_t ref(const Renumber &f) {
        flush();
        const int32_t v = r.index();
        _out.index(f(v));
        _from = r.pos();
        return v;
    }
    // A state frame (an object flagged HasStack): its node and state, the
    // events it ignores, its latent action, and in a node, where.
    void frame(const Renumber &f) {
        const int32_t node = ref(f);
        ref(f);
        r.skip(12);
        if (node != 0)
            r.index();
    }
    // An array's count, each element at least `least` bytes.
    size_t count(size_t least) {
        const int32_t n = r.index();
        if (!r.ok() || n < 0 || size_t(n) * least > r.remaining()) {
            r.fail();
            return 0;
        }
        return size_t(n);
    }
    std::vector<uint8_t> finish() {
        _out.data.insert(_out.data.end(), _data.begin() + std::ptrdiff_t(_from), _data.end());
        return std::move(_out.data);
    }

private:
    void flush() {
        _out.data.insert(_out.data.end(), _data.begin() + std::ptrdiff_t(_from),
                         _data.begin() + std::ptrdiff_t(r.pos()));
        _from = r.pos();
    }
    const std::vector<uint8_t> &_data;
    ByteWriter _out;
    size_t _from = 0;
};

// A Model's references (model.cpp reads the rest), objects' and names': its
// state frame if it has one, no properties (None), its
// bounds, vectors, points and nodes; each surface's texture and actor (a
// brush the editor built it from); its vertices; each zone's actor; its
// Polys; its light maps, light bits, bounds, leaf hulls and leaves; its
// lights (actors, each light map's list ended by none).
bool modelRefs(const std::vector<uint8_t> &data, int version, bool stack, int32_t none, const Renumber &f,
               const Renumber &names, std::vector<uint8_t> &out) {
    RefCopy c(data);
    Reader &r = c.r;
    if (stack)
        c.frame(f);
    if (c.ref(names) != none)
        return false;
    r.skip(version > 61 ? 41 : 37);
    r.skip(c.count(12) * 12);
    r.skip(c.count(12) * 12);
    for (size_t n = c.count(43); n > 0 && r.ok(); --n) {
        r.skip(25);
        for (int k = 0; k < 7; ++k)
            r.index();
        r.skip(11);
    }
    for (size_t n = c.count(14); n > 0 && r.ok(); --n) {
        c.ref(f);
        r.u32();
        for (int k = 0; k < 6; ++k)
            r.index();
        r.skip(4);
        c.ref(f);
    }
    for (size_t n = c.count(2); n > 0 && r.ok(); --n) {
        r.index();
        r.index();
    }
    r.i32();
    const int32_t zones = r.i32();
    if (!r.ok() || zones < 0 || zones > 64)
        return false;
    for (int32_t z = 0; z < zones && r.ok(); ++z) {
        c.ref(f);
        r.skip(16);
    }
    c.ref(f);
    for (size_t n = c.count(30); n > 0 && r.ok(); --n) {
        r.skip(16);
        r.index();
        r.index();
        r.skip(12);
    }
    r.skip(c.count(1));
    r.skip(c.count(25) * 25);
    r.skip(c.count(4) * 4);
    for (size_t n = c.count(11); n > 0 && r.ok(); --n) {
        r.index();
        r.index();
        r.index();
        r.skip(8);
    }
    for (size_t n = c.count(1); n > 0 && r.ok(); --n)
        c.ref(f);
    if (!r.ok())
        return false;
    out = c.finish();
    return true;
}

// A Polys object's references, objects' and names': its state frame if it
// has one, no properties (None), the count twice, then each
// polygon's actor (a brush), texture and item name.
bool polysRefs(const std::vector<uint8_t> &data, bool stack, int32_t none, const Renumber &f, const Renumber &names,
               std::vector<uint8_t> &out) {
    RefCopy c(data);
    Reader &r = c.r;
    if (stack)
        c.frame(f);
    if (c.ref(names) != none)
        return false;
    const int32_t count = r.i32();
    r.i32();
    if (!r.ok() || count < 0)
        return false;
    for (int32_t k = 0; k < count && r.ok(); ++k) {
        const int32_t n = r.index();
        if (n < 0 || n > 64)
            return false;
        r.skip(48 + size_t(n) * 12 + 4);
        c.ref(f);
        c.ref(f);
        c.ref(names); // its item name
        r.index();
        r.index();
        r.skip(4);
    }
    if (!r.ok())
        return false;
    out = c.finish();
    return true;
}

// An object's tagged properties written again, its object and name
// references renumbered (each property's size worked out afresh, as a
// reference's length may change): after its state frame if it has one,
// up to None, then the rest as it is. Structs: PointRegion (its zone, a
// leaf, a zone number) and those of plain numbers. False for anything else
// (arrays, other structs), or if they don't read.
bool rewriteProperties(const Package &map, const std::vector<uint8_t> &data, bool stack, const Renumber &refs,
                       const Renumber &names, std::vector<uint8_t> &out) {
    Reader r(data);
    ByteWriter w;
    if (stack) {
        const int32_t node = r.index();
        w.index(refs(node));
        w.index(refs(r.index()));
        for (int k = 0; k < 12; ++k)
            w.u8(r.u8());
        if (node != 0)
            w.index(r.index());
    }
    auto nameOf = [&](int32_t n) {
        return n >= 0 && size_t(n) < map.names().size() ? fs::toLower(map.names()[size_t(n)].name) : std::string();
    };
    static const std::set<std::string> kPlain = {"vector", "rotator", "color",  "plane",      "scale",
                                                 "coords", "range",   "sphere", "boundingbox"};
    for (int count = 0; r.ok() && count < 100000; ++count) {
        const int32_t name = r.index();
        w.index(names(name));
        if (nameOf(name) == "none")
            break;
        const uint8_t info = r.u8();
        const auto type = static_cast<PropertyType>(info & 15);
        int32_t structName = -1;
        if (type == PropertyType::kStruct)
            structName = r.index();
        static constexpr size_t kSizes[5] = {1, 2, 4, 12, 16};
        const int code = (info >> 4) & 7;
        const size_t size = code < 5 ? kSizes[code] : code == 5 ? r.u8() : code == 6 ? r.u16() : size_t(r.u32());
        std::vector<uint8_t> index;
        if ((info & 0x80) && type != PropertyType::kBool) {
            const uint8_t b0 = r.u8();
            index.push_back(b0);
            for (int k = (b0 & 0xc0) == 0x80 ? 1 : (b0 & 0xc0) == 0xc0 ? 3 : 0; k > 0; --k)
                index.push_back(r.u8());
        }
        if (!r.ok() || r.remaining() < (type == PropertyType::kBool ? 0 : size))
            return false;
        ByteWriter value;
        const size_t at = data.size() - r.remaining();
        switch (type) {
        case PropertyType::kBool:
            break;
        case PropertyType::kObject:
        case PropertyType::kClass:
            value.index(refs(r.index()));
            break;
        case PropertyType::kName:
            value.index(names(r.index()));
            break;
        case PropertyType::kStruct:
            if (nameOf(structName) == "pointregion") {
                value.index(refs(r.index()));
                value.i32(r.i32());
                value.u8(r.u8());
                break;
            }
            if (!kPlain.count(nameOf(structName)))
                return false;
            [[fallthrough]];
        case PropertyType::kByte:
        case PropertyType::kInt:
        case PropertyType::kFloat:
        case PropertyType::kString:
        case PropertyType::kVector:
        case PropertyType::kRotator:
        case PropertyType::kStr:
            value.bytes(data.data() + at, size);
            r.skip(size);
            break;
        default:
            return false;
        }
        if (type != PropertyType::kBool && data.size() - r.remaining() != at + size)
            return false;
        // The tag again: its type, its size's code, then its value.
        const size_t n = type == PropertyType::kBool ? size : value.data.size();
        int newCode = 7;
        for (int k = 0; k < 5; ++k)
            if (n == kSizes[k])
                newCode = k;
        if (newCode == 7)
            newCode = n <= 0xff ? 5 : n <= 0xffff ? 6 : 7;
        w.u8(uint8_t((info & 0x8f) | (newCode << 4)));
        if (type == PropertyType::kStruct)
            w.index(names(structName));
        if (newCode == 5)
            w.u8(uint8_t(n));
        else if (newCode == 6)
            w.u16(uint16_t(n));
        else if (newCode == 7)
            w.u32(uint32_t(n));
        w.bytes(index.data(), index.size());
        w.append(value.data);
    }
    if (!r.ok())
        return false;
    w.bytes(data.data() + (data.size() - r.remaining()), r.remaining());
    out = std::move(w.data);
    return true;
}

// Data that starts with no properties (None) and holds no other names or
// objects (a C3D mesh state: its strings are its own).
bool leadingName(const std::vector<uint8_t> &data, int32_t none, const Renumber &names, std::vector<uint8_t> &out) {
    RefCopy c(data);
    if (c.ref(names) != none)
        return false;
    out = c.finish();
    return true;
}

// The Level object's model and reach list (levelModelSpan): the model,
// then each reach's start and end (navigation points).
bool reachRefs(const std::vector<uint8_t> &span, const Renumber &f, std::vector<uint8_t> &out) {
    RefCopy c(span);
    Reader &r = c.r;
    c.ref(f);
    for (int32_t n = r.index(); n > 0 && r.ok(); --n) {
        r.i32();
        c.ref(f);
        c.ref(f);
        r.skip(13);
    }
    if (!r.ok() || r.remaining() != 0)
        return false;
    out = c.finish();
    return true;
}

bool transientClass(const VmClass *c) {
    for (; c; c = c->parent())
        if (c->classFlags & kClassTransient)
            return true;
    return false;
}

bool sameValue(const Value &a, const Value &b) {
    if (a.type != b.type)
        return false;
    switch (a.type) {
    case VType::kFloat:
        return a.f == b.f || (std::isnan(a.f) && std::isnan(b.f));
    case VType::kObject:
        return a.asObject() == b.asObject();
    case VType::kString:
        return a.s == b.s;
    case VType::kStruct:
        if (a.fields.size() != b.fields.size())
            return false;
        for (size_t k = 0; k < a.fields.size(); ++k)
            if (!sameValue(a.fields[k], b.fields[k]))
                return false;
        return true;
    default:
        return a.i == b.i;
    }
}

} // namespace

// Saved in two goes: the first finds every name and import the save uses
// (none of its file written), the second writes it with only those, the
// tables numbered again (as the original's saves have only what they use).
Error ScriptLevel::writeGameSave(const std::string &path) {
    SaveTables tables;
    if (Error err = writeSave(path, tables, true))
        return err;
    return writeSave(path, tables, false);
}

Error ScriptLevel::writeSave(const std::string &path, SaveTables &tables, bool collect) {
    PackageWriter w(_map);
    if (!collect && tables.trim)
        w.keepTables(tables.names, tables.imports);
    int32_t mapNone = 0;
    for (size_t k = 0; k < _map.names().size(); ++k)
        if (fs::toLower(_map.names()[k].name) == "none")
            mapNone = int32_t(k);
    const Renumber same = [](int32_t n) { return n; };
    const Renumber baseNames = [&](int32_t n) { return w.baseName(n); };
    const int version = _map.version();
    if (!_levelInfo || _levelInfo->package != &_map)
        return Error::fail("the level has no LevelInfo of its map's");
    const int32_t mapLevelOuter = _map.exports()[size_t(_levelInfo->exportIndex)].package;

    // What's saved: the level's actors and ghosts, and every object they
    // lead to that the level made, or that's one of the map's own; not the
    // engine's viewport, console and canvas, nor objects of transient
    // classes.
    VmObject *viewport = nullptr;
    VmObject *console = nullptr;
    if (_player)
        if (const Value *v = prop(_player, "Player"); v && v->asObject()) {
            viewport = v->o;
            if (const Value *c = prop(viewport, "Console"))
                console = c->asObject();
        }
    std::map<const VmObject *, int32_t> refs;
    std::vector<VmObject *> saved;
    std::deque<VmObject *> todo;
    auto wanted = [&](VmObject *o) {
        return o && !o->deleted && !o->asClass && o->cls && o != viewport && o != console && o != _canvas &&
               (!o->package || o->package == &_map) && (o->package || o->cls->package) && !transientClass(o->cls);
    };
    auto add = [&](VmObject *o) {
        if (wanted(o) && !refs.count(o)) {
            refs[o] = 0;
            saved.push_back(o);
            todo.push_back(o);
        }
    };
    for (VmObject *a : _actors)
        add(a);
    for (VmObject *g : _ghosts)
        add(g);
    std::function<void(const Value &)> scan = [&](const Value &v) {
        if (v.type == VType::kObject)
            add(v.asObject());
        else if (v.type == VType::kStruct)
            for (const Value &f : v.fields)
                scan(f);
    };
    while (!todo.empty()) {
        VmObject *o = todo.front();
        todo.pop_front();
        // The map's own objects that aren't actors (its models, mesh
        // states) are kept as they are: what they lead to is too.
        if (o->package == &_map && !o->isActor)
            continue;
        for (const Value &v : o->values)
            scan(v);
    }
    // The map's exports kept, numbered again in their order: those saved,
    // the Level object, what its model and reach list, the models and their
    // polygons lead to, and the packages they're in. Not those for the
    // editor alone (UnrealEd's building brushes, their models and polygons,
    // its cameras), which the game never loads (Core.dll 0x1012d02b): the
    // original's saves leave them out, and references to them are none.
    // Should the map's data not read so, all are kept, those empty.
    const auto &exports = _map.exports();
    const std::vector<int> levels = _map.exportsOfClass("Level");
    if (levels.empty())
        return Error::fail(_map.name() + ": no Level object");
    std::vector<uint8_t> mapLevel;
    if (Error err = _map.readExport(levels.front(), mapLevel))
        return err;
    size_t modelBegin = 0;
    size_t reachEnd = 0;
    if (!levelModelSpan(mapLevel, version, modelBegin, reachEnd))
        return Error::fail(_map.name() + ": its Level object doesn't read");
    const std::vector<uint8_t> reach(mapLevel.begin() + std::ptrdiff_t(modelBegin),
                                     mapLevel.begin() + std::ptrdiff_t(reachEnd));
    auto editorOnly = [&](size_t i) {
        return !(exports[i].flags & (objflag::kLoadForClient | objflag::kLoadForServer));
    };
    std::vector<bool> keep(exports.size(), false);
    std::deque<size_t> leadsTo;
    // (An actor deleted, by the level or in the map itself, isn't kept, as
    // the original's saves have none: what points at it, a model's list of
    // lights too, is none.)
    auto deletedActor = [&](size_t i) {
        if (VmObject *o = _vm.objectAt(_map, int32_t(i) + 1); o && o->isActor)
            if (refs.count(o) || o->deleted)
                return o->deleted && !refs.count(o);
        // One the level never had: deleted in the map itself (bDeleteMe).
        std::vector<uint8_t> data;
        if (_map.readExport(int(i), data))
            return false;
        Reader r(data, size_t(exports[i].serialOffset));
        PropertyList props;
        return !beginObject(_map, int(i), r, props) && props.boolean("bDeleteMe");
    };
    auto keepRef = [&](int32_t ref) {
        if (ref > 0 && size_t(ref) <= exports.size() && !keep[size_t(ref - 1)] && !editorOnly(size_t(ref - 1)) &&
            !deletedActor(size_t(ref - 1))) {
            keep[size_t(ref - 1)] = true;
            leadsTo.push_back(size_t(ref - 1));
        }
        return ref;
    };
    for (VmObject *o : saved)
        if (o->package == &_map)
            keepRef(o->exportIndex + 1);
    keepRef(levels.front() + 1);
    std::vector<uint8_t> scratch;
    bool renumber = reachRefs(reach, keepRef, scratch);
    while (renumber && !leadsTo.empty()) {
        const size_t i = leadsTo.front();
        leadsTo.pop_front();
        // Its package (or group), whatever its flags.
        for (int32_t outer = exports[i].package; outer > 0 && !keep[size_t(outer - 1)];
             outer = exports[size_t(outer - 1)].package) {
            keep[size_t(outer - 1)] = true;
            leadsTo.push_back(size_t(outer - 1));
        }
        const std::string cls = fs::toLower(_map.className(int32_t(i) + 1));
        std::vector<uint8_t> data;
        const bool stack = exports[i].flags & objflag::kHasStack;
        if (cls == "model" || cls == "polys") {
            renumber = !_map.readExport(int(i), data) &&
                       (cls == "model" ? modelRefs(data, version, stack, mapNone, keepRef, same, scratch)
                                       : polysRefs(data, stack, mapNone, keepRef, same, scratch));
            continue;
        }
        // One kept as the map has it: what its properties lead to too.
        // What the level has now leads where its values do.
        if (VmObject *o = _vm.objectAt(_map, int32_t(i) + 1); o && refs.count(o))
            continue;
        if (cls == "c3dmeshstate" || exports[i].serialSize <= 0)
            continue;
        renumber = !_map.readExport(int(i), data) && rewriteProperties(_map, data, stack, keepRef, same, scratch);
    }
    if (!renumber)
        keep.assign(exports.size(), true);
    const std::vector<int32_t> renumbered = w.keepOnly(keep);
    auto fromMap = [&](int32_t ref) {
        return ref > 0 && size_t(ref) < renumbered.size() ? renumbered[size_t(ref)] : w.baseRef(ref);
    };

    // The new exports, named as the level names them (made unique), in their
    // outer.
    std::set<std::pair<int32_t, std::string>> taken;    // outer, lower-case name
    std::map<const VmObject *, std::string> savedNames; // those renamed or new
    for (size_t i = 0; i < exports.size(); ++i)
        if (fromMap(int32_t(i) + 1) > 0)
            taken.insert({fromMap(exports[i].package), fs::toLower(_map.name(exports[i].objectName))});
    auto classImport = [&](const VmClass *c) { return w.import(c->package->name() + "." + c->name, "Core", "Class"); };
    for (VmObject *o : saved)
        if (o->package == &_map)
            refs[o] = fromMap(o->exportIndex + 1);
    const int32_t levelOuter = fromMap(mapLevelOuter);
    for (VmObject *o : saved) {
        if (o->package)
            continue;
        int32_t outer = levelOuter;
        if (!o->isActor)
            if (const Value *v = prop(o, "Outer"); v && v->asObject() && refs.count(v->o) && refs[v->o] > 0)
                outer = refs[v->o];
        std::string name = _vm.nameText(o->name);
        if (name.empty() || name == "None")
            name = o->cls->name;
        for (int n = 1; taken.count({outer, fs::toLower(name)}); ++n)
            name = _vm.nameText(o->name) + "_" + std::to_string(n);
        taken.insert({outer, fs::toLower(name)});
        savedNames[o] = name;
        refs[o] = w.addExport(classImport(o->cls), outer, name, o->isActor ? kActorFlags : kObjectFlags);
    }

    // An object reference as the save has it.
    auto refOf = [&](VmObject *o) -> int32_t {
        if (!o || o->deleted)
            return 0;
        if (o->asClass) {
            const VmClass *c = o->asClass;
            return c->package ? classImport(c) : 0;
        }
        if (auto it = refs.find(o); it != refs.end())
            return it->second;
        if (o->package && o->package != &_map && o->exportIndex >= 0) {
            const Package &p = *o->package;
            const Export &e = p.exports()[size_t(o->exportIndex)];
            std::string classPackage = "Core";
            std::string className = "Object";
            if (e.classRef < 0) {
                const Import &ci = p.imports()[size_t(-e.classRef - 1)];
                className = p.name(ci.objectName);
                const int32_t outer = ci.package;
                classPackage = outer < 0 ? p.name(p.imports()[size_t(-outer - 1)].objectName) : p.name(ci.classPackage);
            } else if (e.classRef > 0) {
                className = p.name(p.exports()[size_t(e.classRef - 1)].objectName);
                classPackage = p.name();
            } else {
                className = "Class";
            }
            return w.import(p.name() + "." + p.path(o->exportIndex + 1), classPackage, className);
        }
        return 0;
    };

    // Values as properties and struct fields store them.
    std::function<void(ByteWriter &, const VmProperty &, const Value &)> valueBytes =
        [&](ByteWriter &b, const VmProperty &p, const Value &v) {
            switch (p.type) {
            case VType::kByte:
                b.u8(uint8_t(v.asInt()));
                break;
            case VType::kInt:
                b.i32(v.asInt());
                break;
            case VType::kBool:
                b.u8(v.asBool() ? 1 : 0);
                break;
            case VType::kFloat:
                b.f32(v.asFloat());
                break;
            case VType::kName:
                b.index(w.name(v.i ? _vm.nameText(v.i) : "None"));
                break;
            case VType::kString:
                b.string(v.s);
                break;
            case VType::kObject:
                b.index(refOf(v.o));
                break;
            case VType::kStruct:
                if (p.structType)
                    for (const VmProperty *f : p.structType->properties)
                        for (int k = 0; k < f->arrayDim; ++k) {
                            const size_t at = size_t(f->slot + k);
                            valueBytes(b, *f, at < v.fields.size() ? v.fields[at] : f->zero());
                        }
                break;
            default:
                break;
            }
        };
    // Its properties that differ from its class's defaults, tagged; then None.
    // (A class's own first, then its parent's, as the engine lists them.)
    auto properties = [&](ByteWriter &b, const VmObject *o) {
        const VmClass *c = o->cls;
        std::vector<const VmProperty *> order;
        for (const VmClass *k = c; k; k = k->parent()) {
            const int from = k->parent() ? k->parent()->slots : 0;
            for (const VmProperty *p : k->properties)
                if (p->slot >= from && p->slot < k->slots && std::find(order.begin(), order.end(), p) == order.end())
                    order.push_back(p);
        }
        for (const VmProperty *p : order) {
            if (p->flags & propflag::kTransient)
                continue;
            if (fs::toLower(p->name) == "outer" || fs::toLower(p->name) == "class" || fs::toLower(p->name) == "name")
                continue;
            for (int k = 0; k < p->arrayDim; ++k) {
                const size_t slot = size_t(p->slot + k);
                if (slot >= o->values.size())
                    continue;
                Value v = o->values[slot];
                if (v.type == VType::kObject && refOf(v.asObject()) == 0)
                    v.o = nullptr;
                const Value &d = slot < c->defaults.size() ? c->defaults[slot] : p->zero();
                if (sameValue(v, d) || (v.type == VType::kObject && !v.o && !d.asObject()))
                    continue;
                if (p->type == VType::kNone)
                    continue;
                b.index(w.name(p->name));
                if (p->type == VType::kBool) {
                    b.u8(uint8_t(3 | (v.asBool() ? 0x80 : 0)));
                    continue;
                }
                ByteWriter data;
                valueBytes(data, *p, v);
                uint8_t type = 0;
                switch (p->type) {
                case VType::kByte:
                    type = 1;
                    break;
                case VType::kInt:
                    type = 2;
                    break;
                case VType::kFloat:
                    type = 4;
                    break;
                case VType::kObject:
                    type = 5;
                    break;
                case VType::kName:
                    type = 6;
                    break;
                case VType::kString:
                    type = 13;
                    break;
                case VType::kStruct:
                    type = 10;
                    break;
                default:
                    break;
                }
                const size_t n = data.data.size();
                uint8_t sizeCode = n == 1      ? 0
                                   : n == 2    ? 1
                                   : n == 4    ? 2
                                   : n == 12   ? 3
                                   : n == 16   ? 4
                                   : n < 256   ? 5
                                   : n < 65536 ? 6
                                               : 7;
                b.u8(uint8_t(type | sizeCode << 4 | (k > 0 ? 0x80 : 0)));
                if (p->type == VType::kStruct)
                    b.index(w.name(p->structType ? p->structType->name : "None"));
                if (sizeCode == 5)
                    b.u8(uint8_t(n));
                else if (sizeCode == 6)
                    b.u16(uint16_t(n));
                else if (sizeCode == 7)
                    b.u32(uint32_t(n));
                if (k > 0) {
                    if (k < 128)
                        b.u8(uint8_t(k));
                    else if (k < 16384) {
                        b.u8(uint8_t(0x80 | (k >> 8)));
                        b.u8(uint8_t(k));
                    } else {
                        b.u8(uint8_t(0xC0 | (k >> 24)));
                        b.u8(uint8_t(k >> 16));
                        b.u8(uint8_t(k >> 8));
                        b.u8(uint8_t(k));
                    }
                }
                b.append(data.data);
            }
        }
        b.index(w.name("None"));
    };
    // A state, or a class as its own state, as an import.
    auto stateRef = [&](const VmState *s) -> int32_t {
        if (!s)
            return 0;
        if (s->kind == VmStruct::Kind::kClass)
            return classImport(static_cast<const VmClass *>(s));
        const VmClass *owner = s->owner;
        if (!owner || !owner->package)
            return 0;
        return w.importIn(classImport(owner), s->name, "Core", "State");
    };
    // An actor's state frame: its code's node and its state, the events it
    // listens for (its state's and class's, less those it ignores or has
    // disabled; all, for one that never began), its latent action and
    // where its code is.
    auto frame = [&](ByteWriter &b, VmObject *a) {
        const VmState *state = a->state ? a->state : a->cls;
        const VmState *node = a->codeState && a->codeAt >= 0 ? a->codeState : state;
        b.index(stateRef(node));
        b.index(stateRef(state));
        uint64_t probe = (state->probeMask | a->cls->probeMask) & state->ignoreMask;
        if (const Value *begun = prop(a, "bHasBegunPlay"); begun && !begun->asBool())
            probe = ~0ull;
        for (NameId n : a->disabled)
            for (int k = 0; k < 64; ++k)
                if (fs::toLower(_vm.nameText(n)) == fs::toLower(probeEvent(k)))
                    probe &= ~(1ull << k);
        b.u32(uint32_t(probe));
        b.u32(uint32_t(probe >> 32));
        int32_t offset = -1;
        int32_t latent = 0;
        if (a->codeState && a->codeAt >= 0) {
            // The statement it's at; waiting in Sleep, the time left goes
            // in LatentFloat; waiting in anything else, the call before it
            // is made again.
            int want = a->codeAt;
            if (a->latent && a->sleeping <= 0 && want > 0)
                --want;
            if (a->latent && a->sleeping > 0)
                latent = 256;
            for (const auto &[off, statement] : a->codeState->statementAt)
                if (statement == want)
                    offset = int32_t(off);
        }
        b.i32(latent);
        b.index(offset);
    };

    // A C3D mesh state for each saved actor with a C3D mesh and none the
    // map gave it (one spawned): new, its groups and what it holds as below,
    // no IK requests or targets (the scripts set them again each tick).
    std::map<const VmObject *, int32_t> newMeshStates;
    for (VmObject *a : saved) {
        if (!a->isActor || a->package == &_map || !usesC3DMesh(a))
            continue;
        const Value *ms = prop(a, "C3DOurMeshState");
        if (ms && ms->asObject() && refs.count(ms->o))
            continue;
        const int32_t ref = w.addExport(w.import("Engine.C3DMESHSTATE", "Core", "Class"), refs[a],
                                        "C3DMESHSTATE" + std::to_string(++_meshStates), kObjectFlags);
        newMeshStates[a] = ref;
        ByteWriter m;
        m.index(w.name("None"));
        m.u32(0xFFFFDE80u);
        m.i32(2);
        m.i32(22);
        const Value *mesh = prop(a, "Mesh");
        const std::string meshName = mesh && mesh->asObject() ? _vm.nameText(mesh->o->name) : "";
        m.u16(uint16_t(meshName.size()));
        m.bytes(meshName.data(), meshName.size());
        m.i32(0);
        m.i32(0); // IK targets
        m.i32(0);
        m.i32(5);
        m.i32(-1);
        writeMeshGroups(m, a);
        m.i32(0); // the IK chains' targets
        m.i32(0);
        m.i32(10);
        writeMeshAttachments(m, a, savedNames);
        const std::string owner = _vm.nameText(a->name);
        m.u16(uint16_t(owner.size()));
        m.bytes(owner.data(), owner.size());
        w.setData(ref, std::move(m.data));
    }

    // The objects' data.
    for (VmObject *o : saved) {
        const int32_t ref = refs[o];
        if (ref <= 0)
            continue;
        if (o->package == &_map && !o->isActor) {
            // A mesh state: its groups as the actor's animation has them,
            // and what the actor holds.
            if (o->cls && fs::toLower(o->cls->name) == "c3dmeshstate")
                if (const Value *outer = prop(o, "Outer"); outer && outer->asObject() && outer->o->isActor) {
                    std::vector<uint8_t> data;
                    size_t begin = 0;
                    size_t end = 0;
                    size_t heldBegin = 0;
                    size_t heldEnd = 0;
                    if (!_map.readExport(o->exportIndex, data) && meshStateGroupSpan(data, begin, end) &&
                        meshStateAttachmentSpan(data, heldBegin, heldEnd)) {
                        ByteWriter g;
                        writeMeshGroups(g, outer->o);
                        ByteWriter held;
                        writeMeshAttachments(held, outer->o, savedNames);
                        const auto at = [&](size_t k) { return data.begin() + std::ptrdiff_t(k); };
                        std::vector<uint8_t> out(data.begin(), at(begin));
                        out.insert(out.end(), g.data.begin(), g.data.end());
                        out.insert(out.end(), at(end), at(heldBegin));
                        out.insert(out.end(), held.data.begin(), held.data.end());
                        out.insert(out.end(), at(heldEnd), data.end());
                        if (tables.trim && !collect && leadingName(out, mapNone, baseNames, scratch))
                            out = std::move(scratch);
                        w.setData(ref, std::move(out));
                    }
                }
            continue;
        }
        ByteWriter b;
        // (Its new mesh state, where it gets one.)
        Value meshStateWas;
        Value *meshState = nullptr;
        if (auto it = newMeshStates.find(o); it != newMeshStates.end())
            if ((meshState = prop(o, "C3DOurMeshState"))) {
                meshStateWas = *meshState;
                refs[&_meshStateMarker] = it->second;
                meshState->type = VType::kObject;
                meshState->o = &_meshStateMarker;
            }
        if (o->isActor) {
            if (o->latent && o->sleeping > 0)
                if (Value *v = prop(o, "LatentFloat"))
                    *v = Value::floatValue(o->sleeping);
            frame(b, o);
            w.setFlags(ref, kActorFlags);
        }
        properties(b, o);
        if (meshState)
            *meshState = meshStateWas;
        w.setData(ref, std::move(b.data));
    }

    // What the save keeps of the map as it is: its models and their
    // polygons, and the rest the level hasn't (its summary), renumbered.
    if (renumber)
        for (size_t i = 0; i < exports.size(); ++i) {
            const int32_t ref = fromMap(int32_t(i) + 1);
            if (ref <= 0)
                continue;
            const std::string cls = fs::toLower(_map.className(int32_t(i) + 1));
            std::vector<uint8_t> data;
            const bool stack = exports[i].flags & objflag::kHasStack;
            if (cls == "model" || cls == "polys") {
                if (!_map.readExport(int(i), data) &&
                    (cls == "model" ? modelRefs(data, version, stack, mapNone, fromMap, baseNames, scratch)
                                    : polysRefs(data, stack, mapNone, fromMap, baseNames, scratch)))
                    w.setData(ref, std::move(scratch));
            } else if (w.baseData(ref) >= 0 && exports[i].serialSize > 0 && !_map.readExport(int(i), data)) {
                // The rest kept as they are, their references renumbered: a
                // mesh state's only name, others' properties.
                if (cls == "c3dmeshstate" ? leadingName(data, mapNone, baseNames, scratch)
                                          : rewriteProperties(_map, data, stack, fromMap, baseNames, scratch))
                    w.setData(ref, std::move(scratch));
            }
        }

    // The Level object.
    ByteWriter L;
    L.index(w.name("None"));
    // The actors: the LevelInfo, the builder brush's place (none), the rest.
    std::vector<int32_t> list{refs[_levelInfo], 0};
    for (VmObject *a : _actors)
        if (a != _levelInfo && refs.count(a) && refs[a] > 0)
            list.push_back(refs[a]);
    L.i32(int32_t(list.size()));
    L.i32(int32_t(list.size()));
    for (int32_t r : list)
        L.index(r);
    // The URL.
    L.string("DS9");
    L.string("");
    // (A level that's itself a saved game keeps the map it was saved on.)
    L.string(savedByGame() && !_saved.urlMap.empty() ? _saved.urlMap : _map.name());
    L.string("");
    std::vector<std::string> options{"Name=Player"};
    if (_player && _player->cls && _player->cls->package)
        options.push_back("Class=" + _player->cls->package->name() + "." + _player->cls->name);
    L.index(int32_t(options.size()));
    for (const std::string &op : options)
        L.string(op);
    L.i32(7777);
    L.i32(1);
    // The model and the reach list, as the map has them.
    if (renumber && reachRefs(reach, fromMap, scratch))
        L.append(scratch);
    else
        L.append(reach);
    const Value *now = prop(_levelInfo, "TimeSeconds");
    L.f32(now ? now->asFloat() : _time);
    L.index(0); // first deleted
    for (int k = 0; k < 16; ++k)
        L.index(0); // text blocks
    L.index(0);     // travel
    // The ghosts.
    std::vector<int32_t> ghosts;
    for (VmObject *g : _ghosts)
        if (refs.count(g) && refs[g] > 0)
            ghosts.push_back(refs[g]);
    L.index(int32_t(ghosts.size()));
    for (int32_t g : ghosts)
        L.index(g);
    writeDialogEngine(L, w, refOf);
    // The sounds playing (the music the HUD's, playing since it began).
    struct Entry {
        int32_t actor;
        int32_t id;
        std::string path;
        float volume;
        float radius;
        float pitch;
        float start;
    };
    std::vector<Entry> sounds;
    auto pathOf = [](const ObjectRef &s) { return s.package->name() + "." + s.package->path(s.exportIndex + 1); };
    std::map<int32_t, int> listIndex;
    for (size_t k = 0; k < list.size(); ++k)
        listIndex[list[k]] = int(k);
    int none = 0;
    for (const TrackedSound &t : _trackedSounds) {
        if (t.start + t.seconds < _time || !t.request.actor || !refs.count(t.request.actor) || !t.request.sound)
            continue;
        const int32_t actor = refs[t.request.actor];
        const int index = listIndex.count(actor) ? listIndex[actor] : 0;
        const int32_t id =
            t.request.slot == 0 ? -16 * ++none : index * 16 + t.request.slot * 2 + (t.request.noOverride ? 1 : 0);
        sounds.push_back(
            {actor, id, pathOf(t.request.sound), t.request.volume, t.request.radius, t.request.pitch, t.start});
    }
    if (VmObject *music = currentMusic(); music && music->package && _musicActor && refs.count(_musicActor))
        sounds.push_back({refs[_musicActor], -16 * ++none, pathOf({music->package, music->exportIndex}), _music.volume,
                          1e6f, 1, _musicStart});
    L.index(int32_t(sounds.size()));
    for (const Entry &e : sounds) {
        L.index(e.actor);
        L.i32(e.id);
        L.string(e.path);
        L.f32(e.volume);
        L.f32(e.radius);
        L.f32(e.pitch);
        L.f32(e.start);
    }
    L.i32(0);
    w.setData(fromMap(levels.front() + 1), std::move(L.data));

    // Kept as the map numbers them: the map's actors the level no longer
    // has stay, unreferenced (the engine loads only what the level leads
    // to), and the editor's own, and the polygons only its models lead to,
    // are left empty. (A brush's polygons are loadable themselves, but only
    // its model leads to them: those of models the game loads are kept.)
    if (!renumber) {
        std::set<int32_t> polysKept;
        std::set<int32_t> polysDropped;
        for (const int i : _map.exportsOfClass("Model")) {
            Model m;
            if (!readModel(_map, i, m, false) && m.polys > 0)
                (editorOnly(size_t(i)) ? polysDropped : polysKept).insert(m.polys);
        }
        for (size_t i = 0; i < exports.size(); ++i) {
            const auto ref = int32_t(i + 1);
            if (exports[i].serialSize > 0 && (editorOnly(i) || (polysDropped.count(ref) && !polysKept.count(ref))))
                w.setData(ref, {});
        }
    }
    if (!collect)
        return w.write(path);

    // What the save uses: the names and imports asked for while writing
    // it, those of the export table, those the map's data kept as it is
    // holds, and what those imports are in and named.
    std::vector<bool> names = w.namesUsed();
    std::vector<bool> imports = w.importsUsed();
    const Renumber markName = [&](int32_t n) {
        if (n >= 0) {
            if (names.size() <= size_t(n))
                names.resize(size_t(n) + 1, false);
            names[size_t(n)] = true;
        }
        return n;
    };
    const Renumber markRef = [&](int32_t ref) {
        if (ref < 0) {
            if (imports.size() <= size_t(-ref - 1))
                imports.resize(size_t(-ref), false);
            imports[size_t(-ref - 1)] = true;
        }
        return ref;
    };
    bool whole = renumber;
    for (int32_t ref = 1; size_t(ref) <= w.exportCount(); ++ref) {
        const Export &e = w.exportAt(ref);
        markName(e.objectName);
        markRef(e.classRef);
        markRef(e.superRef);
        markRef(e.package);
    }
    markName(mapNone);
    for (size_t i = 0; whole && i < exports.size(); ++i) {
        const int32_t ref = fromMap(int32_t(i) + 1);
        if (ref <= 0 || exports[i].serialSize <= 0)
            continue;
        const std::string cls = fs::toLower(_map.className(int32_t(i) + 1));
        const bool stack = exports[i].flags & objflag::kHasStack;
        std::vector<uint8_t> data;
        if (cls == "model" || cls == "polys")
            whole = !_map.readExport(int(i), data) &&
                    (cls == "model" ? modelRefs(data, version, stack, mapNone, markRef, markName, scratch)
                                    : polysRefs(data, stack, mapNone, markRef, markName, scratch));
        else if (VmObject *o = _vm.objectAt(_map, int32_t(i) + 1); o && refs.count(o))
            continue; // written from the level as it is now
        else
            whole = !_map.readExport(int(i), data) &&
                    (cls == "c3dmeshstate" ? leadingName(data, mapNone, markName, scratch)
                                           : rewriteProperties(_map, data, stack, markRef, markName, scratch));
    }
    const std::vector<Import> &table = w.imports();
    for (bool more = true; more;) {
        more = false;
        for (size_t k = 0; k < imports.size() && k < table.size(); ++k) {
            if (!imports[k])
                continue;
            const Import &im = table[k];
            markName(im.classPackage);
            markName(im.className);
            markName(im.objectName);
            if (im.package < 0 && !(size_t(-im.package - 1) < imports.size() && imports[size_t(-im.package - 1)])) {
                markRef(im.package);
                more = true;
            }
        }
    }
    tables.trim = whole;
    tables.names = std::move(names);
    tables.imports = std::move(imports);
    return Error::success();
}

// An actor's C3D animation groups: FullBody (its animation), LowerTorso and
// UpperTorso (its layers), then its face channels.
void ScriptLevel::writeMeshGroups(ByteWriter &b, VmObject *a) {
    auto str16 = [&](const std::string &s) {
        b.u16(uint16_t(s.size()));
        b.bytes(s.data(), s.size());
    };
    struct G {
        std::string group;
        std::string sequence = "NONACTIVE";
        int32_t source = 0;
        int32_t type = 2;
        int32_t state = 0;
        float frame = 0;
        float rate = 1;
        float weight = 1;
        float fromWeight = 0;
        float fromFrame = 0;
        float elapsed = 0;
        float duration = 1;
        std::string pending;
        float pendingFrame = 0;
        float pendingWeight = 0;
        float pendingTween = 0;
        std::string bone;
        std::array<int32_t, 3> clear{};
    };
    std::vector<G> groups;
    G full;
    full.group = "FullBody";
    if (const Value *s = prop(a, "AnimSequence"); s && s->i && _vm.nameText(s->i) != "None") {
        full.sequence = _vm.nameText(s->i);
        full.source = 1;
        full.type = 1;
        full.state = 1;
        full.duration = 0.1f;
        if (const Value *f = prop(a, "AnimFrame"))
            full.frame = std::max(0.0f, f->asFloat());
    }
    groups.push_back(full);
    const auto *layers = layersOf(a);
    for (const char *part : {"LowerTorso", "UpperTorso"}) {
        G g;
        g.group = part;
        if (layers)
            if (auto it = layers->find(_vm.name(part)); it != layers->end() && it->second.sequence) {
                // Its mode as the engine keeps it, and a fade's time gone
                // and length.
                const AnimLayer &l = it->second;
                g.sequence = _vm.nameText(l.sequence);
                g.source = 1;
                g.type = 1;
                if (l.weightRate < 0)
                    g.state = 6;
                else if (l.weightRate > 0)
                    g.state = l.rate == 0 && !l.loop ? 5 : l.loop ? 4 : 2;
                else if (l.finished)
                    g.state = 0;
                else
                    g.state = l.loop ? 3 : 1;
                g.frame = std::max(0.0f, l.frame);
                g.rate = l.rate;
                g.weight = l.weight;
                g.duration = 0.1f;
                if (l.weightRate != 0 && std::fabs(l.weightRate) < 1e5f) {
                    g.duration = 1 / std::fabs(l.weightRate);
                    const float done = l.weightRate > 0 ? l.weight : 1 - l.weight;
                    g.elapsed = std::clamp(done, 0.0f, 1.0f) * g.duration;
                }
                g.bone = l.bone ? _vm.nameText(l.bone) : "";
                for (size_t k = 0; k < 3; ++k)
                    g.clear[k] = l.clear[k] ? 1 : 0;
            }
        groups.push_back(g);
    }
    if (const SkinChannels *skins = skinsOf(a))
        for (const auto &[tag, c] : *skins) {
            G g;
            g.group = _vm.nameText(tag);
            if (c.sequence) {
                g.sequence = _vm.nameText(c.sequence);
                g.type = 4;
                g.state = c.tweening ? 5 : 0;
                g.frame = c.frame;
                g.weight = c.weight;
                g.fromWeight = c.fromWeight;
                g.fromFrame = c.fromFrame;
                g.elapsed = c.elapsed;
                g.duration = c.duration;
            }
            if (c.pending) {
                g.pending = _vm.nameText(c.pendingSequence);
                g.pendingFrame = c.pendingFrame;
                g.pendingWeight = c.pendingWeight;
                g.pendingTween = c.pendingTween;
            }
            groups.push_back(g);
        }
    b.i32(int32_t(groups.size()));
    b.i32(int32_t(groups.size()));
    b.i32(10);
    for (const G &g : groups) {
        str16(g.group);
        str16(g.sequence);
        b.i32(g.source);
        b.f32(g.frame);
        b.i32(g.type);
        b.i32(g.state);
        b.f32(g.rate);
        b.f32(g.weight);
        b.f32(g.fromWeight);
        b.f32(g.fromFrame);
        b.f32(g.elapsed);
        b.f32(0);
        b.f32(g.duration);
        b.f32(0);
        str16(g.pending);
        b.f32(g.pendingFrame);
        b.f32(g.pendingWeight);
        b.f32(g.pendingTween);
        str16(g.bone);
        for (int32_t c : g.clear)
            b.i32(c);
    }
}

// What an actor holds, as its mesh state keeps it: each held actor's name
// (as the save names it), its point and the holder's, and an identity
// matrix with the flags the original's saves have (7).
void ScriptLevel::writeMeshAttachments(ByteWriter &b, const VmObject *parent,
                                       const std::map<const VmObject *, std::string> &names) {
    std::vector<std::pair<const VmObject *, const Attachment *>> held;
    for (const auto &[child, at] : _attachments)
        if (at.parent == parent && !child->deleted)
            held.emplace_back(child, &at);
    b.i32(int32_t(held.size()));
    b.i32(int32_t(held.size()));
    b.i32(10);
    auto str16 = [&](const std::string &text) {
        b.u16(uint16_t(text.size()));
        b.bytes(text.data(), text.size());
    };
    auto pointText = [&](NameId point) { return point ? _vm.nameText(point) : std::string(); };
    for (const auto &[child, at] : held) {
        const auto name = names.find(child);
        str16(name != names.end() ? name->second : _vm.nameText(child->name));
        str16(pointText(at->childPoint));
        str16(pointText(at->parentPoint));
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 3; ++col)
                b.f32(row == col ? 1.0f : 0.0f);
        b.i32(7);
    }
}

// The dialogue engine as the engine keeps it: set up; its missions, types
// and speakers; its lines in key order (flags: 2 the general table's, 1
// running on from the line before; type's priority; times said; place;
// row in the tables; next and return line's places or 0xFFFF; voice, key,
// text, pause, next and return keys, tag, event, condition; a name; a
// camera; the animations and faces); the lines being said and those
// waiting (Engine.dll 0x1040af60, 0x1040b050); its faces; its cameras;
// and its next thread's number.
void ScriptLevel::writeDialogEngine(ByteWriter &b, PackageWriter &w, const std::function<int32_t(VmObject *)> &refOf) {
    loadDialog();
    b.i32(1);
    for (size_t t = 0; t < 3; ++t) {
        b.index(int32_t(_dialogHeaders[t].size()));
        for (const auto &[id, word] : _dialogHeaders[t]) {
            b.u16(uint16_t(id));
            b.string(word);
        }
    }
    const size_t n = _dialogLines.size();
    std::vector<int> place(n, 0xFFFF);
    for (size_t k = 0; k < _dialogOrder.size(); ++k)
        place[size_t(_dialogOrder[k])] = int(k);
    // Their cameras, in the tables' order: each CAMERA read in turn (one
    // with no place keeping the one before's), the same ones once.
    struct Camera {
        int32_t flags = 0;
        float v[12] = {};
        bool operator==(const Camera &o) const { return flags == o.flags && std::memcmp(v, o.v, sizeof v) == 0; }
    };
    std::vector<Camera> cameras;
    std::vector<int> cameraOf(n, 0xFFFF);
    {
        Camera c;
        for (size_t i = 0; i < n; ++i) {
            const std::string &cam = _dialogLines[i].camera;
            if (cam.empty() || _dialogRecs[i].flags == 0)
                continue;
            c.flags = 0;
            c.v[6] = 90;
            c.v[7] = 0;
            std::string inner = cam.substr(cam.find('(') + 1);
            inner = inner.substr(0, inner.rfind(')'));
            std::istringstream parts(inner);
            for (std::string part; parts >> part;) {
                if (part.size() < 3 || part[1] != ':')
                    continue;
                const char key = char(std::toupper(uint8_t(part[0])));
                const std::string value = part.substr(2);
                switch (key) {
                case 'A':
                    c.flags |= 1;
                    break;
                case 'R':
                    c.flags |= 2;
                    break;
                case 'C':
                    c.flags |= 4;
                    break;
                case 'D':
                case 'T':
                    c.flags |= 8;
                    break;
                case 'V':
                    c.flags |= value != "0" ? 16 : 0;
                    break;
                case 'H':
                    c.flags |= value != "0" ? 32 : 0;
                    break;
                default:
                    break;
                }
                if (key == 'A' || key == 'R')
                    std::sscanf(value.c_str(), "%f,%f,%f,%f,%f,%f", &c.v[0], &c.v[1], &c.v[2], &c.v[3], &c.v[4],
                                &c.v[5]);
                else if (key == 'F')
                    c.v[6] = std::strtof(value.c_str(), nullptr);
                else if (key == 'D')
                    c.v[7] = std::strtof(value.c_str(), nullptr);
            }
            auto it = std::find(cameras.begin(), cameras.end(), c);
            cameraOf[i] = int(it - cameras.begin());
            if (it == cameras.end())
                cameras.push_back(c);
        }
    }
    auto nameOr = [&](const std::string &s) { return w.name(s.empty() ? "None" : s); };
    b.index(int32_t(n));
    for (const int i : _dialogOrder) {
        const DialogLine &l = _dialogLines[size_t(i)];
        const DialogRec &r = _dialogRecs[size_t(i)];
        const bool general = fs::toLower(l.table) == "gen_dialogue";
        b.u16(uint16_t((general ? 2 : 0) | (r.runOn ? 1 : 0)));
        b.u16(uint16_t(r.group));
        b.u16(uint16_t(std::min(_dialogPlays[size_t(i)], 0xFFFF)));
        b.u16(uint16_t(place[size_t(i)]));
        b.u16(uint16_t(l.row + 1));
        b.u16(uint16_t(r.next >= 0 ? place[size_t(r.next)] : 0xFFFF));
        b.u16(uint16_t(r.back >= 0 ? place[size_t(r.back)] : 0xFFFF));
        b.string(l.record);
        b.string(r.key);
        b.string(l.rawText);
        b.f32(l.pause);
        b.string(r.nextPattern);
        b.string(r.backPattern);
        b.index(nameOr(r.tag));
        b.string(r.event);
        // (Kept as the engine's reading leaves it: brackets and commas spaces.)
        std::string condition = r.condition.empty() ? std::string() : l.rawCondition;
        for (char &ch : condition)
            if (ch == '(' || ch == ')' || ch == ',')
                ch = ' ';
        b.string(condition);
        b.index(w.name("None"));
        b.u16(uint16_t(cameraOf[size_t(i)]));
        b.index(nameOr(l.speakerAnim));
        b.index(nameOr(l.listenerAnim));
        b.index(nameOr(l.speakerFace));
        b.index(nameOr(l.listenerFace));
    }
    // Each line being said: its place, the place of the line whose
    // RETURNLINE its thread goes back to (0xFFFF none), when it's done (in
    // level seconds, a HUD line's on the clock that runs while paused),
    // its speaker and thread. Each waiting: its place, speaker and thread.
    const Value *now = prop(_levelInfo, "TimeSeconds");
    const Value *noPause = prop(_levelInfo, "NoPauseTimeSeconds");
    auto placeOf = [&](int line) { return line >= 0 && size_t(line) < n ? place[size_t(line)] : 0xFFFF; };
    std::vector<const DialogThread *> saying;
    std::vector<const DialogThread *> waiting;
    for (const DialogThread &t : _dialogThreads)
        if (t.line >= 0)
            (t.queued ? waiting : saying).push_back(&t);
    b.index(int32_t(saying.size()));
    for (const DialogThread *t : saying) {
        b.u16(uint16_t(placeOf(t->line)));
        b.u16(uint16_t(placeOf(t->backFrom)));
        const Value *clock = t->hudClock ? noPause : now;
        b.f32((clock ? clock->asFloat() : 0) + t->timeLeft);
        b.index(refOf(t->speaker));
        b.i32(t->id);
    }
    b.index(int32_t(waiting.size()));
    for (const DialogThread *t : waiting) {
        b.u16(uint16_t(placeOf(t->line)));
        b.index(refOf(t->speaker));
        b.i32(t->id);
    }
    b.index(int32_t(_dialogHeaders[3].size()));
    for (const auto &[id, word] : _dialogHeaders[3]) {
        b.u16(uint16_t(id));
        b.string(word);
    }
    // A camera: its flags, then where (A or R: x, y, z, then pitch, yaw,
    // roll in degrees), its field of view and distance.
    b.index(int32_t(cameras.size()));
    for (const Camera &c : cameras) {
        b.i32(c.flags);
        for (float f : c.v)
            b.f32(f);
    }
    b.i32(_nextDialogThread);
}

} // namespace reluva::unreal
