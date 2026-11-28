// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/mission_scripts.h"

#include "common/pe.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <string_view>

namespace reluva::storm3d {

namespace {

constexpr uint32_t kMemory = 64u << 20;

// Reads Visual C++'s decorated names, as Microsoft's documentation and
// the names themselves show them, as far as the stack's needs go: how many
// bytes a type takes as an argument.
class Decoration {
public:
    explicit Decoration(const std::string &s) : _s(s) {}
    bool done() const { return _at >= _s.size(); }
    char peek() const { return done() ? '\0' : _s[_at]; }
    char next() { return done() ? '\0' : _s[_at++]; }
    bool failed() const { return _failed; }

    // A name's fragments, each ending in '@' or a back reference's digit,
    // the whole ending in '@'.
    std::string qualified() {
        std::vector<std::string> parts;
        while (!done() && peek() != '@') {
            if (std::isdigit(static_cast<unsigned char>(peek()))) {
                next();
                parts.emplace_back("?");
                continue;
            }
            std::string part;
            while (!done() && peek() != '@')
                part += next();
            next();
            parts.push_back(part);
        }
        next();
        std::string out;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            out += (out.empty() ? "" : "::") + *it;
        return out;
    }

    // A type's bytes as an argument.
    uint32_t type() {
        const size_t start = _at;
        uint32_t size = 4;
        switch (const char c = next()) {
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'I':
        case 'J':
        case 'K':
        case 'M':
            break;
        case 'N':
        case 'O':
            size = 8;
            break;
        case 'X':
            size = 0;
            break;
        case '_':
            size = (next() == 'N') ? 4 : 8;
            break;
        case 'P':
        case 'Q':
        case 'R':
        case 'S':
        case 'A':
        case 'B':
            if (peek() == '6') {
                next();
                next(); // its calling convention
                type();
                parameters();
            } else {
                next(); // const, volatile
                type();
            }
            break;
        case 'V':
        case 'U':
        case 'T':
            qualified();
            break;
        case 'W':
            next(); // the enum's underlying size
            qualified();
            break;
        default:
            if (std::isdigit(static_cast<unsigned char>(c))) {
                const size_t n = size_t(c - '0');
                size = n < _remembered.size() ? _remembered[n] : 4;
                return size;
            }
            _failed = true;
            return 0;
        }
        // Types longer than a letter can be referred to again by number.
        if (_at - start > 1 && _remembered.size() < 10) {
            _remembered.push_back(size);
            _rememberedKinds.push_back(kindAt(start));
        }
        return size;
    }

    // What the next type is: a float, a bool, a string, else a word; a
    // back reference, what the type it repeats is ("PADM0H": a string, a
    // float, a string again, an integer).
    char kind() const {
        if (std::isdigit(static_cast<unsigned char>(peek()))) {
            const size_t n = size_t(peek() - '0');
            return n < _rememberedKinds.size() ? _rememberedKinds[n] : 'i';
        }
        return kindAt(_at);
    }
    char kindAt(size_t at) const {
        const std::string_view rest = std::string_view(_s).substr(at);
        if (rest.starts_with("M"))
            return 'f';
        if (rest.starts_with("_N"))
            return 'b';
        if (rest.starts_with("PAD") || rest.starts_with("PBD"))
            return 's';
        return 'i';
    }

