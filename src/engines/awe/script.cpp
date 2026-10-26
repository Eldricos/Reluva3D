// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/script.h"

#include "common/fs.h"
#include "engines/awe/tables.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::awe {

const std::string& ScriptEvent::arg(size_t i) const {
    static const std::string none;
    return i < words.size() ? words[i] : none;
}

std::string_view ScriptRunner::baseOfAsync(std::string_view verb) {
    // The handlers that compare both forms (0x409ce0): the "a" form reports
    // the event done as soon as it has started what it asks.
    static constexpr std::string_view kAsync[] = {"walka",  "runa",  "talka", "fadea", "sounda",
                                                  "anima",  "turna", "rotatea", "playa"};
    for (std::string_view a : kAsync)
        if (verb == a)
            return a.substr(0, a.size() - 1);
    return {};
}

void ScriptRunner::run(std::string_view commands) {
    for (Command& c : parseCommands(commands))
        add(std::move(c.words), 0);
}

void ScriptRunner::add(std::vector<std::string> words, int depth) {
    if (words.empty())
        return;
    words.front() = fs::toLower(words.front());
    // The game keeps five words of an event.
    if (words.size() > 5)
        words.resize(5);
    words.resize(std::max<size_t>(words.size(), 1));
    auto word = [&](size_t i) { return i < words.size() ? words[i] : std::string(); };
    const std::string& verb = words.front();
    if (verb == "script" && depth < 8) {
        // Another file's commands, one a line (0x409600).
        for (Command& c : parseCommands(host_.scriptText(word(1))))
            add(std::move(c.words), depth + 1);
        return;
    }
    if (verb == "dooropen" || verb == "doorlocked") {
        // The macros of 0x4091f0 and 0x409320.
        const std::string point = word(1), side = word(2), number = word(3);
        const bool open = verb == "dooropen";
        std::vector<std::vector<std::string>> steps = {{"walk", point},
                                                       {"rotate", "-" + point},
                                                       {"anima", "doorknob" + side},
                                                       {"turn", "down"},
                                                       {"wait", open ? ".25" : ".75"}};
        if (open)
            steps.push_back({"fade", "out"});
        steps.push_back({"sounda", verb + number});
        if (!open)
            steps.push_back({"wait", "1"});
        steps.push_back({"turn", "home"});
        if (open)
            steps.push_back({"animate", "2"});
        for (auto& st : steps)
            queue_.push_back(ScriptEvent{std::move(st)});
        return;
    }
    queue_.push_back(ScriptEvent{std::move(words)});
}

void ScriptRunner::clear() {
    queue_.clear();
    running_ = false;
    waitingFor_.clear();
    waitLeft_ = -1;
}

void ScriptRunner::update(float seconds) {
    if (running_ && waitLeft_ >= 0) {
        waitLeft_ -= seconds;
        if (waitLeft_ < 0)
            finished("wait");
    }
    if (!running_ && !queue_.empty())
        start();
}

void ScriptRunner::finished(std::string_view verb) {
    if (!running_ || verb != waitingFor_)
        return;
    running_ = false;
    waitingFor_.clear();
    waitLeft_ = -1;
    queue_.pop_front();
    // The next runs on the next update, except a fade, which runs at once
    // (0x409c2a).
    if (!queue_.empty() && queue_.front().verb() == "fade")
        start();
}

void ScriptRunner::start() {
    running_ = true;
    const ScriptEvent e = queue_.front();
    if (onEvent)
        onEvent(e);
    if (e.verb() == "wait") {
        // Its seconds count down in update() (0x40b530).
        waitingFor_ = "wait";
        waitLeft_ = float(std::atof(e.arg(1).c_str()));
        return;
    }
    if (runOwn(e)) {
        waitingFor_ = e.verb();
        return finished(e.verb());
    }
    const std::string_view base = baseOfAsync(e.verb());
    ScriptEvent asked = e;
    if (!base.empty())
        asked.words.front() = std::string(base);
    waitingFor_ = asked.verb();
    const bool goesOn = host_.start(asked, base.empty());
    if (!goesOn || !base.empty())
        finished(asked.verb());
}

bool ScriptRunner::runOwn(const ScriptEvent& e) {
    const std::string& v = e.verb();
    auto number = [](const std::string& s) { return std::atoi(s.c_str()); };
    if (v == "tag") {
        // tag name n | +n | -n (0x40b08c); an unset tag counts as 0.
        const std::string name = fs::toLower(e.arg(1));
        const std::string& value = e.arg(2);
        int& t = state_.tags.try_emplace(name, 0).first->second;
        if (!value.empty() && (value[0] == '+' || value[0] == '-'))
            t += value[0] == '+' ? number(value.substr(1)) : -number(value.substr(1));
        else
            t = number(value);
    } else if (v == "removetag") {
        state_.tags.erase(fs::toLower(e.arg(1)));
    } else if (v == "add") {
        state_.items.insert(fs::toLower(e.arg(1)));
    } else if (v == "remove") {
        state_.items.erase(fs::toLower(e.arg(1)));
    } else if (v == "clear") {
        state_.items.clear();
    } else if (v == "act") {
        state_.act = fs::toLower(e.arg(1));
    } else if (v == "weather") {
        state_.values["weather"] = number(e.arg(1));
    } else if (v == "setchar") {
        character_ = fs::toLower(e.arg(1));
    } else {
        return false;
    }
    return true;
}

} // namespace reluva::awe
