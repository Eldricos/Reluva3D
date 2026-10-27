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
    // Executables ask for 0x400000 and up: that's kept clear meanwhile.
    const uint32_t exeArea = 0x400000, exeAreaSize = 0x1000000;
    const bool reserved = cpu.nativeBase() > exeArea + exeAreaSize && arena_->claim(exeArea, exeAreaSize);
    peb_ = arena_->alloc(Cpu::kPageSize, Cpu::kPageSize);
    cpu.write32(peb_ + 0x08, kExeHandle);
    const uint32_t stack = arena_->alloc(kStackSize, 0x10000);
    stackTop_ = stack + kStackSize;
    tib_ = allocTib(stackTop_, stack, nextThreadId_);
    cpu.fsBase = tib_;
    cpu.setStack(stackTop_ - 16);

    commandLine_ = arena_->alloc(256);
    cpu.writeString(commandLine_, "stvoy.exe", 256);
    environment_ = arena_->alloc(16); // empty: two NULs
    environmentW_ = arena_->alloc(16);
    if (reserved)
        arena_->free(exeArea);
    addStandIns();
    addFileStandIns();
    addThreadStandIns();
}

uint32_t Win32Process::allocTib(uint32_t stackTop, uint32_t stackBottom, uint32_t id) {
    // The exception handler chain's head, the stack's bounds, its own
    // address, the ids, the process's block; TLS slots at 0xE10.
    const uint32_t tib = arena_->alloc(Cpu::kPageSize, Cpu::kPageSize);
    cpu_.write32(tib + 0x00, 0xffffffffu);
    cpu_.write32(tib + 0x04, stackTop);
    cpu_.write32(tib + 0x08, stackBottom);
    cpu_.write32(tib + 0x18, tib);
    cpu_.write32(tib + 0x20, 0x100); // process id
    cpu_.write32(tib + 0x24, id);
    cpu_.write32(tib + 0x2c, tib + 0xe10); // the TLS array (as Windows 95's)
    cpu_.write32(tib + 0x30, peb_);
    return tib;
}

void Win32Process::setProgram(std::string windowsPath, std::string commandLine) {
    programPath_ = std::move(windowsPath);
    cpu_.writeString(commandLine_, commandLine, 256);
}

uint32_t Win32Process::addHandle(std::shared_ptr<KernelObject> object) {
    const uint32_t h = nextHandle_;
    nextHandle_ += 4;
    handles_[h] = std::move(object);
    return h;
}

bool Win32Process::closeHandle(uint32_t handle) { return handles_.erase(handle) > 0; }

const Win32Process::Module* Win32Process::module(const std::string& name) const {
    for (const auto& m : modules_)
        if (dllKey(m->name) == dllKey(name))
            return m.get();
    return nullptr;
}

const Win32Process::Module* Win32Process::moduleAt(uint32_t base) const {
    for (const auto& m : modules_)
        if (m->base == base)
            return m.get();
    return nullptr;
}

const Win32Process::Module* Win32Process::moduleContaining(uint32_t address) const {
    for (const auto& m : modules_)
        if (address >= m->base && address - m->base < m->size)
            return m.get();
    return nullptr;
}

uint32_t Win32Process::exeBase() const {
    for (const auto& m : modules_)
        if (!(m->image.characteristics & 0x2000)) // IMAGE_FILE_DLL
            return m->base;
    return kExeHandle;
}

uint32_t Win32Process::moduleHandle(const std::string& name) const {
    if (dllKey(name) == "kernel32.dll")
        return kKernel32Handle;
    const Module* m = module(name);
    return m ? m->base : 0;
}

uint32_t Win32Process::provide(const std::string& dll, const std::string& function, Cpu::Native fn) {
    const std::string key = dllKey(dll) + "!" + function;
    return provided_[key] = cpu_.addNative(dll + "!" + function, std::move(fn));
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
        // Zero-filled sections (.bss) have no data in the file, whatever
        // their raw size says (Watcom's linker gives one).
        if (s.virtualAddress >= size || s.rawOffset >= file.size() || s.rawOffset == 0 || (s.characteristics & 0x80))
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
    stdcall(k, "VirtualAlloc", 4, [this](Cpu& c) {
        const uint32_t address = c.arg(0), size = (c.arg(1) + 0xfff) & ~0xfffu;
        if (!address)
            return arena_->alloc(size, 0x10000);
        // Committing pages reserved before: they're there already.
        return c.valid(address, size) ? address : 0u;
    });
    stdcall(k, "VirtualFree", 3, [this](Cpu& c) {
        if (c.arg(2) & 0x8000) // MEM_RELEASE
            arena_->free(c.arg(0));
        return 1u;
    });
    // Page protection isn't kept (every page is readable and writable).
    stdcall(k, "VirtualProtect", 4, [](Cpu& c) {
        if (c.arg(3))
            c.write32(c.arg(3), 4); // PAGE_READWRITE
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
        return c.arg(0) ? moduleHandle(c.string(c.arg(0))) : exeBase();
    });
    stdcall(k, "GetModuleFileNameA", 3, [this](Cpu& c) {
        // The program's folder, and the module's name (the program's own
        // path for it, or for 0).
        const size_t slash = programPath_.rfind('\\');
        std::string path = programPath_;
        for (const auto& m : modules_)
            if (m->base == c.arg(0) && m->base != exeBase())
                path = programPath_.substr(0, slash + 1) + m->name;
        const uint32_t size = c.arg(2);
        if (!c.writeString(c.arg(1), path, size))
            return 0u;
        return uint32_t(std::min<size_t>(path.size(), size - 1));
    });
    stdcall(k, "GetProcAddress", 2, [this](Cpu& c) -> uint32_t {
        const uint32_t handle = c.arg(0), name = c.arg(1);
        const bool byOrdinal = name < 0x10000;
        const std::string function = byOrdinal ? std::string() : c.string(name);
        if (handle == kKernel32Handle) {
            auto it = provided_.find("kernel32.dll!" + function);
            return it == provided_.end() ? 0u : it->second;
        }
        for (const auto& m : modules_)
            if (m->base == handle)
                for (const PeImage::Export& e : m->image.exports)
                    if ((byOrdinal ? e.ordinal == name : e.name == function) && e.forwarder.empty())
                        return m->base + e.rva;
        lastError_ = 127; // ERROR_PROC_NOT_FOUND
        return 0u;
    });
    stdcall(k, "LoadLibraryA", 1, [this](Cpu& c) {
        const uint32_t h = moduleHandle(c.string(c.arg(0)));
        if (!h)
            lastError_ = 126; // ERROR_MOD_NOT_FOUND
        return h;
    });
    stdcall(k, "FreeLibrary", 1, [](Cpu&) { return 1u; });
    stdcall(k, "DisableThreadLibraryCalls", 1, [](Cpu&) { return 1u; });
    stdcall(k, "GetCurrentProcess", 0, [](Cpu&) { return 0xffffffffu; });
    stdcall(k, "GetCurrentProcessId", 0, [](Cpu&) { return 0x100u; });
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
    stdcall(k, "TerminateProcess", 2, [](Cpu& c) {
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
