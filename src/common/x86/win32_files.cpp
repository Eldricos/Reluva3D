// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// KERNEL32's files, as Microsoft documents them: handles to files on this
// machine, found through Win32Process::mapPath; searches; memory-mapped
// views (copies, as the programs here only read them); and the drives and
// volumes a program checks for its CD.

#include "common/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>

namespace reluva::x86 {

namespace {

constexpr uint32_t kInvalidHandle = 0xffffffffu;
constexpr uint32_t kErrorFileNotFound = 2, kErrorPathNotFound = 3, kErrorAccessDenied = 5,
                   kErrorInvalidHandle = 6, kErrorNoMoreFiles = 18;
// FILETIME of 1970-01-01, in 100 ns units since 1601.
constexpr uint64_t kUnixEpoch = 116444736000000000ULL;

std::string upper(std::string s) {
    for (char& c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Whether `name` matches a Windows wildcard (* and ?), ignoring case. "*.*"
// matches names without a dot too, as Windows' does.
bool wildcard(const std::string& pattern, const std::string& name) {
    if (pattern == "*.*" || pattern == "*")
        return true;
    const std::string p = upper(pattern), n = upper(name);
    size_t pi = 0, ni = 0, star = std::string::npos, mark = 0;
    while (ni < n.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == n[ni])) {
            ++pi, ++ni;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ni;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ni = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*')
        ++pi;
    return pi == p.size();
}

uint64_t fileTimeOf(const std::filesystem::path& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    if (ec)
        return kUnixEpoch;
    const auto sys = std::chrono::time_point_cast<std::chrono::seconds>(
        t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const int64_t unix = sys.time_since_epoch().count();
    return kUnixEpoch + uint64_t(std::max<int64_t>(unix, 0)) * 10000000ULL;
}

} // namespace

std::string Win32Process::fullPath(const std::string& path) const {
    std::string p = path;
    std::replace(p.begin(), p.end(), '/', '\\');
    if (p.size() >= 2 && p[1] == ':')
        return p;
    if (!p.empty() && p[0] == '\\')
        return currentFolder_.substr(0, 2) + p;
    std::string full = currentFolder_;
    if (!full.empty() && full.back() != '\\')
        full += '\\';
    // ".\" and "..\" as Windows resolves them.
    while (!p.empty()) {
        const size_t slash = p.find('\\');
        const std::string part = p.substr(0, slash);
        p = slash == std::string::npos ? std::string() : p.substr(slash + 1);
        if (part == ".")
            continue;
        if (part == "..") {
            if (full.size() > 3) {
                full.pop_back();
                full.erase(full.rfind('\\') + 1);
            }
            continue;
        }
        full += part;
        if (slash != std::string::npos || p.size())
            full += '\\';
    }
    return full;
}

void Win32Process::setCurrentFolder(const std::string& folder) {
    currentFolder_ = fullPath(folder);
    if (currentFolder_.empty() || currentFolder_.back() != '\\')
        currentFolder_ += '\\';
}

uint32_t Win32Process::openFile(const std::string& windowsPath, const char* mode) {
    const std::string full = fullPath(windowsPath);
    const bool writing = std::strpbrk(mode, "wa+") != nullptr;
    const auto host = mapPath ? mapPath(full, writing) : std::nullopt;
    if (!host) {
        lastError_ = writing ? kErrorAccessDenied : kErrorFileNotFound;
        return 0;
    }
    std::FILE* f = std::fopen(host->string().c_str(), mode);
    if (!f) {
        lastError_ = kErrorFileNotFound;
        return 0;
    }
    const uint32_t handle = nextFile_;
    nextFile_ += 4;
    files_[handle] = {f, full};
    return handle;
}

Win32Process::OpenFile* Win32Process::file(uint32_t handle) {
    auto it = files_.find(handle);
    return it == files_.end() ? nullptr : &it->second;
}

bool Win32Process::closeFile(uint32_t handle) {
    auto it = files_.find(handle);
    if (it == files_.end())
        return false;
    std::fclose(it->second.file);
    files_.erase(it);
    return true;
}

void Win32Process::addFileStandIns() {
    const char* k = "KERNEL32.dll";

    // CreateFileA(name, access, share, security, disposition, flags, template)
    stdcall(k, "CreateFileA", 7, [this](Cpu& c) {
        const std::string name = c.string(c.arg(0));
        const uint32_t access = c.arg(1), disposition = c.arg(4);
        const bool write = (access & 0x40000000u) != 0; // GENERIC_WRITE
        const char* mode = "rb";
        switch (disposition) {
        case 1: // CREATE_NEW
        case 2: // CREATE_ALWAYS
            mode = write ? "w+b" : "wb";
            break;
        case 4: // OPEN_ALWAYS
            mode = write ? "a+b" : "rb";
            break;
        default: // OPEN_EXISTING, TRUNCATE_EXISTING
            mode = write ? "r+b" : "rb";
            break;
        }
        const uint32_t h = openFile(name, mode);
        if (h && disposition == 4 && write)
            std::fseek(file(h)->file, 0, SEEK_SET);
        return h ? h : kInvalidHandle;
    });
    // ReadFile(handle, buffer, size, read, overlapped)
    stdcall(k, "ReadFile", 5, [this](Cpu& c) {
        OpenFile* f = file(c.arg(0));
        const uint32_t buffer = c.arg(1), size = c.arg(2);
        if (c.arg(3))
            c.write32(c.arg(3), 0);
        if (!f || !c.valid(buffer, size)) {
            lastError_ = kErrorInvalidHandle;
            return 0u;
        }
        const size_t n = std::fread(c.memory().data() + buffer, 1, size, f->file);
        if (c.arg(3))
            c.write32(c.arg(3), uint32_t(n));
        return 1u;
    });
    // SetFilePointer(handle, low, high*, method): 32-bit offsets only.
    stdcall(k, "SetFilePointer", 4, [this](Cpu& c) {
        OpenFile* f = file(c.arg(0));
        if (!f) {
            lastError_ = kErrorInvalidHandle;
            return 0xffffffffu;
        }
        static const int whence[3] = {SEEK_SET, SEEK_CUR, SEEK_END};
        const int32_t distance = int32_t(c.arg(1));
        if (c.arg(3) > 2 || std::fseek(f->file, distance, whence[c.arg(3)]) != 0)
            return 0xffffffffu;
        if (c.arg(2))
            c.write32(c.arg(2), 0);
        return uint32_t(std::ftell(f->file));
    });
    stdcall(k, "GetFileSize", 2, [this](Cpu& c) {
        OpenFile* f = file(c.arg(0));
        if (!f)
            return 0xffffffffu;
        const long at = std::ftell(f->file);
        std::fseek(f->file, 0, SEEK_END);
        const long size = std::ftell(f->file);
        std::fseek(f->file, at, SEEK_SET);
        if (c.arg(1))
            c.write32(c.arg(1), 0);
        return uint32_t(size);
    });
    // GetFileTime(handle, created*, accessed*, written*): all the file's
    // last write.
    stdcall(k, "GetFileTime", 4, [this](Cpu& c) {
        OpenFile* f = file(c.arg(0));
        if (!f)
            return 0u;
        const auto host = mapPath ? mapPath(f->windowsPath, false) : std::nullopt;
        const uint64_t t = host ? fileTimeOf(*host) : kUnixEpoch;
        for (int i = 1; i <= 3; ++i)
            if (c.arg(i)) {
                c.write32(c.arg(i), uint32_t(t));
                c.write32(c.arg(i) + 4, uint32_t(t >> 32));
            }
        return 1u;
    });
    stdcall(k, "CompareFileTime", 2, [](Cpu& c) {
        const uint64_t a = c.read32(c.arg(0)) | uint64_t(c.read32(c.arg(0) + 4)) << 32;
        const uint64_t b = c.read32(c.arg(1)) | uint64_t(c.read32(c.arg(1) + 4)) << 32;
        return a < b ? 0xffffffffu : a > b ? 1u : 0u;
    });
    stdcall(k, "SetEndOfFile", 1, [](Cpu&) { return 1u; });
    stdcall(k, "FlushFileBuffers", 1, [this](Cpu& c) {
        if (OpenFile* f = file(c.arg(0)))
            std::fflush(f->file);
        return 1u;
    });
    stdcall(k, "CloseHandle", 1, [this](Cpu& c) {
        closeFile(c.arg(0));
        mappings_.erase(c.arg(0));
        return 1u;
    });
    stdcall(k, "DeleteFileA", 1, [this](Cpu& c) {
        const auto host = mapPath ? mapPath(fullPath(c.string(c.arg(0))), true) : std::nullopt;
        std::error_code ec;
        if (!host || !std::filesystem::remove(*host, ec)) {
            lastError_ = kErrorFileNotFound;
            return 0u;
        }
        return 1u;
    });
    stdcall(k, "GetFileAttributesA", 1, [this](Cpu& c) {
        const auto host = mapPath ? mapPath(fullPath(c.string(c.arg(0))), false) : std::nullopt;
        std::error_code ec;
        if (!host || !std::filesystem::exists(*host, ec)) {
            lastError_ = kErrorFileNotFound;
            return 0xffffffffu;
        }
        return std::filesystem::is_directory(*host, ec) ? 0x10u : 0x80u; // DIRECTORY, NORMAL
    });
    stdcall(k, "GetCurrentDirectoryA", 2, [this](Cpu& c) {
        std::string dir = currentFolder_;
        if (dir.size() > 3)
            dir.pop_back(); // no trailing backslash, but for a drive's root
        if (c.arg(0) <= dir.size())
            return uint32_t(dir.size() + 1);
        c.writeString(c.arg(1), dir, c.arg(0));
        return uint32_t(dir.size());
    });
    stdcall(k, "SetCurrentDirectoryA", 1, [this](Cpu& c) {
        setCurrentFolder(c.string(c.arg(0)));
        return 1u;
    });

    // The 16-bit era's: OpenFile(name, OFSTRUCT*, style), _lread, _lclose.
    stdcall(k, "OpenFile", 3, [this](Cpu& c) {
        const std::string name = c.string(c.arg(0));
        const uint32_t style = c.arg(2);
        const uint32_t h = openFile(name, (style & 3) ? "r+b" : "rb");
        if (c.valid(c.arg(1), 136)) {
            c.write8(c.arg(1), 136);
            c.writeString(c.arg(1) + 8, fullPath(name), 128);
        }
        if (!h)
            return kInvalidHandle;
        if (style & 0x4000) { // OF_EXIST: just whether it's there
            closeFile(h);
            return 1u;
        }
        return h;
    });
    stdcall(k, "_lread", 3, [this](Cpu& c) {
        OpenFile* f = file(c.arg(0));
        if (!f || !c.valid(c.arg(1), c.arg(2)))
            return 0xffffffffu;
        return uint32_t(std::fread(c.memory().data() + c.arg(1), 1, c.arg(2), f->file));
    });
    stdcall(k, "_lclose", 1, [this](Cpu& c) { return closeFile(c.arg(0)) ? 0u : 0xffffffffu; });

    // Searches: FindFirstFileA(pattern, WIN32_FIND_DATAA*), FindNextFileA,
    // FindClose. WIN32_FIND_DATAA: attributes, three FILETIMEs, size (high,
    // low), two reserved words, the name (260), the short name (14).
    auto fill = [](Cpu& c, uint32_t data, const std::filesystem::directory_entry& e) {
        if (!c.valid(data, 320))
            return;
        std::memset(c.memory().data() + data, 0, 320);
        std::error_code ec;
        const bool dir = e.is_directory(ec);
        c.write32(data, dir ? 0x10 : 0x80);
        const uint64_t t = fileTimeOf(e.path());
        for (uint32_t i = 0; i < 3; ++i) {
            c.write32(data + 4 + 8 * i, uint32_t(t));
            c.write32(data + 8 + 8 * i, uint32_t(t >> 32));
        }
        c.write32(data + 32, dir ? 0 : uint32_t(e.file_size(ec)));
        c.writeString(data + 44, e.path().filename().string(), 260);
    };
    stdcall(k, "FindFirstFileA", 2, [this, fill](Cpu& c) {
        const std::string full = fullPath(c.string(c.arg(0)));
        const size_t slash = full.rfind('\\');
        const std::string folder = full.substr(0, slash + 1), pattern = full.substr(slash + 1);
        const auto host = mapPath ? mapPath(folder, false) : std::nullopt;
        Search s;
        std::error_code ec;
        if (host)
            for (std::filesystem::directory_iterator it(*host, ec), end; !ec && it != end; it.increment(ec))
                if (wildcard(pattern, it->path().filename().string()))
                    s.entries.push_back(*it);
        std::sort(s.entries.begin(), s.entries.end());
        if (s.entries.empty()) {
            lastError_ = host ? kErrorFileNotFound : kErrorPathNotFound;
            return kInvalidHandle;
        }
        fill(c, c.arg(1), s.entries[0]);
        s.next = 1;
        const uint32_t handle = nextFile_;
        nextFile_ += 4;
        searches_[handle] = std::move(s);
        return handle;
    });
    stdcall(k, "FindNextFileA", 2, [this, fill](Cpu& c) {
        auto it = searches_.find(c.arg(0));
        if (it == searches_.end() || it->second.next >= it->second.entries.size()) {
            lastError_ = kErrorNoMoreFiles;
            return 0u;
        }
        fill(c, c.arg(1), it->second.entries[it->second.next++]);
        return 1u;
    });
    stdcall(k, "FindClose", 1, [this](Cpu& c) { return searches_.erase(c.arg(0)) ? 1u : 0u; });

    // Mappings: CreateFileMappingA(file, security, protect, sizeHigh,
    // sizeLow, name) and MapViewOfFile(mapping, access, offsetHigh,
    // offsetLow, bytes): a view is a copy of the file's bytes in the
    // process's memory, which is all a program reading its data needs.
    stdcall(k, "CreateFileMappingA", 6, [this](Cpu& c) {
        Mapping m;
        m.file = c.arg(0) == kInvalidHandle ? 0 : c.arg(0);
        m.size = c.arg(4);
        if (m.file) {
            OpenFile* f = file(m.file);
            if (!f) {
                lastError_ = kErrorInvalidHandle;
                return 0u;
            }
            if (!m.size) {
                const long at = std::ftell(f->file);
                std::fseek(f->file, 0, SEEK_END);
                m.size = uint32_t(std::ftell(f->file));
                std::fseek(f->file, at, SEEK_SET);
            }
        }
        const uint32_t handle = nextFile_;
        nextFile_ += 4;
        mappings_[handle] = m;
        return handle;
    });
    stdcall(k, "MapViewOfFile", 5, [this](Cpu& c) {
        auto it = mappings_.find(c.arg(0));
        if (it == mappings_.end()) {
            lastError_ = kErrorInvalidHandle;
            return 0u;
        }
        const Mapping& m = it->second;
        const uint32_t offset = c.arg(3);
        if (offset > m.size)
            return 0u;
        const uint32_t size = c.arg(4) ? std::min(c.arg(4), m.size - offset) : m.size - offset;
        const uint32_t view = arena_->alloc(std::max(size, 1u), Cpu::kPageSize);
        if (!view) {
            lastError_ = 8; // ERROR_NOT_ENOUGH_MEMORY
            return 0u;
        }
        if (OpenFile* f = file(m.file)) {
            const long at = std::ftell(f->file);
            std::fseek(f->file, long(offset), SEEK_SET);
            const size_t n = std::fread(c.memory().data() + view, 1, size, f->file);
            (void)n;
            std::fseek(f->file, at, SEEK_SET);
        }
        return view;
    });
    stdcall(k, "UnmapViewOfFile", 1, [this](Cpu& c) {
        arena_->free(c.arg(0));
        return 1u;
    });

    // Drives: C: (a hard disk, where the game is installed) and the CD-ROM
    // drive, whose volume label is the game's CD's if it was given.
    stdcall(k, "GetLogicalDriveStringsA", 2, [this](Cpu& c) {
        std::string list = std::string("C:\\") + '\0' + cdDrive + ":\\" + '\0';
        if (c.arg(0) < list.size() + 1)
            return uint32_t(list.size() + 1);
        for (size_t i = 0; i < list.size(); ++i)
            c.write8(c.arg(1) + uint32_t(i), uint8_t(list[i]));
        c.write8(c.arg(1) + uint32_t(list.size()), 0);
        return uint32_t(list.size());
    });
    stdcall(k, "GetLogicalDrives", 0, [this](Cpu&) {
        return (1u << 2) | (1u << (std::toupper(static_cast<unsigned char>(cdDrive[0])) - 'A'));
    });
    stdcall(k, "GetDriveTypeA", 1, [this](Cpu& c) {
        const std::string root = upper(c.arg(0) ? c.string(c.arg(0)) : currentFolder_.substr(0, 3));
        if (root.rfind("C:", 0) == 0)
            return 3u; // DRIVE_FIXED
        if (root.rfind(upper(cdDrive) + ":", 0) == 0)
            return 5u; // DRIVE_CDROM
        return 1u;     // DRIVE_NO_ROOT_DIR
    });
    // GetVolumeInformationA(root, name, nameSize, serial*, maxLength*,
    // flags*, fileSystem, fileSystemSize)
    stdcall(k, "GetVolumeInformationA", 8, [this](Cpu& c) {
        const std::string root = upper(c.string(c.arg(0)));
        const bool cd = root.rfind(upper(cdDrive) + ":", 0) == 0;
        if (cd && cdLabel.empty()) {
            lastError_ = 21; // ERROR_NOT_READY: no disc
            return 0u;
        }
        if (!cd && root.rfind("C:", 0) != 0) {
            lastError_ = kErrorPathNotFound;
            return 0u;
        }
        if (c.arg(1))
            c.writeString(c.arg(1), cd ? cdLabel : "", c.arg(2));
        if (c.arg(3))
            c.write32(c.arg(3), cd ? 0x79cd426au : 0x1e1a7a00u); // the retail CD's serial; any for C:
        if (c.arg(4))
            c.write32(c.arg(4), cd ? 110 : 255);
        if (c.arg(5))
            c.write32(c.arg(5), cd ? 0x80004u : 0x700ffu);
        if (c.arg(6))
            c.writeString(c.arg(6), cd ? "CDFS" : "NTFS", c.arg(7));
        return 1u;
    });
}

} // namespace reluva::x86
