// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool stharbinger's commands for DEEP.EXE, the game's program: a
// map of its strings and who uses them, and listings of its code (as made
// for docs/engines/stharbinger/reverse-engineering/). Ported from STDS9H's
// harbinger_strings.py and harbinger_disasm.py, with the same output.
//
// DEEP.EXE is the DOS/4GW extender followed by the game itself: a small MZ
// stub and a 32-bit LE executable. Its objects are loaded where IDA and
// Ghidra put them (code at 0x10000, data at 0x50000), and its fixup table
// (every 32-bit pointer the loader relocates) says which code, or which data
// table, refers to each string.

#include "common/x86/disasm.h"
#include "tools/tool.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

using reluva::tool::Bytes;

// A 32-bit LE executable: its objects as loaded, and its fixups.
struct LeObject {
    int n = 0;
    uint32_t vsize = 0, base = 0, flags = 0, first = 0, count = 0;
    bool code = false;
    Bytes image;
};

struct Fixup {
    uint32_t source, target;
    int type;
};

struct LeFile {
    long long mz = 0; // the program's own MZ stub (-1 if none was found)
    size_t le = 0;
    uint32_t pageSize = 0, npages = 0, lastPage = 0;
    uint32_t entryObject = 0, entryOffset = 0;
    std::vector<LeObject> objects;
    std::vector<Fixup> fixups;

    const LeObject* objectAt(uint32_t va) const {
        for (const LeObject& o : objects)
            if (o.base <= va && va < o.base + o.vsize)
                return &o;
        return nullptr;
    }
};

uint16_t u16(const Bytes& d, size_t o) { return o + 2 <= d.size() ? uint16_t(d[o] | d[o + 1] << 8) : 0; }
uint32_t u32(const Bytes& d, size_t o) {
    return o + 4 <= d.size()
               ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24
               : 0;
}

// The last "MZ" ending at or before `end`, or -1.
long long findMzBefore(const Bytes& d, long long end) {
    for (long long i = end - 2; i >= 0; --i)
        if (d[size_t(i)] == 'M' && d[size_t(i) + 1] == 'Z')
            return i;
    return -1;
}

