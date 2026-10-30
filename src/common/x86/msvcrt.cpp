// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/x86/msvcrt.h"
#include "common/x86/format.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <set>

namespace reluva::x86 {

namespace {

constexpr const char* kDll = "MSVCRT.dll";
constexpr uint32_t kFileSize = 32; // sizeof(FILE)
constexpr uint32_t kErrnoNoEntry = 2;

// The C runtime's own state, shared by its functions.
struct State {
    explicit State(Win32Process& process) : p(process), c(process.cpu()) {}
    Win32Process& p;
    Cpu& c;
    uint32_t iob = 0;                      // stdin, stdout, stderr: three FILEs
    std::map<uint32_t, uint32_t> handles;  // FILE address -> the process's file handle
    std::map<uint32_t, int> ungot;         // FILE address -> a character ungetc put back
    std::set<uint32_t> text;               // FILEs opened in text mode
    uint32_t holdrand = 1;                 // rand's seed, as Visual C++ keeps it
    uint32_t strtokNext = 0;
    uint32_t tm = 0, ctimeText = 0, fmode = 0, commode = 0, adjustFdiv = 0, acmdln = 0, errnoAt = 0;
    uint32_t argv = 0, envp = 0;
    uint32_t exceptionVftable = 0;

    uint32_t alloc(uint32_t n) { return p.memory().alloc(std::max(n, 1u)); }
    void setErrno(uint32_t e) { c.write32(errnoAt, e); }

    // The length of the NUL-terminated string at `a`, without reading past
    // memory's end (0 if it isn't in memory).
    size_t length(uint32_t a) const {
        if (!c.valid(a, 1))
            return 0;
        const uint8_t* base = c.memory().data() + a;
        const void* nul = std::memchr(base, 0, c.memorySize() - a);
        return nul ? size_t(static_cast<const uint8_t*>(nul) - base) : c.memorySize() - a;
    }
    std::string str(uint32_t a) const {
        return std::string(reinterpret_cast<const char*>(c.memory().data() + a), length(a));
    }
    char* ptr(uint32_t a) { return reinterpret_cast<char*>(c.memory().data() + a); }

