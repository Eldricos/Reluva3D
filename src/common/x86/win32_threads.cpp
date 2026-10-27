// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Threads, waiting and the multimedia timer. The program's threads take
// turns on the one processor: each runs on a host thread of its own, and
// only the one holding the turn runs. They change over when one waits (a
// wait, Sleep, an event), and every few thousand instructions, so that a
// thread polling a variable lets the others on. From the behaviour
// Microsoft documents for these functions.

#include "common/x86/win32.h"

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
};

struct MutexObject : KernelObject {
    uint32_t owner = 0; // thread id
    int count = 0;
};

} // namespace

uint64_t Win32Process::now() const { return milliseconds(); }

std::shared_ptr<Win32Process::Thread> Win32Process::createThread(std::function<uint32_t()> body, bool suspended,
                                                                 std::string name) {
    // Threads that have ended give back their host thread and memory.
    for (auto it = threads_.begin(); it != threads_.end();) {
        Thread& old = **it;
        if (old.finished && old.host.joinable()) {
            old.host.join();
            arena_->free(old.stackTop - kThreadStack);
            arena_->free(old.tib);
            it = threads_.erase(it);
        } else {
            ++it;
        }
    }
    auto t = std::make_shared<Thread>();
    t->id = (nextThreadId_ += 4);
    t->name = std::move(name);
    t->body = std::move(body);
    t->suspendCount = suspended ? 1 : 0;
    const uint32_t stack = arena_->alloc(kThreadStack, 0x10000);
    t->stackTop = stack + kThreadStack;
    t->tib = allocTib(t->stackTop, stack, t->id);
    t->context.reg[Cpu::ESP] = t->stackTop - 16;
    t->context.fsBase = t->tib;
    t->context.sreg = cpu_.sreg;
    threads_.push_back(t);
    t->host = std::thread([this, t] { runThread(t); });
    return t;
}

void Win32Process::resumeThread(Thread& t) {
    if (t.suspendCount > 0)
        --t.suspendCount;
}

void Win32Process::runThread(std::shared_ptr<Thread> t) {
    std::unique_lock<std::mutex> lock(mutex_);
    turn_.wait(lock, [&] { return running_ == t.get(); });
    cpu_.loadContext(t->context);
    t->started = true;
    uint32_t code = 0;
    if (!exiting_ && !t->terminate) {
        lock.unlock();
        code = t->body();
        lock.lock();
    }
    t->finished = true;
    if (!t->exitRequested)
        t->exitCode = code;
    schedule(lock);
}

bool Win32Process::schedule(std::unique_lock<std::mutex>& lock) {
    Thread* self = running_;
    auto runnable = [this](const Thread& t) {
        if (t.finished || t.suspendCount > 0)
            return false;
        if (exiting_ || t.terminate || !t.waitFor)
            return true;
        if (t.waitUntil && now() >= t.waitUntil)
            return true;
        return t.waitFor();
    };
    // As Windows does: the runnable thread of highest priority, in turn
    // with those of the same priority (from the one after this, this one
    // last); one of lower priority runs only while those wait.
    size_t at = 0;
    for (size_t i = 0; i < threads_.size(); ++i)
        if (threads_[i].get() == self)
            at = i;
    Thread* next = nullptr;
    while (!next) {
        for (size_t i = 1; i <= threads_.size(); ++i) {
            Thread* t = threads_[(at + i) % threads_.size()].get();
            if (runnable(*t) && (!next || t->priority > next->priority))
                next = t;
        }
        if (!next) {
            if (self->finished && std::none_of(threads_.begin(), threads_.end(),
                                               [](const auto& t) { return !t->finished; }))
                return false; // nothing left at all
            // Everyone waits: for time to pass.
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lock.lock();
        }
    }
    if (next == self)
        return true;
    if (!self->finished)
        self->context = cpu_.saveContext();
    running_ = next;
    turn_.notify_all();
    if (self->finished)
        return false;
    turn_.wait(lock, [&] { return running_ == self; });
    cpu_.loadContext(self->context);
    return true;
}

