// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

namespace reluva::x86 {

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "kernel32.dll" from "KERNEL32", "kernel32.DLL"...
std::string dllKey(const std::string& name) {
    std::string s = lower(name);
    if (s.find('.') == std::string::npos)
        s += ".dll";
    return s;
}

uint64_t steadyMs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

constexpr uint32_t kKernel32Handle = 0x7ff00000, kExeHandle = 0x00400000;
constexpr uint32_t kStdHandles = 0x100; // stdin, stdout, stderr: 0x100 to 0x102
constexpr uint32_t kInvalidHandle = 0xffffffffu;

} // namespace

// --- Arena ---------------------------------------------------------------------

Arena::Arena(Cpu& cpu, uint32_t start, uint32_t end) : cpu_(cpu) {
    if (end > start)
        free_[start] = end - start;
}

void Arena::release(uint32_t start, uint32_t size) {
    auto next = free_.lower_bound(start);
    if (next != free_.end() && start + size == next->first) {
        size += next->second;
        next = free_.erase(next);
    }
    if (next != free_.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == start) {
            prev->second += size;
            return;
        }
    }
    free_[start] = size;
}

uint32_t Arena::alloc(uint32_t size, uint32_t align) {
    if (!size)
        size = 1;
    size = (size + 15) & ~15u;
    if (align < 16)
        align = 16;
    for (auto it = free_.begin(); it != free_.end(); ++it) {
        const uint32_t start = it->first, length = it->second;
        const uint32_t aligned = (start + align - 1) & ~(align - 1);
        if (aligned < start || uint64_t(aligned - start) + size > length)
            continue;
        free_.erase(it);
        if (aligned > start)
            free_[start] = aligned - start;
        if (aligned - start + size < length)
            free_[aligned + size] = length - (aligned - start) - size;
        used_[aligned] = size;
        std::memset(cpu_.memory().data() + aligned, 0, size);
        return aligned;
    }
    return 0;
}

bool Arena::claim(uint32_t address, uint32_t size) {
    size = (size + 15) & ~15u;
    auto it = free_.upper_bound(address);
    if (it == free_.begin())
        return false;
    --it;
    const uint32_t start = it->first, length = it->second;
    if (uint64_t(address) + size > uint64_t(start) + length)
        return false;
    free_.erase(it);
    if (address > start)
        free_[start] = address - start;
    if (address + size < start + length)
        free_[address + size] = start + length - address - size;
    used_[address] = size;
    std::memset(cpu_.memory().data() + address, 0, size);
    return true;
}

void Arena::free(uint32_t address) {
    auto it = used_.find(address);
    if (it == used_.end())
        return;
    const uint32_t size = it->second;
    used_.erase(it);
    release(address, size);
}

uint32_t Arena::size(uint32_t address) const {
    auto it = used_.find(address);
    return it == used_.end() ? 0 : it->second;
}

uint32_t Arena::freeBytes() const {
    uint32_t n = 0;
    for (const auto& [start, size] : free_)
        n += size;
    return n;
}

// --- The process ---------------------------------------------------------------

uint32_t Win32Process::Module::exportAddress(const std::string& function) const {
    for (const PeImage::Export& e : image.exports)
        if (e.name == function && e.forwarder.empty())
            return base + e.rva;
    return 0;
}

