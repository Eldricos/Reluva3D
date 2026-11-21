// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/crash_report.h"

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>
#else
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define RELUVA_HAVE_BACKTRACE 1
#endif
#endif

// AddressSanitizer catches crashes itself, and reports them better.
#if defined(__SANITIZE_ADDRESS__)
#define RELUVA_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RELUVA_SANITIZED 1
#endif
#endif

namespace reluva {

namespace {

// Hexadecimal into `out` (at least 19 bytes), without the C library, which
// a crash may have left unusable.
const char *hex(uintptr_t v, char *out) {
    char digits[17];
    int n = 0;
    do {
        digits[n++] = "0123456789abcdef"[v & 0xf];
        v >>= 4;
    } while (v);
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < n; ++i)
        out[2 + i] = digits[n - 1 - i];
    out[2 + n] = 0;
    return out;
}

#ifdef _WIN32

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): set once at start-up, read by the crash filter
wchar_t g_report[MAX_PATH * 2];
wchar_t g_dump[MAX_PATH * 2];
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void put(HANDLE f, const char *s) {
    DWORD written = 0;
    WriteFile(f, s, DWORD(std::strlen(s)), &written, nullptr);
}

// An address as its module's name and the offset into it, as a .pdb
// finds it.
void putAddress(HANDLE f, const void *address) {
    char text[32];
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &module) &&
        module) {
        char name[MAX_PATH] = {};
        GetModuleFileNameA(module, name, MAX_PATH);
        const char *base = std::strrchr(name, '\\');
        put(f, base ? base + 1 : name);
        put(f, "+");
        put(f, hex(uintptr_t(address) - uintptr_t(module), text));
    } else {
        put(f, hex(uintptr_t(address), text));
    }
    put(f, "\r\n");
}

LONG WINAPI onCrash(EXCEPTION_POINTERS *e) {
    const HANDLE f = CreateFileW(g_report, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        char text[32];
        put(f, "Reluva crashed: exception ");
        put(f, hex(e->ExceptionRecord->ExceptionCode, text));
        put(f, " at ");
        putAddress(f, e->ExceptionRecord->ExceptionAddress);
        put(f, "Called from (innermost first, the crash's handling included):\r\n");
        void *frames[64];
        const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
        for (USHORT i = 0; i < n; ++i)
            putAddress(f, frames[i]);
        CloseHandle(f);
    }
    const HANDLE dump = CreateFileW(g_dump, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), e, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(dump);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#else

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): set once at start-up, read by the signal handler
char g_report[4096];
char g_altStack[64 * 1024]; // for a crash that ran out of stack
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void put(int fd, const char *s) {
    for (size_t left = std::strlen(s); left;) {
        const ssize_t n = write(fd, s, left);
        if (n <= 0)
            return;
        s += n;
        left -= size_t(n);
    }
}

void onCrash(int sig, siginfo_t *info, void * /*context*/) {
    // Only what's safe in a signal handler: open, write, backtrace (made
    // ready at start-up), then the signal again as it would have been.
    char text[32];
    for (const int fd : {open(g_report, O_WRONLY | O_CREAT | O_TRUNC, 0644), int(STDERR_FILENO)}) {
        if (fd < 0)
            continue;
        put(fd, "Reluva crashed: signal ");
        put(fd, hex(uintptr_t(sig), text));
        put(fd, ", address ");
        put(fd, hex(uintptr_t(info ? info->si_addr : nullptr), text));
        put(fd, "\nCalled from (innermost first, the handler included):\n");
#ifdef RELUVA_HAVE_BACKTRACE
        void *frames[64];
        backtrace_symbols_fd(frames, backtrace(frames, 64), fd);
#endif
        if (fd != STDERR_FILENO)
            close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

#endif

} // namespace

void installCrashReport(const std::filesystem::path &report) {
#ifdef RELUVA_SANITIZED
    (void)report;
#elif defined(_WIN32)
    std::filesystem::path dump = report;
    dump.replace_extension(".dmp");
    wcsncpy_s(g_report, report.wstring().c_str(), _TRUNCATE);
    wcsncpy_s(g_dump, dump.wstring().c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(onCrash);
#else
    std::strncpy(g_report, report.string().c_str(), sizeof g_report - 1);
#ifdef RELUVA_HAVE_BACKTRACE
    // The first call loads what backtrace needs, which a handler mustn't.
    void *frame = nullptr;
    backtrace(&frame, 1);
#endif
    stack_t alt{};
    alt.ss_sp = g_altStack;
    alt.ss_size = sizeof g_altStack;
    sigaltstack(&alt, nullptr);
    struct sigaction action {};
    action.sa_sigaction = onCrash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
        sigaction(sig, &action, nullptr);
#endif
}

} // namespace reluva