    bool isStd(uint32_t file) const { return file >= iob && file < iob + 3 * kFileSize; }
};

// A double argument on the stack, from its first 4-byte slot.
double argDouble(const Cpu& c, int slot) {
    const uint64_t bits = uint64_t(c.arg(slot)) | uint64_t(c.arg(slot + 1)) << 32;
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

int lowerAscii(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch; }

} // namespace

int scanFormat(Cpu& cpu, const std::string& in, const std::string& f, uint32_t args) {
    size_t i = 0, fi = 0;
    int stored = 0, argIndex = 0;
    bool anyInput = false;
    auto nextPointer = [&] { return cpu.read32(args + 4 * uint32_t(argIndex++)); };
    auto skipSpace = [&] {
        while (i < in.size() && std::isspace(static_cast<unsigned char>(in[i])))
            ++i;
    };
    while (fi < f.size()) {
        const char fc = f[fi];
        if (std::isspace(static_cast<unsigned char>(fc))) {
            skipSpace();
            ++fi;
            continue;
        }
        if (fc != '%') {
            if (i >= in.size() || in[i] != fc)
                break;
            ++i, ++fi;
            continue;
        }
        ++fi;
        if (fi < f.size() && f[fi] == '%') {
            skipSpace();
            if (i >= in.size() || in[i] != '%')
                break;
            ++i, ++fi;
            continue;
        }
        bool suppress = false;
        if (fi < f.size() && f[fi] == '*')
            suppress = true, ++fi;
        size_t width = 0;
        while (fi < f.size() && std::isdigit(static_cast<unsigned char>(f[fi])))
            width = width * 10 + size_t(f[fi++] - '0');
        int size = 4; // h: 2, l: 4 (8 for floats), L/I64/ll: 8
        bool longFloat = false;
        if (fi < f.size() && f[fi] == 'h')
            size = 2, ++fi;
        else if (fi < f.size() && f[fi] == 'l') {
            longFloat = true, ++fi;
            if (fi < f.size() && f[fi] == 'l')
                size = 8, ++fi;
        } else if (fi < f.size() && f[fi] == 'L')
            longFloat = true, size = 8, ++fi;
        else if (f.compare(fi, 3, "I64") == 0)
            size = 8, fi += 3;
        if (fi >= f.size())
            break;
        const char conv = f[fi++];
        if (conv != 'c' && conv != '[' && conv != 'n')
            skipSpace();
        if (conv == 'n') {
            if (!suppress)
                cpu.write32(nextPointer(), uint32_t(i));
            continue;
        }
        if (i >= in.size())
            return anyInput || stored ? stored : -1;
        const size_t limit = width ? std::min(in.size(), i + width) : in.size();
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            const int base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X') ? 16 : conv == 'i' ? 0 : 10;
            const std::string field = in.substr(i, limit - i);
            char* end = nullptr;
            const long long v = std::strtoll(field.c_str(), &end, base);
            const size_t used = size_t(end - field.c_str());
            if (!used)
                break;
            i += used;
            anyInput = true;
            if (!suppress) {
                const uint32_t to = nextPointer();
                if (size == 2)
                    cpu.write16(to, uint16_t(v));
                else if (size == 8) {
                    cpu.write32(to, uint32_t(uint64_t(v)));
                    cpu.write32(to + 4, uint32_t(uint64_t(v) >> 32));
                } else
                    cpu.write32(to, uint32_t(v));
                ++stored;
            }
        } else if (conv == 'f' || conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G') {
            const std::string field = in.substr(i, limit - i);
            char* end = nullptr;
            const double v = std::strtod(field.c_str(), &end);
            const size_t used = size_t(end - field.c_str());
            if (!used)
                break;
            i += used;
            anyInput = true;
            if (!suppress) {
                const uint32_t to = nextPointer();
                if (longFloat) {
                    uint64_t bits;
                    std::memcpy(&bits, &v, 8);
                    cpu.write32(to, uint32_t(bits));
                    cpu.write32(to + 4, uint32_t(bits >> 32));
                } else {
                    const float fv = float(v);
                    uint32_t bits;
                    std::memcpy(&bits, &fv, 4);
                    cpu.write32(to, bits);
                }
                ++stored;
            }
        } else if (conv == 's') {
            const size_t start = i;
            while (i < limit && !std::isspace(static_cast<unsigned char>(in[i])))
                ++i;
            anyInput = true;
            if (!suppress) {
                const uint32_t to = nextPointer();
                for (size_t k = start; k < i; ++k)
                    cpu.write8(to + uint32_t(k - start), uint8_t(in[k]));
                cpu.write8(to + uint32_t(i - start), 0);
                ++stored;
            }
        } else if (conv == 'c') {
            const size_t n = width ? width : 1;
            if (i + n > in.size())
                break;
            anyInput = true;
            if (!suppress) {
                const uint32_t to = nextPointer();
                for (size_t k = 0; k < n; ++k)
                    cpu.write8(to + uint32_t(k), uint8_t(in[i + k]));
                ++stored;
            }
            i += n;
        } else if (conv == '[') {
            bool negate = false;
            if (fi < f.size() && f[fi] == '^')
                negate = true, ++fi;
            std::string set;
            if (fi < f.size() && f[fi] == ']')
                set += ']', ++fi;
            while (fi < f.size() && f[fi] != ']')
                set += f[fi++];
            if (fi < f.size())
                ++fi;
            auto inSet = [&](char ch) {
                for (size_t k = 0; k < set.size(); ++k) {
                    if (k + 2 < set.size() && set[k + 1] == '-') {
                        if (ch >= set[k] && ch <= set[k + 2])
                            return true;
                        k += 2;
                    } else if (set[k] == ch) {
                        return true;
                    }
                }
                return false;
            };
            const size_t start = i;
            while (i < limit && inSet(in[i]) != negate)
                ++i;
            if (i == start)
                break;
            anyInput = true;
            if (!suppress) {
                const uint32_t to = nextPointer();
                for (size_t k = start; k < i; ++k)
                    cpu.write8(to + uint32_t(k - start), uint8_t(in[k]));
                cpu.write8(to + uint32_t(i - start), 0);
                ++stored;
            }
        } else {
            break; // a conversion not supported
        }
    }
    return stored;
}

void addMsvcrt(Win32Process& process) {
    auto s = std::make_shared<State>(process);
    State& st = *s;
    Cpu& c = process.cpu();
    auto provideCdecl = [&process](const char* name, std::function<uint32_t(Cpu&)> fn) {
        process.provideCdecl(kDll, name, std::move(fn));
    };
    // A function returning a double, on the FPU stack.
    auto cdeclDouble = [&process](const char* name, std::function<double(Cpu&)> fn) {
        process.provide(kDll, name, [fn = std::move(fn)](Cpu& c) {
            c.fpuPush(fn(c));
            return 0u;
        });
    };
    // A member function: `this` in ECX, the arguments popped by it.
    auto thiscall = [&process](const char* name, int args, std::function<uint32_t(Cpu&, uint32_t self)> fn) {
        const uint32_t pop = uint32_t(args) * 4;
        process.provide(kDll, name, [fn = std::move(fn), pop](Cpu& c) {
            c.reg[Cpu::EAX] = fn(c, c.reg[Cpu::ECX]);
            return pop;
        });
    };

    // --- Variables -------------------------------------------------------------
    st.iob = st.alloc(3 * kFileSize);
    st.tm = st.alloc(36);
    st.ctimeText = st.alloc(32);
    st.fmode = st.alloc(4);
    st.commode = st.alloc(4);
    st.adjustFdiv = st.alloc(4); // 0: the Pentium's FDIV is sound
    st.acmdln = st.alloc(4);
    st.errnoAt = st.alloc(4);
    c.write32(st.acmdln, process.commandLineAddress());
    // argv: the program's path alone.
    const std::string program = process.programFolder() + process.programName();
    const uint32_t arg0 = st.alloc(uint32_t(program.size() + 1));
    c.writeString(arg0, program, uint32_t(program.size() + 1));
    st.argv = st.alloc(8);
    c.write32(st.argv, arg0);
    st.envp = st.alloc(4);
    process.provideData(kDll, "_iob", st.iob);
    process.provideData(kDll, "_adjust_fdiv", st.adjustFdiv);
    process.provideData(kDll, "_acmdln", st.acmdln);
    provideCdecl("__p__fmode", [s](Cpu&) { return s->fmode; });
    provideCdecl("__p__commode", [s](Cpu&) { return s->commode; });
    provideCdecl("_errno", [s](Cpu&) { return s->errnoAt; });

    // --- Start-up and ending -----------------------------------------------------
    provideCdecl("__set_app_type", [](Cpu&) { return 0u; });
    provideCdecl("__setusermatherr", [](Cpu&) { return 0u; });
    // __getmainargs(argc*, argv*, envp*, wildcards, startinfo*)
    provideCdecl("__getmainargs", [s](Cpu& c) {
        c.write32(c.arg(0), 1);
        c.write32(c.arg(1), s->argv);
        c.write32(c.arg(2), s->envp);
        return 0u;
    });
    // _initterm(begin, end): calls each function in the table (the
    // module's static constructors).
    provideCdecl("_initterm", [](Cpu& c) {
        for (uint32_t at = c.arg(0); at < c.arg(1); at += 4) {
            const uint32_t fn = c.read32(at);
            uint32_t result = 0;
            if (fn)
                if (Error e = c.call(fn, {}, result)) {
                    c.abort(e.message);
                    break;
                }
        }
        return 0u;
    });
    // Functions to run at the end: kept by the module itself
    // (__dllonexit's table) or here; the process never ends normally, so
    // they're not run.
    provideCdecl("_onexit", [](Cpu& c) { return c.arg(0); });
    provideCdecl("atexit", [](Cpu&) { return 0u; });
    provideCdecl("__dllonexit", [](Cpu& c) { return c.arg(0); });
    auto ending = [s](Cpu& c) {
        s->p.exited = c.arg(0);
        c.abort("the program called exit(" + std::to_string(int32_t(c.arg(0))) + ")");
        return 0u;
    };
    provideCdecl("exit", ending);
    provideCdecl("_exit", ending);
    provideCdecl("abort", [](Cpu& c) {
        c.abort("the program called abort()");
        return 0u;
    });
    provideCdecl("_purecall", [](Cpu& c) {
        c.abort("a pure virtual function was called");
        return 0u;
    });
    provideCdecl("?terminate@@YAXXZ", [](Cpu& c) {
        c.abort("the program called terminate()");
        return 0u;
    });

    // --- Exceptions -----------------------------------------------------------------
    // Structured and C++ exceptions aren't dispatched yet: the handlers are
    // only called when one is, so reaching them, or throwing, ends the run
    // (naming the C++ type thrown, from its ThrowInfo).
    provideCdecl("_XcptFilter", [](Cpu&) { return 0u; }); // EXCEPTION_CONTINUE_SEARCH
    provideCdecl("_except_handler3", [](Cpu& c) {
        c.abort("an exception reached a handler; exceptions aren't dispatched yet");
        return 1u;
    });
    provideCdecl("__CxxFrameHandler", [](Cpu& c) {
        c.abort("an exception reached a C++ handler; exceptions aren't dispatched yet");
        return 1u;
    });
    // _CxxThrowException(object, ThrowInfo*): the ThrowInfo's catchable
    // types' first is the object's own type; its TypeDescriptor's name
    // follows two pointers.
    process.provideStdcall(kDll, "_CxxThrowException", 2, [](Cpu& c) {
        const uint32_t info = c.arg(1);
        std::string type = "?";
        if (c.valid(info, 16)) {
            const uint32_t types = c.read32(info + 12);
            const uint32_t first = c.valid(types, 8) ? c.read32(types + 4) : 0;
            const uint32_t descriptor = c.valid(first, 8) ? c.read32(first + 4) : 0;
            if (c.valid(descriptor, 9))
                type = c.string(descriptor + 8, 200);
        }
        c.abort("the program threw a C++ exception (" + type + "); exceptions aren't dispatched yet");
        return 0u;
    });
    // _EH_prolog: sets up a function's exception frame, with the handler in
    // EAX, as Visual C++'s does: pushes -1, the handler and the previous
    // frame (FS:[0]), links the new frame in, keeps EBP where the return
    // address was and points EBP at it, then returns.
    process.provide(kDll, "_EH_prolog", [](Cpu& c) {
        uint32_t esp = c.reg[Cpu::ESP];
        const uint32_t ret = c.read32(esp);
        esp -= 4;
        c.write32(esp, 0xffffffffu);
        esp -= 4;
        c.write32(esp, c.reg[Cpu::EAX]);
        esp -= 4;
        c.write32(esp, c.read32(c.fsBase));
        c.write32(c.fsBase, esp);
        c.write32(esp + 12, c.reg[Cpu::EBP]);
        c.reg[Cpu::EBP] = esp + 12;
        esp -= 4;
        c.write32(esp, ret);
        c.reg[Cpu::ESP] = esp;
        return 0u;
    });

    // class exception, as Visual C++ 6 lays it out: its vftable, the
    // message, and whether the message was copied (to free).
    st.exceptionVftable = st.alloc(8);
    const uint32_t deleting = c.addNative("MSVCRT.dll!exception::`scalar deleting destructor'", [s](Cpu& c) {
        const uint32_t self = c.reg[Cpu::ECX];
        if (c.read32(self + 8))
            s->p.memory().free(c.read32(self + 4));
        if (c.arg(0) & 1)
            s->p.memory().free(self);
        c.reg[Cpu::EAX] = self;
        return 4u;
    });
    const uint32_t what = c.addNative("MSVCRT.dll!exception::what", [s](Cpu& c) {
        const uint32_t message = c.read32(c.reg[Cpu::ECX] + 4);
        static const char kUnknown[] = "Unknown exception";
        if (!message) {
            const uint32_t text = s->alloc(sizeof kUnknown);
            c.writeString(text, kUnknown, sizeof kUnknown);
            c.reg[Cpu::EAX] = text;
        } else {
            c.reg[Cpu::EAX] = message;
        }
        return 0u;
    });
    c.write32(st.exceptionVftable, deleting);
    c.write32(st.exceptionVftable + 4, what);
    process.provideData(kDll, "??_7exception@@6B@", st.exceptionVftable);
    auto copyMessage = [s](uint32_t self, uint32_t message) {
        Cpu& c = s->c;
        c.write32(self, s->exceptionVftable);
        if (message) {
            const size_t n = s->length(message);
            const uint32_t copy = s->alloc(uint32_t(n + 1));
            std::memcpy(s->ptr(copy), s->ptr(message), n + 1);
            c.write32(self + 4, copy);
            c.write32(self + 8, 1);
        } else {
            c.write32(self + 4, 0);
            c.write32(self + 8, 0);
        }
    };
    thiscall("??0exception@@QAE@XZ", 0, [copyMessage](Cpu&, uint32_t self) {
        copyMessage(self, 0);
        return self;
    });
    thiscall("??0exception@@QAE@ABQBD@Z", 1, [copyMessage](Cpu& c, uint32_t self) {
        copyMessage(self, c.read32(c.arg(0)));
        return self;
    });
    thiscall("??0exception@@QAE@ABV0@@Z", 1, [copyMessage](Cpu& c, uint32_t self) {
        copyMessage(self, c.read32(c.arg(0) + 4));
        return self;
    });
    thiscall("??1exception@@UAE@XZ", 0, [s](Cpu& c, uint32_t self) {
        if (c.read32(self + 8))
            s->p.memory().free(c.read32(self + 4));
        c.write32(self + 8, 0);
        return 0u;
    });
    process.provideData(kDll, "?what@exception@@UBEPBDXZ", what);
    thiscall("??1type_info@@UAE@XZ", 0, [](Cpu&, uint32_t) { return 0u; });

    // --- Memory -----------------------------------------------------------------------
    provideCdecl("malloc", [s](Cpu& c) { return s->p.memory().alloc(std::max(c.arg(0), 1u)); });
    provideCdecl("??2@YAPAXI@Z", [s](Cpu& c) { return s->p.memory().alloc(std::max(c.arg(0), 1u)); }); // operator new
    provideCdecl("calloc", [s](Cpu& c) {
        const uint64_t n = uint64_t(c.arg(0)) * c.arg(1);
        return n > 0x7fffffff ? 0u : s->p.memory().alloc(std::max(uint32_t(n), 1u)); // alloc zeroes
    });
    auto free = [s](Cpu& c) {
        if (c.arg(0))
            s->p.memory().free(c.arg(0));
        return 0u;
    };
    provideCdecl("free", free);
    provideCdecl("??3@YAXPAX@Z", free); // operator delete
    provideCdecl("realloc", [s](Cpu& c) {
        const uint32_t old = c.arg(0), size = c.arg(1);
        Arena& m = s->p.memory();
        if (!old)
            return m.alloc(std::max(size, 1u));
        if (!size) {
            m.free(old);
            return 0u;
        }
        const uint32_t oldSize = m.size(old);
        if (size <= oldSize)
            return old;
        const uint32_t p = m.alloc(size);
        if (!p)
            return 0u;
        std::memmove(s->ptr(p), s->ptr(old), oldSize);
        m.free(old);
        return p;
    });
    provideCdecl("_msize", [s](Cpu& c) { return s->p.memory().size(c.arg(0)); });
    // These three say what they write (Cpu::noteWrite): they're called so
    // often that the instruction cache can't check all its code after each.
    provideCdecl("memcpy", [s](Cpu& c) {
        if (c.valid(c.arg(0), c.arg(2)) && c.valid(c.arg(1), c.arg(2))) {
            std::memmove(s->ptr(c.arg(0)), s->ptr(c.arg(1)), c.arg(2));
            c.noteWrite(c.arg(0), c.arg(2));
        }
        return c.arg(0);
    });
    provideCdecl("memmove", [s](Cpu& c) {
        if (c.valid(c.arg(0), c.arg(2)) && c.valid(c.arg(1), c.arg(2))) {
            std::memmove(s->ptr(c.arg(0)), s->ptr(c.arg(1)), c.arg(2));
            c.noteWrite(c.arg(0), c.arg(2));
        }
        return c.arg(0);
    });
    provideCdecl("memset", [s](Cpu& c) {
        if (c.valid(c.arg(0), c.arg(2))) {
            std::memset(s->ptr(c.arg(0)), int(c.arg(1) & 0xff), c.arg(2));
            c.noteWrite(c.arg(0), c.arg(2));
        }
        return c.arg(0);
    });
    provideCdecl("memcmp", [s](Cpu& c) {
        if (!c.valid(c.arg(0), c.arg(2)) || !c.valid(c.arg(1), c.arg(2)))
            return 0u;
        return uint32_t(std::memcmp(s->ptr(c.arg(0)), s->ptr(c.arg(1)), c.arg(2)));
    });
    provideCdecl("memchr", [s](Cpu& c) -> uint32_t {
        if (!c.valid(c.arg(0), c.arg(2)))
            return 0u;
        const void* hit = std::memchr(s->ptr(c.arg(0)), int(c.arg(1) & 0xff), c.arg(2));
        return hit ? c.arg(0) + uint32_t(static_cast<const char*>(hit) - s->ptr(c.arg(0))) : 0u;
    });

    // --- Strings (the "C" locale's) --------------------------------------------------
    provideCdecl("strlen", [s](Cpu& c) { return uint32_t(s->length(c.arg(0))); });
    provideCdecl("strcpy", [s](Cpu& c) {
        const size_t n = s->length(c.arg(1));
        if (c.valid(c.arg(0), uint32_t(n + 1)))
            std::memmove(s->ptr(c.arg(0)), s->ptr(c.arg(1)), n + 1);
        return c.arg(0);
    });
    provideCdecl("strcat", [s](Cpu& c) {
        const size_t at = s->length(c.arg(0)), n = s->length(c.arg(1));
        if (c.valid(c.arg(0), uint32_t(at + n + 1)))
            std::memmove(s->ptr(c.arg(0)) + at, s->ptr(c.arg(1)), n + 1);
        return c.arg(0);
    });
    // strncpy(to, from, n): pads with NULs, doesn't terminate a full copy.
    provideCdecl("strncpy", [s](Cpu& c) {
        const uint32_t n = c.arg(2);
        if (!c.valid(c.arg(0), n))
            return c.arg(0);
        const size_t len = std::min<size_t>(s->length(c.arg(1)), n);
        std::memmove(s->ptr(c.arg(0)), s->ptr(c.arg(1)), len);
        std::memset(s->ptr(c.arg(0)) + len, 0, n - len);
        return c.arg(0);
    });
    provideCdecl("strncat", [s](Cpu& c) {
        const size_t at = s->length(c.arg(0)), n = std::min<size_t>(s->length(c.arg(1)), c.arg(2));
        if (c.valid(c.arg(0), uint32_t(at + n + 1))) {
            std::memmove(s->ptr(c.arg(0)) + at, s->ptr(c.arg(1)), n);
            s->ptr(c.arg(0))[at + n] = 0;
        }
        return c.arg(0);
    });
    auto compare = [s](uint32_t a, uint32_t b, size_t n, bool fold) -> uint32_t {
        for (size_t i = 0; i < n; ++i) {
            int x = s->c.read8(a + uint32_t(i)), y = s->c.read8(b + uint32_t(i));
            if (fold)
                x = lowerAscii(x), y = lowerAscii(y);
            if (x != y)
                return uint32_t(x < y ? -1 : 1);
            if (!x)
                break;
        }
        return 0u;
    };
    provideCdecl("strcmp", [compare](Cpu& c) { return compare(c.arg(0), c.arg(1), SIZE_MAX, false); });
    provideCdecl("strncmp", [compare](Cpu& c) { return compare(c.arg(0), c.arg(1), c.arg(2), false); });
    provideCdecl("_stricmp", [compare](Cpu& c) { return compare(c.arg(0), c.arg(1), SIZE_MAX, true); });
    provideCdecl("_strcmpi", [compare](Cpu& c) { return compare(c.arg(0), c.arg(1), SIZE_MAX, true); });
    provideCdecl("_strnicmp", [compare](Cpu& c) { return compare(c.arg(0), c.arg(1), c.arg(2), true); });
    provideCdecl("strchr", [s](Cpu& c) -> uint32_t {
        const size_t n = s->length(c.arg(0));
        const char ch = char(c.arg(1));
        if (!ch)
            return c.arg(0) + uint32_t(n);
        const void* hit = std::memchr(s->ptr(c.arg(0)), ch, n);
        return hit ? c.arg(0) + uint32_t(static_cast<const char*>(hit) - s->ptr(c.arg(0))) : 0u;
    });
    provideCdecl("strrchr", [s](Cpu& c) -> uint32_t {
        const size_t n = s->length(c.arg(0));
        const char ch = char(c.arg(1));
        for (size_t i = n + 1; i-- > 0;)
            if (s->ptr(c.arg(0))[i] == ch)
                return c.arg(0) + uint32_t(i);
        return 0u;
    });
    provideCdecl("strstr", [s](Cpu& c) -> uint32_t {
        const std::string hay = s->str(c.arg(0)), needle = s->str(c.arg(1));
        const size_t at = hay.find(needle);
        return at == std::string::npos ? 0u : c.arg(0) + uint32_t(at);
    });
    provideCdecl("strspn", [s](Cpu& c) {
        return uint32_t(std::min(s->str(c.arg(0)).find_first_not_of(s->str(c.arg(1))), s->length(c.arg(0))));
    });
    provideCdecl("strcspn", [s](Cpu& c) {
        return uint32_t(std::min(s->str(c.arg(0)).find_first_of(s->str(c.arg(1))), s->length(c.arg(0))));
    });
    provideCdecl("strpbrk", [s](Cpu& c) -> uint32_t {
        const size_t at = s->str(c.arg(0)).find_first_of(s->str(c.arg(1)));
        return at == std::string::npos ? 0u : c.arg(0) + uint32_t(at);
    });
    // strtok(text, delimiters): Visual C++ keeps its place per thread; one
    // thread here.
    provideCdecl("strtok", [s](Cpu& c) -> uint32_t {
        uint32_t at = c.arg(0) ? c.arg(0) : s->strtokNext;
        if (!at)
            return 0u;
        const std::string delims = s->str(c.arg(1));
        while (c.read8(at) && delims.find(char(c.read8(at))) != std::string::npos)
            ++at;
        if (!c.read8(at)) {
            s->strtokNext = 0;
            return 0u;
        }
        uint32_t end = at;
        while (c.read8(end) && delims.find(char(c.read8(end))) == std::string::npos)
            ++end;
        if (c.read8(end)) {
            c.write8(end, 0);
            s->strtokNext = end + 1;
        } else {
            s->strtokNext = 0;
        }
        return at;
    });
    provideCdecl("_strdup", [s](Cpu& c) -> uint32_t {
        if (!c.arg(0))
            return 0u;
        const size_t n = s->length(c.arg(0));
        const uint32_t copy = s->alloc(uint32_t(n + 1));
        std::memcpy(s->ptr(copy), s->ptr(c.arg(0)), n + 1);
        return copy;
    });
    auto mapCase = [s](uint32_t at, int (*fn)(int)) {
        for (uint32_t i = at; s->c.read8(i); ++i)
            s->c.write8(i, uint8_t(fn(s->c.read8(i))));
        return at;
    };
    provideCdecl("_strupr", [mapCase](Cpu& c) { return mapCase(c.arg(0), [](int ch) { return ch >= 'a' && ch <= 'z' ? ch - 32 : ch; }); });
    provideCdecl("_strlwr", [mapCase](Cpu& c) { return mapCase(c.arg(0), lowerAscii); });

    // Characters, the "C" locale's classes.
    auto ctype = [&provideCdecl](const char* name, int (*fn)(int)) {
        provideCdecl(name, [fn](Cpu& c) {
            const int ch = int32_t(c.arg(0));
            return uint32_t(ch >= 0 && ch < 128 && fn(ch) ? 1 : 0);
        });
    };
    ctype("isalpha", [](int ch) { return std::isalpha(ch); });
    ctype("isdigit", [](int ch) { return std::isdigit(ch); });
    ctype("isspace", [](int ch) { return std::isspace(ch); });
    ctype("isalnum", [](int ch) { return std::isalnum(ch); });
    ctype("isupper", [](int ch) { return std::isupper(ch); });
    ctype("islower", [](int ch) { return std::islower(ch); });
    ctype("isxdigit", [](int ch) { return std::isxdigit(ch); });
    ctype("isprint", [](int ch) { return std::isprint(ch); });
    ctype("ispunct", [](int ch) { return std::ispunct(ch); });
    ctype("iscntrl", [](int ch) { return std::iscntrl(ch); });
    provideCdecl("toupper", [](Cpu& c) {
        const int ch = int32_t(c.arg(0));
        return uint32_t(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    });
    provideCdecl("tolower", [](Cpu& c) { return uint32_t(lowerAscii(int32_t(c.arg(0)))); });

    // --- Numbers ------------------------------------------------------------------------
    provideCdecl("atoi", [s](Cpu& c) { return uint32_t(int32_t(std::strtol(s->str(c.arg(0)).c_str(), nullptr, 10))); });
    provideCdecl("atol", [s](Cpu& c) { return uint32_t(int32_t(std::strtol(s->str(c.arg(0)).c_str(), nullptr, 10))); });
    cdeclDouble("atof", [s](Cpu& c) { return std::strtod(s->str(c.arg(0)).c_str(), nullptr); });
    cdeclDouble("strtod", [s](Cpu& c) {
        const std::string text = s->str(c.arg(0));
        char* end = nullptr;
        const double v = std::strtod(text.c_str(), &end);
        if (c.arg(1))
            c.write32(c.arg(1), c.arg(0) + uint32_t(end - text.c_str()));
        return v;
    });
    auto strtoInteger = [s](Cpu& c, bool isSigned) {
        const std::string text = s->str(c.arg(0));
        char* end = nullptr;
        uint32_t v;
        if (isSigned) {
            const long long x = std::strtoll(text.c_str(), &end, int(c.arg(2)));
            v = uint32_t(int32_t(std::clamp<long long>(x, INT32_MIN, INT32_MAX)));
        } else {
            v = uint32_t(std::strtoull(text.c_str(), &end, int(c.arg(2))));
        }
        if (c.arg(1))
            c.write32(c.arg(1), c.arg(0) + uint32_t(end - text.c_str()));
        return v;
    };
    provideCdecl("strtol", [strtoInteger](Cpu& c) { return strtoInteger(c, true); });
    provideCdecl("strtoul", [strtoInteger](Cpu& c) { return strtoInteger(c, false); });
    // _itoa(value, buffer, radix): negative only in base 10.
    auto itoa = [](Cpu& c, uint32_t value, bool negative) {
        const uint32_t radix = std::clamp(c.arg(2), 2u, 36u);
        std::string digits;
        uint32_t v = negative ? uint32_t(-int64_t(int32_t(value))) : value;
        do {
            const uint32_t d = v % radix;
            digits.insert(digits.begin(), char(d < 10 ? '0' + d : 'a' + d - 10));
            v /= radix;
        } while (v);
        if (negative)
            digits.insert(digits.begin(), '-');
        c.writeString(c.arg(1), digits, uint32_t(digits.size() + 1));
        return c.arg(1);
    };
    provideCdecl("_itoa", [itoa](Cpu& c) { return itoa(c, c.arg(0), c.arg(2) == 10 && int32_t(c.arg(0)) < 0); });
    provideCdecl("_ltoa", [itoa](Cpu& c) { return itoa(c, c.arg(0), c.arg(2) == 10 && int32_t(c.arg(0)) < 0); });
    provideCdecl("_ultoa", [itoa](Cpu& c) { return itoa(c, c.arg(0), false); });
    provideCdecl("abs", [](Cpu& c) { return uint32_t(std::abs(int32_t(c.arg(0)))); });
    provideCdecl("labs", [](Cpu& c) { return uint32_t(std::abs(int32_t(c.arg(0)))); });
    // rand: Visual C++'s generator (seed * 214013 + 2531011, bits 16 to
    // 30), so a game's random choices come out as they did.
    provideCdecl("srand", [s](Cpu& c) {
        s->holdrand = c.arg(0);
        return 0u;
    });
    provideCdecl("rand", [s](Cpu&) {
        s->holdrand = s->holdrand * 214013u + 2531011u;
        return (s->holdrand >> 16) & 0x7fffu;
    });
    // qsort(base, count, size, compare): a merge sort, which needs only
    // the comparison to order consistently, calling the program's.
    provideCdecl("qsort", [s](Cpu& c) {
        const uint32_t base = c.arg(0), count = c.arg(1), size = c.arg(2), fn = c.arg(3);
        if (count < 2 || !size || !c.valid(base, count * size))
            return 0u;
        std::vector<uint32_t> order(count);
        for (uint32_t i = 0; i < count; ++i)
            order[i] = i;
        // Comparisons on copies of the elements kept aside, so the program
        // sees its elements whole while they're compared.
        const uint32_t copy = s->alloc(count * size);
        std::memcpy(s->ptr(copy), s->ptr(base), size_t(count) * size);
        bool failed = false;
        std::vector<uint32_t> tmp(count);
        for (uint32_t width = 1; width < count && !failed; width *= 2)
            for (uint32_t lo = 0; lo < count && !failed; lo += 2 * width) {
                const uint32_t mid = std::min(lo + width, count), hi = std::min(lo + 2 * width, count);
                uint32_t a = lo, b = mid, out = lo;
                while (a < mid && b < hi) {
                    uint32_t r = 0;
                    if (Error e = c.call(fn, {copy + order[b] * size, copy + order[a] * size}, r)) {
                        c.abort(e.message);
                        failed = true;
                        break;
                    }
                    tmp[out++] = int32_t(r) < 0 ? order[b++] : order[a++];
                }
                while (a < mid)
                    tmp[out++] = order[a++];
                while (b < hi)
                    tmp[out++] = order[b++];
                std::copy(tmp.begin() + lo, tmp.begin() + hi, order.begin() + lo);
            }
        if (!failed)
            for (uint32_t i = 0; i < count; ++i)
                std::memcpy(s->ptr(base + i * size), s->ptr(copy + order[i] * size), size);
        s->p.memory().free(copy);
        return 0u;
    });

    // --- Maths ----------------------------------------------------------------------------
    cdeclDouble("floor", [](Cpu& c) { return std::floor(argDouble(c, 0)); });
    cdeclDouble("ceil", [](Cpu& c) { return std::ceil(argDouble(c, 0)); });
    cdeclDouble("fabs", [](Cpu& c) { return std::fabs(argDouble(c, 0)); });
    cdeclDouble("sqrt", [](Cpu& c) { return std::sqrt(argDouble(c, 0)); });
    cdeclDouble("sin", [](Cpu& c) { return std::sin(argDouble(c, 0)); });
    cdeclDouble("cos", [](Cpu& c) { return std::cos(argDouble(c, 0)); });
    cdeclDouble("tan", [](Cpu& c) { return std::tan(argDouble(c, 0)); });
    cdeclDouble("atan", [](Cpu& c) { return std::atan(argDouble(c, 0)); });
    cdeclDouble("atan2", [](Cpu& c) { return std::atan2(argDouble(c, 0), argDouble(c, 2)); });
    cdeclDouble("exp", [](Cpu& c) { return std::exp(argDouble(c, 0)); });
    cdeclDouble("log", [](Cpu& c) { return std::log(argDouble(c, 0)); });
    cdeclDouble("pow", [](Cpu& c) { return std::pow(argDouble(c, 0), argDouble(c, 2)); });
    cdeclDouble("fmod", [](Cpu& c) { return std::fmod(argDouble(c, 0), argDouble(c, 2)); });
    // modf(x, &whole): the fraction, the whole part stored.
    cdeclDouble("modf", [](Cpu& c) {
        double whole = 0;
        const double fraction = std::modf(argDouble(c, 0), &whole);
        uint64_t bits;
        std::memcpy(&bits, &whole, 8);
        c.write32(c.arg(2), uint32_t(bits));
        c.write32(c.arg(2) + 4, uint32_t(bits >> 32));
        return fraction;
    });
    // The _CI functions take their operands on the FPU stack (the second
    // operand on top) and leave the result there.
    auto fpu1 = [&process](const char* name, double (*fn)(double)) {
        process.provide(kDll, name, [fn](Cpu& c) {
            c.fpuPush(fn(c.fpuPop()));
            return 0u;
        });
    };
    auto fpu2 = [&process](const char* name, double (*fn)(double, double)) {
        process.provide(kDll, name, [fn](Cpu& c) {
            const double y = c.fpuPop(), x = c.fpuPop();
            c.fpuPush(fn(x, y));
            return 0u;
        });
    };
    fpu1("_CIacos", [](double x) { return std::acos(x); });
    fpu1("_CIasin", [](double x) { return std::asin(x); });
    fpu1("_CIatan", [](double x) { return std::atan(x); });
    fpu1("_CIsqrt", [](double x) { return std::sqrt(x); });
    fpu1("_CIsin", [](double x) { return std::sin(x); });
    fpu1("_CIcos", [](double x) { return std::cos(x); });
    fpu1("_CItan", [](double x) { return std::tan(x); });
    fpu1("_CIexp", [](double x) { return std::exp(x); });
    fpu1("_CIlog", [](double x) { return std::log(x); });
    fpu1("_CIlog10", [](double x) { return std::log10(x); });
    fpu2("_CIpow", [](double x, double y) { return std::pow(x, y); });
    fpu2("_CIfmod", [](double x, double y) { return std::fmod(x, y); });
    fpu2("_CIatan2", [](double y, double x) { return std::atan2(y, x); });
    // _ftol: the top of the FPU stack, popped, as a 64-bit integer cut
    // towards zero, in EDX:EAX.
    process.provide(kDll, "_ftol", [](Cpu& c) {
        const double v = std::trunc(c.fpuPop());
        const int64_t i = v >= 9.2e18 || v <= -9.2e18 || std::isnan(v) ? INT64_MIN : int64_t(v);
        c.reg[Cpu::EAX] = uint32_t(uint64_t(i));
        c.reg[Cpu::EDX] = uint32_t(uint64_t(i) >> 32);
        return 0u;
    });
    // _controlfp(new, mask): Visual C++'s portable control bits, as the
    // x87 control word holds them.
    provideCdecl("_controlfp", [](Cpu& c) {
        auto toPortable = [](uint16_t cw) {
            uint32_t v = 0;
            if (cw & 0x01) v |= 0x10;    // invalid
            if (cw & 0x02) v |= 0x80000; // denormal
            if (cw & 0x04) v |= 0x08;    // zero divide
            if (cw & 0x08) v |= 0x04;    // overflow
            if (cw & 0x10) v |= 0x02;    // underflow
            if (cw & 0x20) v |= 0x01;    // inexact
            v |= uint32_t((cw >> 10) & 3) << 8; // rounding
            const int pc = (cw >> 8) & 3;
            v |= pc == 0 ? 0x20000u : pc == 2 ? 0x10000u : 0u; // precision: 24, 53, 64 bits
            return v;
        };
        const uint32_t current = toPortable(c.fpu.control);
        const uint32_t mask = c.arg(1), wanted = (current & ~mask) | (c.arg(0) & mask);
        uint16_t cw = uint16_t(c.fpu.control & ~0x0f3fu);
        if (wanted & 0x10) cw |= 0x01;
        if (wanted & 0x80000) cw |= 0x02;
        if (wanted & 0x08) cw |= 0x04;
        if (wanted & 0x04) cw |= 0x08;
        if (wanted & 0x02) cw |= 0x10;
        if (wanted & 0x01) cw |= 0x20;
        cw |= uint16_t(((wanted >> 8) & 3) << 10);
        const uint32_t pc = wanted & 0x30000;
        cw |= uint16_t((pc == 0x20000 ? 0 : pc == 0x10000 ? 2 : 3) << 8);
        c.fpu.control = cw;
        return wanted;
    });

    // --- Formatted text ----------------------------------------------------------------------
    // The arguments after the fixed ones are on the stack from slot `first`.
    auto varargs = [](const Cpu& c, int first) { return c.reg[Cpu::ESP] + 4 + 4 * uint32_t(first); };
    auto put = [s](uint32_t to, const std::string& text) {
        s->c.writeString(to, text, uint32_t(text.size() + 1));
        return uint32_t(text.size());
    };
    provideCdecl("sprintf", [put, varargs](Cpu& c) { return put(c.arg(0), formatPrintf(c, c.arg(1), varargs(c, 2))); });
    provideCdecl("vsprintf", [put](Cpu& c) { return put(c.arg(0), formatPrintf(c, c.arg(1), c.arg(2))); });
    // _snprintf(buffer, size, format, ...): -1 (and no NUL) if it doesn't fit.
    auto boundedPrintf = [](Cpu& c, uint32_t args) {
        const std::string text = formatPrintf(c, c.arg(2), args);
        const uint32_t size = c.arg(1);
        const uint32_t n = uint32_t(std::min<size_t>(text.size(), size));
        for (uint32_t i = 0; i < n; ++i)
            c.write8(c.arg(0) + i, uint8_t(text[i]));
        if (text.size() < size) {
            c.write8(c.arg(0) + n, 0);
            return uint32_t(text.size());
        }
        return 0xffffffffu;
    };
    provideCdecl("_snprintf", [boundedPrintf, varargs](Cpu& c) { return boundedPrintf(c, varargs(c, 3)); });
    provideCdecl("_vsnprintf", [boundedPrintf](Cpu& c) { return boundedPrintf(c, c.arg(3)); });
    provideCdecl("sscanf", [s, varargs](Cpu& c) {
        return uint32_t(scanFormat(c, s->str(c.arg(0)), s->str(c.arg(1)), varargs(c, 2)));
    });

    // --- Files (FILE) -------------------------------------------------------------------------
    // A FILE is 32 bytes in the program's memory; the ones it opens are
    // backed by the process's file handles. stdout and stderr go to the
    // process's output, stdin is empty. A file opened in text mode (no
    // 'b') has CR-LF line ends, read as LF and written from LF, as on
    // Windows; Ctrl-Z isn't taken as the end.
    auto output = [s](uint32_t file, std::string text) -> bool {
        if (s->isStd(file)) {
            if (file != s->iob && s->p.output)
                s->p.output(text);
            return true;
        }
        auto it = s->handles.find(file);
        if (it == s->handles.end())
            return false;
        Win32Process::OpenFile* f = s->p.file(it->second);
        if (s->text.count(file))
            for (size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 2))
                text.insert(at, 1, '\r');
        return f && std::fwrite(text.data(), 1, text.size(), f->file) == text.size();
    };
    auto hostFile = [s](uint32_t file) -> std::FILE* {
        auto it = s->handles.find(file);
        if (it == s->handles.end())
            return nullptr;
        Win32Process::OpenFile* f = s->p.file(it->second);
        return f ? f->file : nullptr;
    };
    // The next character of a file the program opened, with what ungetc
    // put back and text mode's CR-LF read as LF; EOF at the end.
    auto getChar = [s](uint32_t file, std::FILE* f) -> int {
        if (auto u = s->ungot.find(file); u != s->ungot.end()) {
            const int ch = u->second;
            s->ungot.erase(u);
            return ch;
        }
        int ch = std::fgetc(f);
        if (ch == '\r' && s->text.count(file)) {
            const int next = std::fgetc(f);
            if (next == '\n')
                ch = '\n';
            else if (next != EOF)
                std::ungetc(next, f);
        }
        return ch;
    };
    provideCdecl("fopen", [s](Cpu& c) -> uint32_t {
        std::string mode = s->str(c.arg(1));
        mode.erase(std::remove_if(mode.begin(), mode.end(), [](char ch) { return ch == 't'; }), mode.end());
        const bool text = mode.find('b') == std::string::npos;
        if (text)
            mode += 'b'; // the translation is ours
        const uint32_t handle = s->p.openFile(s->str(c.arg(0)), mode.c_str());
        if (!handle) {
            s->setErrno(kErrnoNoEntry);
            return 0u;
        }
        const uint32_t file = s->alloc(kFileSize);
        c.write32(file + 16, handle); // _file
        s->handles[file] = handle;
        if (text)
            s->text.insert(file);
        return file;
    });
    provideCdecl("fclose", [s](Cpu& c) {
        const uint32_t file = c.arg(0);
        auto it = s->handles.find(file);
        if (it == s->handles.end())
            return s->isStd(file) ? 0u : 0xffffffffu;
        s->p.closeFile(it->second);
        s->handles.erase(it);
        s->ungot.erase(file);
        s->text.erase(file);
        s->p.memory().free(file);
        return 0u;
    });
    provideCdecl("fprintf", [output, varargs](Cpu& c) {
        const std::string text = formatPrintf(c, c.arg(1), varargs(c, 2));
        return output(c.arg(0), text) ? uint32_t(text.size()) : 0xffffffffu;
    });
    provideCdecl("vfprintf", [output](Cpu& c) {
        const std::string text = formatPrintf(c, c.arg(1), c.arg(2));
        return output(c.arg(0), text) ? uint32_t(text.size()) : 0xffffffffu;
    });
    provideCdecl("printf", [s, varargs](Cpu& c) {
        const std::string text = formatPrintf(c, c.arg(0), varargs(c, 1));
        if (s->p.output)
            s->p.output(text);
        return uint32_t(text.size());
    });
    provideCdecl("fputs", [s, output](Cpu& c) { return output(c.arg(1), s->str(c.arg(0))) ? 0u : 0xffffffffu; });
    provideCdecl("fputc", [output](Cpu& c) { return output(c.arg(1), std::string(1, char(c.arg(0)))) ? c.arg(0) & 0xff : 0xffffffffu; });
    provideCdecl("fwrite", [s, output](Cpu& c) {
        const uint32_t bytes = c.arg(1) * c.arg(2);
        if (!c.valid(c.arg(0), bytes) || !c.arg(1))
            return 0u;
        return output(c.arg(3), std::string(s->ptr(c.arg(0)), bytes)) ? c.arg(2) : 0u;
    });
    provideCdecl("fread", [s, hostFile, getChar](Cpu& c) {
        std::FILE* f = hostFile(c.arg(3));
        const uint32_t size = c.arg(1), bytes = c.arg(1) * c.arg(2);
        if (!f || !size || !c.valid(c.arg(0), bytes))
            return 0u;
        if (s->text.count(c.arg(3))) {
            uint32_t n = 0;
            for (int ch; n < bytes && (ch = getChar(c.arg(3), f)) != EOF; ++n)
                c.write8(c.arg(0) + n, uint8_t(ch));
            return n / size;
        }
        uint32_t at = 0;
        if (auto u = s->ungot.find(c.arg(3)); u != s->ungot.end() && bytes) {
            c.write8(c.arg(0), uint8_t(u->second));
            s->ungot.erase(u);
            at = 1;
        }
        const size_t n = std::fread(s->ptr(c.arg(0)) + at, 1, bytes - at, f) + at;
        return uint32_t(n / size);
    });
    provideCdecl("fgetc", [hostFile, getChar](Cpu& c) {
        std::FILE* f = hostFile(c.arg(0));
        return f ? uint32_t(getChar(c.arg(0), f)) : 0xffffffffu;
    });
    provideCdecl("ungetc", [s](Cpu& c) {
        if (int32_t(c.arg(0)) == -1)
            return 0xffffffffu;
        s->ungot[c.arg(1)] = int(c.arg(0) & 0xff);
        return c.arg(0) & 0xff;
    });
    provideCdecl("fgets", [hostFile, getChar](Cpu& c) -> uint32_t {
        std::FILE* f = hostFile(c.arg(2));
        const int32_t n = int32_t(c.arg(1));
        if (!f || n <= 0 || !c.valid(c.arg(0), uint32_t(n)))
            return 0u;
        int32_t i = 0;
        while (i < n - 1) {
            const int ch = getChar(c.arg(2), f);
            if (ch == EOF)
                break;
            c.write8(c.arg(0) + uint32_t(i++), uint8_t(ch));
            if (ch == '\n')
                break;
        }
        if (!i)
            return 0u;
        c.write8(c.arg(0) + uint32_t(i), 0);
        return c.arg(0);
    });
    provideCdecl("fseek", [s, hostFile](Cpu& c) {
        std::FILE* f = hostFile(c.arg(0));
        s->ungot.erase(c.arg(0));
        static const int whence[3] = {SEEK_SET, SEEK_CUR, SEEK_END};
        return f && c.arg(2) <= 2 && std::fseek(f, int32_t(c.arg(1)), whence[c.arg(2)]) == 0 ? 0u : 0xffffffffu;
    });
    provideCdecl("ftell", [s, hostFile](Cpu& c) {
        std::FILE* f = hostFile(c.arg(0));
        if (!f)
            return 0xffffffffu;
        return uint32_t(std::ftell(f) - (s->ungot.count(c.arg(0)) ? 1 : 0));
    });
    provideCdecl("feof", [s, hostFile](Cpu& c) {
        std::FILE* f = hostFile(c.arg(0));
        return f && !s->ungot.count(c.arg(0)) && std::feof(f) ? 1u : 0u;
    });
    provideCdecl("ferror", [hostFile](Cpu& c) {
        std::FILE* f = hostFile(c.arg(0));
        return f && std::ferror(f) ? 1u : 0u;
    });
    provideCdecl("fflush", [hostFile](Cpu& c) {
        if (std::FILE* f = hostFile(c.arg(0)))
            std::fflush(f);
        return 0u;
    });
    provideCdecl("setvbuf", [](Cpu&) { return 0u; });

    // --- Files by name ------------------------------------------------------------------------
    auto host = [s](uint32_t name, bool writing) {
        const std::string full = s->p.fullPath(s->str(name));
        return s->p.mapPath ? s->p.mapPath(full, writing) : std::nullopt;
    };
    // _access(path, mode): 0 if it's there; mode 2 or 6 asks for writing.
    provideCdecl("_access", [s, host](Cpu& c) {
        const auto path = host(c.arg(0), (c.arg(1) & 2) != 0);
        std::error_code ec;
        if (!path || !std::filesystem::exists(*path, ec)) {
            s->setErrno(kErrnoNoEntry);
            return 0xffffffffu;
        }
        return 0u;
    });
    // _stat(path, struct _stat*): Visual C++ 6's layout (36 bytes):
    // st_dev, st_ino (16 bits), st_mode (16), st_nlink, st_uid, st_gid
    // (16 each), st_rdev, st_size, st_atime, st_mtime, st_ctime.
    provideCdecl("_stat", [s, host](Cpu& c) {
        const auto path = host(c.arg(0), false);
        std::error_code ec;
        if (!path || !std::filesystem::exists(*path, ec) || !c.valid(c.arg(1), 36)) {
            s->setErrno(kErrnoNoEntry);
            return 0xffffffffu;
        }
        const uint32_t out = c.arg(1);
        std::memset(s->ptr(out), 0, 36);
        const bool dir = std::filesystem::is_directory(*path, ec);
        const std::string full = s->p.fullPath(s->str(c.arg(0)));
        const uint32_t drive = uint32_t(std::toupper(static_cast<unsigned char>(full[0])) - 'A');
        c.write32(out, drive);
        c.write16(out + 6, uint16_t((dir ? 0x4000 : 0x8000) | 0x100 | 0x80 | (dir ? 0x40 : 0)));
        c.write16(out + 8, 1);
        c.write32(out + 16, drive);
        c.write32(out + 20, dir ? 0 : uint32_t(std::filesystem::file_size(*path, ec)));
        const auto t = std::filesystem::last_write_time(*path, ec);
        const int64_t unix = std::chrono::duration_cast<std::chrono::seconds>(
                                 (t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now())
                                     .time_since_epoch())
                                 .count();
        for (uint32_t at : {24u, 28u, 32u})
            c.write32(out + at, uint32_t(std::max<int64_t>(unix, 0)));
        return 0u;
    });
    provideCdecl("_chdir", [s](Cpu& c) {
        s->p.setCurrentFolder(s->str(c.arg(0)));
        return 0u;
    });
    // _getcwd(buffer, size): the current folder, without its last backslash
    // unless it's a drive's root; a buffer is allocated if none is given.
    provideCdecl("_getcwd", [s](Cpu& c) -> uint32_t {
        std::string dir = s->p.currentFolder();
        if (dir.size() > 3)
            dir.pop_back();
        uint32_t to = c.arg(0);
        const uint32_t size = c.arg(1);
        if (!to)
            to = s->alloc(std::max<uint32_t>(size, uint32_t(dir.size() + 1)));
        else if (size <= dir.size())
            return 0u;
        c.writeString(to, dir, uint32_t(dir.size() + 1));
        return to;
    });
    // _splitpath(path, drive, dir, name, ext) and _makepath(path, drive,
    // dir, name, ext), as documented.
    provideCdecl("_splitpath", [s](Cpu& c) {
        std::string p = s->str(c.arg(0));
        std::string drive, dir, name, ext;
        if (p.size() >= 2 && p[1] == ':')
            drive = p.substr(0, 2), p = p.substr(2);
        const size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos)
            dir = p.substr(0, slash + 1), p = p.substr(slash + 1);
        const size_t dot = p.rfind('.');
        if (dot != std::string::npos)
            ext = p.substr(dot), p = p.substr(0, dot);
        name = p;
        const std::pair<uint32_t, const std::string*> parts[] = {
            {c.arg(1), &drive}, {c.arg(2), &dir}, {c.arg(3), &name}, {c.arg(4), &ext}};
        for (const auto& [to, text] : parts)
            if (to)
                c.writeString(to, *text, uint32_t(text->size() + 1));
        return 0u;
    });
    provideCdecl("_makepath", [s](Cpu& c) {
        std::string p;
        if (c.arg(1)) {
            const std::string drive = s->str(c.arg(1));
            if (!drive.empty())
                p += drive.substr(0, 1) + ":";
        }
        if (c.arg(2)) {
            const std::string dir = s->str(c.arg(2));
            p += dir;
            if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
                p += '\\';
        }
        if (c.arg(3))
            p += s->str(c.arg(3));
        if (c.arg(4)) {
            const std::string ext = s->str(c.arg(4));
            if (!ext.empty() && ext[0] != '.')
                p += '.';
            p += ext;
        }
        c.writeString(c.arg(0), p, uint32_t(p.size() + 1));
        return 0u;
    });