bool Win32Process::wait(std::function<bool()> ready, uint32_t timeoutMs) {
    if (ready())
        return true;
    if (timeoutMs == 0)
        return false;
    Thread* self = running_;
    if (!self) // no threads: nothing else could make it ready
        return false;
    const uint64_t until = timeoutMs == kInfinite ? 0 : now() + timeoutMs;
    std::unique_lock<std::mutex> lock(mutex_);
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
    if (!running_)
        return;
    std::unique_lock<std::mutex> lock(mutex_);
    schedule(lock);
}

void Win32Process::preempt() {
    if (!running_)
        return;
    for (const auto& t : threads_)
        if (t.get() != running_ && !t->finished && t->suspendCount == 0 && t->priority > running_->priority &&
            (!t->waitFor || t->waitFor() || (t->waitUntil && now() >= t->waitUntil))) {
            yield();
            return;
        }
}

bool Win32Process::ending() const { return exiting_ || (running_ && running_->terminate); }

void Win32Process::exitAll(std::unique_lock<std::mutex>& lock) {
    // Each thread still running unwinds in turn: its waits fail, its slices stop.
    exiting_ = true;
    for (auto& t : threads_) {
        if (t->finished || t.get() == running_)
            continue;
        t->suspendCount = 0;
        Thread* self = running_;
        self->context = cpu_.saveContext();
        running_ = t.get();
        turn_.notify_all();
        turn_.wait(lock, [&] { return running_ == self; });
        cpu_.loadContext(self->context);
    }
}

Error Win32Process::run(const Module& exe, uint32_t& exitCode) {
    exitCode = 0;
    if (!exe.entry)
        return Error::fail(exe.name + " has no entry point");
    // The main thread is the caller's.
    auto main = std::make_shared<Thread>();
    main->id = 0x104;
    main->stackTop = stackTop_;
    main->tib = tib_;
    main->started = true;
    main->name = "main";
    threads_.insert(threads_.begin(), main);
    running_ = main.get();
    cpu_.setSlice(slice, [this](Cpu&) {
        yield();
        return !ending();
    });
    cpu_.stopMessage = "the process is ending";
    uint32_t result = 0;
    Error e = cpu_.call(exe.entry, {}, result);
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ended = exiting_;
        if (!ended)
            exitCode_ = result; // its start-up returned: the process ends
        exitAll(lock);
        main->finished = true;
        if (ended && failure_.empty())
            e = Error::success();
        if (!failure_.empty())
            e = Error::fail(failure_);
    }
    for (auto& t : threads_)
        if (t->host.joinable())
            t->host.join();
    cpu_.setSlice(0, nullptr);
    running_ = nullptr;
    exitCode = exitCode_;
    return e;
}

uint32_t Win32Process::runTimers() {
    while (!ending()) {
        // The next due, if any; else wait for one to be set.
        uint64_t due = 0;
        for (const auto& [id, t] : timers_)
            if (!due || t.due < due)
                due = t.due;
        if (!due) {
            wait([this] { return !timers_.empty() || ending(); }, kInfinite);
            continue;
        }
        const uint64_t t = now();
        if (t < due) {
            const size_t count = timers_.size();
            // Woken early if timers come or go.
            wait([this, count] { return timers_.size() != count; }, uint32_t(due - t));
            continue;
        }
        for (auto it = timers_.begin(); it != timers_.end();) {
            if (it->second.due > now()) {
                ++it;
                continue;
            }
            const uint32_t id = it->first;
            const Timer timer = it->second;
            if (timer.periodic)
                it->second.due = std::max(timer.due + timer.delay, now()); // no bursts to catch up
            else
                timers_.erase(it);
            uint32_t result = 0;
            if (Error e = cpu_.call(timer.callback, {id, 0, timer.user, 0, 0}, result)) {
                if (!ending()) {
                    failure_ = "timer callback: " + e.message;
                    exiting_ = true;
                }
                return 0;
            }
            it = timers_.upper_bound(id); // the callback may have changed them
        }
    }
    return 0;
}

