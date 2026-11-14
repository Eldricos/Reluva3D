// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game/script.h"

#include "engines/awe/games.h"

#include "common/fs.h"
#include "engines/awe/formats/tables.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::awe {

const std::string &ScriptEvent::arg(size_t i) const {
    static const std::string kNone;
    return i < words.size() ? words[i] : kNone;
}

void addItem(GameState &state, const std::string &item, int st, int slot) {
    // 0x41bc10: a new one each time, whatever's there already; passports
    // and fingerprint cards go to their own pages.
    const std::string name = fs::toLower(item);
    if (name.empty())
        return;
    if (name.rfind("pass", 0) == 0)
        st = 5;
    else if (name.rfind("print", 0) == 0)
        st = 6;
    GameState::Item added{name, st, 0};
    // Its slot: the one given (a saved game's), else the lowest free one
    // (0x41bfb0); the window turns to its page.
    if (st == 1 || st == 2)
        added.slot = slot >= 0 ? slot : state.freeSlot();
    state.items.push_back(added);
    if (st != 1 && st != 2)
        return;
    state.itemPage = added.slot / 16;
    // Just taken: the one before stops blinking (0x41ba80).
    if (st == 1) {
        for (GameState::Item &i : state.items)
            if (i.state == 1 && &i != &state.items.back())
                i.state = 2;
        state.fresh = name;
    }
}

void addTime(GameState &state, int seconds) {
    // Timers stop at 32000 (0x4125c0).
    for (auto &[name, value] : state.timers)
        if (value < 32000)
            value = std::min(32000, value + seconds);
}

std::string_view ScriptRunner::baseOfAsync(std::string_view verb) {
    // The handlers that compare both forms (0x409ce0): the "a" form reports
    // the event done as soon as it has started what it asks.
    static constexpr std::string_view kAsync[] = {"walka", "runa",  "talka",   "fadea", "sounda",
                                                  "anima", "turna", "rotatea", "playa"};
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
    _character = _owner;
    // A condition in brackets ("(tag x >3, has y)") decides, there and
    // then, whether the commands after it are added, up to the next one.
    bool doIt = true;
    bool open = false;
    std::string condition;
    for (Command &c : parseCommands(commands)) {
        std::string text;
        for (const std::string &w : c.words)
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
            doIt = testCondition(condition, _state);
            open = false;
        }
    }
}

void ScriptRunner::runNext(std::string_view commands) {
    if (_queue.empty())
        return run(commands);
    // Added at the back, then moved to just after the front.
    std::deque<ScriptEvent> rest(_queue.begin() + 1, _queue.end());
    _queue.erase(_queue.begin() + 1, _queue.end());
    run(commands);
    _queue.insert(_queue.end(), rest.begin(), rest.end());
}