Win32Process::Win32Process(Cpu& cpu) : cpu_(cpu) {
    arena_ = std::make_unique<Arena>(cpu, 0x10000, cpu.nativeBase() & ~0xfffu);
    startMs_ = steadyMs();
    milliseconds = [this] { return uint32_t(steadyMs() - startMs_); };

    // The thread: its stack, and its information block (which FS points
    // at): the exception handler chain's head, the stack's bounds, its own
    // address and the process's block's.
    // At the top of memory, below the native functions, so the bottom is
    // free for images: an EXE without relocations must be at 0x400000.
    const uint32_t top = (cpu.nativeBase() & ~0xffffu) - 0x10000;
    const uint32_t stack = top - kStackSize;
    tib_ = stack - 2 * Cpu::kPageSize;
    peb_ = stack - Cpu::kPageSize;
    arena_->claim(tib_, 2 * Cpu::kPageSize + kStackSize);
    stackTop_ = stack + kStackSize;
    cpu.write32(tib_ + 0x00, 0xffffffffu);
    cpu.write32(tib_ + 0x04, stackTop_);
    cpu.write32(tib_ + 0x08, stack);
    cpu.write32(tib_ + 0x18, tib_);
    cpu.write32(tib_ + 0x20, 0x100); // process id
    cpu.write32(tib_ + 0x24, 0x104); // thread id
    cpu.write32(tib_ + 0x30, peb_);
    cpu.write32(peb_ + 0x08, kExeHandle);
    cpu.fsBase = tib_;
    cpu.setStack(stackTop_ - 16);

    commandLine_ = arena_->alloc(1024);
    cpu.writeString(commandLine_, programName_, 1024);
    environment_ = arena_->alloc(16); // empty: two NULs
    environmentW_ = arena_->alloc(16);
    addStandIns();
}

const Win32Process::Module* Win32Process::module(const std::string& name) const {
    for (const auto& m : modules_)
        if (dllKey(m->name) == dllKey(name))
            return m.get();
    return nullptr;
}

const Win32Process::Module* Win32Process::loadFromDisk(const std::string& name) {
    if (!mapPath)
        return nullptr;
    std::string path = name;
    if (path.find('\\') == std::string::npos && path.find(':') == std::string::npos)
        path = programFolder_ + path;
    else
        path = fullPath(path);
    if (path.find('.', path.rfind('\\') + 1) == std::string::npos) // no extension: .DLL is meant
        path += ".dll";
    const auto file = mapPath(path, false);
    if (!file)
        return nullptr;
    std::FILE* f = std::fopen(file->string().c_str(), "rb");
    if (!f)
        return nullptr;
    std::vector<uint8_t> bytes;
    uint8_t buffer[65536];
    for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
        bytes.insert(bytes.end(), buffer, buffer + n);
    std::fclose(f);
    const Module* m = nullptr;
    const std::string fileName = path.substr(path.rfind('\\') + 1);
    if (load(fileName, bytes, m) || !m)
        return nullptr;
    if (Error e = attach(*m)) {
        if (output)
            output("LoadLibraryA: " + e.message + "\n");
        return nullptr;
    }
    return m;
}

uint32_t Win32Process::moduleHandle(const std::string& name) const {
    if (const Module* m = module(name))
        return m->base;
    // A DLL stood in for: one with something provided. Each gets a handle
    // of its own, in the order first asked for; KERNEL32's is the first.
    const std::string key = dllKey(name);
    if (key == "kernel32.dll")
        return kKernel32Handle;
    auto known = std::find(standInDlls_.begin(), standInDlls_.end(), key);
    if (known != standInDlls_.end())
        return kKernel32Handle + 0x1000 * uint32_t(1 + (known - standInDlls_.begin()));
    auto it = provided_.lower_bound(key + "!");
    if (it == provided_.end() || it->first.compare(0, key.size() + 1, key + "!") != 0)
        return 0;
    standInDlls_.push_back(key);
    return kKernel32Handle + 0x1000 * uint32_t(standInDlls_.size());
}

void Win32Process::setProgram(const std::string& exeName, const std::string& folder) {
    programName_ = exeName;
    programFolder_ = folder;
    setCurrentFolder(folder);
    // Quoted, as Windows' shell starts programs.
    cpu_.writeString(commandLine_, "\"" + folder + exeName + "\"", 1024);
}

void Win32Process::provide(const std::string& dll, const std::string& function, Cpu::Native fn) {
    const std::string key = dllKey(dll) + "!" + function;
    provided_[key] = cpu_.addNative(dll + "!" + function, std::move(fn));
}

