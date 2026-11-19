// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Saved games, for a running level (ScriptLevel): the level as it is now
// written to a file of Reluva's own (not the original's, which saves the
// level as a package), and a level started from one. What's kept: every
// object the level's actors lead to (their properties, states, state
// code's place, latent waits, timers), the map's actors that are gone,
// and the engine's own records of them (touching, moves under way,
// animation layers and waits, skin channels, attachments, bends, the
// dialogue). The HUD's load list (DrawLoadGameList, LoadByFileID,
// DeleteByFileID) is here too. See docs/engines/unreal_vm.md.

#include "engines/unreal/vm/vm_level.h"

#include "engines/unreal/formats/game_save.h"

#include "common/fs.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iterator>

namespace reluva::unreal {

namespace {

// 2: with the ghosts; 3: a layer's whole state (its clears, least rate and
// what it's fading in over). 2 still reads.
constexpr char kMagic[8] = {'R', 'L', 'V', 'S', 'A', 'V', 'E', '3'};
// The version a file is (its magic's last byte), 0 if it isn't one.
int saveVersion(const char *magic) {
    if (std::memcmp(magic, kMagic, 7) != 0 || (magic[7] != '2' && magic[7] != '3'))
        return 0;
    return magic[7] - '0';
}

class Out {
public:
    void u8(uint8_t v) { _s.push_back(char(v)); }
    void i32(int32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void i64(int64_t v) { raw(&v, 8); }
    void str(const std::string &v) {
        i32(int32_t(v.size()));
        _s += v;
    }
    void vec(const std::array<float, 3> &v) {
        for (float f : v)
            f32(f);
    }
    const std::string &data() const { return _s; }

private:
    void raw(const void *p, size_t n) { _s.append(static_cast<const char *>(p), n); }
    std::string _s;
};

class In {
public:
    explicit In(std::string data) : _d(std::move(data)) {}
    uint8_t u8() {
        uint8_t v = 0;
        raw(&v, 1);
        return v;
    }
    int32_t i32() {
        int32_t v = 0;
        raw(&v, 4);
        return v;
    }
    float f32() {
        float v = 0;
        raw(&v, 4);
        return v;
    }
    int64_t i64() {
        int64_t v = 0;
        raw(&v, 8);
        return v;
    }
    std::string str() {
        const int32_t n = i32();
        if (n < 0 || size_t(n) > _d.size() - _at) {
            _ok = false;
            return {};
        }
        std::string v = _d.substr(_at, size_t(n));
        _at += size_t(n);
        return v;
    }
    std::array<float, 3> vec() { return {f32(), f32(), f32()}; }
    // A count of things to read, each at least `least` bytes: refused if
    // the file can't hold that many.
    int32_t count(size_t least = 1) {
        const int32_t n = i32();
        if (n < 0 || size_t(n) * least > _d.size() - _at) {
            _ok = false;
            return 0;
        }
        return n;
    }
    bool ok() const { return _ok; }

private:
    void raw(void *p, size_t n) {
        if (n > _d.size() - _at) {
            _ok = false;
            std::memset(p, 0, n);
            return;
        }
        std::memcpy(p, _d.data() + _at, n);
        _at += n;
    }
    std::string _d;
    size_t _at = 0;
    bool _ok = true;
};

// How an object reference is written.
enum RefKind : uint8_t {
    kRefNone,
    kRefSaved,    // one of the saved objects, by its number
    kRefPackage,  // an object of a package (a texture, a mesh): its package and export
    kRefClass,    // a class, by its package and name (or its name, made up by the engine)
    kRefViewport, // the player's viewport, console and canvas: the new level's own
    kRefConsole,
    kRefCanvas,
    kRefMapExport, // one of the map's objects that isn't an actor (its Level)
};

std::string qualifiedName(const VmClass *c) {
    return c->package ? c->package->name() + "." + c->name : c->name;
}

std::string readFile(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

// The header: what's in it, which map, when.
Error ScriptLevel::readSaveInfo(const std::filesystem::path &file, SaveInfo &out) {
    // The original's: the level as a package, named from its title and
    // the time played ("Bajoran_Ship_-_Ke'el_Tul-97.dsa"), dated by the
    // file (as the game's load list has them).
    if (isPackageFile(file.string())) {
        out.file = file;
        out.byGame = true;
        out.map = file.stem().string();
        out.description = out.map;
        std::replace(out.description.begin(), out.description.end(), '_', ' ');
        std::error_code ec;
        const auto when = std::filesystem::last_write_time(file, ec);
        if (!ec)
            out.time = std::chrono::duration_cast<std::chrono::seconds>(
                           (when - std::filesystem::file_time_type::clock::now()) +
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
        return Error::success();
    }
    std::ifstream f(file, std::ios::binary);
    char magic[8] = {};
    if (!f.read(magic, 8) || !saveVersion(magic))
        return Error::fail(file.string() + ": not a saved game of Reluva's");
    std::string head(4096, '\0');
    f.read(head.data(), std::streamsize(head.size()));
    head.resize(size_t(f.gcount()));
    In in(head);
    out.file = file;
    out.map = in.str();
    out.description = in.str();
    out.time = in.i64();
    return in.ok() ? Error::success() : Error::fail(file.string() + ": cut short");
}

// The saved games in the save folder, newest first: QuickSave.dsa and
// <Title>-<n>.dsa, the game's own names: Reluva's files and the original's.
std::vector<ScriptLevel::SaveInfo> ScriptLevel::savedGames() const {
    std::vector<SaveInfo> out;
    std::error_code ec;
    if (_saveFolder.empty())
        return out;
    for (const auto &entry : std::filesystem::directory_iterator(_saveFolder, ec)) {
        const std::string ext = fs::toLower(entry.path().extension().string());
        SaveInfo info;
        if ((ext == ".dss" || ext == ".dsa") && !readSaveInfo(entry.path(), info))
            out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const SaveInfo &a, const SaveInfo &b) {
        return a.time != b.time ? a.time > b.time : a.file.filename() < b.file.filename();
    });
    return out;
}

// The level saved, in Reluva's own format: "QuickSave" to QuickSave.dsa,
// anything else as the game names its saves.
Error ScriptLevel::saveGame(const std::string &slot) {
    if (_saveFolder.empty())
        return Error::fail("no folder to save games in");
    std::error_code ec;
    std::filesystem::create_directories(_saveFolder, ec);
    std::filesystem::path file;
    std::string description;
    if (fs::toLower(slot) == "quicksave") {
        file = _saveFolder / "QuickSave.dsa";
        description = "QuickSave";
    } else {
        // Named as the game names them: the level's title (else its name),
        // spaces as underscores, then the seconds played, counting up past
        // any already there.
        if (const Value *title = _levelInfo->find(_vm, "Title"); title && !title->s.empty())
            description = title->s;
        else
            description = _map.name();
        std::string base = description.substr(0, 64);
        std::replace(base.begin(), base.end(), ' ', '_');
        const Value *played = _player ? prop(_player, "GamePlayedTime") : nullptr;
        int n = played ? int(played->asFloat()) : 0;
        while (std::filesystem::exists(_saveFolder / (base + "-" + std::to_string(n) + ".dsa"), ec))
            ++n;
        file = _saveFolder / (base + "-" + std::to_string(n) + ".dsa");
    }
    // As the original saves it, so the original loads it too (Reluva's
    // own format, below, stays for its older saves).
    if (!reluvaFormat)
        return writeGameSave(file.string());

    // The objects: the level's actors and what they lead to that the level
    // made (not the packages' objects, nor the engine's viewport, console
    // and canvas, which a loaded level makes anew).
    VmObject *viewport = nullptr;
    VmObject *console = nullptr;
    if (_player)
        if (const Value *v = _player->find(_vm, "Player"); v && v->asObject()) {
            viewport = v->o;
            if (const Value *c = viewport->find(_vm, "Console"))
                console = c->asObject();
        }
    std::vector<VmObject *> saved;
    std::map<const VmObject *, int> ids;
    auto mapActor = [&](const VmObject *o) { return o->package == &_map && o->isActor && o->exportIndex >= 0; };
    auto wanted = [&](const VmObject *o) {
        return o && !o->deleted && !o->asClass && o->cls && (!o->package || mapActor(o)) && o != viewport &&
               o != console && o != _canvas && (o->package || o->cls->package);
    };
    auto add = [&](VmObject *o) {
        if (wanted(o) && !ids.count(o)) {
            ids[o] = int(saved.size());
            saved.push_back(o);
        }
    };
    for (VmObject *a : _actors)
        add(a);
    for (VmObject *g : _ghosts)
        add(g);
    std::function<void(const Value &)> follow = [&](const Value &v) {
        if (v.type == VType::kObject)
            add(v.o);
        for (const Value &f : v.fields)
            follow(f);
    };
    for (size_t k = 0; k < saved.size(); ++k)
        for (const Value &v : saved[k]->values)
            follow(v);

    Out out;
    out.str(_map.name());
    out.str(description);
    out.i64(int64_t(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));
    auto ref = [&](const VmObject *o) {
        if (!o || o->deleted) {
            out.u8(kRefNone);
        } else if (auto it = ids.find(o); it != ids.end()) {
            out.u8(kRefSaved);
            out.i32(it->second);
        } else if (o == viewport) {
            out.u8(kRefViewport);
        } else if (o == console) {
            out.u8(kRefConsole);
        } else if (o == _canvas) {
            out.u8(kRefCanvas);
        } else if (o->asClass) {
            out.u8(kRefClass);
            out.str(qualifiedName(o->asClass));
        } else if (o->package == &_map && o->exportIndex >= 0) {
            out.u8(kRefMapExport);
            out.i32(o->exportIndex);
        } else if (o->package && o->exportIndex >= 0) {
            out.u8(kRefPackage);
            out.str(o->package->name());
            out.i32(o->exportIndex);
        } else {
            out.u8(kRefNone);
        }
    };
    std::function<void(const Value &)> value = [&](const Value &v) {
        out.u8(uint8_t(v.type));
        switch (v.type) {
        case VType::kByte:
        case VType::kInt:
        case VType::kBool:
            out.i32(v.i);
            break;
        case VType::kName:
            out.str(v.i ? _vm.nameText(v.i) : std::string());
            break;
        case VType::kFloat:
            out.f32(v.f);
            break;
        case VType::kString:
            out.str(v.s);
            break;
        case VType::kObject:
            ref(v.o);
            break;
        case VType::kStruct:
            out.i32(int32_t(v.fields.size()));
            for (const Value &f : v.fields)
                value(f);
            break;
        case VType::kNone:
            break;
        }
    };
    auto name = [&](NameId n) { out.str(n ? _vm.nameText(n) : std::string()); };

    out.i32(int32_t(saved.size()));
    for (const VmObject *o : saved) {
        if (mapActor(o)) {
            out.u8(0);
            out.i32(o->exportIndex);
        } else {
            out.u8(1);
            out.str(qualifiedName(o->cls));
            name(o->name);
        }
    }
    for (const VmObject *o : saved) {
        out.i32(int32_t(o->values.size()));
        for (const Value &v : o->values)
            value(v);
        name(o->state ? o->state->nameId : 0);
        int depth = -1;
        int d = 0;
        for (const VmStruct *s = o->state; s && s->kind == VmStruct::Kind::kState; s = s->super, ++d)
            if (s == o->codeState)
                depth = d;
        out.i32(depth);
        out.i32(o->codeAt);
        out.f32(o->sleeping);
        out.u8(o->latent);
        out.i32(o->stateSerial);
        out.i32(int32_t(o->disabled.size()));
        for (NameId n : o->disabled)
            name(n);
        out.f32(o->timerRate);
        out.f32(o->timerCount);
        out.u8(o->timerLoop);
    }

    // The level's own records.
    auto has = [&](const VmObject *o) { return ids.count(o) > 0; };
    out.i32(int32_t(std::count_if(_actors.begin(), _actors.end(), has)));
    for (VmObject *a : _actors)
        if (has(a))
            out.i32(ids[a]);
    out.i32(int32_t(std::count_if(_ghosts.begin(), _ghosts.end(), has)));
    for (VmObject *g : _ghosts)
        if (has(g))
            out.i32(ids[g]);
    ref(_player);
    ref(_game);
    out.f32(_time);
    out.i32(_ticks);
    auto keyed = [&](const auto &table, auto &&write) {
        out.i32(int32_t(std::count_if(table.begin(), table.end(), [&](const auto &e) { return has(e.first); })));
        for (const auto &[o, entry] : table)
            if (has(o)) {
                out.i32(ids[o]);
                write(entry);
            }
    };
    keyed(_touching, [&](const std::vector<VmObject *> &list) {
        out.i32(int32_t(list.size()));
        for (VmObject *t : list)
            ref(t);
    });
    keyed(_goals, [&](const Goal &g) {
        out.u8(uint8_t(g.kind));
        out.vec(g.point);
        ref(g.target);
        out.u8(g.facing);
        out.vec(g.focus);
        ref(g.focusActor);
        out.f32(g.speed);
        out.f32(g.timeLeft);
        out.i32(g.stateSerial);
    });
    out.i32(int32_t(std::count_if(_fallers.begin(), _fallers.end(), has)));
    for (VmObject *f : _fallers)
        if (has(f))
            out.i32(ids[f]);
    keyed(_animWaits, [&](int serial) { out.i32(serial); });
    keyed(_layers, [&](const std::map<NameId, AnimLayer> &groups) {
        out.i32(int32_t(groups.size()));
        for (const auto &[group, l] : groups) {
            name(group);
            name(l.sequence);
            name(l.bone);
            out.u8(l.rest);
            out.f32(l.frame);
            out.f32(l.rate);
            out.u8(l.loop);
            out.u8(l.finished);
            out.f32(l.weight);
            out.f32(l.weightRate);
            out.f32(l.minRate);
            for (bool c : l.clear)
                out.u8(c);
            name(l.fromSequence);
            out.f32(l.fromFrame);
            out.f32(l.fromWeight);
            out.u8(l.fromLoop);
        }
    });
    keyed(_skins, [&](const SkinChannels &channels) {
        out.i32(int32_t(channels.size()));
        for (const auto &[tag, s] : channels) {
            name(tag);
            name(s.sequence);
            out.f32(s.frame);
            out.f32(s.weight);
            out.f32(s.fromFrame);
            out.f32(s.fromWeight);
            out.f32(s.elapsed);
            out.f32(s.duration);
            out.u8(s.tweening);
            out.u8(s.pending);
            name(s.pendingSequence);
            out.f32(s.pendingFrame);
            out.f32(s.pendingWeight);
            out.f32(s.pendingTween);
        }
    });
    keyed(_layerWaits, [&](const std::pair<NameId, int> &w) {
        name(w.first);
        out.i32(w.second);
    });
    keyed(_attachments, [&](const Attachment &a) {
        ref(a.parent);
        name(a.parentPoint);
        name(a.childPoint);
    });
    keyed(_bends, [&](const Bends &b) {
        // This tick's turns, kept so a frame drawn before the scripts next
        // run shows them.
        const bool current = b.opsTick == int64_t(_ticks) || b.opsTick + 1 == int64_t(_ticks);
        out.i32(current ? int32_t(b.ops.size()) : 0);
        if (current)
            for (const Bends::Op &op : b.ops) {
                out.i32(op.kind);
                name(op.bone);
                out.i32(op.axis);
                out.f32(op.degrees);
                for (int32_t k : op.rotation)
                    out.i32(k);
                for (bool r : op.remove)
                    out.u8(r);
            }
        for (const Bends::Ik &ik : b.limbs) {
            out.u8(ik.place);
            out.u8(ik.turn);
            out.vec(ik.at);
            for (const Vec &axis : ik.axes)
                out.vec(axis);
            out.f32(ik.placeWeight);
            out.f32(ik.turnWeight);
        }
        // The effectors are worked out again from the next tick's animation.
    });
    out.u8(_dialogLoaded);
    out.i32(int32_t(_dialogPlays.size()));
    for (int n : _dialogPlays)
        out.i32(n);
    out.i32(int32_t(_dialogThreads.size()));
    for (const DialogThread &t : _dialogThreads) {
        out.i32(t.id);
        out.i32(t.line);
        ref(t.self);
        ref(t.speaker);
        ref(t.listener);
        out.f32(t.seconds);
        out.f32(t.timeLeft);
        out.f32(t.elapsed);
        out.u8(t.queued);
        out.u8(t.hudClock);
        out.i32(t.back);
        out.str(t.backPattern);
    }
    out.i32(_nextDialogThread);
    out.i32(int32_t(_dialogRandom));
    out.i32(int32_t(_surfaceSwaps.surfaces.size()));
    for (const auto &[surf, tex] : _surfaceSwaps.surfaces) {
        out.i32(surf);
        out.str(tex.package->name());
        out.i32(tex.exportIndex);
    }
    out.i32(int32_t(_surfaceSwaps.world.size()));
    for (const auto &[from, to] : _surfaceSwaps.world) {
        out.str(from.first->name());
        out.i32(from.second);
        out.str(to.package->name());
        out.i32(to.exportIndex);
    }
    const auto &counts = _vm.nameCounts();
    out.i32(int32_t(counts.size()));
    for (const auto &[cls, n] : counts) {
        out.str(qualifiedName(cls));
        out.i32(n);
    }

    // The textures' own properties, which the scripts change and the
    // engine keeps across a load, its packages staying loaded: the HUD's
    // places (SetHudData), the FLC textures' playing (Mode, FrameNum).
    std::vector<const VmObject *> textures;
    for (const auto &o : _vm.objects())
        if (o->package && o->package != &_map && o->exportIndex >= 0 && !o->asClass && !o->deleted && o->cls &&
            _vm.isA(o.get(), "Texture"))
            textures.push_back(o.get());
    out.i32(int32_t(textures.size()));
    for (const VmObject *t : textures) {
        out.str(t->package->name());
        out.i32(t->exportIndex);
        out.i32(int32_t(t->values.size()));
        for (const Value &v : t->values)
            value(v);
    }

    const std::filesystem::path partial = file.string() + ".part";
    {
        std::ofstream f(partial, std::ios::binary | std::ios::trunc);
        f.write(kMagic, 8);
        f.write(out.data().data(), std::streamsize(out.data().size()));
        if (!f)
            return Error::fail(partial.string() + ": couldn't be written");
    }
    std::filesystem::rename(partial, file, ec);
    if (ec)
        return Error::fail(file.string() + ": " + ec.message());
    return Error::success();
}

// The level as a saved game has it, in place of starting it and logging
// the player in: the map's actors (just loaded) given their saved
// properties, those gone removed, the level's own made again, then the
// level's records. The player gets a new viewport and console, and
// bJustLoaded, as the engine sets it; and the game isn't paused (it saves
// paused).
Error ScriptLevel::restore(const std::filesystem::path &file) {
    std::string data = readFile(file);
    const int version = data.size() < 8 ? 0 : saveVersion(data.data());
    if (!version)
        return Error::fail(file.string() + ": not a saved game of Reluva's");
    In in(data.substr(8));
    if (in.str() != _map.name())
        return Error::fail(file.string() + ": saved on another map");
    in.str();
    in.i64();

    std::map<int, VmObject *> byExport;
    for (VmObject *a : _actors)
        if (a->exportIndex >= 0)
            byExport[a->exportIndex] = a;
    std::vector<VmObject *> objects;
    std::set<VmObject *> kept;
    const int32_t n = in.count(5);
    for (int32_t k = 0; k < n && in.ok(); ++k) {
        VmObject *o = nullptr;
        if (in.u8() == 0) {
            auto it = byExport.find(in.i32());
            if (it != byExport.end())
                o = it->second;
        } else {
            const std::string cls = in.str();
            const std::string objName = in.str();
            if (VmClass *c = _vm.findClass(cls))
                o = _vm.create(c, _vm.name(objName));
        }
        if (!o)
            return Error::fail(file.string() + ": an object that can't be made again");
        objects.push_back(o);
        kept.insert(o);
    }
    for (VmObject *a : _actors)
        if (!kept.count(a))
            a->deleted = true;

    VmObject *viewport = nullptr; // made once the player is known
    auto ref = [&]() -> VmObject * {
        switch (in.u8()) {
        case kRefSaved: {
            const int32_t id = in.i32();
            return id >= 0 && size_t(id) < objects.size() ? objects[size_t(id)] : nullptr;
        }
        case kRefPackage: {
            const std::string package = in.str();
            const int32_t index = in.i32();
            const Package *p = _vm.packages().get(package);
            return p ? _vm.objectAt(*p, index + 1) : nullptr;
        }
        case kRefClass: {
            VmClass *c = _vm.findClass(in.str());
            return c ? c->object : nullptr;
        }
        case kRefMapExport:
            return _vm.objectAt(_map, in.i32() + 1);
        case kRefViewport:
            return reinterpret_cast<VmObject *>(1); // put right once it's made
        case kRefConsole:
            return reinterpret_cast<VmObject *>(2);
        case kRefCanvas:
            return reinterpret_cast<VmObject *>(3);
        default:
            return nullptr;
        }
    };
    std::vector<Value *> clientRefs; // values pointing at the viewport, console, canvas
    // A value read into its slot (whose struct it keeps), or, if the slot's
    // type isn't what was saved, read and dropped.
    std::function<void(Value &, bool)> value = [&](Value &v, bool keep) {
        const auto type = VType(in.u8());
        Value read;
        keep = keep && type == v.type;
        Value &to = keep ? v : read;
        to.type = type;
        switch (type) {
        case VType::kByte:
        case VType::kInt:
        case VType::kBool:
            to.i = in.i32();
            break;
        case VType::kName: {
            const std::string text = in.str();
            to.i = text.empty() ? 0 : _vm.name(text);
            break;
        }
        case VType::kFloat:
            to.f = in.f32();
            break;
        case VType::kString:
            to.s = in.str();
            break;
        case VType::kObject:
            to.o = ref();
            if (reinterpret_cast<uintptr_t>(to.o) <= 3 && to.o) {
                if (keep)
                    clientRefs.push_back(&to);
                else
                    to.o = nullptr;
            }
            break;
        case VType::kStruct: {
            const int32_t fields = in.count(1);
            if (size_t(fields) != to.fields.size())
                to.fields.resize(size_t(fields));
            for (Value &f : to.fields)
                value(f, keep);
            break;
        }
        case VType::kNone:
            break;
        }
    };
    auto name = [&]() -> NameId {
        const std::string text = in.str();
        return text.empty() ? 0 : _vm.name(text);
    };
    for (VmObject *o : objects) {
        const int32_t count = in.count(1);
        if (size_t(count) != o->values.size())
            return Error::fail(file.string() + ": " + o->cls->name + "'s properties don't match its class");
        for (Value &v : o->values)
            value(v, true);
        const NameId stateName = name();
        o->state = stateName ? _vm.findState(o->cls, stateName) : nullptr;
        const int32_t depth = in.i32();
        o->codeState = nullptr;
        int d = 0;
        for (VmStruct *s = o->state; s && s->kind == VmStruct::Kind::kState; s = s->super, ++d)
            if (d == depth)
                o->codeState = static_cast<VmState *>(s);
        o->codeAt = in.i32();
        if (!o->codeState)
            o->codeAt = -1;
        o->sleeping = in.f32();
        o->latent = in.u8() != 0;
        o->stateSerial = in.i32();
        o->disabled.clear();
        for (int32_t k = in.count(4); k > 0; --k)
            o->disabled.insert(name());
        o->timerRate = in.f32();
        o->timerCount = in.f32();
        o->timerLoop = in.u8() != 0;
    }

    _actors.clear();
    for (int32_t k = in.count(4); k > 0; --k) {
        const int32_t id = in.i32();
        if (id >= 0 && size_t(id) < objects.size())
            _actors.push_back(objects[size_t(id)]);
    }
    _ghosts.clear();
    for (int32_t k = in.count(4); k > 0; --k) {
        const int32_t id = in.i32();
        if (id >= 0 && size_t(id) < objects.size())
            _ghosts.push_back(objects[size_t(id)]);
    }
    _player = ref();
    _game = ref();
    _time = in.f32();
    _ticks = in.i32();
    auto object = [&]() -> VmObject * {
        const int32_t id = in.i32();
        return id >= 0 && size_t(id) < objects.size() ? objects[size_t(id)] : nullptr;
    };
    auto keyed = [&](auto &&read) {
        for (int32_t k = in.count(4); k > 0 && in.ok(); --k) {
            VmObject *o = object();
            read(o);
        }
    };
    _touching.clear();
    keyed([&](VmObject *o) {
        std::vector<VmObject *> &list = _touching[o];
        for (int32_t k = in.count(1); k > 0; --k)
            if (VmObject *t = ref())
                list.push_back(t);
    });
    _goals.clear();
    keyed([&](VmObject *o) {
        Goal g;
        g.kind = Goal::Kind(in.u8());
        g.point = in.vec();
        g.target = ref();
        g.facing = in.u8() != 0;
        g.focus = in.vec();
        g.focusActor = ref();
        g.speed = in.f32();
        g.timeLeft = in.f32();
        g.stateSerial = in.i32();
        _goals[o] = g;
    });
    _fallers.clear();
    for (int32_t k = in.count(4); k > 0; --k)
        if (VmObject *o = object())
            _fallers.insert(o);
    _animWaits.clear();
    keyed([&](VmObject *o) { _animWaits[o] = in.i32(); });
    _layers.clear();
    keyed([&](VmObject *o) {
        for (int32_t k = in.count(4); k > 0; --k) {
            const NameId group = name();
            AnimLayer l;
            l.sequence = name();
            l.bone = name();
            l.rest = in.u8() != 0;
            l.frame = in.f32();
            l.rate = in.f32();
            l.loop = in.u8() != 0;
            l.finished = in.u8() != 0;
            l.weight = in.f32();
            l.weightRate = in.f32();
            if (version >= 3) {
                l.minRate = in.f32();
                for (bool &c : l.clear)
                    c = in.u8() != 0;
                l.fromSequence = name();
                l.fromFrame = in.f32();
                l.fromWeight = in.f32();
                l.fromLoop = in.u8() != 0;
            }
            _layers[o][group] = l;
        }
    });
    _skins.clear();
    keyed([&](VmObject *o) {
        for (int32_t k = in.count(4); k > 0; --k) {
            const NameId tag = name();
            SkinChannel s;
            s.sequence = name();
            s.frame = in.f32();
            s.weight = in.f32();
            s.fromFrame = in.f32();
            s.fromWeight = in.f32();
            s.elapsed = in.f32();
            s.duration = in.f32();
            s.tweening = in.u8() != 0;
            s.pending = in.u8() != 0;
            s.pendingSequence = name();
            s.pendingFrame = in.f32();
            s.pendingWeight = in.f32();
            s.pendingTween = in.f32();
            _skins[o].push_back({tag, s});
        }
    });
    _layerWaits.clear();
    keyed([&](VmObject *o) {
        const NameId group = name();
        _layerWaits[o] = {group, in.i32()};
    });
    _attachments.clear();
    keyed([&](VmObject *o) {
        Attachment a;
        a.parent = ref();
        a.parentPoint = name();
        a.childPoint = name();
        _attachments[o] = a;
    });
    _bends.clear();
    keyed([&](VmObject *o) {
        Bends &b = _bends[o];
        for (int32_t k = in.count(4); k > 0; --k) {
            Bends::Op op;
            op.kind = in.i32();
            op.bone = name();
            op.axis = in.i32();
            op.degrees = in.f32();
            for (int32_t &v : op.rotation)
                v = in.i32();
            for (bool &r : op.remove)
                r = in.u8() != 0;
            b.ops.push_back(op);
        }
        b.opsTick = int64_t(_ticks);
        for (Bends::Ik &ik : b.limbs) {
            ik.place = in.u8() != 0;
            ik.turn = in.u8() != 0;
            ik.at = in.vec();
            for (Vec &axis : ik.axes)
                axis = in.vec();
            ik.placeWeight = in.f32();
            ik.turnWeight = in.f32();
        }
    });
    if (in.u8())
        loadDialog();
    const int32_t plays = in.count(4);
    for (int32_t k = 0; k < plays; ++k) {
        const int32_t p = in.i32();
        if (size_t(k) < _dialogPlays.size())
            _dialogPlays[size_t(k)] = p;
    }
    _dialogThreads.clear();
    for (int32_t k = in.count(4); k > 0; --k) {
        DialogThread t;
        t.id = in.i32();
        t.line = in.i32();
        t.self = ref();
        t.speaker = ref();
        t.listener = ref();
        t.seconds = in.f32();
        t.timeLeft = in.f32();
        t.elapsed = in.f32();
        t.queued = in.u8() != 0;
        t.hudClock = in.u8() != 0;
        t.back = in.i32();
        t.backPattern = in.str();
        t.phoneme = 0;
        if (t.line >= 0 && size_t(t.line) < _dialogLines.size())
            _dialogThreads.push_back(t);
    }
    _nextDialogThread = in.i32();
    _dialogRandom = uint32_t(in.i32());
    _surfaceSwaps = {};
    auto packageRef = [&]() -> ObjectRef {
        const Package *p = _vm.packages().get(in.str());
        const int32_t index = in.i32();
        return p ? ObjectRef{p, index} : ObjectRef{};
    };
    for (int32_t k = in.count(12); k > 0; --k) {
        const int32_t surf = in.i32();
        if (const ObjectRef tex = packageRef())
            _surfaceSwaps.surfaces[surf] = tex;
    }
    for (int32_t k = in.count(16); k > 0; --k) {
        const ObjectRef from = packageRef();
        const ObjectRef to = packageRef();
        if (from && to)
            _surfaceSwaps.world[{from.package, from.exportIndex}] = to;
    }
    _surfaceSwaps.serial = 1;
    for (int32_t k = in.count(8); k > 0; --k) {
        const std::string cls = in.str();
        const int32_t count = in.i32();
        if (VmClass *c = _vm.findClass(cls))
            _vm.nameCounts()[c] = std::max(_vm.nameCounts()[c], count);
    }
    for (int32_t k = in.count(12); k > 0 && in.ok(); --k) {
        const std::string package = in.str();
        const int32_t index = in.i32();
        const Package *p = _vm.packages().get(package);
        VmObject *t = p ? _vm.objectAt(*p, index + 1) : nullptr;
        const int32_t count = in.count(1);
        const bool fits = t && size_t(count) == t->values.size();
        Value scratch;
        for (int32_t v = 0; v < count && in.ok(); ++v)
            value(fits ? t->values[size_t(v)] : scratch, fits);
    }
    if (!in.ok() || !_player || !_game)
        return Error::fail(file.string() + ": cut short");

    // The engine's side of the player, made anew, and what pointed at the
    // old one pointed at it.
    attachViewport();
    if (const Value *v = _player->find(_vm, "Player"))
        viewport = v->asObject();
    VmObject *console = this->console();
    for (Value *v : clientRefs) {
        const auto which = reinterpret_cast<uintptr_t>(v->o);
        v->o = which == 1 ? viewport : which == 2 ? console : canvas();
    }
    if (Value *v = _player->find(_vm, "bJustLoaded"))
        *v = Value::boolValue(true);
    if (Value *v = _levelInfo->find(_vm, "Pauser"))
        *v = Value::stringValue("");
    _collidersTick = -1;
    _posed.clear();
    _posedBones.clear();
    return Error::success();
}

void registerSaveNatives(Vm &vm) {
    auto reg = [&](const char *name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    // DrawLoadGameList(): each saved game to the HUD's DrawLoadGameFile(Id,
    // its name, when), newest first, from 1.
    reg("CTVE_Hud.DrawLoadGameList", [](NativeCall &c) {
        if (!c.vm.level || !c.self)
            return;
        int id = 0;
        for (const ScriptLevel::SaveInfo &s : c.vm.level->savedGames()) {
            char when[32] = {};
            const std::time_t t = std::time_t(s.time);
            if (const std::tm *tm = std::localtime(&t))
                std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
            c.vm.callEvent(c.self, "DrawLoadGameFile",
                           {Value::intValue(++id), Value::stringValue(s.description), Value::stringValue(when)});
        }
    });
    // LoadByFileID(int Id): that game's file to the HUD's HudLoadFile, which
    // travels to it ("?LOAD=Save1.dss").
    reg("CTVE_Hud.LoadByFileID", [](NativeCall &c) {
        if (!c.vm.level || !c.self)
            return;
        const auto saves = c.vm.level->savedGames();
        if (c.i(0) >= 1 && size_t(c.i(0)) <= saves.size())
            c.vm.callEvent(c.self, "HudLoadFile",
                           {Value::stringValue(saves[size_t(c.i(0) - 1)].file.filename().string())});
    });
    // DeleteByFileID(int Id): that game's file deleted.
    reg("CTVE_Hud.DeleteByFileID", [](NativeCall &c) {
        if (!c.vm.level)
            return;
        const auto saves = c.vm.level->savedGames();
        if (c.i(0) >= 1 && size_t(c.i(0)) <= saves.size()) {
            std::error_code ec;
            std::filesystem::remove(saves[size_t(c.i(0) - 1)].file, ec);
        }
    });
}

// The original's saved games.

bool ScriptLevel::savedByGame() const {
    const Value *begun = _levelInfo ? _levelInfo->find(_vm, "bBegunPlay") : nullptr;
    return begun && begun->asBool();
}

// A saved game of the original's, as the engine's LoadMap takes it up: the
// level as saved (bBegunPlay set, so nothing begins again); its time; each
// actor back in the state, and at the place in its state code, it was
// saved in; its animation groups and face channels (its C3D mesh state);
// the dialogue table's counts; the sounds that were playing, from where
// they were; the player's bJustLoaded; then the player logs in again,
// taking up the saved pawn (GameInfo.Login reuses a PlayerPawn without a
// player), and the game's unpaused.
VmObject *ScriptLevel::resumeSavedGame(const std::string &playerName, std::string &error) {
    _vm.holdEvents = false;
    if (const Value *g = prop(_levelInfo, "Game"))
        _game = g->asObject();
    if (const Value *t = prop(_levelInfo, "TimeSeconds"))
        _time = t->asFloat();
    _lipLast = _time;
    // The states.
    for (const auto &[a, f] : _savedFrames) {
        if (a->deleted || !f.stateNode)
            continue;
        const ObjectRef st = _vm.packages().resolve(_map, f.stateNode);
        if (!st)
            continue;
        const std::string stateName = st.package->objectName(st.exportIndex + 1);
        VmState *state = a->cls ? _vm.findState(a->cls, _vm.name(stateName)) : nullptr;
        if (!state || state == a->cls)
            continue;
        a->state = state;
        ++a->stateSerial;
        a->codeState = nullptr;
        a->codeAt = -1;
        if (!f.node || f.offset < 0)
            continue;
        // Its code: the state of that name in the class the node is in.
        const ObjectRef node = _vm.packages().resolve(_map, f.node);
        if (!node)
            continue;
        const std::string path = node.package->path(node.exportIndex + 1);
        const std::string owner = path.substr(0, path.find('.'));
        VmState *code = nullptr;
        for (VmStruct *s = state; s && s->kind == VmStruct::Kind::kState; s = s->super)
            if (auto *ss = static_cast<VmState *>(s); ss->owner && fs::toLower(ss->owner->name) == fs::toLower(owner)) {
                code = ss;
                break;
            }
        if (!code)
            continue;
        // The statement at the offset (the one after the latent call it
        // waits in); waiting in anything but Sleep, the call made again
        // (a move, a FinishAnim: the actor's properties have what it needs).
        int at = -1;
        int before = -1;
        uint32_t best = 0;
        uint32_t bestBefore = 0;
        for (const auto &[off, statement] : code->statementAt) {
            if (off >= uint32_t(f.offset) && (at < 0 || off < best))
                at = statement, best = off;
            if (off < uint32_t(f.offset) && (before < 0 || off > bestBefore))
                before = statement, bestBefore = off;
        }
        a->codeState = code;
        a->codeAt = at;
        if (f.latent != 0) {
            if (f.latent == 256) { // Sleep: what's left of it
                const Value *left = prop(a, "LatentFloat");
                a->latent = true;
                a->sleeping = left ? left->asFloat() : 0;
            } else if (before >= 0) {
                a->codeAt = before;
            }
        }
    }
    // What each actor was touching (its Touching), so nothing it's in
    // already is touched again.
    for (VmObject *a : _actors)
        if (const Value *t = prop(a, "Touching"))
            for (size_t k = 0; k < 4; ++k)
                if (VmObject *o = t[k].asObject(); o && !o->deleted)
                    _touching[a].push_back(o);
    // The C3D mesh states: the layers on the body and the face channels.
    for (VmObject *a : _actors) {
        const Value *ms = prop(a, "C3DOurMeshState");
        VmObject *o = ms ? ms->asObject() : nullptr;
        if (!o || !o->package || o->exportIndex < 0)
            continue;
        MeshState m;
        if (readMeshState(*o->package, o->exportIndex, m))
            continue;
        for (const MeshStateGroup &g : m.groups) {
            const bool active = !g.sequence.empty() && fs::toLower(g.sequence) != "nonactive";
            if (g.group.rfind("sk", 0) == 0) {
                SkinChannel c;
                if (active)
                    c.sequence = _vm.name(g.sequence);
                c.frame = g.frame;
                c.weight = g.weight;
                c.fromFrame = g.fromFrame;
                c.fromWeight = g.fromWeight;
                c.elapsed = g.elapsed;
                c.duration = g.duration;
                c.tweening = g.state == 5;
                if (!g.pendingSequence.empty() && fs::toLower(g.pendingSequence) != "nonactive") {
                    c.pending = true;
                    c.pendingSequence = _vm.name(g.pendingSequence);
                    c.pendingFrame = g.pendingFrame;
                    c.pendingWeight = g.pendingWeight;
                    c.pendingTween = g.pendingTween;
                }
                _skins[a].push_back({_vm.name(g.group), c});
            } else if (active && (g.type == 1 || g.type == 2) && fs::toLower(g.group) != "fullbody") {
                // A body group, by its mode: looping or not, held, a tween,
                // fading in or out (its time gone of its length).
                AnimLayer l;
                l.sequence = _vm.name(g.sequence);
                l.bone = g.bone.empty() ? 0 : _vm.name(g.bone);
                l.rest = fs::toLower(g.group) == "lowertorso";
                l.frame = g.frame;
                l.rate = g.state == 5 ? 0 : g.rate;
                l.loop = g.state == 3 || g.state == 4;
                l.finished = g.state == 0;
                const float fade = g.duration > 0 ? std::clamp(g.elapsed / g.duration, 0.0f, 1.0f) : 1;
                l.weight = 1;
                if (g.state == 2 || g.state == 4 || g.state == 5) {
                    l.weight = fade;
                    l.weightRate = g.duration > 0 ? 1 / g.duration : 1e6f;
                } else if (g.state == 6) {
                    l.weight = 1 - fade;
                    l.weightRate = g.duration > 0 ? -1 / g.duration : -1e6f;
                }
                for (size_t k = 0; k < 3; ++k)
                    l.clear[k] = g.clear[k] != 0;
                _layers[a][_vm.name(g.group)] = l;
            }
        }
    }
    // How often each line's been said.
    loadDialog();
    std::map<std::string, int> said;
    for (const LevelExtras::Line &l : _saved.lines)
        if (l.said > 0)
            said[l.key] = l.said;
    for (size_t i = 0; i < _dialogRecs.size(); ++i)
        if (auto it = said.find(_dialogRecs[i].key); it != said.end())
            _dialogPlays[i] = it->second;
    // The sounds playing (the music's the player's CurrentMusic, which the
    // page starts again from the HUD's StartTimeOffset).
    for (const LevelExtras::Sound &snd : _saved.sounds) {
        VmObject *a = snd.actor > 0 ? _vm.objectAt(_map, snd.actor) : nullptr;
        const size_t dot = snd.path.find('.');
        const Package *p = dot == std::string::npos ? nullptr : _vm.packages().get(snd.path.substr(0, dot));
        const std::string name = snd.path.substr(snd.path.rfind('.') + 1);
        const int e = p ? p->findExport(name, "Sound") : -1;
        if (!a || e < 0)
            continue;
        VmObject *sound = _vm.objectAt(*p, e + 1);
        if (soundName(sound).find("MUSIC") != std::string::npos || streamedSound(sound)) {
            // Music: the player's, from where it was.
            if (const Value *who = prop(_levelInfo, "Player"); who && who->asObject())
                if (Value *v = prop(who->o, "CurrentMusic"))
                    v->o = sound;
            _music.volume = snd.volume;
            _music.startSeconds = std::max(0.0f, _time - snd.start);
            ++_music.serial;
            continue;
        }
        SoundRequest r;
        r.actor = a;
        r.sound = {p, e};
        r.slot = snd.id < 0 ? 0 : (snd.id >> 1) & 7;
        r.noOverride = snd.id >= 0 && (snd.id & 1);
        r.volume = snd.volume;
        r.radius = snd.radius;
        r.pitch = snd.pitch;
        r.startSeconds = std::max(0.0f, _time - snd.start);
        _soundRequests.push_back(r);
    }
    // The player, back.
    if (Value *v = prop(_levelInfo, "Pauser"))
        *v = Value::stringValue("");
    _options = "?load";
    VmObject *player = login({}, playerName, error, {});
    if (player)
        if (Value *v = prop(player, "bJustLoaded"))
            *v = Value::boolValue(true);
    return player;
}

} // namespace reluva::unreal
