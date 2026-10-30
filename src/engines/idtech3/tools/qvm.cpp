// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's id Tech 3 commands for studying game code compiled to QVMs.

#include "tools/tool.h"
#include "engines/idtech3/tools/game_folder.h"

#include "engines/idtech3/cvars.h"
#include "engines/idtech3/qvm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva::tool;
using namespace reluva::idtech3::tool;

// Loads a QVM from a game folder's search path.
bool loadQvm(const char* folder, const char* name, reluva::idtech3::Qvm& vm) {
    reluva::SearchSet set;
    if (!mountIdTech3(folder, set))
        return false;
    Bytes data;
    reluva::Error err = set.read(name, data);
    if (!err)
        err = vm.load(data);
    if (err) {
        std::fprintf(stderr, "%s: %s\n", name, err.message.c_str());
        return false;
    }
    return true;
}

// One instruction, with what its operand probably means: the text a
// constant points at, a float, or the system call a call makes.
void printQvmInstruction(const reluva::idtech3::Qvm& vm, size_t i, bool mark) {
    namespace q3 = reluva::idtech3;
    const auto& code = vm.code();
    const auto& in = code[i];
    std::printf("%s%7zu  %-9s", mark ? "> " : "  ", i, q3::qvmop::name(in.op));
    if (q3::qvmop::operandSize(in.op))
        std::printf(" %d", in.operand);
    if (in.op == q3::qvmop::Enter)
        std::printf("    ; function start, %d bytes of frame", in.operand);
    if (in.op == q3::qvmop::Const) {
        const int32_t v = in.operand;
        if (i + 1 < code.size() && code[i + 1].op == q3::qvmop::Call && v < 0)
            std::printf("    ; system call %d", -1 - v);
        else if (i + 1 < code.size() && code[i + 1].op == q3::qvmop::Call)
            std::printf("    ; calls the function at %d", v);
        else if (v > 0 && uint32_t(v) >= vm.litStart() && uint32_t(v) < vm.litEnd()) {
            const std::string text = vm.string(v, 60);
            bool printable = text.size() >= 2;
            for (char c : text)
                printable = printable && ((c >= 32 && c < 127) || c == '\n');
            if (printable)
                std::printf("    ; \"%s\"", text.c_str());
        }
        const float f = q3::Qvm::asFloat(v);
        const float a = std::fabs(f);
        if ((v & 0x7f800000) != 0 && (v < -65536 || v > 65536) && a > 1e-6f && a < 1e7f)
            std::printf("    ; %g as a float", double(f));
    }
    std::printf("\n");
}

int cmdQvmInfo(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::fprintf(stderr, "usage: reluva-tool qvm-info <game folder> <vm/x.qvm> [-s]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    q3::Qvm vm;
    if (!loadQvm(argv[2], argv[3], vm))
        return 1;
    const auto& code = vm.code();
    int functions = 0;
    // System calls in the code: a constant negative target just before a
    // CALL. Per number: how many call sites, and in how many functions.
    std::map<int, std::pair<int, std::set<int>>> calls;
    int indirect = 0, function = -1;
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i].op == q3::qvmop::Enter) {
            ++functions;
            function = int(i);
        }
        if (code[i].op != q3::qvmop::Call)
            continue;
        if (i > 0 && code[i - 1].op == q3::qvmop::Const) {
            const int32_t target = code[i - 1].operand;
            if (target < 0) {
                auto& [n, fns] = calls[-1 - target];
                ++n;
                fns.insert(function);
            }
        } else {
            ++indirect;
        }
    }
    std::printf("%s: %zu instructions in %d functions, %u bytes of initialised data and literals, %zu bytes of memory\n",
                argv[3], code.size(), functions, unsigned(vm.litEnd()), vm.memory().size());
    std::printf("%d calls through function pointers\n", indirect);
    size_t tableBytes = 0;
    for (const auto& [first, end] : vm.protectedRanges())
        tableBytes += end - first;
    std::printf("%zu switch jump tables (%zu bytes), kept from writes while running\n", vm.protectedRanges().size(),
                tableBytes);
    std::printf("system calls (number: call sites, functions):\n");
    for (const auto& [number, info] : calls)
        std::printf("  %4d: %4d sites in %3zu functions\n", number, info.first, info.second.size());
    if (argc == 5 && std::strcmp(argv[4], "-s") == 0) {
        // Printable runs in the initialised data, which hold the strings.
        const auto& mem = vm.memory();
        std::string run;
        size_t start = 0;
        for (size_t i = 0; i < mem.size() && i < vm.litEnd(); ++i) {
            const char c = char(mem[i]);
            if (c >= 32 && c < 127) {
                if (run.empty())
                    start = i;
                run += c;
                continue;
            }
            if (c == 0 && run.size() >= 3)
                std::printf("  %8zu  %s\n", start, run.c_str());
            run.clear();
        }
    }
    return 0;
}