void Win32Process::provideStdcall(const std::string& dll, const std::string& function, int args,
                                  std::function<uint32_t(Cpu&)> fn) {
    stdcall(dll.c_str(), function.c_str(), args, std::move(fn));
}

void Win32Process::provideCdecl(const std::string& dll, const std::string& function, std::function<uint32_t(Cpu&)> fn) {
    provide(dll, function, [fn = std::move(fn)](Cpu& c) {
        c.reg[Cpu::EAX] = fn(c);
        return 0u;
    });
}

void Win32Process::provideData(const std::string& dll, const std::string& name, uint32_t address) {
    provided_[dllKey(dll) + "!" + name] = address;
}

bool Win32Process::provides(const std::string& dll, const std::string& name) const {
    return provided_.count(dllKey(dll) + "!" + name) != 0;
}

uint32_t Win32Process::provided(const std::string& dll, const std::string& name) const {
    auto it = provided_.find(dllKey(dll) + "!" + name);
    return it == provided_.end() ? 0 : it->second;
}

void Win32Process::stdcall(const char* dll, const char* name, int args, std::function<uint32_t(Cpu&)> fn) {
    const uint32_t pop = uint32_t(args) * 4;
    provide(dll, name, [fn = std::move(fn), pop](Cpu& c) {
        c.reg[Cpu::EAX] = fn(c);
        return pop;
    });
}

uint32_t Win32Process::bind(const std::string& dll, const std::string& function, uint32_t ordinal) {
    const std::string name = function.empty() ? "#" + std::to_string(ordinal) : function;
    const std::string key = dllKey(dll) + "!" + name;
    if (auto it = provided_.find(key); it != provided_.end())
        return it->second;
    if (const Module* m = module(dll))
        for (const PeImage::Export& e : m->image.exports)
            if ((function.empty() ? e.ordinal == ordinal : e.name == function) && e.forwarder.empty())
                return m->base + e.rva;
    missing_.push_back(dll + "!" + name);
    const std::string message = "call to " + dll + "!" + name + ", which isn't stood in for";
    const uint32_t address = cpu_.addNative(dll + "!" + name, [message](Cpu& c) {
        c.abort(message);
        return 0u;
    });
    provided_[key] = address;
    return address;
}

Error Win32Process::load(const std::string& name, const std::vector<uint8_t>& file, const Module*& out) {
    out = nullptr;
    auto m = std::make_unique<Module>();
    m->name = name;
    if (Error e = parsePe(file, m->image); e)
        return e;
    const PeImage& pe = m->image;
    if (pe.machine != 0x14c || pe.pe32Plus)
        return Error::fail(name + " isn't a 32-bit x86 image");
    const uint32_t size = (pe.imageSize + Cpu::kPageSize - 1) & ~(Cpu::kPageSize - 1);
    uint32_t base = 0;
    if (pe.imageBase + uint64_t(size) <= cpu_.nativeBase() && arena_->claim(uint32_t(pe.imageBase), size))
        base = uint32_t(pe.imageBase);
    else if (pe.relocationCount || !(pe.characteristics & 1))
        base = arena_->alloc(size, 0x10000);
    if (!base)
        return Error::fail("No room for " + name + " (" + std::to_string(size) + " bytes)");
    m->base = base;
    m->size = size;

    uint8_t* image = cpu_.memory().data() + base;
    std::memcpy(image, file.data(), std::min<size_t>({file.size(), pe.headersSize, size}));
    for (const PeImage::Section& s : pe.sections) {
        if (s.virtualAddress >= size || s.rawOffset >= file.size())
            continue;
        const uint32_t room = size - s.virtualAddress;
        const size_t n = std::min<size_t>({s.rawSize, s.virtualSize ? std::max(s.virtualSize, s.rawSize) : s.rawSize,
                                           room, file.size() - s.rawOffset});
        std::memcpy(image + s.virtualAddress, file.data() + s.rawOffset, n);
    }
    const uint32_t delta = base - uint32_t(pe.imageBase);
    if (delta)
        for (uint32_t rva : pe.relocations)
            if (rva + 4 <= size) {
                uint32_t v;
                std::memcpy(&v, image + rva, 4);
                v += delta;
                std::memcpy(image + rva, &v, 4);
            }
    for (const PeImage::ImportedDll& dll : pe.imports)
        for (const PeImage::Import& f : dll.functions)
            if (f.iatRva + 4 <= size)
                cpu_.write32(base + f.iatRva, bind(dll.name, f.name, f.ordinal));
    m->entry = pe.entryPoint ? base + pe.entryPoint : 0;
    modules_.push_back(std::move(m));
    out = modules_.back().get();
    return Error::success();
}