bool loadLe(const Bytes& d, LeFile& le, std::string& error) {
    // The LE header: "LE", byte and word order 0, format level 0, a CPU
    // type 1..4; then the MZ stub whose e_lfanew points to it.
    static const uint8_t sig[8] = {'L', 'E', 0, 0, 0, 0, 0, 0};
    bool found = false;
    for (size_t i = 0; i + 8 <= d.size() && !found; ++i) {
        if (std::memcmp(&d[i], sig, 8) != 0)
            continue;
        const uint16_t cpu = u16(d, i + 8);
        if (cpu >= 1 && cpu <= 4) {
            le.le = i;
            long long mz = findMzBefore(d, (long long)i);
            while (mz > 0 && u32(d, size_t(mz) + 0x3c) != uint32_t((long long)i - mz))
                mz = findMzBefore(d, mz);
            le.mz = mz;
            found = true;
        }
        i += 7; // matches don't overlap
    }
    if (!found) {
        error = "no LE header found";
        return false;
    }
    const size_t h = le.le;
    le.pageSize = u32(d, h + 0x28);
    le.npages = u32(d, h + 0x14);
    le.lastPage = u32(d, h + 0x2c);
    le.entryObject = u32(d, h + 0x18);
    le.entryOffset = u32(d, h + 0x1c);
    const long long dataPages = le.mz + u32(d, h + 0x80); // relative to the program's own MZ stub
    const size_t objectTable = h + u32(d, h + 0x40);
    for (uint32_t i = 0; i < u32(d, h + 0x44); ++i) {
        const size_t o = objectTable + 24 * size_t(i);
        LeObject obj;
        obj.n = int(i) + 1;
        obj.vsize = u32(d, o);
        obj.base = u32(d, o + 4);
        obj.flags = u32(d, o + 8);
        obj.first = u32(d, o + 12);
        obj.count = u32(d, o + 16);
        obj.code = (obj.flags & 4) != 0;
        le.objects.push_back(std::move(obj));
    }
    const size_t pageMap = h + u32(d, h + 0x48);
    std::vector<long long> pageFile;
    for (uint32_t i = 0; i < le.npages; ++i) {
        const size_t p = pageMap + 4 * size_t(i);
        const uint32_t num = p + 3 <= d.size() ? uint32_t(d[p]) << 16 | uint32_t(d[p + 1]) << 8 | d[p + 2] : 0;
        pageFile.push_back(dataPages + (long long)(num - 1) * le.pageSize);
    }
    for (LeObject& o : le.objects) {
        o.image.assign(o.vsize, 0);
        for (uint32_t k = 0; k < o.count; ++k) {
            const uint32_t pg = o.first - 1 + k;
            if (pg >= le.npages)
                break;
            const uint32_t size = pg == le.npages - 1 ? le.lastPage : le.pageSize;
            const long long from = pageFile[pg];
            const size_t at = size_t(k) * le.pageSize;
            if (from < 0 || size_t(from) >= d.size() || at >= o.vsize)
                continue;
            const size_t n = std::min({size_t(size), d.size() - size_t(from), size_t(o.vsize) - at});
            std::memcpy(o.image.data() + at, d.data() + from, n);
        }
    }

    // The fixups: per page, records of (source type, flags, source offset or
    // a list of them, target object, target offset).
    const size_t fpt = h + u32(d, h + 0x68), frt = h + u32(d, h + 0x6c);
    std::map<uint32_t, std::pair<const LeObject*, uint32_t>> pageObject;
    for (const LeObject& o : le.objects)
        for (uint32_t k = 0; k < o.count; ++k)
            pageObject[o.first - 1 + k] = {&o, k};
    for (uint32_t pg = 0; pg < le.npages; ++pg) {
        const auto it = pageObject.find(pg);
        if (it == pageObject.end()) {
            error = "page " + std::to_string(pg + 1) + " belongs to no object";
            return false;
        }
        const LeObject& obj = *it->second.first;
        const uint32_t k = it->second.second;
        size_t p = frt + u32(d, fpt + 4 * size_t(pg));
        const size_t end = frt + u32(d, fpt + 4 * size_t(pg + 1));
        while (p < end) {
            if (p + 2 > d.size()) {
                error = "fixup table runs past the end of the file";
                return false;
            }
            const uint8_t src = d[p], fl = d[p + 1];
            p += 2;
            uint8_t cnt = 0;
            int16_t srcoff = 0;
            if (src & 0x20) {
                cnt = p < d.size() ? d[p] : 0;
                p += 1;
            } else {
                srcoff = int16_t(u16(d, p));
                p += 2;
            }
            if (fl & 3) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "import/entry fixups are not handled (flags %#x)", fl);
                error = buf;
                return false;
            }
            const uint32_t tobj = (fl & 0x40) ? u16(d, p) : (p < d.size() ? d[p] : 0);
            p += (fl & 0x40) ? 2 : 1;
            uint32_t toff = 0;
            if ((src & 0xf) != 2) {
                toff = (fl & 0x10) ? u32(d, p) : u16(d, p);
                p += (fl & 0x10) ? 4 : 2;
            }
            std::vector<int16_t> sources;
            if (src & 0x20) {
                for (int i = 0; i < cnt; ++i)
                    sources.push_back(int16_t(u16(d, p + 2 * size_t(i))));
                p += 2 * size_t(cnt);
            } else {
                sources.push_back(srcoff);
            }
            if (tobj < 1 || tobj > le.objects.size()) {
                error = "a fixup's target is object " + std::to_string(tobj) + ", which isn't there";
                return false;
            }
            const uint32_t tva = le.objects[tobj - 1].base + toff;
            for (int16_t so : sources)
                le.fixups.push_back({uint32_t(obj.base + k * le.pageSize + int32_t(so)), tva, src & 0xf});
        }
    }
    return true;
}

