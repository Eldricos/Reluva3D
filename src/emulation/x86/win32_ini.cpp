// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// KERNEL32's INI files (GetPrivateProfileStringA and its kin), as
// Microsoft documents them, read and written through the process's paths
// (mapPath, or its drives). A file named without a folder is in the
// Windows folder, C:\WINDOWS.

#include "emulation/x86/win32.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace reluva::x86 {

namespace {

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(uint8_t(c)));
    return s;
}

std::string trim(const std::string &s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// A file's lines, and where its sections and keys are among them.
struct Ini {
    std::vector<std::string> lines;

    // The line of a section's header, or -1.
    int section(const std::string &name) const {
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string t = trim(lines[i]);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']' &&
                lower(trim(t.substr(1, t.size() - 2))) == lower(trim(name)))
                return int(i);
        }
        return -1;
    }
    // The line after a section's last, from its header.
    size_t sectionEnd(int header) const {
        size_t i = size_t(header) + 1;
        while (i < lines.size()) {
            const std::string t = trim(lines[i]);
            if (!t.empty() && t.front() == '[')
                break;
            ++i;
        }
        return i;
    }
    // A key's line in a section, or -1.
    int key(int header, const std::string &name) const {
        for (size_t i = size_t(header) + 1; i < sectionEnd(header); ++i) {
            const size_t eq = lines[i].find('=');
            if (eq != std::string::npos && lower(trim(lines[i].substr(0, eq))) == lower(trim(name)))
                return int(i);
        }
        return -1;
    }
    std::vector<std::string> sections() const {
        std::vector<std::string> out;
        for (const std::string &l : lines) {
            const std::string t = trim(l);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']')
                out.push_back(trim(t.substr(1, t.size() - 2)));
        }
        return out;
    }
    std::vector<std::string> keys(int header) const {
        std::vector<std::string> out;
        for (size_t i = size_t(header) + 1; i < sectionEnd(header); ++i) {
            const size_t eq = lines[i].find('=');
            if (eq != std::string::npos)
                out.push_back(trim(lines[i].substr(0, eq)));
        }
        return out;
    }
    // A value, its surrounding quotes taken off.
    std::optional<std::string> value(const std::string &sectionName, const std::string &keyName) const {
        const int s = section(sectionName);
        if (s < 0)
            return std::nullopt;
        const int k = key(s, keyName);
        if (k < 0)
            return std::nullopt;
        std::string v = trim(lines[size_t(k)].substr(lines[size_t(k)].find('=') + 1));
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
            v = v.substr(1, v.size() - 2);
        return v;
    }
};

} // namespace