Error Win32Process::start(const Module& exe, uint32_t& exitCode) {
    exitCode = 0;
    if (!exe.entry)
        return Error::fail(exe.name + " has no entry point");
    if (Error e = cpu_.call(exe.entry, {}, exitCode); e)
        return Error::fail(exe.name + ": " + e.message);
    return Error::success();
}

Error Win32Process::attach(const Module& m) {
    if (!m.entry)
        return Error::success();
    uint32_t result = 0;
    if (Error e = cpu_.call(m.entry, {m.base, 1 /* DLL_PROCESS_ATTACH */, 0}, result); e)
        return Error::fail(m.name + "'s start-up failed: " + e.message);
    if (!result)
        return Error::fail(m.name + "'s start-up refused to load");
    return Error::success();
}

// --- KERNEL32's stand-ins ---------------------------------------------------------

void Win32Process::addStandIns() {
    Cpu& c = cpu_;
    const char* k = "KERNEL32.dll";
    auto say = [this](const std::string& s) {
        if (output)
            output(s);
    };

    // The version: Windows XP (NT 5.1, build 2600), so a C runtime of the
    // time uses the system's heap rather than its own.
    stdcall(k, "GetVersion", 0, [](Cpu&) { return 0x0a280105u; });
    stdcall(k, "GetVersionExA", 1, [](Cpu& c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 148) || c.read32(p) < 148)
            return 0u;
        c.write32(p + 4, 5);
        c.write32(p + 8, 1);
        c.write32(p + 12, 2600);
        c.write32(p + 16, 2); // VER_PLATFORM_WIN32_NT
        c.write8(p + 20, 0);
        return 1u;
    });

    // The heap, and pages.
    stdcall(k, "HeapCreate", 3, [this](Cpu&) { return heapHandle_ += 0x10; });
    stdcall(k, "HeapDestroy", 1, [](Cpu&) { return 1u; });
    stdcall(k, "GetProcessHeap", 0, [](Cpu&) { return 0x7ffe0000u; });
    stdcall(k, "HeapAlloc", 3, [this](Cpu& c) { return arena_->alloc(std::max(c.arg(2), 1u)); });
    stdcall(k, "HeapFree", 3, [this](Cpu& c) {
        arena_->free(c.arg(2));
        return 1u;
    });
    stdcall(k, "HeapSize", 3, [this](Cpu& c) {
        const uint32_t n = arena_->size(c.arg(2));
        return n ? n : 0xffffffffu;
    });
    stdcall(k, "HeapReAlloc", 4, [this](Cpu& c) {
        const uint32_t flags = c.arg(1), old = c.arg(2), size = std::max(c.arg(3), 1u);
        const uint32_t oldSize = arena_->size(old);
        if (!oldSize)
            return 0u;
        if (size <= oldSize)
            return old;
        if (flags & 0x10) // HEAP_REALLOC_IN_PLACE_ONLY
            return 0u;
        const uint32_t p = arena_->alloc(size);
        if (!p)
            return 0u;
        std::memmove(c.memory().data() + p, c.memory().data() + old, oldSize);
        arena_->free(old);
        return p;
    });
    // VirtualAlloc(address, size, type, protection): anywhere, or at an
    // address asked for if nothing's there (else ERROR_INVALID_ADDRESS, and
    // the program tries elsewhere). Committing pages of a block reserved
    // before gives them back as they are.
    stdcall(k, "VirtualAlloc", 4, [this](Cpu& c) {
        const uint32_t size = (c.arg(1) + 0xfff) & ~0xfffu;
        if (!c.arg(0)) {
            const uint32_t a = arena_->alloc(size, 0x10000);
            if (a)
                virtualBlocks_[a] = size;
            else
                lastError_ = 8; // ERROR_NOT_ENOUGH_MEMORY
            return a;
        }
        const uint32_t address = c.arg(0) & ~0xfffu;
        auto it = virtualBlocks_.upper_bound(address);
        if (it != virtualBlocks_.begin()) {
            --it;
            if (address >= it->first && address + size <= it->first + it->second)
                return address;
        }
        const uint32_t base = address & ~0xffffu;
        const uint32_t whole = (address + size - base + 0xfff) & ~0xfffu;
        if (!arena_->claim(base, whole)) {
            lastError_ = 487; // ERROR_INVALID_ADDRESS
            return 0u;
        }
        virtualBlocks_[base] = whole;
        return base; // a reservation starts on the 64 KB boundary below

    });
    stdcall(k, "VirtualFree", 3, [this](Cpu& c) {
        if (c.arg(2) & 0x8000) { // MEM_RELEASE
            arena_->free(c.arg(0));
            virtualBlocks_.erase(c.arg(0));
        }
        return 1u;
    });
    stdcall(k, "IsBadReadPtr", 2, [](Cpu& c) { return c.arg(1) && !c.valid(c.arg(0), c.arg(1)) ? 1u : 0u; });
    stdcall(k, "IsBadWritePtr", 2, [](Cpu& c) { return c.arg(1) && !c.valid(c.arg(0), c.arg(1)) ? 1u : 0u; });
    stdcall(k, "IsBadCodePtr", 1, [](Cpu& c) { return c.valid(c.arg(0), 1) ? 0u : 1u; });

    // The process: its command line, environment (empty), start-up details.
    stdcall(k, "GetCommandLineA", 0, [this](Cpu&) { return commandLine_; });
    stdcall(k, "GetEnvironmentStrings", 0, [this](Cpu&) { return environment_; });
    stdcall(k, "GetEnvironmentStringsA", 0, [this](Cpu&) { return environment_; });
    stdcall(k, "GetEnvironmentStringsW", 0, [this](Cpu&) { return environmentW_; });
    stdcall(k, "FreeEnvironmentStringsA", 1, [](Cpu&) { return 1u; });
    stdcall(k, "FreeEnvironmentStringsW", 1, [](Cpu&) { return 1u; });
    stdcall(k, "GetEnvironmentVariableA", 3, [this](Cpu&) {
        lastError_ = 203; // ERROR_ENVVAR_NOT_FOUND
        return 0u;
    });
    stdcall(k, "GetStartupInfoA", 1, [](Cpu& c) {
        const uint32_t p = c.arg(0);
        if (c.valid(p, 68)) {
            std::memset(c.memory().data() + p, 0, 68);
            c.write32(p, 68);
        }
        return 0u;
    });
    stdcall(k, "GetModuleHandleA", 1, [this](Cpu& c) {
        return c.arg(0) ? moduleHandle(c.string(c.arg(0))) : kExeHandle;
    });
    stdcall(k, "GetModuleFileNameA", 3, [this](Cpu& c) {
        std::string name = programName_;
        for (const auto& m : modules_)
            if (m->base == c.arg(0))
                name = m->name;
        const std::string path = programFolder_ + name;
        const uint32_t size = c.arg(2);
        if (!c.writeString(c.arg(1), path, size))
            return 0u;
        return uint32_t(std::min<size_t>(path.size(), size - 1));
    });
    stdcall(k, "GetProcAddress", 2, [this](Cpu& c) -> uint32_t {
        const uint32_t handle = c.arg(0), name = c.arg(1);
        const bool byOrdinal = name < 0x10000;
        const std::string function = byOrdinal ? std::string() : c.string(name);
        if (handle >= kKernel32Handle && handle <= kKernel32Handle + 0x1000 * uint32_t(standInDlls_.size()) &&
            (handle - kKernel32Handle) % 0x1000 == 0 && !byOrdinal) {
            const uint32_t i = (handle - kKernel32Handle) / 0x1000;
            const std::string dll = i == 0 ? "kernel32.dll" : standInDlls_[i - 1];
            auto it = provided_.find(dll + "!" + function);
            if (it == provided_.end())
                lastError_ = 127; // ERROR_PROC_NOT_FOUND
            return it == provided_.end() ? 0u : it->second;
        }
        for (const auto& m : modules_)
            if (m->base == handle)
                for (const PeImage::Export& e : m->image.exports)
                    if ((byOrdinal ? e.ordinal == name : e.name == function) && e.forwarder.empty())
                        return m->base + e.rva;
        lastError_ = 127; // ERROR_PROC_NOT_FOUND
        if (output)
            output("GetProcAddress found no " + (byOrdinal ? "#" + std::to_string(name) : function) + "\n");
        return 0u;
    });
    // LoadLibraryA: a module loaded or stood in for already, or else one
    // in the program's folder (or where the name says), loaded and started.
    stdcall(k, "LoadLibraryA", 1, [this](Cpu& c) {
        const std::string name = c.string(c.arg(0));
        if (const uint32_t h = moduleHandle(name))
            return h;
        if (const Module* m = loadFromDisk(name))
            return m->base;
        lastError_ = 126; // ERROR_MOD_NOT_FOUND
        return 0u;
    });
    stdcall(k, "FreeLibrary", 1, [](Cpu&) { return 1u; });
    stdcall(k, "DisableThreadLibraryCalls", 1, [](Cpu&) { return 1u; });
    stdcall(k, "GetCurrentProcess", 0, [](Cpu&) { return 0xffffffffu; });
    stdcall(k, "GetCurrentProcessId", 0, [](Cpu&) { return 0x100u; });
    stdcall(k, "GetCurrentThreadId", 0, [](Cpu&) { return 0x104u; });
    stdcall(k, "GetLastError", 0, [this](Cpu&) { return lastError_; });
    stdcall(k, "SetLastError", 1, [this](Cpu& c) {
        lastError_ = c.arg(0);
        return 0u;
    });
    stdcall(k, "SetHandleCount", 1, [](Cpu& c) { return c.arg(0); });
    stdcall(k, "SetUnhandledExceptionFilter", 1, [this](Cpu& c) {
        const uint32_t old = exceptionFilter_;
        exceptionFilter_ = c.arg(0);
        return old;
    });
    stdcall(k, "UnhandledExceptionFilter", 1, [](Cpu&) { return 1u; }); // EXCEPTION_EXECUTE_HANDLER
    stdcall(k, "SetConsoleCtrlHandler", 2, [](Cpu&) { return 1u; });
    stdcall(k, "ExitProcess", 1, [this](Cpu& c) {
        exited = c.arg(0);
        c.abort("the program ended the process (exit code " + std::to_string(c.arg(0)) + ")");
        return 0u;
    });
    stdcall(k, "TerminateProcess", 2, [this](Cpu& c) {
        exited = c.arg(1);
        c.abort("the program ended the process (exit code " + std::to_string(c.arg(1)) + ")");
        return 0u;
    });
    stdcall(k, "RaiseException", 4, [](Cpu& c) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08x", c.arg(0));
        c.abort(std::string("the program raised exception ") + buf);
        return 0u;
    });
    stdcall(k, "RtlUnwind", 4, [](Cpu& c) {
        c.abort("exception unwinding (RtlUnwind) isn't stood in for");
        return 0u;
    });
    stdcall(k, "OutputDebugStringA", 1, [say](Cpu& c) {
        say(c.string(c.arg(0)));
        return 0u;
    });
    stdcall(k, "GetTickCount", 0, [this](Cpu&) { return milliseconds(); });
    stdcall("WINMM.dll", "timeGetTime", 0, [this](Cpu&) { return milliseconds(); });

    // Files: only the standard handles, whose output is passed on.
    stdcall(k, "GetStdHandle", 1, [](Cpu& c) {
        const int32_t which = int32_t(c.arg(0));
        return which >= -12 && which <= -10 ? kStdHandles + uint32_t(-10 - which) : kInvalidHandle;
    });
    stdcall(k, "SetStdHandle", 2, [](Cpu&) { return 1u; });
    stdcall(k, "GetFileType", 1, [](Cpu& c) {
        return c.arg(0) >= kStdHandles && c.arg(0) <= kStdHandles + 2 ? 2u : 0u; // FILE_TYPE_CHAR
    });
    stdcall(k, "WriteFile", 5, [this, say](Cpu& c) {
        const uint32_t handle = c.arg(0), buffer = c.arg(1), size = c.arg(2), written = c.arg(3);
        if (!c.valid(buffer, size))
            return 0u;
        if (handle == kStdHandles + 1 || handle == kStdHandles + 2)
            say(std::string(reinterpret_cast<const char*>(c.memory().data() + buffer), size));
        else if (OpenFile* f = file(handle))
            if (std::fwrite(c.memory().data() + buffer, 1, size, f->file) != size)
                return 0u;
        if (written)
            c.write32(written, size);
        return 1u;
    });
    addFileStandIns();
    addSystemStandIns();
    addUserStandIns();
    addRegistryStandIns();

    // Text: code page 1252, mapped to Unicode's first 256 characters (the
    // C runtime's locale tables are built from these; the "C" locale only
    // looks at ASCII).
    stdcall(k, "GetACP", 0, [](Cpu&) { return 1252u; });
    stdcall(k, "GetOEMCP", 0, [](Cpu&) { return 437u; });
    stdcall(k, "GetCPInfo", 2, [](Cpu& c) {
        const uint32_t p = c.arg(1);
        if (!c.valid(p, 20))
            return 0u;
        std::memset(c.memory().data() + p, 0, 20);
        c.write32(p, 1);       // MaxCharSize
        c.write8(p + 4, '?');  // DefaultChar
        return 1u;
    });
    auto ctype = [](uint32_t ch) -> uint16_t {
        if (ch > 0x7f)
            return 0;
        const int i = int(ch);
        uint16_t t = 0;
        if (std::isupper(i))
            t |= 0x1 | 0x100;
        if (std::islower(i))
            t |= 0x2 | 0x100;
        if (std::isdigit(i))
            t |= 0x4;
        if (std::isspace(i))
            t |= 0x8;
        if (std::ispunct(i))
            t |= 0x10;
        if (std::iscntrl(i))
            t |= 0x20;
        if (i == ' ' || i == '\t')
            t |= 0x40;
        if (std::isxdigit(i))
            t |= 0x80;
        return t;
    };
    // GetStringTypeA(locale, type, text, length, out), GetStringTypeW(type, text, length, out)
    stdcall(k, "GetStringTypeA", 5, [ctype](Cpu& c) {
        if (c.arg(1) != 1) // CT_CTYPE1 only
            return 0u;
        int32_t n = int32_t(c.arg(3));
        if (n < 0)
            n = int32_t(c.string(c.arg(2)).size()) + 1;
        for (int32_t i = 0; i < n; ++i)
            c.write16(c.arg(4) + 2 * uint32_t(i), ctype(c.read8(c.arg(2) + uint32_t(i))));
        return 1u;
    });
    stdcall(k, "GetStringTypeW", 4, [ctype](Cpu& c) {
        if (c.arg(0) != 1)
            return 0u;
        int32_t n = int32_t(c.arg(2));
        if (n < 0)
            for (n = 0; c.read16(c.arg(1) + 2 * uint32_t(n)); ++n)
                ;
        for (int32_t i = 0; i < n; ++i)
            c.write16(c.arg(3) + 2 * uint32_t(i), ctype(c.read16(c.arg(1) + 2 * uint32_t(i))));
        return 1u;
    });
    // LCMapString(locale, flags, source, length, out, outLength): case only.
    auto mapCase = [](uint32_t flags, uint32_t ch) -> uint32_t {
        if (ch > 0x7f)
            return ch;
        if (flags & 0x100)
            return uint32_t(std::tolower(int(ch)));
        if (flags & 0x200)
            return uint32_t(std::toupper(int(ch)));
        return ch;
    };
    stdcall(k, "LCMapStringA", 6, [mapCase](Cpu& c) {
        int32_t n = int32_t(c.arg(3));
        if (n < 0)
            n = int32_t(c.string(c.arg(2)).size()) + 1;
        const uint32_t room = c.arg(5);
        if (!room)
            return uint32_t(n);
        if (uint32_t(n) > room)
            return 0u;
        for (int32_t i = 0; i < n; ++i)
            c.write8(c.arg(4) + uint32_t(i), uint8_t(mapCase(c.arg(1), c.read8(c.arg(2) + uint32_t(i)))));
        return uint32_t(n);
    });
    stdcall(k, "LCMapStringW", 6, [mapCase](Cpu& c) {
        int32_t n = int32_t(c.arg(3));
        if (n < 0) {
            for (n = 0; c.read16(c.arg(2) + 2 * uint32_t(n)); ++n)
                ;
            ++n;
        }
        const uint32_t room = c.arg(5);
        if (!room)
            return uint32_t(n);
        if (uint32_t(n) > room)
            return 0u;
        for (int32_t i = 0; i < n; ++i)
            c.write16(c.arg(4) + 2 * uint32_t(i),
                      uint16_t(mapCase(c.arg(1), c.read16(c.arg(2) + 2 * uint32_t(i)))));
        return uint32_t(n);
    });
    // MultiByteToWideChar(codePage, flags, text, length, out, outLength)
    stdcall(k, "MultiByteToWideChar", 6, [](Cpu& c) {
        int32_t n = int32_t(c.arg(3));
        if (n < 0)
            n = int32_t(c.string(c.arg(2)).size()) + 1;
        const uint32_t room = c.arg(5);
        if (!room)
            return uint32_t(n);
        if (uint32_t(n) > room)
            return 0u;
        for (int32_t i = 0; i < n; ++i)
            c.write16(c.arg(4) + 2 * uint32_t(i), c.read8(c.arg(2) + uint32_t(i)));
        return uint32_t(n);
    });
    // WideCharToMultiByte(codePage, flags, text, length, out, outLength, default, usedDefault)
    stdcall(k, "WideCharToMultiByte", 8, [](Cpu& c) {
        int32_t n = int32_t(c.arg(3));
        if (n < 0) {
            for (n = 0; c.read16(c.arg(2) + 2 * uint32_t(n)); ++n)
                ;
            ++n;
        }
        const uint32_t room = c.arg(5);
        bool usedDefault = false;
        if (room) {
            if (uint32_t(n) > room)
                return 0u;
            for (int32_t i = 0; i < n; ++i) {
                uint16_t ch = c.read16(c.arg(2) + 2 * uint32_t(i));
                if (ch > 0xff) {
                    ch = c.arg(6) ? c.read8(c.arg(6)) : '?';
                    usedDefault = true;
                }
                c.write8(c.arg(4) + uint32_t(i), uint8_t(ch));
            }
        }
        if (c.arg(7))
            c.write32(c.arg(7), usedDefault ? 1 : 0);
        return uint32_t(n);
    });
    (void)c;
}

} // namespace reluva::x86