void ScriptRunner::fastForward() {
    for (int guard = 0; !idle() && guard < 1000; guard++) {
        if (!_running)
            start();
        else
            finished(_waitingFor);
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
    const std::string &verb = words.front();
    if (verb == "script" && depth < 8) {
        // Another file's commands, one a line (0x409600).
        for (Command &c : parseCommands(_host.scriptText(word(1))))
            add(std::move(c.words), depth + 1);
        return;
    }
    if (verb == "dooropen" || verb == "doorlocked") {
        // The macros of 0x4091f0 and 0x409320.
        const std::string point = word(1);
        const std::string side = word(2);
        const std::string number = word(3);
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
        for (auto &st : steps)
            _queue.push_back(ScriptEvent{std::move(st)});
        return;
    }
    _queue.push_back(ScriptEvent{std::move(words)});
}

std::string ScriptRunner::current() const {
    std::string out;
    if (!_queue.empty())
        for (const std::string &w : _queue.front().words)
            out += (out.empty() ? "" : " ") + w;
    return out;
}

void ScriptRunner::clear() {
    _queue.clear();
    _locked = false;
    _running = false;
    _waitingFor.clear();
    _waitLeft = -1;
}

void ScriptRunner::update(float seconds) {
    if (_running && _waitLeft >= 0) {
        _waitLeft -= seconds;
        if (_waitLeft < 0)
            finished("wait");
    }
    if (!_running && !_queue.empty())
        start();
}

void ScriptRunner::finished(std::string_view verb) {
    if (!_running || verb != _waitingFor)
        return;
    _running = false;
    _lastDone = _waitingFor;
    _waitingFor.clear();
    _waitLeft = -1;
    _queue.pop_front();
    if (_queue.empty())
        _locked = false; // 0x409c4e
    if (_queue.empty() && onEmpty)
        onEmpty();
    // The next runs on the next update, except a fade, which runs at once
    // (0x409c2a).
    if (!_queue.empty() && _queue.front().verb() == "fade")
        start();
}

void ScriptRunner::start() {
    _running = true;
    const ScriptEvent e = _queue.front();
    // Clicks are refused from here on, except during walks and turns, and
    // the event right after an "unlock" (0x409d06; +0x10 is the verb last
    // done).
    if (e.verb() != "walk" && e.verb() != "run" && e.verb() != "rotate" && _lastDone != "unlock")
        _locked = true;
    if (onEvent)
        onEvent(e);
    if (e.verb() == "wait") {
        // Its seconds count down in update() (0x40b530).
        _waitingFor = "wait";
        _waitLeft = float(std::atof(e.arg(1).c_str()));
        return;
    }
    if (runOwn(e)) {
        _waitingFor = e.verb();
        return finished(e.verb());
    }
    const std::string_view base = baseOfAsync(e.verb());
    ScriptEvent asked = e;
    if (!base.empty())
        asked.words.front() = std::string(base);
    _waitingFor = asked.verb();
    const bool goesOn = _host.start(*this, asked, base.empty());
    if (!goesOn || !base.empty())
        finished(asked.verb());
}

bool ScriptRunner::runOwn(const ScriptEvent &e) {
    const std::string &v = e.verb();
    auto number = [](const std::string &s) { return std::atoi(s.c_str()); };
    if (v == "tag") {
        // tag name n | +n | -n (0x40b08c); an unset tag counts as 0.
        const std::string name = fs::toLower(e.arg(1));
        const std::string &value = e.arg(2);
        int &t = _state.tags.try_emplace(name, 0).first->second;
        if (!value.empty() && (value[0] == '+' || value[0] == '-'))
            t += value[0] == '+' ? number(value.substr(1)) : -number(value.substr(1));
        else
            t = number(value);
    } else if (v == "removetag") {
        _state.tags.erase(fs::toLower(e.arg(1)));
    } else if (v == "add") {
        // In the bag (0x419ca0 with state 2).
        addItem(_state, e.arg(1), 2);
    } else if (v == "remove") {
        // Gone, the first of the name (0x41c260); "remove all": the one in
        // hand put back, then everything in the bag taken away, to come
        // back with "restore all" (0x41c3e0).
        const std::string item = fs::toLower(e.arg(1));
        if (item == "all") {
            _state.held.clear();
            _state.fresh.clear();
            for (GameState::Item &i : _state.items)
                if (i.state >= 1 && i.state <= 3)
                    i.state = 7;
        } else {
            for (auto it = _state.items.begin(); it != _state.items.end(); ++it)
                if (it->name == item) {
                    if (it->state == 3)
                        _state.held.clear();
                    _state.items.erase(it);
                    break;
                }
        }
    } else if (v == "restore") {
        // Back in the bag, in the lowest free slot: the first used-up one
        // of the name (0x41c320); "restore all" what "remove all" took,
        // the small key going and "securityunlocked" with it (0x41c430).
        const std::string item = fs::toLower(e.arg(1));
        if (item == "all") {
            for (GameState::Item &i : _state.items)
                if (i.state == 7) {
                    i.slot = _state.freeSlot();
                    i.state = 2;
                }
            for (auto it = _state.items.begin(); it != _state.items.end(); ++it)
                if (it->name == "keysmall") {
                    _state.items.erase(it);
                    break;
                }
            _state.tags.erase("securityunlocked");
        } else if (item != "this") {
            if (GameState::Item *i = _state.findItem(item, {4})) {
                i->slot = _state.freeSlot();
                i->state = 2;
            }
        }
    } else if (v == "use") {
        // Used up, the first of the name in the bag, just taken or in hand
        // (0x41c160): kept, for "used" and "restore".
        if (GameState::Item *i = _state.findItem(e.arg(1), {1, 2, 3})) {
            if (i->state == 3)
                _state.held.clear();
            if (i->state == 1)
                _state.fresh.clear();
            i->state = 4;
        }
    } else if (v == "clear") {
        _state.items.clear();
        _state.held.clear();
        _state.itemPage = 0;
        _state.fresh.clear();
    } else if (v == "timer") {
        // A new timer at 0 (0x410ac0), after any of the same name.
        _state.timers.emplace_back(fs::toLower(e.arg(1)), 0);
    } else if (v == "time") {
        // Time passes (0x4125c0): every timer goes on by n seconds.
        addTime(_state, number(e.arg(1)));
    } else if (v == "reset") {
        // A new game (0x411aa0): the story state and the inventory
        // emptied, the conversations' progress with it (+0x2b4), the act
        // back to the first.
        _state.tags.clear();
        _state.texts.clear();
        _state.timers.clear();
        _state.talks.clear();
        _state.items.clear();
        _state.held.clear();
        _state.itemPage = 0;
        _state.fresh.clear();
        _state.act = "01a";
    } else if (v == "savescene") {
        // savescene this|<scene> [wp] [flag] (0x40acd3): for "gotoscene".
        const std::string scene = fs::toLower(e.arg(1));
        _state.texts["savedscene"] = (scene == "this" ? fs::toLower(_state.scene) : scene + ".ac2") + " " +
                                     (e.arg(2).empty() ? "-1" : e.arg(2)) + " " + (e.arg(3).empty() ? "0" : e.arg(3));
    } else if (v == "weather") {
        _state.values["weather"] = number(e.arg(1));
    } else if (v == "advance" || v == "terminate" || v == "juncture" || v == "leavebox" || v == "nobox") {
        // The dialogue's words, read from a line's Povent where it's set
        // (dialogue.cpp); as events they're over at once (0x409fa3,
        // 0x40b28a, 0x40a3ae...).
    } else if (v == "lock" || v == "unlock") {
        // Clicks refused, or taken while what follows runs (0x40a5e4).
        _locked = v == "lock";
    } else if (v == "setchar") {
        _character = fs::toLower(e.arg(1));
    } else if (!(_state.game && _state.game->verb && _state.game->verb(_state, e.words))) {
        // (A verb of the game's own is done there: attwn's "inc".)
        return false;
    }
    return true;
}

} // namespace reluva::awe
