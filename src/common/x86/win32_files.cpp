// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// KERNEL32's files, folders and drives, on folders of the host: each drive
// letter the program sees is a folder, its names matched ignoring case,
// with what the program writes kept apart if asked (Win32Process::Drive).
// From the behaviour Microsoft documents for these functions.

#include "common/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace reluva::x86 {

namespace {

namespace stdfs = std::filesystem;

constexpr uint32_t kInvalidHandle = 0xffffffffu;
constexpr uint32_t kGenericWrite = 0x40000000;
// Errors.
constexpr uint32_t kFileNotFound = 2, kPathNotFound = 3, kAccessDenied = 5, kInvalidHandleError = 6,
                   kNoMoreFiles = 18, kFileExists = 80, kAlreadyExists = 183;
// Attributes.
constexpr uint32_t kDirectory = 0x10, kArchive = 0x20, kReadOnly = 0x01;

std::string lower(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct FileObject : KernelObject {
    std::FILE* file = nullptr;
    std::string path; // the Windows path
    ~FileObject() override {
        if (file)
            std::fclose(file);
    }
};

struct FindObject : KernelObject {
    struct Entry {
        std::string name;
        stdfs::path host;
    };
    std::vector<Entry> entries;
    size_t next = 0;
};

struct MappingObject : KernelObject {
    std::vector<uint8_t> data;
};

// "*.viv" and "tr0?.frd" against a name, ignoring case.
bool matches(const char* pattern, const char* name) {
    if (!*pattern)
        return !*name;
    if (*pattern == '*') {
        // "*.*" matches names without a dot too, as on Windows.
        if (std::strcmp(pattern, "*.*") == 0)
            return true;
        for (const char* n = name;; ++n) {
            if (matches(pattern + 1, n))
                return true;
            if (!*n)
                return false;
        }
    }
    if (!*name)
        return false;
    if (*pattern != '?' && std::tolower(static_cast<unsigned char>(*pattern)) !=
                               std::tolower(static_cast<unsigned char>(*name)))
        return false;
    return matches(pattern + 1, name + 1);
}

// A path's parts after "X:\".
std::vector<std::string> parts(const std::string& full) {
    std::vector<std::string> out;
    size_t at = 3;
    while (at < full.size()) {
        const size_t slash = full.find('\\', at);
        const size_t end = slash == std::string::npos ? full.size() : slash;
        if (end > at)
            out.push_back(full.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

// The host path for `names` under `base`, each found ignoring case where
// there is one; the rest as given.
stdfs::path resolve(const stdfs::path& base, const std::vector<std::string>& names) {
    stdfs::path p = base;
    bool exists = true;
    for (const std::string& n : names) {
        if (exists) {
            std::error_code ec;
            stdfs::path found;
            if (stdfs::exists(p / n, ec)) {
                found = p / n;
            } else {
                const std::string want = lower(n);
                for (const auto& e : stdfs::directory_iterator(p, ec))
                    if (lower(e.path().filename().string()) == want) {
                        found = e.path();
                        break;
                    }
            }
            if (!found.empty()) {
                p = found;
                continue;
            }
            exists = false;
        }
        p /= n;
    }
    return p;
}

// A FILETIME (100 ns units since 1601) for a host file's time.
uint64_t fileTime(stdfs::file_time_type t) {
#if defined(_MSVC_STL_VERSION)
    // Microsoft's file_clock counts FILETIME's units from its epoch already
    // (and has no to_sys, which the standard leaves optional).
    return uint64_t(std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(
                        t.time_since_epoch())
                        .count());
#else
    const auto sys = std::chrono::file_clock::to_sys(t);
    const int64_t unix100ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sys.time_since_epoch()).count() / 100;
    return uint64_t(unix100ns + 116444736000000000ll);
#endif
}

} // namespace

void Win32Process::addDrive(Drive drive) {
    drive.letter = char(std::toupper(static_cast<unsigned char>(drive.letter)));
    std::erase_if(drives_, [&](const Drive& d) { return d.letter == drive.letter; });
    drives_.push_back(std::move(drive));
}

void Win32Process::setCurrentDirectory(const std::string& windowsPath) { currentDirectory_ = fullPath(windowsPath); }

std::string Win32Process::fullPath(const std::string& windowsPath) const {
    std::string p = windowsPath;
    std::replace(p.begin(), p.end(), '/', '\\');
    std::string full;
    if (p.size() >= 2 && p[1] == ':') {
        const char letter = char(std::toupper(static_cast<unsigned char>(p[0])));
        if (p.size() >= 3 && p[2] == '\\')
            full = p;
        else if (letter == currentDirectory_[0])
            full = currentDirectory_ + "\\" + p.substr(2);
        else
            full = std::string(1, letter) + ":\\" + p.substr(2);
        full[0] = letter;
    } else if (!p.empty() && p[0] == '\\') {
        full = currentDirectory_.substr(0, 2) + p;
    } else {
        full = currentDirectory_ + "\\" + p;
    }
    // ".", ".." and doubled separators.
    std::vector<std::string> kept;
    for (const std::string& part : parts(full)) {
        if (part == ".")
            continue;
        if (part == "..") {
            if (!kept.empty())
                kept.pop_back();
            continue;
        }
        kept.push_back(part);
    }
    std::string out = full.substr(0, 2) + "\\";
    for (size_t i = 0; i < kept.size(); ++i)
        out += (i ? "\\" : "") + kept[i];
    return out;
}

stdfs::path Win32Process::hostPath(const std::string& windowsPath, bool forWriting) const {
    const std::string full = fullPath(windowsPath);
    const Drive* drive = nullptr;
    for (const Drive& d : drives_)
        if (d.letter == full[0])
            drive = &d;
    if (!drive)
        return {};
    std::vector<std::string> names = parts(full);
    if (!drive->writable.empty()) {
        // The names as the drive's own files spell them, where it has them,
        // so that a copy written is the file's twin.
        std::vector<std::string> spelled;
        for (const auto& part : resolve(drive->root, names).lexically_relative(drive->root))
            spelled.push_back(part.string());
        if (spelled.size() == names.size())
            names = spelled;
        const stdfs::path copy = resolve(drive->writable, names);
        std::error_code ec;
        if (forWriting || stdfs::exists(copy, ec))
            return copy;
    }
    return resolve(drive->root, names);
}

void Win32Process::addFileStandIns() {
    const char* k = "KERNEL32.dll";
    auto driveOf = [this](const std::string& full) -> const Drive* {
        for (const Drive& d : drives_)
            if (d.letter == full[0])
                return &d;
        return nullptr;
    };
    // Whether the program may write at a path: not on a CD, not where there's no drive.
    auto writable = [driveOf](const std::string& full) {
        const Drive* d = driveOf(full);
        return d && d->type != 5;
    };
    // The writable copy of a file, made from the drive's own if there's one.
    auto writableCopy = [this](const std::string& path, bool keepContents) {
        const stdfs::path to = hostPath(path, true);
        const stdfs::path from = hostPath(path, false);
        std::error_code ec;
        stdfs::create_directories(to.parent_path(), ec);
        if (keepContents && from != to && stdfs::exists(from, ec) && !stdfs::exists(to, ec))
            stdfs::copy_file(from, to, ec);
        return to;
    };

    // CreateFileA(name, access, share, security, disposition, flags, template)
    auto open = [this, writable, writableCopy](const std::string& name, bool write, uint32_t disposition) -> uint32_t {
        // COM1-COM9 (or \\.\COMn): the host's serial ports.
        {
            std::string device = name.rfind("\\\\.\\", 0) == 0 ? name.substr(4) : name;
            std::transform(device.begin(), device.end(), device.begin(),
                           [](unsigned char ch) { return char(std::tolower(ch)); });
            if (device.size() == 4 && device.compare(0, 3, "com") == 0 && device[3] >= '1' && device[3] <= '9') {
                auto port = openSerialPort ? openSerialPort(device[3] - '0') : nullptr;
                if (fileOpened)
                    fileOpened(name, port != nullptr);
                if (!port) {
                    lastError_ = kFileNotFound;
                    return kInvalidHandle;
                }
                lastError_ = 0;
                return addHandle(port);
            }
        }
        const std::string full = fullPath(name);
        // NUL, in any folder and with any extension, is the null device:
        // an empty file nothing written to lasts in.
        {
            std::string base = full.substr(full.find_last_of('\\') + 1);
            base = base.substr(0, base.find('.'));
            std::transform(base.begin(), base.end(), base.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
            if (base == "nul") {
                auto f = std::make_shared<FileObject>();
                f->path = full;
                f->file = std::tmpfile();
                if (!f->file) {
                    lastError_ = kAccessDenied;
                    return kInvalidHandle;
                }
                lastError_ = 0;
                return addHandle(f);
            }
        }
        const stdfs::path existing = hostPath(name, false);
        std::error_code ec;
        const bool exists = !existing.empty() && stdfs::is_regular_file(existing, ec);
        if (existing.empty() || stdfs::is_directory(existing, ec)) {
            lastError_ = existing.empty() ? kPathNotFound : kAccessDenied;
            if (fileOpened)
                fileOpened(full, false);
            return kInvalidHandle;
        }
        if ((disposition == 3 || disposition == 5) && !exists) { // OPEN_EXISTING, TRUNCATE_EXISTING
            lastError_ = kFileNotFound;
            if (fileOpened)
                fileOpened(full, false);
            return kInvalidHandle;
        }
        if (disposition == 1 && exists) { // CREATE_NEW
            lastError_ = kFileExists;
            return kInvalidHandle;
        }
        const bool truncate = disposition == 1 || disposition == 2 || disposition == 5;
        auto f = std::make_shared<FileObject>();
        f->path = full;
        if (write || truncate || !exists) {
            if (!writable(full)) {
                lastError_ = kAccessDenied;
                return kInvalidHandle;
            }
            const stdfs::path host = writableCopy(name, !truncate);
            f->file = std::fopen(host.string().c_str(), truncate || !stdfs::exists(host, ec) ? "w+b" : "r+b");
        } else {
            f->file = std::fopen(existing.string().c_str(), "rb");
        }
        if (!f->file) {
            lastError_ = kAccessDenied;
            return kInvalidHandle;
        }
        if (fileOpened)
            fileOpened(full, true);
        lastError_ = (disposition == 2 || disposition == 4) && exists ? kAlreadyExists : 0;
        return addHandle(f);
    };
    stdcall(k, "CreateFileA", 7, [open](Cpu& c) {
        return open(c.string(c.arg(0)), (c.arg(1) & kGenericWrite) != 0, c.arg(4));
    });
    // _lopen(name, mode): OF_READ 0, OF_WRITE 1, OF_READWRITE 2; an HFILE, -1 if it fails.
    stdcall(k, "_lopen", 2, [open](Cpu& c) { return open(c.string(c.arg(0)), (c.arg(1) & 3) != 0, 3); });
    stdcall(k, "_lclose", 1, [this](Cpu& c) { return closeHandle(c.arg(0)) ? 0u : kInvalidHandle; });

    stdcall(k, "ReadFile", 5, [this](Cpu& c) {
        const uint32_t buffer = c.arg(1), size = c.arg(2), done = c.arg(3);
        if (done)
            c.write32(done, 0);
        if (!c.valid(buffer, size) && size) {
            lastError_ = 998; // ERROR_NOACCESS
            return 0u;
        }
        if (auto device = object<DeviceObject>(c.arg(0))) {
            uint32_t n = 0, error = 0;
            const uint32_t overlapped = c.arg(4);
            const bool ok = device->read(c, buffer, size, overlapped != 0, n, error);
            if (done)
                c.write32(done, n);
            if (overlapped && ok && n == 0 && size) {
                // Nothing yet: pending, its event (OVERLAPPED +16) signalled
                // when there is, the bytes taken by GetOverlappedResult.
                pendingReads_[overlapped] = {c.arg(0), buffer, size};
                c.write32(overlapped, 0x103); // STATUS_PENDING
                if (const uint32_t event = c.read32(overlapped + 16))
                    signalEventWhen(event, [device] { return device->readable(); });
                lastError_ = 997; // ERROR_IO_PENDING
                return 0u;
            }
            if (overlapped && ok) { // Internal (status 0) and InternalHigh (bytes)
                c.write32(overlapped, 0);
                c.write32(overlapped + 4, n);
                if (const uint32_t event = c.read32(overlapped + 16))
                    setEvent(event);
            }
            lastError_ = error;
            return ok ? 1u : 0u;
        }
        auto f = object<FileObject>(c.arg(0));
        if (!f) {
            lastError_ = kInvalidHandleError;
            return 0u;
        }
        const size_t n = size ? std::fread(c.memory().data() + buffer, 1, size, f->file) : 0;
        if (done)
            c.write32(done, uint32_t(n));
        return 1u;
    });
    // GetOverlappedResult(handle, OVERLAPPED*, LPDWORD done, wait): a
    // device read left pending takes its bytes now (waiting for some if
    // asked); anything else finished when it was started.
    stdcall(k, "GetOverlappedResult", 4, [this](Cpu& c) {
        const uint32_t overlapped = c.arg(1);
        auto it = pendingReads_.find(overlapped);
        if (it == pendingReads_.end()) {
            if (c.arg(2))
                c.write32(c.arg(2), c.read32(overlapped + 4));
            return 1u;
        }
        const PendingRead p = it->second;
        auto device = object<DeviceObject>(p.handle);
        if (!device) {
            pendingReads_.erase(it);
            lastError_ = 995; // ERROR_OPERATION_ABORTED
            return 0u;
        }
        if (!device->readable()) {
            if (!c.arg(3) || !wait([device] { return device->readable(); }, 0xffffffffu)) {
                lastError_ = 996; // ERROR_IO_INCOMPLETE
                return 0u;
            }
        }
        pendingReads_.erase(it);
        uint32_t n = 0, error = 0;
        device->read(c, p.buffer, p.size, true, n, error);
        c.write32(overlapped, 0);
        c.write32(overlapped + 4, n);
        if (c.arg(2))
            c.write32(c.arg(2), n);
        return 1u;
    });
    stdcall(k, "WriteFile", 5, [this](Cpu& c) {
        const uint32_t handle = c.arg(0), buffer = c.arg(1), size = c.arg(2), done = c.arg(3);
        if (!c.valid(buffer, size) && size)
            return 0u;
        if (done)
            c.write32(done, 0);
        const char* data = reinterpret_cast<const char*>(c.memory().data() + buffer);
        if (handle == kStdHandles + 1 || handle == kStdHandles + 2) {
            if (output)
                output(std::string(data, size));
        } else if (auto device = object<DeviceObject>(handle)) {
            uint32_t n = 0, error = 0;
            const bool ok = device->write(c, buffer, size, n, error);
            if (done)
                c.write32(done, n);
            lastError_ = error;
            return ok ? 1u : 0u;
        } else {
            auto f = object<FileObject>(handle);
            if (!f) {
                lastError_ = kInvalidHandleError;
                return 0u;
            }
            if (size && std::fwrite(data, 1, size, f->file) != size) {
                lastError_ = kAccessDenied;
                return 0u;
            }
        }
        if (done)
            c.write32(done, size);
        return 1u;
    });
    // SetFilePointer(handle, distance, distanceHigh*, method): the new position.
    stdcall(k, "SetFilePointer", 4, [this](Cpu& c) {
        auto f = object<FileObject>(c.arg(0));
        if (!f) {
            lastError_ = kInvalidHandleError;
            return kInvalidHandle;
        }
        int64_t distance = int32_t(c.arg(1));
        if (c.arg(2))
            distance = int64_t(uint64_t(c.read32(c.arg(2))) << 32 | c.arg(1));
        const int whence = c.arg(3) == 1 ? SEEK_CUR : c.arg(3) == 2 ? SEEK_END : SEEK_SET;
        if (std::fseek(f->file, long(distance), whence) != 0) {
            lastError_ = 131; // ERROR_NEGATIVE_SEEK
            return kInvalidHandle;
        }
        const long at = std::ftell(f->file);
        if (c.arg(2))
            c.write32(c.arg(2), 0);
        lastError_ = 0;
        return uint32_t(at);
    });
    stdcall(k, "GetFileSize", 2, [this](Cpu& c) {
        auto f = object<FileObject>(c.arg(0));
        if (!f) {
            lastError_ = kInvalidHandleError;
            return kInvalidHandle;
        }
        const long at = std::ftell(f->file);
        std::fseek(f->file, 0, SEEK_END);
        const long size = std::ftell(f->file);
        std::fseek(f->file, at, SEEK_SET);
        if (c.arg(1))
            c.write32(c.arg(1), 0);
        return uint32_t(size);
    });
    stdcall(k, "SetEndOfFile", 1, [this](Cpu& c) {
        auto f = object<FileObject>(c.arg(0));
        if (!f)
            return 0u;
        std::fflush(f->file);
        const long at = std::ftell(f->file);
        std::error_code ec;
        stdfs::resize_file(hostPath(f->path, true), uintmax_t(at), ec);
        return ec ? 0u : 1u;
    });
    stdcall(k, "FlushFileBuffers", 1, [this](Cpu& c) {
        if (auto f = object<FileObject>(c.arg(0)))
            std::fflush(f->file);
        return 1u;
    });
    stdcall(k, "GetFileType", 1, [this](Cpu& c) {
        if ((c.arg(0) >= kStdHandles && c.arg(0) <= kStdHandles + 2) || object<DeviceObject>(c.arg(0)))
            return 2u; // FILE_TYPE_CHAR
        return object<FileObject>(c.arg(0)) ? 1u : 0u; // FILE_TYPE_DISK
    });
    // GetFileTime(handle, created*, accessed*, written*)
    stdcall(k, "GetFileTime", 4, [this](Cpu& c) {
        auto f = object<FileObject>(c.arg(0));
        if (!f)
            return 0u;
        std::error_code ec;
        const uint64_t t = fileTime(stdfs::last_write_time(hostPath(f->path, false), ec));
        for (int i = 1; i <= 3; ++i)
            if (c.arg(i)) {
                c.write32(c.arg(i), uint32_t(t));
                c.write32(c.arg(i) + 4, uint32_t(t >> 32));
            }
        return 1u;
    });
    stdcall(k, "CompareFileTime", 2, [](Cpu& c) {
        const uint64_t a = uint64_t(c.read32(c.arg(0) + 4)) << 32 | c.read32(c.arg(0));
        const uint64_t b = uint64_t(c.read32(c.arg(1) + 4)) << 32 | c.read32(c.arg(1));
        return a < b ? 0xffffffffu : a > b ? 1u : 0u;
    });
    stdcall(k, "CloseHandle", 1, [this](Cpu& c) {
        const uint32_t h = c.arg(0);
        if (h >= kStdHandles && h <= kStdHandles + 2)
            return 1u;
        if (!closeHandle(h)) {
            lastError_ = kInvalidHandleError;
            return 0u;
        }
        return 1u;
    });

    // Searches: FindFirstFileA(pattern, WIN32_FIND_DATAA*).
    auto fillFind = [](Cpu& c, uint32_t p, const FindObject::Entry& e) {
        if (!c.valid(p, 320))
            return;
        std::memset(c.memory().data() + p, 0, 320);
        std::error_code ec;
        const bool dir = stdfs::is_directory(e.host, ec);
        c.write32(p, dir ? kDirectory : kArchive);
        const uint64_t t = fileTime(stdfs::last_write_time(e.host, ec));
        for (uint32_t at : {4u, 12u, 20u}) {
            c.write32(p + at, uint32_t(t));
            c.write32(p + at + 4, uint32_t(t >> 32));
        }
        c.write32(p + 32, dir ? 0 : uint32_t(stdfs::file_size(e.host, ec)));
        c.writeString(p + 44, e.name, 260);
        if (e.name.size() <= 12)
            c.writeString(p + 304, e.name, 14);
    };
    stdcall(k, "FindFirstFileA", 2, [this, fillFind](Cpu& c) {
        const std::string full = fullPath(c.string(c.arg(0)));
        const size_t slash = full.rfind('\\');
        const std::string folder = full.substr(0, slash), pattern = full.substr(slash + 1);
        auto find = std::make_shared<FindObject>();
        // The writable copies first, then the drive's, each name once.
        std::vector<stdfs::path> dirs;
        const stdfs::path copy = hostPath(folder + "\\", true), own = hostPath(folder + "\\", false);
        for (const stdfs::path& d : {copy, own})
            if (std::find(dirs.begin(), dirs.end(), d) == dirs.end() && !d.empty())
                dirs.push_back(d);
        // A folder without a writable copy: hostPath(.., true) gave the drive's own.
        std::vector<std::string> seen;
        for (const stdfs::path& d : dirs) {
            std::error_code ec;
            for (const auto& e : stdfs::directory_iterator(d, ec)) {
                const std::string name = e.path().filename().string();
                if (!matches(pattern.c_str(), name.c_str()) ||
                    std::find(seen.begin(), seen.end(), lower(name)) != seen.end())
                    continue;
                seen.push_back(lower(name));
                find->entries.push_back({name, e.path()});
            }
        }
        std::sort(find->entries.begin(), find->entries.end(),
                  [](const auto& a, const auto& b) { return lower(a.name) < lower(b.name); });
        if (find->entries.empty()) {
            lastError_ = kFileNotFound;
            return kInvalidHandle;
        }
        fillFind(c, c.arg(1), find->entries[0]);
        find->next = 1;
        return addHandle(find);
    });
    stdcall(k, "FindNextFileA", 2, [this, fillFind](Cpu& c) {
        auto find = object<FindObject>(c.arg(0));
        if (!find || find->next >= find->entries.size()) {
            lastError_ = kNoMoreFiles;
            return 0u;
        }
        fillFind(c, c.arg(1), find->entries[find->next++]);
        return 1u;
    });
    stdcall(k, "FindClose", 1, [this](Cpu& c) { return closeHandle(c.arg(0)) ? 1u : 0u; });

    stdcall(k, "GetFileAttributesA", 1, [this](Cpu& c) {
        const stdfs::path p = hostPath(c.string(c.arg(0)), false);
        std::error_code ec;
        if (p.empty() || !stdfs::exists(p, ec)) {
            lastError_ = kFileNotFound;
            return kInvalidHandle;
        }
        if (stdfs::is_directory(p, ec))
            return kDirectory;
        const Drive* d = nullptr;
        for (const Drive& drive : drives_)
            if (drive.letter == fullPath(c.string(c.arg(0)))[0])
                d = &drive;
        return d && d->type == 5 ? kArchive | kReadOnly : kArchive;
    });
    stdcall(k, "SetFileAttributesA", 2, [](Cpu&) { return 1u; });
    stdcall(k, "CreateDirectoryA", 2, [this, writable](Cpu& c) {
        const std::string full = fullPath(c.string(c.arg(0)));
        std::error_code ec;
        if (!writable(full)) {
            lastError_ = kAccessDenied;
            return 0u;
        }
        if (stdfs::is_directory(hostPath(full, false), ec)) {
            lastError_ = kAlreadyExists;
            return 0u;
        }
        return stdfs::create_directories(hostPath(full, true), ec) ? 1u : 0u;
    });
    // Only the writable copies are removed: the drive's own files stay.
    auto remove = [this, writable](const std::string& name) {
        const std::string full = fullPath(name);
        std::error_code ec;
        if (!writable(full))
            return false;
        const stdfs::path p = hostPath(full, true);
        if (!stdfs::exists(p, ec)) {
            lastError_ = kFileNotFound;
            return false;
        }
        return stdfs::remove(p, ec);
    };
    stdcall(k, "DeleteFileA", 1, [remove](Cpu& c) { return remove(c.string(c.arg(0))) ? 1u : 0u; });
    stdcall(k, "RemoveDirectoryA", 1, [remove](Cpu& c) { return remove(c.string(c.arg(0))) ? 1u : 0u; });
    stdcall(k, "MoveFileA", 2, [this, writable, writableCopy](Cpu& c) {
        const std::string from = c.string(c.arg(0)), to = c.string(c.arg(1));
        if (!writable(fullPath(from)) || !writable(fullPath(to))) {
            lastError_ = kAccessDenied;
            return 0u;
        }
        const stdfs::path source = writableCopy(from, true), target = hostPath(to, true);
        std::error_code ec;
        stdfs::create_directories(target.parent_path(), ec);
        stdfs::rename(source, target, ec);
        return ec ? 0u : 1u;
    });

    // Folders and drives.
    stdcall(k, "GetCurrentDirectoryA", 2, [this](Cpu& c) {
        const uint32_t size = c.arg(0);
        if (size <= currentDirectory_.size())
            return uint32_t(currentDirectory_.size() + 1);
        c.writeString(c.arg(1), currentDirectory_, size);
        return uint32_t(currentDirectory_.size());
    });
    stdcall(k, "SetCurrentDirectoryA", 1, [this](Cpu& c) {
        const std::string full = fullPath(c.string(c.arg(0)));
        std::error_code ec;
        if (!stdfs::is_directory(hostPath(full, false), ec)) {
            lastError_ = kPathNotFound;
            return 0u;
        }
        currentDirectory_ = full;
        return 1u;
    });
    // GetFullPathNameA(name, size, buffer, filePart*)
    stdcall(k, "GetFullPathNameA", 4, [this](Cpu& c) {
        const std::string full = fullPath(c.string(c.arg(0)));
        const uint32_t size = c.arg(1);
        if (size <= full.size())
            return uint32_t(full.size() + 1);
        c.writeString(c.arg(2), full, size);
        if (c.arg(3))
            c.write32(c.arg(3), c.arg(2) + uint32_t(full.rfind('\\') + 1));
        return uint32_t(full.size());
    });
    stdcall(k, "GetWindowsDirectoryA", 2, [](Cpu& c) {
        c.writeString(c.arg(0), "C:\\WINDOWS", c.arg(1));
        return 10u;
    });
    stdcall(k, "GetSystemDirectoryA", 2, [](Cpu& c) {
        c.writeString(c.arg(0), "C:\\WINDOWS\\SYSTEM", c.arg(1));
        return 17u;
    });
    stdcall(k, "GetLogicalDrives", 0, [this](Cpu&) {
        uint32_t mask = 0;
        for (const Drive& d : drives_)
            mask |= 1u << (d.letter - 'A');
        return mask;
    });
    stdcall(k, "GetLogicalDriveStringsA", 2, [this](Cpu& c) {
        std::vector<char> letters;
        for (const Drive& d : drives_)
            letters.push_back(d.letter);
        std::sort(letters.begin(), letters.end());
        const uint32_t need = uint32_t(letters.size() * 4);
        if (c.arg(0) <= need)
            return need + 1;
        uint32_t p = c.arg(1);
        for (char l : letters) {
            const char root[4] = {l, ':', '\\', 0};
            for (char ch : root)
                c.write8(p++, uint8_t(ch));
        }
        c.write8(p, 0);
        return need;
    });
    // The drive a root path ("D:\", or the current one for NULL) is.
    auto rootDrive = [this](Cpu& c, uint32_t root) -> const Drive* {
        const char letter = char(std::toupper(root ? c.read8(root) : uint8_t(currentDirectory_[0])));
        for (const Drive& d : drives_)
            if (d.letter == letter)
                return &d;
        return nullptr;
    };
    stdcall(k, "GetDriveTypeA", 1, [rootDrive](Cpu& c) {
        const Drive* d = rootDrive(c, c.arg(0));
        return d ? d->type : 1u; // DRIVE_NO_ROOT_DIR
    });
    // GetVolumeInformationA(root, name, nameSize, serial*, maxComponent*, flags*, fsName, fsNameSize)
    stdcall(k, "GetVolumeInformationA", 8, [this, rootDrive](Cpu& c) {
        const Drive* d = rootDrive(c, c.arg(0));
        if (!d) {
            lastError_ = kPathNotFound;
            return 0u;
        }
        if (c.arg(1))
            c.writeString(c.arg(1), d->label, c.arg(2));
        if (c.arg(3))
            c.write32(c.arg(3), d->serial);
        if (c.arg(4))
            c.write32(c.arg(4), 255);
        if (c.arg(5))
            c.write32(c.arg(5), d->type == 5 ? 0x4 : 0x6); // case preserving, Unicode on disk
        if (c.arg(6))
            c.writeString(c.arg(6), d->type == 5 ? "CDFS" : "FAT32", c.arg(7));
        return 1u;
    });
    // GetDiskFreeSpaceA(root, sectorsPerCluster*, bytesPerSector*, free*, total*): a
    // gigabyte free of two (what a program of the time could count).
    stdcall(k, "GetDiskFreeSpaceA", 5, [rootDrive](Cpu& c) {
        const Drive* d = rootDrive(c, c.arg(0));
        if (!d)
            return 0u;
        const uint32_t values[] = {8, 512, d->type == 5 ? 0u : 0x40000u, 0x80000};
        for (int i = 0; i < 4; ++i)
            if (c.arg(i + 1))
                c.write32(c.arg(i + 1), values[i]);
        return 1u;
    });

    // File mappings, read as a copy: CreateFileMappingA(file, security,
    // protect, sizeHigh, sizeLow, name), MapViewOfFile(mapping, access,
    // offsetHigh, offsetLow, bytes).
    stdcall(k, "CreateFileMappingA", 6, [this](Cpu& c) -> uint32_t {
        auto m = std::make_shared<MappingObject>();
        if (auto f = object<FileObject>(c.arg(0))) {
            const long at = std::ftell(f->file);
            std::fseek(f->file, 0, SEEK_END);
            m->data.resize(size_t(std::ftell(f->file)));
            std::fseek(f->file, 0, SEEK_SET);
            m->data.resize(std::fread(m->data.data(), 1, m->data.size(), f->file));
            std::fseek(f->file, at, SEEK_SET);
        } else {
            m->data.resize(c.arg(4)); // memory only (a handle of -1)
        }
        if (c.arg(4) > m->data.size())
            m->data.resize(c.arg(4));
        return addHandle(m);
    });
    stdcall(k, "MapViewOfFile", 5, [this](Cpu& c) -> uint32_t {
        auto m = object<MappingObject>(c.arg(0));
        if (!m)
            return 0u;
        const uint32_t offset = c.arg(3);
        if (offset > m->data.size())
            return 0u;
        const uint32_t bytes = c.arg(4) ? c.arg(4) : uint32_t(m->data.size() - offset);
        const uint32_t p = arena_->alloc(std::max(bytes, 1u), 0x10000);
        if (p)
            std::memcpy(c.memory().data() + p, m->data.data() + offset,
                        std::min<size_t>(bytes, m->data.size() - offset));
        return p;
    });
    stdcall(k, "UnmapViewOfFile", 1, [this](Cpu& c) {
        arena_->free(c.arg(0));
        return 1u;
    });
    stdcall(k, "SetErrorMode", 1, [](Cpu&) { return 0u; });
}

} // namespace reluva::x86
