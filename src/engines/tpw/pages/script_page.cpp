// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's page for the ride and shop scripts.

#include "common/events.h"
#include "common/text.h"
#include "engines/tpw/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr Color kBackground = {24, 28, 36};
constexpr Color kLabel = {230, 230, 230};
constexpr Color kLabelLine = {140, 200, 255};
constexpr Color kDim = {150, 160, 175};
constexpr Color kError = {255, 120, 110};
constexpr int kTop = 24;
constexpr int kLine = font::kLineHeight;
constexpr int kTab = 8;          // characters a tab stop
constexpr uint32_t kTurnMs = 20; // a turn of the scripts, as rse-run's
// A turn is never more than this late: a slow screen doesn't make the
// script race to catch up.
constexpr uint64_t kMostTurns = 10;
constexpr size_t kTraceLines = 200; // the run's trace kept, more than a screen

// Tabs as spaces, to the next stop.
std::string expandTabs(const std::string &line) {
    std::string out;
    for (char c : line) {
        if (c != '\t') {
            out += c;
            continue;
        }
        do
            out += ' ';
        while (out.size() % kTab);
    }
    return out;
}

} // namespace

ScriptPage::ScriptPage(ViewerContext &context) : Page(context) {
    _names = _context.namesEndingIn({".rse"});
    if (!_names.empty())
        selected();
}

std::string ScriptPage::current() const {
    return _names.empty() ? std::string() : _names[_index];
}

void ScriptPage::selected() {
    stop();
    _script = {};
    _lines.clear();
    _summary.clear();
    _error.clear();
    _first = 0;
    std::vector<uint8_t> data;
    Script &script = _script;
    if (Error err = _context.files.read(_names[_index], data)) {
        _error = err.message;
        return;
    }
    if (Error err = script.parse(data)) {
        _error = err.message;
        return;
    }
    _summary = text::format("%zu instructions, %zu variables", script.instructions().size(), script.variables().size());
    for (const std::string &line : text::split(script.disassemble(), '\n'))
        _lines.push_back(expandTabs(line));
}

void ScriptPage::start() {
    const std::string &name = _names[_index];
    const size_t slash = name.rfind('/');
    _host = std::make_unique<SimulatedHost>(_context.files, slash == std::string::npos ? "" : name.substr(0, slash));
    _host->setGuests(true);
    _host->setTraceLimit(kTraceLines);
    _machine = std::make_unique<ScriptMachine>(_script, *_host);
    _lastTick = 0;
}

void ScriptPage::stop() {
    _machine.reset();
    _host.reset();
}

bool ScriptPage::tick(uint64_t nowMs) {
    if (!_machine)
        return false;
    if (_lastTick == 0)
        _lastTick = nowMs;
    const uint64_t turns = std::min<uint64_t>((nowMs - _lastTick) / kTurnMs, kMostTurns);
    if (turns == 0)
        return false;
    _lastTick = nowMs;
    for (uint64_t i = 0; i < turns; ++i)
        runFor(*_machine, *_host, kTurnMs, kTurnMs);
    return true;
}

void ScriptPage::handleKey(uint32_t key) {
    if (key == 'r') {
        if (_machine)
            stop();
        else if (_error.empty())
            start();
        return;
    }
    const size_t page = size_t((kViewerHeight - kTop) / kLine) - 1;
    // Scrolled no further than the last screenful.
    const size_t last = _lines.size() > page ? _lines.size() - page : 0;
    if (key == key::kUp && _first > 0)
        --_first;
    else if (key == key::kDown && _first < last)
        ++_first;
    else if (key == key::kPageUp)
        _first = _first > page ? _first - page : 0;
    else if (key == key::kPageDown)
        _first = std::min(last, _first + page);
    else
        Page::handleKey(key);
}

void ScriptPage::drawRun(Surface &screen) {
    const char *state = _machine->state() == ScriptMachine::State::kRunning   ? "running"
                        : _machine->state() == ScriptMachine::State::kWaiting ? "waiting"
                        : _machine->state() == ScriptMachine::State::kEnded   ? "ended"
                                                                              : "stopped";
    drawLabel(screen, 4, kTop,
              text::format("%.1f s  %s at word %zu%s%s", double(_host->now()) / 1000, state, _machine->address(),
                           _machine->locked() ? ", locked" : "", _machine->error().empty() ? "" : ": ") +
                  _machine->error(),
              _machine->state() == ScriptMachine::State::kFailed ? kError : kLabel);
    // The variables down the left, what it asks of the game on the right.
    int y = kTop + 2 * kLine;
    for (size_t i = 0; i < _machine->variables().size() && y + kLine <= screen.h; ++i, y += kLine)
        drawLabel(screen, 4, y, text::format("%-16s %d", _script.variables()[i].c_str(), _machine->variable(i)),
                  kLabel);
    const auto &trace = _host->trace();
    const size_t rows = size_t((screen.h - kTop - 2 * kLine) / kLine);
    const size_t from = trace.size() > rows ? trace.size() - rows : 0;
    y = kTop + 2 * kLine;
    for (size_t i = from; i < trace.size(); ++i, y += kLine)
        drawLabel(screen, 200, y, trace[i], kDim);
}

void ScriptPage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    drawLabel(screen, 4, 4,
              text::format("%zu/%zu  %s  %s", _index + 1, _names.size(), current().c_str(), _summary.c_str()), kLabel);
    if (!_error.empty()) {
        drawLabel(screen, 4, kTop + 4, _error, kError);
        return;
    }
    if (_machine) {
        drawRun(screen);
        return;
    }
    for (size_t i = _first; i < _lines.size(); ++i) {
        const int y = kTop + int(i - _first) * kLine;
        if (y + kLine > screen.h)
            break;
        const std::string &line = _lines[i];
        const Color c = line.empty() ? kLabel : line[0] == '.' ? kLabelLine : line[0] == ' ' ? kLabel : kDim;
        drawLabel(screen, 4, y, line, c);
    }
}

} // namespace reluva::tpw