void Win32Process::addThreadStandIns() {
    const char* k = "KERNEL32.dll";
    auto selfId = [this]() { return running_ ? running_->id : 0x104u; };
    // A thread from a handle, the current thread's pseudo handle included.
    auto thread = [this](uint32_t h) -> std::shared_ptr<Thread> {
        if (h == kCurrentThread && running_)
            for (auto& t : threads_)
                if (t.get() == running_)
                    return t;
        return object<Thread>(h);
    };
    auto endIfEnding = [this](Cpu& c) {
        if (ending())
            c.abort("the process is ending");
    };

    stdcall(k, "ExitProcess", 1, [this](Cpu& c) {
        exiting_ = true;
        exitCode_ = c.arg(0);
        c.abort("the program ended the process (exit code " + std::to_string(c.arg(0)) + ")");
        return 0u;
    });
    stdcall(k, "GetCurrentThreadId", 0, [selfId](Cpu&) { return selfId(); });
    stdcall(k, "GetCurrentThread", 0, [](Cpu&) { return kCurrentThread; });

    // CreateThread(security, stackSize, start, parameter, flags, id*): the
    // thread runs start(parameter); CREATE_SUSPENDED (4) holds it.
    stdcall(k, "CreateThread", 6, [this](Cpu& c) {
        const uint32_t start = c.arg(2), parameter = c.arg(3);
        std::shared_ptr<Thread> t;
        t = createThread(
            [this, start, parameter]() -> uint32_t {
                uint32_t result = 0;
                Error e = cpu_.call(start, {parameter}, result);
                Thread* self = running_;
                if (e && !self->exitRequested && !exiting_ && !self->terminate) {
                    // A fault in a thread ends the process, as on Windows.
                    failure_ = "thread " + std::to_string(self->id) + ": " + e.message;
                    exiting_ = true;
                }
                return result;
            },
            (c.arg(4) & 4) != 0, "thread");
        if (c.arg(5))
            c.write32(c.arg(5), t->id);
        return addHandle(t);
    });
    stdcall(k, "ExitThread", 1, [this](Cpu& c) {
        running_->exitRequested = true;
        running_->exitCode = c.arg(0);
        c.abort("the thread ended");
        return 0u;
    });
    stdcall(k, "TerminateThread", 2, [this, thread](Cpu& c) {
        auto t = thread(c.arg(0));
        if (!t)
            return 0u;
        t->terminate = true;
        t->exitRequested = true;
        t->exitCode = c.arg(1);
        if (t.get() == running_)
            c.abort("the thread was terminated");
        return 1u;
    });
    stdcall(k, "ResumeThread", 1, [this, thread](Cpu& c) {
        auto t = thread(c.arg(0));
        if (!t)
            return 0xffffffffu;
        const uint32_t before = uint32_t(t->suspendCount);
        resumeThread(*t);
        preempt();
        return before;
    });
    stdcall(k, "SuspendThread", 1, [this, thread](Cpu& c) {
        auto t = thread(c.arg(0));
        if (!t)
            return 0xffffffffu;
        const uint32_t before = uint32_t(t->suspendCount++);
        if (t.get() == running_)
            yield();
        return before;
    });
    stdcall(k, "GetThreadPriority", 1, [thread](Cpu& c) {
        auto t = thread(c.arg(0));
        return t ? uint32_t(t->priority) : 0x7fffffffu;
    });
    stdcall(k, "SetThreadPriority", 2, [this, thread](Cpu& c) {
        auto t = thread(c.arg(0));
        if (t)
            t->priority = int32_t(c.arg(1));
        preempt();
        return t ? 1u : 0u;
    });
    stdcall(k, "GetExitCodeThread", 2, [thread](Cpu& c) {
        auto t = thread(c.arg(0));
        if (!t)
            return 0u;
        c.write32(c.arg(1), t->finished ? t->exitCode : 259u);
        return 1u;
    });
    // DuplicateHandle(sourceProcess, source, targetProcess, target*, access, inherit, options)
    stdcall(k, "DuplicateHandle", 7, [this, thread](Cpu& c) {
        std::shared_ptr<KernelObject> o = thread(c.arg(1));
        if (!o) {
            auto it = handles_.find(c.arg(1));
            if (it != handles_.end())
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

    stdcall(k, "Sleep", 1, [this, endIfEnding](Cpu& c) {
        if (c.arg(0) == 0)
            yield();
        else
            wait([] { return false; }, c.arg(0));
        endIfEnding(c);
        return 0u;
    });
    stdcall(k, "SleepEx", 2, [this, endIfEnding](Cpu& c) {
        if (c.arg(0) == 0)
            yield();
        else
            wait([] { return false; }, c.arg(0));
        endIfEnding(c);
        return 0u;
    });

    // Events and mutexes.
    stdcall(k, "CreateEventA", 4, [this](Cpu& c) {
        auto e = std::make_shared<EventObject>();
        e->manualReset = c.arg(1) != 0;
        e->signaled = c.arg(2) != 0;
        lastError_ = 0;
        return addHandle(e);
    });
    stdcall(k, "SetEvent", 1, [this](Cpu& c) {
        auto e = object<EventObject>(c.arg(0));
        if (e)
            e->signaled = true;
        preempt();
        return e ? 1u : 0u;
    });
    stdcall(k, "ResetEvent", 1, [this](Cpu& c) {
        auto e = object<EventObject>(c.arg(0));
        if (e)
            e->signaled = false;
        return e ? 1u : 0u;
    });
    // PulseEvent: frees those waiting now, then it's reset; with the turns
    // taken here, the waiting threads see it when they next run.
    stdcall(k, "PulseEvent", 1, [this](Cpu& c) {
        auto e = object<EventObject>(c.arg(0));
        if (!e)
            return 0u;
        e->signaled = true;
        yield();
        e->signaled = false;
        return 1u;
    });
    stdcall(k, "CreateMutexA", 3, [this, selfId](Cpu& c) {
        auto m = std::make_shared<MutexObject>();
        if (c.arg(1)) {
            m->owner = selfId();
            m->count = 1;
        }
        lastError_ = 0; // a new one (no other copy of the program runs)
        return addHandle(m);
    });
    stdcall(k, "ReleaseMutex", 1, [this, selfId](Cpu& c) {
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
        if (auto e = object<EventObject>(h))
            return e->signaled;
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
    stdcall(k, "WaitForSingleObject", 2, [this, isSignaled, take, endIfEnding](Cpu& c) {
        const uint32_t h = c.arg(0);
        if (!object<KernelObject>(h) && h != kCurrentThread) {
            lastError_ = 6;
            return kWaitFailed;
        }
        const bool ok = wait([&] { return isSignaled(h); }, c.arg(1));
        endIfEnding(c);
        if (!ok)
            return kWaitTimeout;
        take(h);
        return 0u;
    });
    auto waitMany = [this, isSignaled, take, endIfEnding](Cpu& c, uint32_t count, uint32_t handles, bool all,
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
    stdcall(k, "WaitForMultipleObjects", 4, [waitMany](Cpu& c) {
        return waitMany(c, c.arg(0), c.arg(1), c.arg(2) != 0, c.arg(3));
    });
    stdcall(k, "WaitForMultipleObjectsEx", 5, [waitMany](Cpu& c) {
        return waitMany(c, c.arg(0), c.arg(1), c.arg(2) != 0, c.arg(3));
    });

    // Critical sections, in the program's own structure: lock count (+4),
    // recursion count (+8), owning thread (+12).
    stdcall(k, "InitializeCriticalSection", 1, [](Cpu& c) {
        for (uint32_t i = 0; i < 24; i += 4)
            c.write32(c.arg(0) + i, 0);
        c.write32(c.arg(0) + 4, 0xffffffffu);
        return 0u;
    });
    stdcall(k, "DeleteCriticalSection", 1, [](Cpu&) { return 0u; });
    stdcall(k, "EnterCriticalSection", 1, [this, selfId, endIfEnding](Cpu& c) {
        const uint32_t cs = c.arg(0), self = selfId();
        const uint32_t owner = c.read32(cs + 12);
        if (owner != 0 && owner != self)
            wait([&] { return c.read32(cs + 12) == 0; }, kInfinite);
        endIfEnding(c);
        c.write32(cs + 12, self);
        c.write32(cs + 8, c.read32(cs + 8) + 1);
        c.write32(cs + 4, c.read32(cs + 4) + 1);
        return 0u;
    });
    stdcall(k, "TryEnterCriticalSection", 1, [selfId](Cpu& c) {
        const uint32_t cs = c.arg(0), self = selfId(), owner = c.read32(cs + 12);
        if (owner != 0 && owner != self)
            return 0u;
        c.write32(cs + 12, self);
        c.write32(cs + 8, c.read32(cs + 8) + 1);
        c.write32(cs + 4, c.read32(cs + 4) + 1);
        return 1u;
    });
    stdcall(k, "LeaveCriticalSection", 1, [this](Cpu& c) {
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
    auto tib = [this]() { return running_ ? running_->tib : tib_; };
    stdcall(k, "TlsAlloc", 0, [this](Cpu&) {
        for (size_t i = 0; i < tlsUsed_.size(); ++i)
            if (!tlsUsed_[i]) {
                tlsUsed_[i] = true;
                return uint32_t(i);
            }
        return 0xffffffffu;
    });
    stdcall(k, "TlsFree", 1, [this](Cpu& c) {
        if (c.arg(0) >= tlsUsed_.size())
            return 0u;
        tlsUsed_[c.arg(0)] = false;
        return 1u;
    });
    stdcall(k, "TlsGetValue", 1, [this, tib](Cpu& c) {
        if (c.arg(0) >= 64)
            return 0u;
        lastError_ = 0;
        return c.read32(tib() + 0xe10 + 4 * c.arg(0));
    });
    stdcall(k, "TlsSetValue", 2, [tib](Cpu& c) {
        if (c.arg(0) >= 64)
            return 0u;
        c.write32(tib() + 0xe10 + 4 * c.arg(0), c.arg(1));
        return 1u;
    });

    // The multimedia timer: timeSetEvent(delay, resolution, callback, user,
    // flags) has callback(id, 0, user, 0, 0) called once or (TIME_PERIODIC,
    // 1) every `delay` ms, on the one timer thread, as WINMM does.
    const char* w = "WINMM.dll";
    stdcall(w, "timeSetEvent", 5, [this](Cpu& c) {
        Timer t;
        t.delay = std::max(c.arg(0), 1u);
        t.callback = c.arg(2);
        t.user = c.arg(3);
        t.periodic = c.arg(4) & 1;
        t.due = now() + t.delay;
        const uint32_t id = nextTimer_++;
        timers_[id] = t;
        if (!timerThread_) {
            timerThread_ = createThread([this] { return runTimers(); }, false, "timer");
            timerThread_->priority = 15; // THREAD_PRIORITY_TIME_CRITICAL
        }
        return id;
    });
    stdcall(w, "timeKillEvent", 1, [this](Cpu& c) { return timers_.erase(c.arg(0)) ? 0u : 97u; }); // TIMERR_NOCANDO
    stdcall(w, "timeBeginPeriod", 1, [](Cpu&) { return 0u; });
    stdcall(w, "timeEndPeriod", 1, [](Cpu&) { return 0u; });
    stdcall(w, "timeGetDevCaps", 2, [](Cpu& c) {
        c.write32(c.arg(0), 1);       // wPeriodMin
        c.write32(c.arg(0) + 4, 1000000); // wPeriodMax
        return 0u;
    });
    (void)kCurrentProcess;
}

} // namespace reluva::x86
