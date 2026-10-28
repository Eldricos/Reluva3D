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

void addItem(GameState& state, const std::string& item, int st) {
    // 0x41bc10: passports and fingerprint cards go to their own pages.
    const std::string name = fs::toLower(item);
    if (name.empty())
        return;
    if (name.rfind("pass", 0) == 0)
        st = 5;
    else if (name.rfind("print", 0) == 0)
        st = 6;
    if (state.items.insert(name).second) {
        state.itemOrder.erase(std::remove(state.itemOrder.begin(), state.itemOrder.end(), name), state.itemOrder.end());
        state.itemOrder.push_back(name);
    }
    if (st == 2)
        state.itemStates.erase(name);
    else
        state.itemStates[name] = st;
    if (st != 1 && st != 2)
        return;
    // Its slot: the one it had if that's free, else the lowest free one
    // (0x41bfb0); the window turns to its page.
    auto taken = [&](int slot) {
        for (const auto& [other, s] : state.itemSlots) {
            const int os = state.itemState(other);
            if (other != name && s == slot && os >= 1 && os <= 3)
                return true;
        }
        return false;
    };
    auto at = state.itemSlots.find(name);
    if (at == state.itemSlots.end() || taken(at->second)) {
        int slot = 0;
        while (taken(slot))
            slot++;
        state.itemSlots[name] = slot;
    }
    state.itemPage = state.itemSlots[name] / 16;
    // Just taken: the one before stops blinking (0x41ba80).
    if (st == 1) {
        if (!state.fresh.empty() && state.fresh != name && state.itemState(state.fresh) == 1)
            state.itemStates.erase(state.fresh);
        state.fresh = name;
    }
}

void addTime(GameState& state, int seconds) {
    // Timers stop at 32000 (0x4125c0).
    for (auto& [name, value] : state.timers)
        if (value < 32000)
            value = std::min(32000, value + seconds);
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
    // 0x408c80: every script given to the runner makes the player the
    // character its events act on again (0x408cd8), whatever an earlier
    // one's "setchar" chose; a character's own runner, that character
    // (0x40598f).
    character_ = owner_;
    // A condition in brackets ("(tag x >3, has y)") decides, there and
    // then, whether the commands after it are added, up to the next one.
    bool doIt = true, open = false;
    std::string condition;
    for (Command& c : parseCommands(commands)) {
        std::string text;
        for (const std::string& w : c.words)
            text += (text.empty() ? "" : " ") + w;
        if (!open) {
            const size_t at = text.find('(');
            if (at == std::string::npos) {
                if (doIt)
                    add(std::move(c.words), 0);
                continue;
            }
            condition = text.substr(at + 1);
            open = true;
        } else {
            condition += ", " + text;
        }
        if (const size_t close = condition.find(')'); close != std::string::npos) {
            condition.resize(close);
            doIt = testCondition(condition, state_);
            open = false;
        }
    }
}

void ScriptRunner::runNext(std::string_view commands) {
    if (queue_.empty())
        return run(commands);
    // Added at the back, then moved to just after the front.
    std::deque<ScriptEvent> rest(queue_.begin() + 1, queue_.end());
    queue_.erase(queue_.begin() + 1, queue_.end());
    run(commands);
    queue_.insert(queue_.end(), rest.begin(), rest.end());
}