    // A function's parameters, to their 'Z': their bytes in all.
    // With `kinds`, each one's kind as Signature::kinds has it.
    uint32_t parameters(std::string *kinds = nullptr) {
        uint32_t bytes = 0;
        if (peek() == 'X') {
            next();
            next();
            return 0;
        }
        while (!done() && !_failed && peek() != '@' && peek() != 'Z') {
            if (kinds)
                *kinds += kind();
            bytes += type();
        }
        if (peek() == '@')
            next();
        next();
        return bytes;
    }

private:
    const std::string &_s;
    size_t _at = 0;
    bool _failed = false;
    std::vector<uint32_t> _remembered;
    std::vector<char> _rememberedKinds;
};

} // namespace

std::optional<MissionScripts::Signature> MissionScripts::signatureOf(const std::string &decorated) {
    if (decorated.size() < 4 || decorated[0] != '?')
        return std::nullopt;
    Signature sig;
    Decoration d(decorated);
    d.next();
    // Constructors, destructors and operators: "??0" and the like, then the class.
    std::string special;
    if (d.peek() == '?') {
        d.next();
        const char op = d.next();
        special = op == '0'   ? "constructor"
                  : op == '1' ? "destructor"
                  : op == '4' ? "operator="
                  : op == 'B' ? "operator int"
                              : std::string("operator") + op;
    }
    const std::string scope = d.qualified();
    sig.name = special.empty() ? scope : scope + "::" + special;
    const char kind = d.next();
    if (kind == '3') {
        sig.data = true;
        return sig;
    }
    const bool constructs = special == "constructor" || special == "destructor";
    if (kind == 'Y') {
        if (d.next() != 'A')
            return std::nullopt; // only __cdecl is used
    } else {
        // A member: its access, the object's const-ness, __thiscall.
        sig.member = true;
        d.next();
        if (d.next() != 'E')
            return std::nullopt;
    }
    if (constructs && d.peek() == '@') {
        d.next();
    } else {
        const char ret = d.peek();
        d.type();
        sig.returnsFloat = ret == 'M' || ret == 'N';
    }
    sig.argBytes = d.parameters(&sig.kinds);
    if (d.failed())
        return std::nullopt;
    return sig;
}

float MissionScripts::Arguments::real(int n) const {
    return std::bit_cast<float>(word(n));
}

std::string MissionScripts::Arguments::text(int n) const {
    const uint32_t at = word(n);
    return at ? cpu.string(at) : std::string();
}

MissionScripts::MissionScripts() = default;
MissionScripts::~MissionScripts() = default;

Error MissionScripts::load(const std::string &mission, const std::vector<uint8_t> &script,
                           const std::vector<uint8_t> &rules) {
    _cpu = std::make_unique<x86::Cpu>(kMemory);
    _process = std::make_unique<x86::Win32Process>(*_cpu, "Armada.exe");
    _process->output = [this](const std::string &s) {
        if (trace)
            trace("output: " + s);
    };
    _scratch = _process->memory().alloc(16);
    // Their own C runtime keeps its per-thread data in thread-local
    // storage; they run on one thread, so slots of a table here do.
    _process->provideStdcall("KERNEL32.dll", "TlsAlloc", 0, [this](x86::Cpu &) {
        _tls.push_back(0);
        return uint32_t(_tls.size() - 1);
    });
    _process->provideStdcall("KERNEL32.dll", "TlsFree", 1, [](x86::Cpu &) { return 1u; });
    _process->provideStdcall("KERNEL32.dll", "TlsGetValue", 1,
                             [this](x86::Cpu &c) { return c.arg(0) < _tls.size() ? _tls[c.arg(0)] : 0u; });
    _process->provideStdcall("KERNEL32.dll", "TlsSetValue", 2, [this](x86::Cpu &c) {
        if (c.arg(0) >= _tls.size())
            return 0u;
        _tls[c.arg(0)] = c.arg(1);
        return 1u;
    });
    for (const char *tls : {"TlsAlloc", "TlsFree", "TlsGetValue", "TlsSetValue"})
        _cpu->setWritesTracked(std::string("KERNEL32.dll!") + tls);
    provideImports(script);
    provideImports(rules);
    const x86::Win32Process::Module *s = nullptr;
    const x86::Win32Process::Module *r = nullptr;
    if (Error e = _process->load(mission + "S.dsl", script, s))
        return e;
    _script = s;
    if (Error e = _process->attach(*s))
        return Error::fail(mission + "S.dsl: " + e.message);
    if (Error e = _process->load(mission + "R.drl", rules, r))
        return e;
    if (Error e = _process->attach(*r))
        return Error::fail(mission + "R.drl: " + e.message);
    return Error::success();
}

void MissionScripts::provideImports(const std::vector<uint8_t> &file) {
    PeImage pe;
    if (parsePe(file, pe))
        return;
    for (const PeImage::ImportedDll &dll : pe.imports) {
        if (dll.name != "Armada.exe")
            continue;
        for (const PeImage::Import &fn : dll.functions) {
            if (_process->provides(dll.name, fn.name))
                continue;
            const std::optional<Signature> sig = signatureOf(fn.name);
            if (!sig) {
                _unknown.push_back(fn.name);
                continue;
            }
            if (sig->data) {
                // The exe's variables: room for each, as big as the largest
                // of the engine's objects *(guessed)*.
                const uint32_t at = _process->memory().alloc(0x100);
                _data[fn.name] = at;
                _process->provideData(dll.name, fn.name, at);
                // isCineractiveDone and the like, set by the program each
                // frame; ALWAYS_EVALUATE, never clean, so its rules are tested
                // every pass (0x404ed0) *(from the code)*.
                if (fn.name.find("@@3Vhot_int@@") != std::string::npos) {
                    _hots[at] = {HotKind::kInt, 0};
                } else if (fn.name.find("@@3Vdirty_int@@") != std::string::npos) {
                    _hots[at] = {HotKind::kAlways, -1, 0, true};
                    _dirty.push_front(at);
                } else if (sig->name == "WORLD") {
                    _world = at;
                }
                continue;
            }
            _process->provide(dll.name, fn.name, [this, name = fn.name, sig = *sig](x86::Cpu &cpu) {
                const uint32_t result = callGame(name, sig, cpu);
                if (sig.returnsFloat)
                    cpu.fpuPush(double(std::bit_cast<float>(result)));
                else
                    cpu.reg[x86::Cpu::EAX] = result;
                return sig.member ? sig.argBytes : 0u;
            });
            // They write the scripts' memory only through the processor's
            // writes, so its compiled code needn't be checked after each.
            _cpu->setWritesTracked(dll.name + "!" + fn.name);
        }
    }
}

uint32_t MissionScripts::callGame(const std::string &decorated, const Signature &sig, x86::Cpu &cpu) {
    const Arguments args{cpu, sig.member ? cpu.reg[x86::Cpu::ECX] : 0, sig.kinds, _scratch};
    if (std::optional<uint32_t> own = engine(decorated, sig, args))
        return *own;
    std::optional<uint32_t> result;
    if (game)
        result = game(sig.name, args);
    // The game's clock, if the game doesn't give its own.
    if (!result && sig.name == "GetTime")
        result = std::bit_cast<uint32_t>(_clock);
    if (trace) {
        std::string line = sig.name + "(";
        for (size_t i = 0; i < sig.kinds.size(); ++i) {
            line += i ? ", " : "";
            const int n = int(i);
            switch (sig.kinds[i]) {
            case 'f':
                line += std::to_string(args.real(n));
                break;
            case 'b':
                line += args.flag(n) ? "true" : "false";
                break;
            case 's':
                line += '"' + args.text(n) + '"';
                break;
            default:
                line += std::to_string(args.integer(n));
            }
        }
        line += ")";
        if (!result)
            line += " [not known]";
        trace(line);
    }
    return result.value_or(0);
}

std::optional<uint32_t> MissionScripts::engine(const std::string &decorated, const Signature &sig,
                                               const Arguments &args) {
    const std::string &name = sig.name;
    const uint32_t self = args.self;
    x86::Arena &memory = _process->memory();
    if (name == "hot_int::constructor" || name == "dirty_int::constructor") {
        const bool always = name == "dirty_int::constructor";
        _hots[self] = {always ? HotKind::kAlways : HotKind::kInt, -1, 0, always};
        if (always)
            _dirty.push_front(self);
        mirror(self);
        return self;
    }
    if (name == "timer_int::constructor") {
        _hots[self] = {HotKind::kTimer, 0};
        mirror(self);
        return self;
    }
    if (name == "hot_int::destructor" || name == "timer_int::destructor" || name == "dirty_int::destructor") {
        _dirty.remove(self);
        _timers.remove(self);
        _hots.erase(self);
        return 0u;
    }
    auto hot = _hots.find(self);
    if (name == "hot_int::operator=" || name == "hot_int::set") {
        set(self, args.integer(0));
        return 0u;
    }
    if (name == "hot_int::operator int")
        return hot == _hots.end() ? 0u : uint32_t(hot->second.value);
    if (hot != _hots.end() && name == "hot_int::get") {
        // Its value if changed since it was last taken (0x405310).
        if (!hot->second.dirty)
            return 0u;
        _process->cpu().write32(args.word(0), uint32_t(hot->second.value));
        hot->second.dirty = false;
        mirror(self);
        return 1u;
    }
    if (hot != _hots.end() && name == "timer_int::start") {
        // Runs out after its seconds; under a thousandth, at once. Its value
        // isn't put back to 0 (0x406ad0).
        Hot &h = hot->second;
        const float seconds = args.real(0);
        if (seconds < 0.001f) {
            if (h.remaining > 0)
                _timers.remove(self);
            h.remaining = 0;
            set(self, 1);
        } else {
            if (h.remaining < 0.001f)
                _timers.push_front(self);
            h.remaining = seconds;
        }
        mirror(self);
        return 0u;
    }
    if (hot != _hots.end() && name == "timer_int::stop") {
        if (hot->second.remaining > 0)
            _timers.remove(self);
        hot->second.remaining = 0;
        mirror(self);
        return 0u;
    }
    if (name == "timer_int::time")
        return std::bit_cast<uint32_t>(hot == _hots.end() ? 0.0f : hot->second.remaining);
    if (name == "hot_type::add_dependent_rule") {
        if (hot != _hots.end())
            hot->second.rules.insert(hot->second.rules.begin(), args.word(0));
        return 0u;
    }
    if (name == "hot_type::remove_dependent_rule") {
        if (hot != _hots.end()) {
            std::vector<uint32_t> &rules = hot->second.rules;
            if (auto it = std::find(rules.begin(), rules.end(), args.word(0)); it != rules.end())
                rules.erase(it);
        }
        return 0u;
    }
    if (name == "IIIE::create_rule") {
        // A rule on no list yet: its variables add it (0x405c10, 0x405c80).
        const uint32_t at = memory.alloc(0x80);
        Rule &rule = _rules[at];
        rule.condition = args.word(0);
        rule.action = args.word(1);
        rule.name = args.text(2);
        rule.withCharacter = decorated.find("AAVcharacter") != std::string::npos;
        return at;
    }
    if (name == "IIIE::trash_rule") {
        // Off the satisfied list by its first place there (0x405d00).
        const uint32_t at = args.word(0);
        if (auto it = std::find(_satisfied.begin(), _satisfied.end(), at); it != _satisfied.end())
            _satisfied.erase(it);
        _rules.erase(at);
        return 0u;
    }
    if (name == "character::new_script") {
        const uint32_t at = memory.alloc(0x94);
        _scripts.push_back({at, {}, 0, args.text(0)});
        return at;
    }
    if (name == "character::clear_scripts") {
        _scripts.clear();
        return 0u;
    }
    if (name == "script::add_step") {
        for (Script &script : _scripts)
            if (script.address == self)
                script.steps.push_back(args.word(0));
        return 0u;
    }
    if (name == "linked_list::Add_Data") {
        std::vector<uint32_t> &list = _lists[self];
        list.insert(list.begin(), args.word(0));
        return memory.alloc(12);
    }
    if (name == "linked_list::Remove_Data") {
        std::vector<uint32_t> &list = _lists[self];
        auto it = std::find(list.begin(), list.end(), args.word(0));
        if (it == list.end())
            return 0u;
        list.erase(it);
        return 1u;
    }
    return std::nullopt;
}

void MissionScripts::set(uint32_t hot, int32_t value) {
    // A change marks it, and puts it on the list the next pass takes its
    // rules from (0x4052d0).
    auto it = _hots.find(hot);
    if (it == _hots.end() || it->second.value == value)
        return;
    Hot &h = it->second;
    h.value = value;
    if (!h.dirty) {
        h.dirty = true;
        _dirty.push_front(hot);
    }
    mirror(hot);
}

void MissionScripts::mirror(uint32_t hot) {
    // The program's own places for them, though the scripts read them only
    // through its functions: the mark at 4, the value at 0x20, a timer's
    // seconds at 0x24.
    const Hot &h = _hots.at(hot);
    x86::Cpu &cpu = _process->cpu();
    cpu.write16(hot + 4, h.dirty ? 1 : 0);
    cpu.write32(hot + 0x20, uint32_t(h.value));
    if (h.kind == HotKind::kTimer)
        cpu.write32(hot + 0x24, std::bit_cast<uint32_t>(h.remaining));
}

Error MissionScripts::call(uint32_t function, const std::vector<uint32_t> &args, uint32_t &result) {
    if (Error e = _cpu->call(function, args, result))
        return Error::fail("the mission's scripts faulted: " + e.message);
    return Error::success();
}

Error MissionScripts::tick(float seconds) {
    if (!_cpu)
        return Error::success();
    _clock += seconds;
    // The timers running out: true from then (0x406b70).
    const std::list<uint32_t> timers = _timers;
    for (uint32_t t : timers) {
        Hot &h = _hots.at(t);
        if (h.remaining < 0.001f)
            continue;
        h.remaining -= seconds;
        if (h.remaining < 0.001f) {
            set(t, 1);
            _timers.remove(t);
        }
        mirror(t);
    }
    // Each variable changed queues its rules to be tested, and is taken
    // clean, before they are: a timer run out reads 2, then 0, so its rules
    // see it true for this pass alone (0x406b50).
    for (auto it = _dirty.begin(); it != _dirty.end();) {
        Hot &h = _hots.at(*it);
        for (uint32_t r : h.rules) {
            auto rule = _rules.find(r);
            if (rule == _rules.end() || rule->second.queued)
                continue;
            rule->second.queued = true;
            _affected.push_front(r);
        }
        if (h.kind == HotKind::kInt)
            h.dirty = false;
        else if (h.kind == HotKind::kTimer && h.value == 0)
            h.dirty = false;
        else if (h.kind == HotKind::kTimer)
            h.value = h.value == 1 ? 2 : 0;
        mirror(*it);
        it = h.dirty ? std::next(it) : _dirty.erase(it);
    }
    // Tested, each joining the satisfied list when its result changes to
    // true, leaving when to false; the comparison is of the whole result,
    // so a change from one true to another adds it again (0x405770).
    const std::list<uint32_t> affected = std::move(_affected);
    _affected.clear();
    for (uint32_t r : affected) {
        auto rule = _rules.find(r);
        if (rule == _rules.end())
            continue;
        rule->second.queued = false;
        uint32_t result = 0;
        if (Error e = call(rule->second.condition,
                           rule->second.withCharacter ? std::vector{_world} : std::vector<uint32_t>{}, result))
            return e;
        rule = _rules.find(r);
        if (rule == _rules.end() || int32_t(result) == rule->second.last)
            continue;
        Rule &now = rule->second;
        now.last = int32_t(result);
        if (result) {
            _satisfied.push_front(r);
            now.cell = _satisfied.begin();
            now.satisfied = true;
        } else if (now.satisfied) {
            _satisfied.erase(now.cell);
            now.satisfied = false;
        }
    }
    // The world fires every satisfied rule, newest first; they stay
    // satisfied, firing each pass, until tested false (0x405ad0).
    const std::list<uint32_t> satisfied = _satisfied;
    for (uint32_t r : satisfied) {
        auto rule = _rules.find(r);
        if (rule == _rules.end())
            continue;
        uint32_t ignored = 0;
        if (Error e = call(rule->second.action,
                           rule->second.withCharacter ? std::vector{_world} : std::vector<uint32_t>{}, ignored))
            return e;
    }
    // Each script its next step, oldest first, gone after its last
    // (0x403d10); a script started meanwhile waits for the next pass.
    const size_t running = _scripts.size();
    for (size_t i = 0; i < running && i < _scripts.size();) {
        Script &script = _scripts[i];
        if (script.next < script.steps.size()) {
            uint32_t ignored = 0;
            const uint32_t step = script.steps[script.next++];
            const uint32_t at = script.address;
            if (Error e = call(step, {at}, ignored))
                return e;
        }
        if (i < _scripts.size() && _scripts[i].next >= _scripts[i].steps.size())
            _scripts.erase(_scripts.begin() + std::ptrdiff_t(i));
        else
            ++i;
    }
    return Error::success();
}

void MissionScripts::setCineractive(bool done, bool cancel) {
    for (const auto &[decorated, at] : _data) {
        if (decorated.starts_with("?isCineractiveDone@@"))
            set(at, done ? 1 : 0);
        else if (decorated.starts_with("?isCineractiveCancel@@"))
            set(at, cancel ? 1 : 0);
    }
}

std::optional<int32_t> MissionScripts::variable(const std::string &name) const {
    if (!_script)
        return std::nullopt;
    for (const PeImage::Export &e : _script->image.exports) {
        const std::optional<Signature> sig = signatureOf(e.name);
        if (!sig || !sig->data || sig->name != name)
            continue;
        const uint32_t at = _script->base + e.rva;
        if (auto hot = _hots.find(at); hot != _hots.end())
            return hot->second.value;
        return int32_t(_cpu->read32(at));
    }
    return std::nullopt;
}

} // namespace reluva::storm3d