struct FoundString {
    uint32_t va;
    std::string text;
};

// NUL-terminated runs of printable text (and tabs, line ends), at least
// `minLength` long and at least a third letters or digits.
std::vector<FoundString> findStrings(const LeObject& obj, size_t minLength = 4) {
    std::vector<FoundString> out;
    const Bytes& img = obj.image;
    auto allowed = [](uint8_t c) { return (c >= 0x20 && c <= 0x7e) || c == '\t' || c == '\r' || c == '\n'; };
    size_t i = 0;
    while (i < img.size()) {
        if (!allowed(img[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < img.size() && allowed(img[j]))
            ++j;
        if (j - i >= minLength && j < img.size() && img[j] == 0) {
            std::string s(img.begin() + long(i), img.begin() + long(j));
            size_t alnum = 0;
            for (char c : s)
                alnum += std::isalnum(static_cast<unsigned char>(c)) ? 1 : 0;
            if (alnum >= std::max<size_t>(3, s.size() / 3))
                out.push_back({obj.base + uint32_t(i), s});
            j += 1;
        }
        i = j;
    }
    return out;
}

// What the debug messages say of the function they come from.
std::optional<std::string> functionName(const std::string& s) {
    static const std::regex func(R"(^\s*([A-Za-z_][A-Za-z0-9_]{2,}):\s)");
    static const std::regex in(R"(^\s*In ([A-Z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+)\b)"); // "In Load_Dialogues: ..."
    static const std::regex directive(R"(^\s*([A-Z][a-z]+(?: [A-Z][a-z]+)+) / (cc[A-Za-z]+))"); // ".../ ccSUB"
    static const std::regex bare(R"(^\s*([A-Z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+)$)");              // "Load_Directive_List"
    std::smatch m;
    if (std::regex_search(s, m, func) || std::regex_search(s, m, in))
        return m[1].str();
    if (std::regex_search(s, m, directive)) {
        std::string name = m[1].str();
        std::replace(name.begin(), name.end(), ' ', '_');
        return name + "/" + m[2].str();
    }
    // (Python's "$" also takes a line end before the end.)
    const std::string trimmed = !s.empty() && s.back() == '\n' ? s.substr(0, s.size() - 1) : s;
    if (std::regex_search(trimmed, m, bare))
        return m[1].str();
    return std::nullopt;
}

std::string strip(const std::string& s) {
    const char* ws = " \t\r\n\v\f";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return {};
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

std::string category(const std::string& s) {
    static const std::regex file(
        R"((^|[\\/])[\w\-]+\.(cmp|rle|raw|pal|smk|dat|all|lst|cat|mus|vce|ini|dig|mgd|sav|cfg|txt|ani|pcx|fnt|exe)$|^\.\w{2,3}$|\\)",
        std::regex::icase);
    static const std::regex format(R"(%[-0-9.]*[dsxufcl])");
    static const std::regex runtime(R"(watcom|copyright|dos/4g|run-time|stack overflow|floating)", std::regex::icase);
    if (std::regex_search(strip(s), file))
        return "file";
    if (functionName(s))
        return "debug";
    if (s.find('%') != std::string::npos && std::regex_search(s, format))
        return "format";
    if (std::regex_search(s, runtime))
        return "runtime";
    return "text";
}

// A string as Python's repr() writes it.
std::string pyRepr(const std::string& s) {
    const bool single = s.find('\'') != std::string::npos && s.find('"') == std::string::npos;
    const char q = single ? '"' : '\'';
    std::string out(1, q);
    for (unsigned char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c == q) {
                out += '\\';
                out += char(c);
            } else if (c < 0x20 || c >= 0x7f) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\x%02x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    return out + q;
}

std::string hex8(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}

// A CSV field as Python's csv module writes it (quoted when it must be).
std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s)
        out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

bool writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    if (!out) {
        std::fprintf(stderr, "%s: cannot write\n", path.string().c_str());
        return false;
    }
    return true;
}

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
        out += (i ? "\n" : "") + lines[i];
    return out + "\n";
}