void ScriptRunner::fastForward() {
    for (int guard = 0; !idle() && guard < 1000; guard++) {
        if (!running_)
            start();
        else
            finished(waitingFor_);
    }
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

std::string ScriptRunner::current() const {
    std::string out;
    if (!queue_.empty())
        for (const std::string& w : queue_.front().words)
            out += (out.empty() ? "" : " ") + w;
    return out;
}

void ScriptRunner::clear() {
    queue_.clear();
    locked_ = false;
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
    lastDone_ = waitingFor_;
    waitingFor_.clear();
    waitLeft_ = -1;
    queue_.pop_front();
    if (queue_.empty())
        locked_ = false; // 0x409c4e
    if (queue_.empty() && onEmpty)
        onEmpty();
    // The next runs on the next update, except a fade, which runs at once
    // (0x409c2a).
    if (!queue_.empty() && queue_.front().verb() == "fade")
        start();
}

void ScriptRunner::start() {
    running_ = true;
    const ScriptEvent e = queue_.front();
    // Clicks are refused from here on, except during walks and turns, and
    // the event right after an "unlock" (0x409d06; +0x10 is the verb last
    // done).
    if (e.verb() != "walk" && e.verb() != "run" && e.verb() != "rotate" && lastDone_ != "unlock")
        locked_ = true;
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
    const bool goesOn = host_.start(*this, asked, base.empty());
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
        // In the bag (0x419ca0 with state 2).
        addItem(state_, e.arg(1), 2);
    } else if (v == "remove") {
        // Gone (0x41c260); "remove all": everything carried taken away, to
        // come back with "restore all" (0x41c3e0).
        const std::string item = fs::toLower(e.arg(1));
        if (item == "all") {
            state_.held.clear();
            for (const std::string& i : state_.items)
                if (state_.itemAvailable(i) || state_.itemState(i) == 3)
                    state_.itemStates[i] = 7;
        } else {
            state_.items.erase(item);
            state_.itemStates.erase(item);
            if (state_.held == item)
                state_.held.clear();
        }
    } else if (v == "restore") {
        // Back in the bag: a used-up item (0x41c320); "restore all" what
        // "remove all" took, the small key going and "securityunlocked"
        // with it (0x41c430).
        const std::string item = fs::toLower(e.arg(1));
        if (item == "all") {
            for (auto it = state_.itemStates.begin(); it != state_.itemStates.end();)
                it = it->second == 7 ? state_.itemStates.erase(it) : std::next(it);
            state_.items.erase("keysmall");
            state_.itemStates.erase("keysmall");
            state_.tags.erase("securityunlocked");
        } else if (item != "this" && state_.itemState(item) == 4) {
            state_.itemStates.erase(item);
        }
    } else if (v == "use") {
        // Used up (0x41c160): kept, for "used" and "restore".
        const std::string item = fs::toLower(e.arg(1));
        const int st = state_.itemState(item);
        if (st == 1 || st == 2 || st == 3) {
            state_.itemStates[item] = 4;
            if (state_.held == item)
                state_.held.clear();
        }
    } else if (v == "clear") {
        state_.items.clear();
        state_.itemStates.clear();
        state_.held.clear();
        state_.itemSlots.clear();
        state_.itemOrder.clear();
        state_.itemPage = 0;
        state_.fresh.clear();
    } else if (v == "timer") {
        // A new timer at 0 (0x410ac0), after any of the same name.
        state_.timers.emplace_back(fs::toLower(e.arg(1)), 0);
    } else if (v == "time") {
        // Time passes (0x4125c0): every timer goes on by n seconds.
        addTime(state_, number(e.arg(1)));
    } else if (v == "reset") {
        // A new game (0x411aa0): the story state and the inventory
        // emptied, the act back to the first.
        state_.tags.clear();
        state_.texts.clear();
        state_.timers.clear();
        state_.items.clear();
        state_.itemStates.clear();
        state_.held.clear();
        state_.itemSlots.clear();
        state_.itemOrder.clear();
        state_.itemPage = 0;
        state_.fresh.clear();
        state_.act = "01a";
    } else if (v == "savescene") {
        // savescene this|<scene> [wp] [flag] (0x40acd3): for "gotoscene".
        const std::string scene = fs::toLower(e.arg(1));
        state_.texts["savedscene"] = (scene == "this" ? fs::toLower(state_.scene) : scene + ".ac2") + " " +
                                     (e.arg(2).empty() ? "-1" : e.arg(2)) + " " + (e.arg(3).empty() ? "0" : e.arg(3));
    } else if (v == "weather") {
        state_.values["weather"] = number(e.arg(1));
    } else if (v == "advance" || v == "terminate" || v == "juncture" || v == "leavebox" || v == "nobox") {
        // The dialogue's words, read from a line's Povent where it's set
        // (dialogue.cpp); as events they're over at once (0x409fa3,
        // 0x40b28a, 0x40a3ae...).
    } else if (v == "lock" || v == "unlock") {
        // Clicks refused, or taken while what follows runs (0x40a5e4).
        locked_ = v == "lock";
    } else if (v == "setchar") {
        character_ = fs::toLower(e.arg(1));
    } else {
        return false;
    }
    return true;
}

} // namespace reluva::awe