int cmdQvmRun(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool qvm-run <game folder> <vm/x.qvm> [command [arg...]] [-limit N]\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    q3::Qvm vm;
    if (!loadQvm(argv[2], argv[3], vm))
        return 1;
    int32_t command = 0;
    std::vector<int32_t> args;
    uint64_t limit = 50'000'000;
    bool haveCommand = false;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "-limit") == 0 && i + 1 < argc)
            limit = std::strtoull(argv[++i], nullptr, 0);
        else if (!haveCommand) {
            command = int32_t(std::strtol(argv[i], nullptr, 0));
            haveCommand = true;
        } else
            args.push_back(int32_t(std::strtol(argv[i], nullptr, 0)));
    }
    vm.setInstructionLimit(limit);
    // Log each system call; an argument pointing at text shows the text.
    int logged = 0;
    vm.setSystemCall([&](q3::Qvm& v, int number, const int32_t* a) -> int32_t {
        ++logged;
        std::printf("syscall %4d (", number);
        int last = 5;
        while (last >= 0 && a[last] == 0)
            --last;
        for (int i = 0; i <= last; ++i) {
            std::printf("%s%d", i ? ", " : "", a[i]);
            if (a[i] > 0 && v.valid(a[i], 1)) {
                const std::string s = v.string(a[i], 120);
                bool printable = s.size() >= 2;
                for (char c : s)
                    printable = printable && ((c >= 32 && c < 127) || c == '\n' || c == '\t');
                if (printable) {
                    std::string shown;
                    for (char c : s)
                        shown += c == '\n' ? std::string("\\n") : std::string(1, c);
                    std::printf(" \"%s\"", shown.c_str());
                }
            }
        }
        std::printf(")\n");
        return 0;
    });
    int32_t result = 0;
    reluva::Error err = vm.call(command, args, result);
    std::printf("vmMain(%d) %s %d after %llu instructions and %d system calls\n", command,
                err ? "failed:" : "returned", err ? 0 : result, static_cast<unsigned long long>(vm.executed()), logged);
    if (err)
        std::printf("  %s\n", err.message.c_str());
    return err ? 1 : 0;
}

int cmdQvmRefs(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: reluva-tool qvm-refs <game folder> <vm/x.qvm> <address> <size>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    q3::Qvm vm;
    if (!loadQvm(argv[2], argv[3], vm))
        return 1;
    const long long base = std::strtoll(argv[4], nullptr, 0), size = std::strtoll(argv[5], nullptr, 0);
    // Constants inside the range are addresses of a structure's fields (or
    // of the structure itself): per offset, how often, and what the code
    // does with the address next.
    std::map<long long, std::map<std::string, int>> refs;
    const auto& code = vm.code();
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i].op != q3::qvmop::Const || code[i].operand < base || code[i].operand >= base + size)
            continue;
        const std::string next = i + 1 < code.size() ? q3::qvmop::name(code[i + 1].op) : "end";
        ++refs[code[i].operand - base][next];
    }
    std::printf("constants between %lld and %lld in %s (offset: count followed-by...):\n", base, base + size, argv[3]);
    for (const auto& [offset, nexts] : refs) {
        int total = 0;
        std::string what;
        for (const auto& [op, n] : nexts) {
            total += n;
            what += " " + op + "x" + std::to_string(n);
        }
        std::printf("  %6lld: %4d %s\n", offset, total, what.c_str());
    }
    return 0;
}