void Win32Process::addIniStandIns() {
    const char *k = "KERNEL32.dll";
    // A file named without a folder is the Windows folder's.
    auto iniPath = [this](const std::string &name) {
        return name.find('\\') == std::string::npos && name.find('/') == std::string::npos ? "C:\\WINDOWS\\" + name
                                                                                           : fullPath(name);
    };
    auto readIni = [this, iniPath](const std::string &name) {
        Ini ini;
        const auto host = mapPath ? mapPath(iniPath(name), false) : std::nullopt;
        if (!host)
            return ini;
        std::ifstream in(*host, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            ini.lines.push_back(line);
        }
        return ini;
    };
    // Writes go where mapPath puts what's written (a drive's writable copy).
    auto writeIni = [this, iniPath](const std::string &name, const Ini &ini) {
        const auto host = mapPath ? mapPath(iniPath(name), true) : std::nullopt;
        if (!host)
            return false;
        std::error_code ec;
        std::filesystem::create_directories(host->parent_path(), ec);
        std::ofstream out(*host, std::ios::binary);
        for (const std::string &l : ini.lines)
            out << l << "\r\n";
        return bool(out);
    };
    // Names one after another, each ending in a zero, and a zero after the
    // last; cut short to fit, the return then being size - 2.
    auto writeList = [](Cpu &c, uint32_t buffer, uint32_t size, const std::vector<std::string> &names) {
        if (size < 2) {
            if (size)
                c.write8(buffer, 0);
            return 0u;
        }
        uint32_t at = 0;
        for (const std::string &n : names) {
            if (at + n.size() + 2 > size) {
                const uint32_t room = size - 2 - at;
                for (uint32_t i = 0; i < room; ++i)
                    c.write8(buffer + at + i, uint8_t(n[i]));
                c.write8(buffer + size - 2, 0);
                c.write8(buffer + size - 1, 0);
                return size - 2;
            }
            c.writeString(buffer + at, n, uint32_t(n.size() + 1));
            at += uint32_t(n.size() + 1);
        }
        c.write8(buffer + at, 0);
        return at;
    };

    // GetPrivateProfileStringA(section, key, default, buffer, size, file):
    // no section lists the sections, no key the section's keys; a value or
    // the default (its trailing spaces taken off), cut to fit, the return
    // then being size - 1.
    stdcall(k, "GetPrivateProfileStringA", 6, [readIni, writeList](Cpu &c) {
        const Ini ini = readIni(c.string(c.arg(5)));
        const uint32_t buffer = c.arg(3);
        const uint32_t size = c.arg(4);
        if (!c.arg(0))
            return writeList(c, buffer, size, ini.sections());
        const std::string section = c.string(c.arg(0));
        if (!c.arg(1)) {
            const int s = ini.section(section);
            return writeList(c, buffer, size, s < 0 ? std::vector<std::string>() : ini.keys(s));
        }
        std::string text;
        if (const auto v = ini.value(section, c.string(c.arg(1))))
            text = *v;
        else if (c.arg(2))
            text = c.string(c.arg(2)).substr(0, c.string(c.arg(2)).find_last_not_of(' ') + 1);
        if (size == 0)
            return 0u;
        if (text.size() >= size) {
            c.writeString(buffer, text.substr(0, size - 1), size);
            return size - 1;
        }
        c.writeString(buffer, text, uint32_t(text.size() + 1));
        return uint32_t(text.size());
    });
    // GetPrivateProfileIntA(section, key, default, file): the number the
    // value starts with, or the default if there's no such key.
    stdcall(k, "GetPrivateProfileIntA", 4, [readIni](Cpu &c) {
        const Ini ini = readIni(c.string(c.arg(3)));
        const auto v = ini.value(c.string(c.arg(0)), c.string(c.arg(1)));
        if (!v)
            return c.arg(2);
        return uint32_t(std::strtol(v->c_str(), nullptr, 10));
    });
    // WritePrivateProfileStringA(section, key, text, file): no key removes
    // the section, no text the key; otherwise the key is set, added at the
    // section's end, or the section added at the file's.
    stdcall(k, "WritePrivateProfileStringA", 4, [readIni, writeIni](Cpu &c) {
        if (!c.arg(0))
            return 1u; // flushing Windows' cache: there's none
        const std::string file = c.string(c.arg(3));
        Ini ini = readIni(file);
        const std::string section = c.string(c.arg(0));
        int s = ini.section(section);
        if (!c.arg(1)) {
            if (s >= 0)
                ini.lines.erase(ini.lines.begin() + s, ini.lines.begin() + std::ptrdiff_t(ini.sectionEnd(s)));
            return writeIni(file, ini) ? 1u : 0u;
        }
        const std::string key = c.string(c.arg(1));
        const int existing = s < 0 ? -1 : ini.key(s, key);
        if (!c.arg(2)) {
            if (existing >= 0)
                ini.lines.erase(ini.lines.begin() + existing);
            return writeIni(file, ini) ? 1u : 0u;
        }
        const std::string line = key + "=" + c.string(c.arg(2));
        if (existing >= 0) {
            ini.lines[size_t(existing)] = line;
        } else if (s >= 0) {
            // After the section's last non-blank line.
            size_t at = ini.sectionEnd(s);
            while (at > size_t(s) + 1 && trim(ini.lines[at - 1]).empty())
                --at;
            ini.lines.insert(ini.lines.begin() + std::ptrdiff_t(at), line);
        } else {
            if (!ini.lines.empty() && !trim(ini.lines.back()).empty())
                ini.lines.emplace_back();
            ini.lines.push_back("[" + section + "]");
            ini.lines.push_back(line);
        }
        return writeIni(file, ini) ? 1u : 0u;
    });
}

} // namespace reluva::x86
