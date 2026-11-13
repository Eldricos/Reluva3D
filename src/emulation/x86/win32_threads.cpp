// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Threads, waiting and the multimedia timer. The program's threads take
// turns on the one processor: each runs on a host thread of its own, and
// only the one holding the turn runs. They change over when one waits (a
// wait, Sleep, an event), and every few thousand instructions, so that a
// thread polling a variable lets the others on. From the behaviour
// Microsoft documents for these functions.

#include "emulation/x86/win32.h"

#include <algorithm>
#include <chrono>

namespace reluva::x86 {

namespace {

constexpr uint32_t kInfinite = 0xffffffffu;
constexpr uint32_t kCurrentThread = 0xfffffffeu, kCurrentProcess = 0xffffffffu;
constexpr uint32_t kWaitTimeout = 0x102, kWaitFailed = 0xffffffffu;
constexpr uint32_t kThreadStack = 1u << 20;

struct EventObject : KernelObject {
    bool manualReset = false, signaled = false;
    std::function<bool()> signalWhen; // signalled once this holds (signalEventWhen)
};

struct MutexObject : KernelObject {
    uint32_t owner = 0; // thread id
    int count = 0;
};

} // namespace

uint64_t Win32Process::now() const {
    return milliseconds();
}

void Win32Process::useHostThreads() {
    if (_hostThreads)
        return;
    _hostThreads = true;
    _cpu.write32(_tib + 0x2c, _tib + 0xe10); // the TLS array (as Windows 95's)
    addThreadStandIns();
}

uint32_t Win32Process::allocTib(uint32_t stackTop, uint32_t stackBottom, uint32_t id) {
    // The exception handler chain's head, the stack's bounds, its own
    // address, the ids, the process's block; TLS slots at 0xE10.
    const uint32_t tib = _arena->alloc(Cpu::kPageSize, Cpu::kPageSize);
    _cpu.write32(tib + 0x00, 0xffffffffu);
    _cpu.write32(tib + 0x04, stackTop);
    _cpu.write32(tib + 0x08, stackBottom);
    _cpu.write32(tib + 0x18, tib);
    _cpu.write32(tib + 0x20, 0x100); // process id
    _cpu.write32(tib + 0x24, id);
    _cpu.write32(tib + 0x2c, tib + 0xe10); // the TLS array (as Windows 95's)
    _cpu.write32(tib + 0x30, _peb);
    return tib;
}

// NOLINTNEXTLINE(readability-make-member-function-const): changes one of its event objects
void Win32Process::signalEventWhen(uint32_t handle, std::function<bool()> condition) {
    if (auto e = object<EventObject>(handle)) {
        e->signaled = false;
        e->signalWhen = std::move(condition);
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const): changes one of its event objects
bool Win32Process::setHostEvent(uint32_t handle) {
    auto e = object<EventObject>(handle);
    if (!e)
        return false;
    e->signaled = true;
    return true;
}

std::shared_ptr<Win32Process::Thread> Win32Process::createThread(std::function<uint32_t()> body, bool suspended,
                                                                 std::string name) {
    // Threads that have ended give back their host thread and memory.
    for (auto it = _threads.begin(); it != _threads.end();) {
        Thread &old = **it;
        if (old.finished && old.host.joinable()) {
            old.host.join();
            _arena->free(old.stackTop - kThreadStack);
            _arena->free(old.tib);
            it = _threads.erase(it);
        } else {
            ++it;
        }
    }
    auto t = std::make_shared<Thread>();
    t->id = (_nextThreadId += 4);
    t->name = std::move(name);
    t->body = std::move(body);
    t->suspendCount = suspended ? 1 : 0;
    const uint32_t stack = _arena->alloc(kThreadStack, 0x10000);
    t->stackTop = stack + kThreadStack;
    t->tib = allocTib(t->stackTop, stack, t->id);
    t->context.reg[Cpu::ESP] = t->stackTop - 16;
    t->context.fsBase = t->tib;
    t->context.sreg = _cpu.sreg;
    _threads.push_back(t);
    t->host = std::thread([this, t] { runThread(t); });
    return t;
}

void Win32Process::resumeThread(Thread &t) {
    if (t.suspendCount > 0)
        --t.suspendCount;
}

void Win32Process::runThread(std::shared_ptr<Thread> t) {
    std::unique_lock<std::mutex> lock(_mutex);
    t->turn.wait(lock, [&] { return _running == t.get(); });
    _cpu.loadContext(t->context);
    t->started = true;
    uint32_t code = 0;
    if (!_exiting && !t->terminate) {
        lock.unlock();
        code = t->body();
        lock.lock();
    }
    t->finished = true;
    if (!t->exitRequested)
        t->exitCode = code;
    schedule(lock);
}

bool Win32Process::schedule(std::unique_lock<std::mutex> &lock) {
    Thread *self = _running;
    auto runnable = [this](const Thread &t) {
        if (t.finished || t.suspendCount > 0)
            return false;
        if (_exiting || t.terminate || !t.waitFor)
            return true;
        if (t.waitUntil && now() >= t.waitUntil)
            return true;
        return t.waitFor();
    };
    // As Windows does: the runnable thread of highest priority, in turn
    // with those of the same priority (from the one after this, this one
    // last); one of lower priority runs only while those wait.
    size_t at = 0;
    for (size_t i = 0; i < _threads.size(); ++i)
        if (_threads[i].get() == self)
            at = i;
    Thread *next = nullptr;
    while (!next) {
        for (size_t i = 1; i <= _threads.size(); ++i) {
            Thread *t = _threads[(at + i) % _threads.size()].get();
            if (runnable(*t) && (!next || t->priority > next->priority))
                next = t;
        }
        if (!next) {
            if (self->finished &&
                std::none_of(_threads.begin(), _threads.end(), [](const auto &t) { return !t->finished; }))
                return false; // nothing left at all
            // Everyone waits: for time to pass.
            lock.unlock();
            const auto from = std::chrono::steady_clock::now();
            if (idleTick)
                idleTick();
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++idleWaits;
            idleMicros += uint64_t(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - from).count());
            lock.lock();
        }
    }
    if (next == self)
        return true;
    if (!self->finished)
        self->context = _cpu.saveContext();
    self->instructions += _cpu.executed() - _turnStart;
    _turnStart = _cpu.executed();
    _running = next;
    next->turn.notify_one();
    if (self->finished)
        return false;
    self->turn.wait(lock, [&] { return _running == self; });
    _cpu.loadContext(self->context);
    return true;
}

bool Win32Process::wait(const std::function<bool()> &ready, uint32_t timeoutMs) {
    if (ready())
        return true;
    if (timeoutMs == 0)
        return false;
    Thread *self = _running;
    if (!self) // no threads: nothing else could make it ready
        return false;
    const uint64_t until = timeoutMs == kInfinite ? 0 : now() + timeoutMs;
    std::unique_lock<std::mutex> lock(_mutex);
    while (true) {
        self->waitFor = ready;
        self->waitUntil = until;
        schedule(lock);
        self->waitFor = nullptr;
        self->waitUntil = 0;
        if (ending())
            return false;
        if (ready())
            return true;
        if (until && now() >= until)
            return false;
    }
}

void Win32Process::yield() {
    if (!_running)
        return;
    std::unique_lock<std::mutex> lock(_mutex);
    schedule(lock);
}

void Win32Process::preempt() {
    if (!_running)
        return;
    for (const auto &t : _threads)
        if (t.get() != _running && !t->finished && t->suspendCount == 0 && t->priority > _running->priority &&
            (!t->waitFor || t->waitFor() || (t->waitUntil && now() >= t->waitUntil))) {
            yield();
            return;
        }
}

bool Win32Process::ending() const {
    return _exiting || (_running && _running->terminate);
}

void Win32Process::exitAll(std::unique_lock<std::mutex> &lock) {
    // Each thread still running unwinds in turn: its waits fail, its slices stop.
    _exiting = true;
    for (auto &t : _threads) {
        if (t->finished || t.get() == _running)
            continue;
        t->suspendCount = 0;
        Thread *self = _running;
        self->context = _cpu.saveContext();
        _running = t.get();
        t->turn.notify_one();
        self->turn.wait(lock, [&] { return _running == self; });
        _cpu.loadContext(self->context);
    }
}

Error Win32Process::run(const Module &exe, uint32_t &exitCode) {
    exitCode = 0;
    if (!exe.entry)
        return Error::fail(exe.name + " has no entry point");
    // The main thread is the caller's.
    auto main = std::make_shared<Thread>();
    main->id = 0x104;
    main->stackTop = _stackTop;
    main->tib = _tib;
    main->started = true;
    main->name = "main";
    _threads.insert(_threads.begin(), main);
    _running = main.get();
    _cpu.setSlice(slice, [this](Cpu &c) {
        if (onSlice)
            onSlice(c);
        yield();
        return !ending();
    });
    _cpu.stopMessage = "the process is ending";
    uint32_t result = 0;
    Error e = _cpu.call(exe.entry, {}, result);
    {
        std::unique_lock<std::mutex> lock(_mutex);
        const bool ended = _exiting;
        if (!ended)
            _exitCode = result; // its start-up returned: the process ends
        exitAll(lock);
        main->finished = true;
        if (ended && _failure.empty())
            e = Error::success();
        if (!_failure.empty())
            e = Error::fail(_failure);
    }
    for (auto &t : _threads)
        if (t->host.joinable())
            t->host.join();
    _cpu.setSlice(0, nullptr);
    _running = nullptr;
    exitCode = _exitCode;
    return e;
}

uint32_t Win32Process::runTimers() {
    while (!ending()) {
        // The next due, if any; else wait for one to be set.
        uint64_t due = 0;
        for (const auto &[id, t] : _timers)
            if (!due || t.due < due)
                due = t.due;
        if (!due) {
            wait([this] { return !_timers.empty() || ending(); }, kInfinite);
            continue;
        }
        const uint64_t t = now();
        if (t < due) {
            const size_t count = _timers.size();
            // Woken early if timers come or go.
            wait([this, count] { return _timers.size() != count; }, uint32_t(due - t));
            continue;
        }
        for (auto it = _timers.begin(); it != _timers.end();) {
            if (it->second.due > now()) {
                ++it;
                continue;
            }
            const uint32_t id = it->first;
            const Timer timer = it->second;
            if (timer.periodic)
                it->second.due = std::max(timer.due + timer.delay, now()); // no bursts to catch up
            else
                _timers.erase(it);
            uint32_t result = 0;
            if (Error e = _cpu.call(timer.callback, {id, 0, timer.user, 0, 0}, result)) {
                if (!ending()) {
                    _failure = "timer callback: " + e.message;
                    _exiting = true;
                }
                return 0;
            }
            it = _timers.upper_bound(id); // the callback may have changed them
        }
    }
    return 0;
}

void Win32Process::addThreadStandIns() {
    const char *k = "KERNEL32.dll";
    auto selfId = [this]() { return _running ? _running->id : 0x104u; };
    // A thread from a handle, the current thread's pseudo handle included.
    auto thread = [this](uint32_t h) -> std::shared_ptr<Thread> {
        if (h == kCurrentThread && _running)
            for (auto &t : _threads)
                if (t.get() == _running)
                    return t;
        return object<Thread>(h);
    };
    auto endIfEnding = [this](Cpu &c) {
        if (ending())
            c.abort("the process is ending");
    };

    stdcall(k, "ExitProcess", 1, [this](Cpu &c) {
        _exiting = true;
        _exitCode = c.arg(0);
        c.abort("the program ended the process (exit code " + std::to_string(c.arg(0)) + ")");
        return 0u;
    });
    stdcall(k, "GetCurrentThreadId", 0, [selfId](Cpu &) { return selfId(); });
    stdcall(k, "GetCurrentThread", 0, [](Cpu &) { return kCurrentThread; });

    // CreateThread(security, stackSize, start, parameter, flags, id*): the
    // thread runs start(parameter); CREATE_SUSPENDED (4) holds it.
    stdcall(k, "CreateThread", 6, [this](Cpu &c) {
        const uint32_t start = c.arg(2);
        const uint32_t parameter = c.arg(3);
        std::shared_ptr<Thread> t;
        t = createThread(
            [this, start, parameter]() -> uint32_t {
                uint32_t result = 0;
                Error e = _cpu.call(start, {parameter}, result);
                Thread *self = _running;
                if (e && !self->exitRequested && !_exiting && !self->terminate) {
                    // A fault in a thread ends the process, as on Windows.
                    _failure = "thread " + std::to_string(self->id) + ": " + e.message;
                    _exiting = true;
                }
                return result;
            },
            (c.arg(4) & 4) != 0, "thread");
        if (c.arg(5))
            c.write32(c.arg(5), t->id);
        return addHandle(t);
    });
    stdcall(k, "ExitThread", 1, [this](Cpu &c) {
        _running->exitRequested = true;
        _running->exitCode = c.arg(0);
        c.abort("the thread ended");
        return 0u;
    });
    stdcall(k, "TerminateThread", 2, [this, thread](Cpu &c) {
        auto t = thread(c.arg(0));
        if (!t)
            return 0u;
        t->terminate = true;
        t->exitRequested = true;
        t->exitCode = c.arg(1);
        if (t.get() == _running)
            c.abort("the thread was terminated");
        return 1u;
    });
    stdcall(k, "ResumeThread", 1, [this, thread](Cpu &c) {
        auto t = thread(c.arg(0));
        if (!t)
            return 0xffffffffu;
        const uint32_t before = uint32_t(t->suspendCount);
        resumeThread(*t);
        preempt();
        return before;
    });
    stdcall(k, "SuspendThread", 1, [this, thread](Cpu &c) {
        auto t = thread(c.arg(0));
        if (!t)
            return 0xffffffffu;
        const uint32_t before = uint32_t(t->suspendCount++);
        if (t.get() == _running)
            yield();
        return before;
    });
    stdcall(k, "GetThreadPriority", 1, [thread](Cpu &c) {
        auto t = thread(c.arg(0));
        return t ? uint32_t(t->priority) : 0x7fffffffu;
    });
    stdcall(k, "SetThreadPriority", 2, [this, thread](Cpu &c) {
        auto t = thread(c.arg(0));
        if (t)
            t->priority = int32_t(c.arg(1));
        preempt();
        return t ? 1u : 0u;
    });
    stdcall(k, "GetExitCodeThread", 2, [thread](Cpu &c) {
        auto t = thread(c.arg(0));
        if (!t)
            return 0u;
        c.write32(c.arg(1), t->finished ? t->exitCode : 259u);
        return 1u;
    });
    // DuplicateHandle(sourceProcess, source, targetProcess, target*, access, inherit, options)
    stdcall(k, "DuplicateHandle", 7, [this, thread](Cpu &c) {
        std::shared_ptr<KernelObject> o = thread(c.arg(1));
        if (!o) {
            auto it = _handles.find(c.arg(1));
            if (it != _handles.end())
                o = it->second;
        }
        if (!o)
            return 0u;
        if (c.arg(3))
            c.write32(c.arg(3), addHandle(o));
        if (c.arg(6) & 1) // DUPLICATE_CLOSE_SOURCE
            closeHandle(c.arg(1));
        return 1u;
    });

    stdcall(k, "Sleep", 1, [this, endIfEnding](Cpu &c) {
        if (beforeSleep)
            beforeSleep(c);
        if (c.arg(0) == 0)
            yield();
        else
            wait([] { return false; }, c.arg(0));
        endIfEnding(c);
        return 0u;
    });
    stdcall(k, "SleepEx", 2, [this, endIfEnding](Cpu &c) {
        if (beforeSleep)
            beforeSleep(c);
        if (c.arg(0) == 0)
            yield();
        else
            wait([] { return false; }, c.arg(0));
        endIfEnding(c);
        return 0u;
    });

    // Events and mutexes.
    stdcall(k, "CreateEventA", 4, [this](Cpu &c) {
        auto e = std::make_shared<EventObject>();
        e->manualReset = c.arg(1) != 0;
        e->signaled = c.arg(2) != 0;
        _lastError = 0;
        return addHandle(e);
    });
    stdcall(k, "SetEvent", 1, [this](Cpu &c) {
        auto e = object<EventObject>(c.arg(0));
        if (e)
            e->signaled = true;
        preempt();
        return e ? 1u : 0u;
    });
    stdcall(k, "ResetEvent", 1, [this](Cpu &c) {
        auto e = object<EventObject>(c.arg(0));
        if (e)
            e->signaled = false;
        return e ? 1u : 0u;
    });
    // PulseEvent: frees those waiting now, then it's reset; with the turns
    // taken here, the waiting threads see it when they next run.
    stdcall(k, "PulseEvent", 1, [this](Cpu &c) {
        auto e = object<EventObject>(c.arg(0));
        if (!e)
            return 0u;
        e->signaled = true;
        yield();
        e->signaled = false;
        return 1u;
    });
    stdcall(k, "CreateMutexA", 3, [this, selfId](Cpu &c) {
        auto m = std::make_shared<MutexObject>();
        if (c.arg(1)) {
            m->owner = selfId();
            m->count = 1;
        }
        _lastError = 0; // a new one (no other copy of the program runs)
        return addHandle(m);
    });
    stdcall(k, "ReleaseMutex", 1, [this, selfId](Cpu &c) {
        auto m = object<MutexObject>(c.arg(0));
        if (!m || m->owner != selfId())
            return 0u;
        if (--m->count == 0)
            m->owner = 0;
        preempt();
        return 1u;
    });

    // Waits: whether an object is signalled for this thread, and taking it.
    auto isSignaled = [this, thread, selfId](uint32_t h) -> bool {
        if (auto t = thread(h))
            return t->finished;
        if (auto e = object<EventObject>(h)) {
            if (!e->signaled && e->signalWhen && e->signalWhen()) {
                e->signaled = true;
                e->signalWhen = nullptr;
            }
            return e->signaled;
        }
        if (auto m = object<MutexObject>(h))
            return m->owner == 0 || m->owner == selfId();
        return true; // a file or the like: signalled
    };
    auto take = [this, selfId](uint32_t h) {
        if (auto e = object<EventObject>(h)) {
            if (!e->manualReset)
                e->signaled = false;
        } else if (auto m = object<MutexObject>(h)) {
            m->owner = selfId();
            ++m->count;
        }
    };
    stdcall(k, "WaitForSingleObject", 2, [this, isSignaled, take, endIfEnding](Cpu &c) {
        const uint32_t h = c.arg(0);
        if (!object<KernelObject>(h) && h != kCurrentThread) {
            _lastError = 6;
            return kWaitFailed;
        }
        const bool ok = wait([&] { return isSignaled(h); }, c.arg(1));
        endIfEnding(c);
        if (!ok)
            return kWaitTimeout;
        take(h);
        return 0u;
    });
    auto waitMany = [this, isSignaled, take, endIfEnding](Cpu &c, uint32_t count, uint32_t handles, bool all,
                                                          uint32_t timeout) {
        std::vector<uint32_t> hs;
        for (uint32_t i = 0; i < count && i < 64; ++i)
            hs.push_back(c.read32(handles + 4 * i));
        int which = -1;
        auto ready = [&] {
            if (all)
                return std::all_of(hs.begin(), hs.end(), isSignaled);
            for (size_t i = 0; i < hs.size(); ++i)
                if (isSignaled(hs[i])) {
                    which = int(i);
                    return true;
                }
            return false;
        };
        const bool ok = wait(ready, timeout);
        endIfEnding(c);
        if (!ok)
            return kWaitTimeout;
        ready();
        if (all) {
            for (uint32_t h : hs)
                take(h);
            return 0u;
        }
        take(hs[size_t(which)]);
        return uint32_t(which);
    };
    stdcall(k, "WaitForMultipleObjects", 4,
            [waitMany](Cpu &c) { return waitMany(c, c.arg(0), c.arg(1), c.arg(2) != 0, c.arg(3)); });
    stdcall(k, "WaitForMultipleObjectsEx", 5,
            [waitMany](Cpu &c) { return waitMany(c, c.arg(0), c.arg(1), c.arg(2) != 0, c.arg(3)); });

    // Critical sections, in the program's own structure: lock count (+4),
    // recursion count (+8), owning thread (+12).
    stdcall(k, "InitializeCriticalSection", 1, [](Cpu &c) {
        for (uint32_t i = 0; i < 24; i += 4)
            c.write32(c.arg(0) + i, 0);
        c.write32(c.arg(0) + 4, 0xffffffffu);
        return 0u;
    });
    stdcall(k, "DeleteCriticalSection", 1, [](Cpu &) { return 0u; });
    stdcall(k, "EnterCriticalSection", 1, [this, selfId, endIfEnding](Cpu &c) {
        const uint32_t cs = c.arg(0);
        const uint32_t self = selfId();
        const uint32_t owner = c.read32(cs + 12);
        if (owner != 0 && owner != self)
            wait([&] { return c.read32(cs + 12) == 0; }, kInfinite);
        endIfEnding(c);
        c.write32(cs + 12, self);
        c.write32(cs + 8, c.read32(cs + 8) + 1);
        c.write32(cs + 4, c.read32(cs + 4) + 1);
        return 0u;
    });
    stdcall(k, "TryEnterCriticalSection", 1, [selfId](Cpu &c) {
        const uint32_t cs = c.arg(0);
        const uint32_t self = selfId();
        const uint32_t owner = c.read32(cs + 12);
        if (owner != 0 && owner != self)
            return 0u;
        c.write32(cs + 12, self);
        c.write32(cs + 8, c.read32(cs + 8) + 1);
        c.write32(cs + 4, c.read32(cs + 4) + 1);
        return 1u;
    });
    stdcall(k, "LeaveCriticalSection", 1, [this](Cpu &c) {
        const uint32_t cs = c.arg(0);
        const uint32_t n = c.read32(cs + 8);
        c.write32(cs + 4, c.read32(cs + 4) - 1);
        if (n <= 1) {
            c.write32(cs + 8, 0);
            c.write32(cs + 12, 0);
            preempt();
        } else {
            c.write32(cs + 8, n - 1);
        }
        return 0u;
    });

    // Thread-local storage: 64 slots in each thread's information block.
    auto tib = [this]() { return _running ? _running->tib : _tib; };
    stdcall(k, "TlsAlloc", 0, [this](Cpu &) {
        for (size_t i = 0; i < _tlsUsed.size(); ++i)
            if (!_tlsUsed[i]) {
                _tlsUsed[i] = true;
                return uint32_t(i);
            }
        return 0xffffffffu;
    });
    stdcall(k, "TlsFree", 1, [this](Cpu &c) {
        if (c.arg(0) >= _tlsUsed.size())
            return 0u;
        _tlsUsed[c.arg(0)] = false;
        return 1u;
    });
    stdcall(k, "TlsGetValue", 1, [this, tib](Cpu &c) {
        if (c.arg(0) >= 64)
            return 0u;
        _lastError = 0;
        return c.read32(tib() + 0xe10 + 4 * c.arg(0));
    });
    stdcall(k, "TlsSetValue", 2, [tib](Cpu &c) {
        if (c.arg(0) >= 64)
            return 0u;
        c.write32(tib() + 0xe10 + 4 * c.arg(0), c.arg(1));
        return 1u;
    });

    // The multimedia timer: timeSetEvent(delay, resolution, callback, user,
    // flags) has callback(id, 0, user, 0, 0) called once or (TIME_PERIODIC,
    // 1) every `delay` ms, on the one timer thread, as WINMM does.
    const char *w = "WINMM.dll";
    stdcall(w, "timeSetEvent", 5, [this](Cpu &c) {
        Timer t;
        t.delay = std::max(c.arg(0), 1u);
        t.callback = c.arg(2);
        t.user = c.arg(3);
        t.periodic = c.arg(4) & 1;
        t.due = now() + t.delay;
        const uint32_t id = _nextTimer++;
        _timers[id] = t;
        if (!_timerThread) {
            _timerThread = createThread([this] { return runTimers(); }, false, "timer");
            _timerThread->priority = 15; // THREAD_PRIORITY_TIME_CRITICAL
        }
        return id;
    });
    stdcall(w, "timeKillEvent", 1, [this](Cpu &c) { return _timers.erase(c.arg(0)) ? 0u : 97u; }); // TIMERR_NOCANDO
    stdcall(w, "timeBeginPeriod", 1, [](Cpu &) { return 0u; });
    stdcall(w, "timeEndPeriod", 1, [](Cpu &) { return 0u; });
    stdcall(w, "timeGetDevCaps", 2, [](Cpu &c) {
        c.write32(c.arg(0), 1);           // wPeriodMin
        c.write32(c.arg(0) + 4, 1000000); // wPeriodMax
        return 0u;
    });
    (void)kCurrentProcess;
}

} // namespace reluva::x86
