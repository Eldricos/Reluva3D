// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "devtools/ctest_runner.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

namespace reluva::devtools {

namespace {

// How long ctest has to stop when asked before it's made to, in ms.
constexpr uint64_t kStopGraceMs = 5000;

} // namespace

CtestRunner::CtestRunner(std::filesystem::path ctest, std::filesystem::path buildDir, std::string config)
    : _ctest(std::move(ctest)), _buildDir(std::move(buildDir)), _config(std::move(config)) {
}

CtestRunner::~CtestRunner() {
    _cancel = true;
    if (_thread.joinable())
        _thread.join();
}

bool CtestRunner::start(std::vector<CtestRun> runs, int jobs) {
    if (_running)
        return false;
    if (_thread.joinable())
        _thread.join();
    {
        std::lock_guard lock(_mutex);
        _results.clear();
        _log.clear();
        _byRun.assign(runs.size(), {});
        _failedLast = SIZE_MAX;
        for (size_t r = 0; r < runs.size(); ++r)
            for (const std::string &name : runs[r].tests) {
                _byRun[r][name] = _results.size();
                const auto on = runs[r].on.find(name);
                TestResult result;
                result.name = name;
                result.on = on == runs[r].on.end() ? std::string() : on->second;
                _results.push_back(std::move(result));
            }
        _startedMs = SDL_GetTicks();
        _endedMs = 0;
    }
    _cancel = false;
    _running = true;
    _thread = std::thread(&CtestRunner::work, this, std::move(runs), jobs);
    return true;
}

std::vector<TestResult> CtestRunner::results() const {
    std::lock_guard lock(_mutex);
    return _results;
}

std::vector<std::string> CtestRunner::log() const {
    std::lock_guard lock(_mutex);
    return _log;
}

uint64_t CtestRunner::elapsedMs() const {
    std::lock_guard lock(_mutex);
    if (_startedMs == 0)
        return 0;
    return (_endedMs ? _endedMs : SDL_GetTicks()) - _startedMs;
}

void CtestRunner::note(const std::string &line) {
    std::lock_guard lock(_mutex);
    _log.push_back("-- " + line);
}

void CtestRunner::work(std::vector<CtestRun> runs, int jobs) {
    for (size_t i = 0; i < runs.size() && !_cancel; ++i)
        if (!runOne(runs[i], i, jobs))
            break;
    if (_cancel)
        note("stopped");
    {
        std::lock_guard lock(_mutex);
        _endedMs = SDL_GetTicks();
        // What ctest was in the middle of when it was stopped didn't end.
        for (TestResult &r : _results)
            if (r.state == TestState::kRunning)
                r.state = TestState::kNotRun;
    }
    _running = false;
}

bool CtestRunner::runOne(const CtestRun &run, size_t index, int jobs) {
    const std::string ctest = _ctest.string();
    const std::string dir = _buildDir.string();
    const std::string parallel = std::to_string(std::max(1, jobs));
    const std::string pattern = exactPattern(run.tests);
    std::vector<const char *> args = {ctest.c_str(),         "--test-dir", dir.c_str(),
                                      "--output-on-failure", "-j",         parallel.c_str()};
    if (!_config.empty()) {
        args.push_back("-C");
        args.push_back(_config.c_str());
    }
    args.push_back("-R");
    args.push_back(pattern.c_str());
    args.push_back(nullptr);

    SDL_Environment *env = SDL_CreateEnvironment(true);
    std::string settings;
    for (const auto &[name, value] : run.set) {
        SDL_SetEnvironmentVariable(env, name.c_str(), value.c_str(), true);
        settings += " " + name + "=" + value;
    }
    for (const std::string &name : run.unset)
        SDL_UnsetEnvironmentVariable(env, name.c_str());
    note("run " + std::to_string(index + 1) + ": " + std::to_string(run.tests.size()) + " tests" + settings);

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process *process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (!process) {
        note(std::string("couldn't start ctest: ") + SDL_GetError());
        return false;
    }

    SDL_IOStream *out = SDL_GetProcessOutput(process);
    std::string pending;
    char buffer[4096];
    uint64_t askedToStop = 0;
    int exitCode = 0;
    while (true) {
        const size_t n = out ? SDL_ReadIO(out, buffer, sizeof buffer) : 0;
        if (n > 0) {
            pending.append(buffer, n);
            for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
                std::string line = pending.substr(0, nl);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                addLine(index, line);
                pending.erase(0, nl + 1);
            }
            continue;
        }
        if (_cancel) {
            const uint64_t now = SDL_GetTicks();
            if (!askedToStop) {
                askedToStop = now;
                SDL_KillProcess(process, false);
            } else if (now - askedToStop > kStopGraceMs) {
                SDL_KillProcess(process, true);
            }
        }
        // The output's end, or nothing yet: done once the process is.
        if (SDL_WaitProcess(process, false, &exitCode) && (!out || SDL_GetIOStatus(out) != SDL_IO_STATUS_NOT_READY))
            break;
        SDL_Delay(20);
    }
    if (!pending.empty())
        addLine(index, pending);
    SDL_DestroyProcess(process);
    note("ctest ended (" + std::to_string(exitCode) + ")");
    return true;
}

void CtestRunner::addLine(size_t run, const std::string &line) {
    std::lock_guard lock(_mutex);
    _log.push_back(line);
    const std::optional<CtestProgress> p = readCtestLine(line);
    if (!p) {
        // --output-on-failure prints a failed test's output just after its
        // result, until the next test's line or the summary.
        if (line.find("tests passed") != std::string::npos)
            _failedLast = SIZE_MAX;
        else if (_failedLast != SIZE_MAX)
            _results[_failedLast].output.push_back(line);
        return;
    }
    _failedLast = SIZE_MAX;
    const auto it = _byRun[run].find(p->name);
    if (it == _byRun[run].end())
        return;
    TestResult &r = _results[it->second];
    r.state = p->state;
    if (p->state != TestState::kRunning) {
        r.seconds = p->seconds;
        r.why = p->why;
    }
    if (p->state == TestState::kFailed)
        _failedLast = it->second;
}

} // namespace reluva::devtools
