// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define RELUVA_DUP _dup
#define RELUVA_DUP2 _dup2
#define RELUVA_CLOSE _close
#define RELUVA_READ _read
#define RELUVA_WRITE _write
#else
#include <unistd.h>
#define RELUVA_DUP dup
#define RELUVA_DUP2 dup2
#define RELUVA_CLOSE close
#define RELUVA_READ read
#define RELUVA_WRITE write
#endif

namespace reluva::log {

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): the debug level for the whole program, set from its command line
std::atomic<int> g_level{0};

void print(const char *prefix, const char *fmt, va_list args) {
    char buf[2048];
    std::vsnprintf(buf, sizeof buf, fmt, args);
    std::fprintf(stderr, "%s%s\n", prefix, buf);
}
} // namespace

void setDebugLevel(int l) {
    g_level = l;
}
int debugLevel() {
    return g_level;
}

void debug(int l, const char *fmt, ...) {
    if (l > g_level)
        return;
    va_list args;
    va_start(args, fmt);
    print("", fmt, args);
    va_end(args);
}

void warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    print("WARNING: ", fmt, args);
    va_end(args);
}

} // namespace reluva::log

namespace reluva {

struct StderrLog::State {
    std::FILE *file = nullptr;
    int original = -1; // stderr as it was (-1: none, as a program without a console has)
    int readEnd = -1;
    std::thread copier;
};

StderrLog::StderrLog(const std::filesystem::path &file) : _s(std::make_unique<State>()) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path previous = file;
    previous.replace_filename(file.stem().string() + "-previous" + file.extension().string());
    std::filesystem::rename(file, previous, ec); // the run before's, if there was one
#ifdef _WIN32
    _s->file = _wfopen(file.c_str(), L"wb");
#else
    _s->file = std::fopen(file.c_str(), "wb");
#endif
    if (!_s->file)
        return;
    const std::time_t now = std::time(nullptr);
    char when[64] = "";
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(_s->file, "Reluva, started %s\n", when);
    std::fflush(_s->file);

    int ends[2];
#ifdef _WIN32
    if (_pipe(ends, 4096, _O_BINARY) != 0)
        return;
#else
    if (pipe(ends) != 0)
        return;
#endif
    std::fflush(stderr);
    _s->original = RELUVA_DUP(2);
    if (RELUVA_DUP2(ends[1], 2) == -1) { // (2 on POSIX, 0 on Windows: either is success)
        RELUVA_CLOSE(ends[0]), RELUVA_CLOSE(ends[1]);
        return;
    }
    RELUVA_CLOSE(ends[1]);                    // stderr (2) holds the write end now
    std::setvbuf(stderr, nullptr, _IONBF, 0); // nothing held back if the program dies
    _s->readEnd = ends[0];
    _s->copier = std::thread([state = _s.get()] {
        char buffer[4096];
        for (;;) {
            const int n = int(RELUVA_READ(state->readEnd, buffer, sizeof buffer));
            if (n <= 0)
                break;
            std::fwrite(buffer, 1, size_t(n), state->file);
            std::fflush(state->file);
            if (state->original >= 0)
                if (RELUVA_WRITE(state->original, buffer, unsigned(n)) < 0) {} // a console gone: the file still has it
        }
    });
}

StderrLog::~StderrLog() {
    if (_s->copier.joinable()) {
        // stderr back as it was, which closes the pipe's write end: the
        // copier reads to the end and stops.
        std::fflush(stderr);
        if (_s->original >= 0)
            RELUVA_DUP2(_s->original, 2);
        else
            RELUVA_CLOSE(2);
        _s->copier.join();
        RELUVA_CLOSE(_s->readEnd);
    }
    if (_s->original >= 0)
        RELUVA_CLOSE(_s->original);
    if (_s->file)
        std::fclose(_s->file);
}

} // namespace reluva