int cmdQvmDis(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool qvm-dis <game folder> <vm/x.qvm> <first instruction> [count]\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -ref <address> [size]\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -str <text>\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -syscall <number>\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -syscalls <first> <last>\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -func <instruction>\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -blockcopies\n"
                             "       reluva-tool qvm-dis <game folder> <vm/x.qvm> -fields <address of a pointer>\n");
        return 2;
    }
    namespace q3 = reluva::idtech3;
    q3::Qvm vm;
    if (!loadQvm(argv[2], argv[3], vm))
        return 1;
    const auto& code = vm.code();
    // The function holding instruction i: from its ENTER to the next one.
    auto functionAt = [&](size_t i) {
        size_t first = i;
        while (first > 0 && code[first].op != q3::qvmop::Enter)
            --first;
        size_t last = first + 1;
        while (last < code.size() && code[last].op != q3::qvmop::Enter)
            ++last;
        return std::make_pair(first, last);
    };
    if (std::strcmp(argv[4], "-str") == 0 && argc >= 6) {
        // Every function using a string that contains the text, then the
        // functions those call, each listed whole.
        auto lower = [](std::string t) {
            for (char& c : t)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return t;
        };
        const std::string needle = lower(argv[5]);
        const auto& mem = vm.memory();
        std::map<int32_t, std::string> strings; // address -> text
        for (size_t i = 0; i < vm.litEnd();) {
            size_t j = i;
            while (j < vm.litEnd() && mem[j] >= 32 && mem[j] < 127)
                ++j;
            if (j < vm.litEnd() && mem[j] == 0 && j > i &&
                lower(std::string(mem.begin() + long(i), mem.begin() + long(j))).find(needle) != std::string::npos)
                strings[int32_t(i)] = std::string(mem.begin() + long(i), mem.begin() + long(j));
            i = j + 1;
        }
        std::set<size_t> users, callees;
        for (const auto& [address, text] : strings) {
            std::printf("string at %d: \"%s\"\n", address, text.c_str());
            for (size_t i = 0; i < code.size(); ++i)
                if (code[i].op == q3::qvmop::Const && code[i].operand == address)
                    users.insert(functionAt(i).first);
        }
        for (size_t f : users) {
            const auto [first, last] = functionAt(f);
            for (size_t i = first; i + 1 < last; ++i)
                if (code[i].op == q3::qvmop::Const && code[i + 1].op == q3::qvmop::Call && code[i].operand >= 0 &&
                    size_t(code[i].operand) < code.size() && !users.count(size_t(code[i].operand)))
                    callees.insert(size_t(code[i].operand));
        }
        size_t printed = 0;
        auto list = [&](size_t f, const char* why) {
            const auto [first, last] = functionAt(f);
            std::printf("=== function at %zu (%s), %zu instructions\n", first, why, last - first);
            if (printed > 4000) {
                std::printf("    (not listed: over 4000 lines already)\n");
                return;
            }
            for (size_t i = first; i < last; ++i)
                printQvmInstruction(vm, i, false);
            printed += last - first;
        };
        for (size_t f : users)
            list(f, "uses the string");
        for (size_t f : callees)
            list(f, "called by one above");
        return 0;
    }
    if (std::strcmp(argv[4], "-fields") == 0 && argc >= 6) {
        // The fields used through a pointer kept in a global variable:
        // CONST global, LOAD4 (the pointer), CONST offset, ADD. By offset,
        // how often, and what the code then does (LOAD4: reads, STORE4 or
        // a later store: writes; ARG: passes the field's address).
        const int32_t global = int32_t(std::strtol(argv[5], nullptr, 0));
        std::map<int32_t, std::map<std::string, int>> fields;
        for (size_t i = 0; i + 4 < code.size(); ++i) {
            if (code[i].op != q3::qvmop::Const || code[i].operand != global || code[i + 1].op != q3::qvmop::Load4)
                continue;
            if (code[i + 2].op == q3::qvmop::Const && code[i + 3].op == q3::qvmop::Add)
                ++fields[code[i + 2].operand][q3::qvmop::name(code[i + 4].op)];
            else
                ++fields[0][q3::qvmop::name(code[i + 2].op)];
        }
        std::printf("fields used through the pointer at %d (offset: count followed-by...):\n", global);
        for (const auto& [offset, nexts] : fields) {
            int total = 0;
            std::string what;
            for (const auto& [op, n] : nexts) {
                total += n;
                what += " " + op + "x" + std::to_string(n);
            }
            std::printf("  %6d: %4d %s\n", offset, total, what.c_str());
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-blockcopies") == 0) {
        // Every structure copied whole, by size: a structure's size shows
        // where the code copies one (a command into the movement code...).
        std::map<int32_t, std::vector<size_t>> sizes;
        for (size_t i = 0; i < code.size(); ++i)
            if (code[i].op == q3::qvmop::BlockCopy)
                sizes[code[i].operand].push_back(i);
        std::printf("block copies by size (bytes: count, first instructions):\n");
        for (const auto& [size, at] : sizes) {
            std::printf("  %6d: %4zu ", size, at.size());
            for (size_t k = 0; k < at.size() && k < 6; ++k)
                std::printf(" %zu", at[k]);
            std::printf("\n");
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-syscall") == 0 && argc >= 6) {
        // The code before every call to one system call: how its arguments are made.
        const int32_t target = -1 - int32_t(std::strtol(argv[5], nullptr, 0));
        int found = 0;
        for (size_t i = 0; i + 1 < code.size(); ++i) {
            if (code[i].op != q3::qvmop::Const || code[i].operand != target || code[i + 1].op != q3::qvmop::Call)
                continue;
            if (++found > 30) {
                std::printf("(more than 30 calls)\n");
                break;
            }
            const size_t first = std::max(functionAt(i).first, i >= 16 ? i - 16 : 0);
            std::printf("--- call at instruction %zu\n", i);
            for (size_t j = first; j < std::min(code.size(), i + 8); ++j)
                printQvmInstruction(vm, j, j == i);
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-func") == 0 && argc >= 6) {
        // The whole function holding an instruction, then each function it
        // calls, whole.
        const size_t at = size_t(std::strtoull(argv[5], nullptr, 0));
        if (at >= code.size())
            return 1;
        const auto [first, last] = functionAt(at);
        std::set<size_t> callees;
        for (size_t i = first; i + 1 < last; ++i)
            if (code[i].op == q3::qvmop::Const && code[i + 1].op == q3::qvmop::Call && code[i].operand >= 0 &&
                size_t(code[i].operand) < code.size() && size_t(code[i].operand) != first)
                callees.insert(size_t(code[i].operand));
        std::printf("=== function at %zu, %zu instructions\n", first, last - first);
        for (size_t i = first; i < last; ++i)
            printQvmInstruction(vm, i, i == at);
        for (const size_t f : callees) {
            const auto [cf, cl] = functionAt(f);
            std::printf("=== function at %zu (called by it), %zu instructions\n", cf, cl - cf);
            if (cl - cf > 2000) {
                std::printf("    (not listed: over 2000 instructions)\n");
                continue;
            }
            for (size_t i = cf; i < cl; ++i)
                printQvmInstruction(vm, i, false);
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-syscalls") == 0 && argc >= 7) {
        // Every system call from `first` to `last` the code makes, in
        // short: how many calls, and for a few of them what each argument
        // is made from (a number, a text, a local or global variable...),
        // what the code does with the result, and texts the calling
        // function uses (its messages often name what it calls).
        const int lo = std::atoi(argv[5]), hi = std::atoi(argv[6]);
        auto text = [&](int32_t v) -> std::string {
            if (v <= 0 || uint32_t(v) < vm.litStart() || uint32_t(v) >= vm.litEnd())
                return {};
            const std::string t = vm.string(v, 48);
            for (char c : t)
                if (c < 32 || c >= 127)
                    return {};
            return t;
        };
        // What one argument is made from, from the instructions before its ARG.
        auto describe = [&](size_t argAt, size_t from) -> std::string {
            if (argAt == 0 || argAt - 1 < from)
                return "?";
            const auto& p = code[argAt - 1];
            if (p.op == q3::qvmop::Const) {
                if (const std::string t = text(p.operand); !t.empty())
                    return "\"" + t + "\"";
                return std::to_string(p.operand);
            }
            if (p.op == q3::qvmop::Local)
                return "&local" + std::to_string(p.operand);
            if (p.op == q3::qvmop::Load4 && argAt >= 2 && argAt - 2 >= from) {
                const auto& pp = code[argAt - 2];
                if (pp.op == q3::qvmop::Local)
                    return "local" + std::to_string(pp.operand);
                if (pp.op == q3::qvmop::Const)
                    return "[" + std::to_string(pp.operand) + "]";
                if (pp.op == q3::qvmop::Add && argAt >= 3 && code[argAt - 3].op == q3::qvmop::Const)
                    return "[p+" + std::to_string(code[argAt - 3].operand) + "]";
                return "[...]";
            }
            if (p.op == q3::qvmop::Add && argAt >= 2 && code[argAt - 2].op == q3::qvmop::Const)
                return "p+" + std::to_string(code[argAt - 2].operand);
            return std::string("(") + q3::qvmop::name(p.op) + ")";
        };
        for (int n = lo; n <= hi; ++n) {
            const int32_t target = -1 - n;
            std::vector<size_t> sites;
            for (size_t i = 0; i + 1 < code.size(); ++i)
                if (code[i].op == q3::qvmop::Const && code[i].operand == target && code[i + 1].op == q3::qvmop::Call)
                    sites.push_back(i);
            if (sites.empty())
                continue;
            std::printf("== %d: %zu calls\n", n, sites.size());
            for (size_t k = 0; k < sites.size() && k < 3; ++k) {
                const size_t i = sites[k];
                const auto [first, last] = functionAt(i);
                // The arguments: ARGs since the previous call in the function.
                size_t from = i;
                while (from > first && code[from - 1].op != q3::qvmop::Call)
                    --from;
                std::string args;
                for (size_t j = from; j < i; ++j)
                    if (code[j].op == q3::qvmop::Arg)
                        args += " " + std::to_string(code[j].operand) + ":" + describe(j, from);
                std::string after;
                for (size_t j = i + 2; j < last && j < i + 5; ++j)
                    after += std::string(" ") + q3::qvmop::name(code[j].op) +
                             (q3::qvmop::operandSize(code[j].op) ? " " + std::to_string(code[j].operand) : "");
                std::printf("  at %zu (function %zu): args%s; then%s\n", i, first, args.empty() ? " none" : args.c_str(),
                            after.c_str());
                std::set<std::string> seen;
                for (size_t j = first; j < last && seen.size() < 3; ++j)
                    if (code[j].op == q3::qvmop::Const)
                        if (const std::string t = text(code[j].operand); t.size() >= 4 && seen.insert(t).second)
                            std::printf("      uses \"%s\"\n", t.c_str());
            }
        }
        return 0;
    }
    if (std::strcmp(argv[4], "-ref") == 0 && argc >= 6) {
        // The code around every constant pointing into [address, address + size).
        const long long base = std::strtoll(argv[5], nullptr, 0);
        const long long size = argc >= 7 ? std::strtoll(argv[6], nullptr, 0) : 4;
        int found = 0;
        for (size_t i = 0; i < code.size(); ++i) {
            if (code[i].op != q3::qvmop::Const || code[i].operand < base || code[i].operand >= base + size)
                continue;
            if (++found > 50) {
                std::printf("(more than 50; narrow the range)\n");
                break;
            }
            size_t first = i >= 12 ? i - 12 : 0;
            for (size_t j = i; j > first; --j)
                if (code[j].op == q3::qvmop::Enter) {
                    first = j;
                    break;
                }
            const size_t last = std::min(code.size(), i + 16);
            std::printf("--- offset %lld, instruction %zu\n", code[i].operand - base, i);
            for (size_t j = first; j < last; ++j) {
                if (j > i && code[j].op == q3::qvmop::Enter)
                    break;
                printQvmInstruction(vm, j, j == i);
            }
        }
        return 0;
    }
    const size_t first = size_t(std::strtoull(argv[4], nullptr, 0));
    const size_t count = argc >= 6 ? size_t(std::strtoull(argv[5], nullptr, 0)) : 40;
    for (size_t j = first; j < code.size() && j < first + count; ++j)
        printQvmInstruction(vm, j, false);
    return 0;
}

const Commands commands("idtech3", {
    {"qvm-info", "<game folder> <vm/x.qvm> [-s]",
     "a QVM's sizes, functions, and the system calls its code makes (found in the code, not run); -s "
     "its strings",
     cmdQvmInfo},
    {"qvm-run", "<game folder> <vm/x.qvm> [command [arg...]] [-limit N]",
     "run vmMain(command, args) answering every system call with 0, and log each call and its "
     "arguments",
     cmdQvmRun},
    {"qvm-refs", "<game folder> <vm/x.qvm> <address> <size>",
     "the constants in a QVM's code that point into a structure at address, by offset: which fields it "
     "uses",
     cmdQvmRefs},
    {"qvm-dis", "<game folder> <vm/x.qvm> <first instruction> [count] | -ref <address> [size] | -str <text> | -syscall <number> | -syscalls <first> <last> | -func <instruction> | -blockcopies | -fields <address>",
     "list instructions, with the text, floats and system calls they use; -ref: the code around each "
     "use of an address; -str: the functions using strings with the text, and the functions they call; "
     "-syscall: the code before every call to one system call; -blockcopies: the sizes of the "
     "structures the code copies whole; -fields: the offsets used through a global pointer; -syscalls "
     "first last: each system call in the range in short (calls, arguments, nearby texts); -func N: "
     "the function holding instruction N and the functions it calls",
     cmdQvmDis},
});

} // namespace