int cmdExeStrings(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger exe-strings <DEEP.EXE> <out folder>\n");
        return 1;
    }
    Bytes d;
    if (!reluva::tool::readFile(argv[2], d))
        return 1;
    LeFile le;
    std::string error;
    if (!loadLe(d, le, error)) {
        std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
        return 1;
    }
    const std::filesystem::path out = argv[3];
    std::error_code ec;
    std::filesystem::create_directories(out, ec);

    std::map<uint32_t, std::vector<uint32_t>> refs; // target -> sources
    for (const Fixup& f : le.fixups)
        refs[f.target].push_back(f.source);
    auto refsOf = [&](uint32_t va) -> std::vector<uint32_t> {
        const auto it = refs.find(va);
        return it == refs.end() ? std::vector<uint32_t>{} : it->second;
    };
    std::vector<FoundString> strings;
    for (const LeObject& o : le.objects)
        for (FoundString& s : findStrings(o))
            strings.push_back(std::move(s));
    std::map<uint32_t, std::string> byVa;
    for (const FoundString& s : strings)
        byVa[s.va] = s.text;

    // Function names from "Name: message" strings referenced by code, in
    // the order they're first found.
    std::vector<std::string> funcOrder;
    std::map<std::string, std::set<uint32_t>> funcs;
    for (const FoundString& s : strings) {
        const auto name = functionName(s.text);
        if (!name)
            continue;
        for (uint32_t src : refsOf(s.va)) {
            const LeObject* o = le.objectAt(src);
            if (o && o->code) {
                if (!funcs.count(*name))
                    funcOrder.push_back(*name);
                funcs[*name].insert(src);
            }
        }
    }

    // Arrays of pointers to strings inside the data object.
    std::vector<std::pair<uint32_t, uint32_t>> dataPtrs;
    for (const Fixup& f : le.fixups) {
        const LeObject* o = le.objectAt(f.source);
        if (o && !o->code && byVa.count(f.target))
            dataPtrs.push_back({f.source, f.target});
    }
    std::sort(dataPtrs.begin(), dataPtrs.end());
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> tables;
    std::vector<std::pair<uint32_t, uint32_t>> run;
    for (const auto& st : dataPtrs) {
        const int64_t step = run.empty() ? 0 : int64_t(st.first) - int64_t(run.back().first);
        if (!run.empty() && (step == 4 || step == 8 || step == 12 || step == 16) &&
            (run.size() < 2 || step == int64_t(run[1].first) - int64_t(run[0].first))) {
            run.push_back(st);
        } else {
            if (run.size() >= 3)
                tables.push_back(run);
            run = {st};
        }
    }
    if (run.size() >= 3)
        tables.push_back(run);

    auto where = [&](std::vector<uint32_t> vas) {
        std::sort(vas.begin(), vas.end());
        std::string s;
        for (size_t i = 0; i < vas.size() && i < 12; ++i) {
            const LeObject* o = le.objectAt(vas[i]);
            s += (i ? " " : "") + std::string(o && o->code ? "C:" : "D:") + hex8(vas[i]);
        }
        return s + (vas.size() > 12 ? " ..." : "");
    };
    auto padded = [](const std::string& s, size_t width) {
        return s.size() >= width ? s : s + std::string(width - s.size(), ' ');
    };

    std::vector<std::string> lines = {"# DEEP.EXE strings: address  category  [referenced from: C=code D=data]  text",
                                      ""};
    std::string csv = "address,category,ref_count,referenced_from,text\r\n";
    std::vector<std::pair<std::string, int>> counts; // by category, in the order first seen
    for (const FoundString& s : strings) {
        const std::string cat = category(s.text);
        const std::vector<uint32_t> r = refsOf(s.va);
        lines.push_back(hex8(s.va) + "  " + padded(cat, 7) + " " +
                        padded(r.empty() ? "(no direct reference)" : where(r), 40) + " " + pyRepr(s.text));
        std::vector<uint32_t> sorted = r;
        std::sort(sorted.begin(), sorted.end());
        std::string from;
        for (size_t i = 0; i < sorted.size(); ++i)
            from += (i ? " " : "") + hex8(sorted[i]);
        csv += hex8(s.va) + "," + cat + "," + std::to_string(r.size()) + "," + csvField(from) + "," + csvField(s.text) +
               "\r\n";
        auto c = std::find_if(counts.begin(), counts.end(), [&](const auto& kv) { return kv.first == cat; });
        if (c == counts.end())
            counts.push_back({cat, 1});
        else
            c->second++;
    }
    if (!writeText(out / "strings.txt", join(lines)) || !writeText(out / "strings.csv", csv))
        return 1;

    std::vector<std::string> fl = {"# Code addresses inside functions, named from their own debug messages", ""};
    std::stable_sort(funcOrder.begin(), funcOrder.end(),
                     [&](const std::string& a, const std::string& b) { return *funcs[a].begin() < *funcs[b].begin(); });
    for (const std::string& name : funcOrder) {
        std::string vas;
        for (uint32_t v : funcs[name])
            vas += (vas.empty() ? "" : " ") + hex8(v);
        fl.push_back(padded(name, 40) + " " + vas);
    }
    if (!writeText(out / "functions.txt", join(fl)))
        return 1;

    std::vector<std::string> tl = {"# Arrays of string pointers in the data object (address, stride, entries)", ""};
    for (const auto& t : tables) {
        const uint32_t stride = t[1].first - t[0].first;
        const std::string codeRefs = where(refsOf(t[0].first));
        char head[160];
        std::snprintf(head, sizeof head, "table at %08X, stride %u, %zu entries, used by %s", t[0].first, stride,
                      t.size(), codeRefs.empty() ? "(no direct reference)" : codeRefs.c_str());
        tl.push_back(head);
        for (const auto& [s, tv] : t)
            tl.push_back("    " + hex8(s) + " -> " + hex8(tv) + " " + pyRepr(byVa[tv]));
        tl.push_back("");
    }
    if (!writeText(out / "tables.txt", join(tl)))
        return 1;

    std::vector<std::string> files;
    for (const FoundString& s : strings)
        if (category(s.text) == "file") {
            const std::string w = where(refsOf(s.va));
            files.push_back(hex8(s.va) + "  " + padded(w.empty() ? "-" : w, 36) + " " + pyRepr(s.text));
        }
    if (!writeText(out / "files.txt", join(files)))
        return 1;

    std::stable_sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    const uint32_t entry = le.entryObject >= 1 && le.entryObject <= le.objects.size()
                               ? le.objects[le.entryObject - 1].base + le.entryOffset
                               : le.entryOffset;
    std::printf("program at file offset %s0x%llx, LE header at 0x%zx, entry point %08X\n", le.mz < 0 ? "-" : "",
                static_cast<unsigned long long>(le.mz < 0 ? -le.mz : le.mz), le.le, entry);
    for (const LeObject& o : le.objects)
        std::printf("  object %d (%s): %08X-%08X\n", o.n, o.code ? "code" : "data", o.base, o.base + o.vsize);
    std::string summary;
    for (const auto& [cat, n] : counts)
        summary += (summary.empty() ? "" : ", ") + cat + " " + std::to_string(n);
    std::printf("%zu fixups, %zu strings (%s)\n", le.fixups.size(), strings.size(), summary.c_str());
    std::printf("%zu named functions, %zu string tables -> %s\n", funcs.size(), tables.size(), out.string().c_str());
    return 0;
}

