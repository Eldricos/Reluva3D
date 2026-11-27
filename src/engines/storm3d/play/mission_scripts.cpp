// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/mission_scripts.h"

#include "common/pe.h"

#include <bit>
#include <cctype>
#include <cstring>

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
        if (_at - start > 1 && _remembered.size() < 10)
            _remembered.push_back(size);
        return size;
    }

    // A function's parameters, to their 'Z': their bytes in all.
    uint32_t parameters() {
        uint32_t bytes = 0;
        if (peek() == 'X') {
            next();
            next();
            return 0;
        }
        while (!done() && !_failed && peek() != '@' && peek() != 'Z')
            bytes += type();
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
    sig.argBytes = d.parameters();
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
    provideImports(script);
    provideImports(rules);
    const x86::Win32Process::Module *s = nullptr;
    const x86::Win32Process::Module *r = nullptr;
    if (Error e = _process->load(mission + "S.dsl", script, s))
        return e;
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
        }
    }
}

uint32_t MissionScripts::callGame(const std::string &decorated, const Signature &sig, x86::Cpu &cpu) const {
    const Arguments args{cpu, sig.member ? cpu.reg[x86::Cpu::ECX] : 0};
    std::optional<uint32_t> result;
    if (game)
        result = game(sig.name, args);
    if (trace) {
        std::string line = sig.name + "(";
        for (uint32_t i = 0; i < sig.argBytes / 4; ++i)
            line += (i ? ", " : "") + std::to_string(int32_t(args.word(int(i))));
        line += ")";
        if (!result)
            line += " [not known: " + decorated + "]";
        trace(line);
    }
    return result.value_or(0);
}

} // namespace reluva::storm3d