    // --- Time -------------------------------------------------------------------------------------
    provideCdecl("time", [](Cpu& c) {
        const uint32_t now = uint32_t(std::time(nullptr));
        if (c.arg(0))
            c.write32(c.arg(0), now);
        return now;
    });
    provideCdecl("clock", [s](Cpu&) { return s->p.milliseconds ? s->p.milliseconds() : 0u; }); // CLOCKS_PER_SEC is 1000
    // localtime(time_t*): a struct tm (nine ints) kept here.
    provideCdecl("localtime", [s](Cpu& c) -> uint32_t {
        const std::time_t t = std::time_t(c.read32(c.arg(0)));
        const std::tm* tm = std::localtime(&t);
        if (!tm)
            return 0u;
        const int fields[9] = {tm->tm_sec,  tm->tm_min,  tm->tm_hour, tm->tm_mday, tm->tm_mon,
                               tm->tm_year, tm->tm_wday, tm->tm_yday, tm->tm_isdst};
        for (uint32_t i = 0; i < 9; ++i)
            c.write32(s->tm + 4 * i, uint32_t(fields[i]));
        return s->tm;
    });
    provideCdecl("ctime", [s](Cpu& c) -> uint32_t {
        const std::time_t t = std::time_t(c.read32(c.arg(0)));
        const std::tm* tm = std::localtime(&t);
        if (!tm)
            return 0u;
        char buf[32];
        std::strftime(buf, sizeof buf, "%a %b %d %H:%M:%S %Y\n", tm);
        c.writeString(s->ctimeText, buf, 32);
        return s->ctimeText;
    });