int cmdExeDisasm(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger exe-disasm <DEEP.EXE> <start> <end> [names file]\n");
        return 1;
    }
    Bytes d;
    if (!reluva::tool::readFile(argv[2], d))
        return 1;
    LeFile le;
    std::string error;
    if (!loadLe(d, le, error) || le.objects.empty()) {
        std::fprintf(stderr, "%s: %s\n", argv[2], error.empty() ? "no objects" : error.c_str());
        return 1;
    }
    const uint32_t start = uint32_t(std::strtoul(argv[3], nullptr, 16));
    const uint32_t end = uint32_t(std::strtoul(argv[4], nullptr, 16));

    // Names for addresses: "<hex address> <name>" a line.
    std::map<uint32_t, std::string> names;
    const std::filesystem::path namesFile =
        argc > 5 ? std::filesystem::path(argv[5])
                 : std::filesystem::path("docs/engines/stharbinger/reverse-engineering/handler-addresses.txt");
    if (std::ifstream in(namesFile); in) {
        std::string line;
        while (std::getline(in, line)) {
            char addr[64] = {}, name[256] = {};
            if (std::sscanf(line.c_str(), "%63s %255s", addr, name) == 2)
                names[uint32_t(std::strtoul(addr, nullptr, 16))] = name;
        }
    }

    const LeObject& code = le.objects[0];
    std::map<uint32_t, std::string> strings;
    for (const LeObject& o : le.objects)
        for (const FoundString& s : findStrings(o))
            strings[s.va] = s.text;
    std::map<uint32_t, uint32_t> fixups;
    for (const Fixup& f : le.fixups)
        fixups[f.source] = f.target;

    uint32_t p = start;
    while (p < end) {
        if (p < code.base || p - code.base >= code.image.size())
            break;
        const size_t offset = p - code.base;
        const size_t avail = std::min<size_t>(end - p, code.image.size() - offset);
        const reluva::x86::Instruction i = reluva::x86::disassemble(code.image.data() + offset, avail, p);
        if (!i.valid) {
            std::printf("%08X  db %02x\n", p, code.image[offset]);
            p += 1;
            continue;
        }
        std::string note;
        for (uint32_t x = i.address; x < i.address + uint32_t(i.size); ++x) {
            const auto f = fixups.find(x);
            if (f == fixups.end())
                continue;
            const uint32_t t = f->second;
            const auto s = strings.find(t);
            if (s != strings.end()) {
                note += "  ; ->" + hex8(t) + " " + pyRepr(s->second.substr(0, 50));
            } else {
                const auto n = names.find(t);
                note += "  ; ->" + hex8(t) + (n != names.end() ? " " + n->second : "");
            }
        }
        if (i.hasTarget && (i.mnemonic == "call" || i.mnemonic[0] == 'j')) {
            const auto n = names.find(i.target);
            note += "  ; " + (n != names.end() ? n->second : std::string());
        }
        if (const auto n = names.find(i.address); n != names.end())
            std::printf("\n%s:\n", n->second.c_str());
        std::printf("%08X  %-7s %-40s%s\n", i.address, i.mnemonic.c_str(), i.operands.c_str(), note.c_str());
        p = i.address + uint32_t(i.size);
    }
    return 0;
}

const reluva::tool::Commands commands(
    "stharbinger",
    {
        {"exe-strings", "<DEEP.EXE> <out folder>",
         "map DEEP.EXE's strings: every string with its address, category and the code or data that refers to it "
         "(strings.txt, strings.csv), code named after its debug messages (functions.txt), tables of string "
         "pointers (tables.txt) and the file names it uses (files.txt)",
         cmdExeStrings},
        {"exe-disasm", "<DEEP.EXE> <start> <end> [names file]",
         "disassemble DEEP.EXE's code from start to end (hex addresses: code at 0x10000, data at 0x50000), with "
         "the strings and names its pointers and calls go to; names (\"<address> <name>\" a line) default to "
         "docs/engines/stharbinger/reverse-engineering/handler-addresses.txt",
         cmdExeDisasm},
    });

} // namespace