    // --- Threads ------------------------------------------------------------------------------------
    // _beginthread(start, stack size, arg): run in turns with the main
    // thread (Win32Process::startThread).
    provideCdecl("_beginthread", [s](Cpu& c) { return s->p.startThread(c.arg(0), c.arg(2)); });
    provideCdecl("_endthread", [s](Cpu& c) {
        if (s->p.inThread())
            s->p.threadEnd();
        else
            c.abort("the program ended its main thread (_endthread)");
        return 0u;
    });

    // Those that write only what they note (memcpy, memmove, memset) or
    // nothing at all: frequent enough that each would otherwise send the
    // instruction cache to check all its code again (Cpu::setWritesTracked).
    for (const char* name : {"memcpy", "memmove", "memset", "memcmp", "memchr", "strlen", "strcmp", "strncmp",
                             "_stricmp", "_strnicmp", "toupper", "tolower", "_ftol", "floor", "ceil", "_CIacos",
                             "_CIasin", "_CIatan", "_CIsqrt", "_CIsin", "_CIcos", "_CItan", "_CIexp", "_CIlog",
                             "_CIlog10", "_CIpow", "_CIfmod", "_CIatan2"})
        c.setWritesTracked(std::string(kDll) + "!" + name);
}

} // namespace reluva::x86
