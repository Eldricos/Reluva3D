// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/campaign.h"

#include <cstdlib>

#include "engines/stgenerations/debug.h"
#include "common/serializer.h"

namespace reluva::stgenerations {

namespace {

int toInt(const std::vector<std::string> &words, size_t i) {
    return i < words.size() ? std::atoi(words[i].c_str()) : 0;
}

std::string word(const std::vector<std::string> &words, size_t i) {
    return i < words.size() ? text::toLower(words[i]) : std::string();
}

template<typename T>
void syncSet(Serializer &s, std::set<T> &values, void (*sync)(Serializer &, T &)) {
    std::vector<T> list(values.begin(), values.end());
    s.syncVector(list, sync);
    if (s.isLoading())
        values = std::set<T>(list.begin(), list.end());
}

void syncInt(Serializer &s, int &v) {
    s.syncAsInt(v);
}
void syncString(Serializer &s, std::string &v) {
    s.syncString(v);
}

} // namespace

bool Campaign::load(const std::string &text) {
    _sections.clear();
    std::vector<std::vector<std::string>> *section = nullptr;
    for (std::string line : text::split(text, '\n')) {
        size_t comment = line.find(';');
        if (comment != std::string::npos)
            line.resize(comment);
        line = text::trim(line);
        if (line.empty())
            continue;
        if (line[0] == '[') {
            size_t end = line.find(']');
            section = &_sections[text::toLower(line.substr(1, end == std::string::npos ? end : end - 1))];
            continue;
        }
        if (!section)
            continue;
        std::vector<std::string> words;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                i++;
            size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t')
                i++;
            if (i > start)
                words.push_back(line.substr(start, i - start));
        }
        section->push_back(words);
    }
    if (!_sections.count("start")) {
        _sections.clear();
        return false;
    }
    return true;
}

std::vector<std::string> Campaign::getSectionNames() const {
    std::vector<std::string> names;
    for (const auto &section : _sections)
        names.push_back(section.first);
    return names;
}

void Campaign::start(const std::string &section) {
    _flags.clear();
    _played.clear();
    _won.clear();
    _lost.clear();
    _destroyed.clear();
    _objectFlags.clear();
    _stack.clear();
    _mission = 0;
    _combat = 0;
    _replayCombat = false;
    _clue = false;
    _onTransport.clear();
    _onTactical.clear();
    _deadline = 0;
    _running = true;
    jump(section);
}

void Campaign::jump(const std::string &section) {
    _section = text::toLower(section);
    _line = 0;
    if (!_sections.count(_section)) {
        log::warning("Campaign: no section [%s]", section.c_str());
        _running = false;
    }
}

void Campaign::restartSection() {
    if (_combat)
        _replayCombat = true;
    else
        _line = 0;
}

void Campaign::setFlag(int flag, bool value) {
    if (value)
        _flags.insert(flag);
    else
        _flags.erase(flag);
}

Campaign::Action Campaign::next() {
    Action action;
    if (_replayCombat) {
        _replayCombat = false;
        action.type = Action::kSpaceCombat;
        action.number = _combat;
        return action;
    }
    // A bound on lines run without an action, in case of a loop.
    for (int steps = 0; _running && !_mission && !_combat && steps < 10000; steps++) {
        const std::vector<std::vector<std::string>> &lines = _sections[_section];
        if (_line >= lines.size()) {
            // The original waits on the bridge here, for the player to
            // beam down or engage; with no bridge, nothing more can happen.
            if (_world && _world->isBridgeShown()) {
                action.type = Action::kBridge;
                return action;
            }
            log::warning("Campaign: [%s] ends without going on", _section.c_str());
            _running = false;
            break;
        }
        std::string section = _section;
        size_t line = _line++;
        switch (execute(lines[line], action)) {
        case kAction:
            return action;
        case kBlocked:
            if (_section == section)
                _line = line;
            action.type = Action::kBridge;
            return action;
        case kNext:
            break;
        }
        // While the bridge is up, one command a frame (GM state 7).
        if (_world && _world->isBridgeShown()) {
            action.type = Action::kBridge;
            return action;
        }
    }
    return Action();
}

void Campaign::missionEnded(bool accomplished, bool clue) {
    if (!_mission)
        return;
    _played.insert(_mission);
    (accomplished ? _won : _lost).insert(_mission);
    _clue = clue;
    _mission = 0;
    jump(accomplished ? _wonSection : _lostSection);
}

void Campaign::combatEnded(CombatResult result) {
    if (!_combat)
        return;
    int fleet = _combat;
    _combat = 0;
    // Combats are kept apart from away missions in the results.
    _played.insert(100 + fleet);
    (result == kCombatWon ? _won : _lost).insert(100 + fleet);
    if (result == kEnterpriseDestroyed) {
        jump("EnterpriseDestroyed");
        return;
    }
    if (_world) {
        // A fleet beaten stays beaten; a roaming one that got away attacks
        // next time.
        if (result == kCombatWon)
            _world->bridgeCommand("setfleetflag", {std::to_string(fleet), "8", "TRUE"});
        if (fleet > 12 && fleet < 25)
            _world->bridgeCommand("fleetalert", {std::to_string(fleet), "Red"});
    }
    jump(result == kCombatWon ? _wonSection : _lostSection);
}

void Campaign::transport() {
    if (!_onTransport.empty())
        jump(_onTransport);
}

void Campaign::startCombat(int fleet) {
    if (fleet < 0) {
        if (!_onTactical.empty())
            jump(_onTactical);
    } else if (fleet > 12 && fleet < 25) {
        // The roaming fleets' sections end with Pop, back to the line the
        // script was on (0x41447D).
        _stack.push_back({_section, (int)_line});
        jump(text::format("FightFleet%d", fleet));
    } else {
        jump(text::format("SpaceCombat%d", fleet));
    }
}

bool Campaign::isWaitOver(const std::string &kind) const {
    // Without the bridge every Wait passes (0x4183E1).
    if (!_world || !_world->isBridgeShown())
        return true;
    if (kind == "queaudio" || kind == "quemusic")
        return _world->isAudioQueueEmpty();
    if (kind == "timer")
        return _world->getStardate() >= _deadline - 1e-6 && _world->isEnterpriseIdle();
    if (kind == "warp")
        return !_world->isWarping();
    if (kind == "zoom")
        return !_world->isZooming();
    return false;
}

bool Campaign::test(const std::vector<std::string> &words, size_t &i) const {
    while (i < words.size()) {
        std::string w = word(words, i);
        if (w.compare(0, 2, "if") != 0)
            return true;
        bool pass;
        if (w == "ifflag") {
            pass = _flags.count(toInt(words, i + 1)) != 0;
            i += 2;
        } else if (w == "ifnotflag") {
            pass = !_flags.count(toInt(words, i + 1));
            i += 2;
        } else if (w == "ifactiongameplayed" || w == "ifactiongamewon" || w == "ifactiongamelost" ||
                   w == "ifspacecombatplayed" || w == "ifspacecombatwon" || w == "ifspacecombatlost") {
            int n = toInt(words, i + 1) + (w.compare(2, 5, "space") == 0 ? 100 : 0);
            const std::set<int> &set = text::hasSuffixIgnoreCase(w, "played") ? _played
                                       : text::hasSuffixIgnoreCase(w, "won")  ? _won
                                                                              : _lost;
            pass = set.count(n) != 0;
            i += 2;
        } else if (w == "ifsunsdestroyed") {
            // Equal, not at least (0x41732B).
            pass = (int)_destroyed.size() == toInt(words, i + 1);
            i += 2;
        } else if (w == "ifenterpriseat") {
            pass = _world && _world->isEnterpriseAt(word(words, i + 1));
            i += 2;
        } else if (w == "ifobjectflag" || w == "ifnotobjectflag") {
            bool set = _world ? _world->getObjectFlag(word(words, i + 1), toInt(words, i + 2))
                              : _objectFlags.count(word(words, i + 1) + " " + word(words, i + 2)) != 0;
            pass = w == "ifobjectflag" ? set : !set;
            i += 3;
        } else if (w == "ifclue") {
            // The last away mission left a clue (0x416FFB).
            pass = _clue;
            i += 1;
        } else {
            log::warning("Campaign: unknown condition %s", words[i].c_str());
            return false;
        }
        if (!pass)
            return false;
    }
    return true;
}

Campaign::Result Campaign::execute(const std::vector<std::string> &words, Action &action) {
    size_t i = 0;
    if (!test(words, i) || i >= words.size())
        return kNext;
    std::string command = word(words, i);
    size_t a = i + 1; // the first argument
    std::vector<std::string> args(words.begin() + a, words.end());

    if (command == "jump") {
        jump(words.size() > a ? words[a] : std::string());
    } else if (command == "jumprandom") {
        int count = (int)(words.size() - a);
        if (count > 0)
            jump(words[a + (_random ? _random(count) : 0)]);
    } else if (command == "call") {
        // The caller's place stays on the Call (0x4118E9 doesn't advance it).
        _stack.push_back({_section, (int)_line - 1});
        jump(words.size() > a ? words[a] : std::string());
    } else if (command == "pop" || command == "return") {
        if (_stack.empty()) {
            log::warning("Campaign: %s with nothing to return to in [%s]", words[i].c_str(), _section.c_str());
            _running = false;
        } else {
            // Pop goes back to the line that was running; Return past it.
            _section = _stack.back().first;
            _line = _stack.back().second + (command == "return" ? 1 : 0);
            _stack.pop_back();
        }
    } else if (command == "wait") {
        if (!isWaitOver(word(words, a)))
            return kBlocked;
    } else if (command == "sdtimer") {
        if (_world)
            _deadline = _world->getStardate() + std::atof(word(words, a).c_str());
    } else if (command == "ontransport" || command == "ontactical") {
        std::string target = word(words, a);
        if (!_world) {
            // No bridge to beam down from: do it now; and never engage.
            if (command == "ontransport")
                jump(target);
            return kNext;
        }
        (command == "ontransport" ? _onTransport : _onTactical) = target;
        _world->bridgeCommand(command, args);
    } else if (command == "spacecombat") {
        if (!_world) {
            log::debug(1, "Campaign: space combat %d with no bridge; taking it as lost", toInt(words, a));
            jump(word(words, a + 2));
            return kNext;
        }
        _combat = toInt(words, a);
        _wonSection = word(words, a + 1);
        _lostSection = word(words, a + 2);
        action.type = Action::kSpaceCombat;
        action.number = _combat;
        return kAction;
    } else if (command == "actiongame") {
        _mission = toInt(words, a);
        _wonSection = word(words, a + 1);
        _lostSection = word(words, a + 2);
        action.type = Action::kActionGame;
        action.number = _mission;
        return kAction;
    } else if (command == "setflag") {
        setFlag(toInt(words, a), text::equalsIgnoreCase(word(words, a + 1), "true"));
    } else if (command == "setobjectflag") {
        std::string key = word(words, a) + " " + word(words, a + 1);
        if (word(words, a + 2) == "true")
            _objectFlags.insert(key);
        else
            _objectFlags.erase(key);
        if (_world)
            _world->bridgeCommand(command, args);
    } else if (command == "setactiongameplayed") {
        _played.insert(toInt(words, a));
    } else if (command == "destroy") {
        _destroyed.insert(word(words, a));
        if (_world)
            _world->bridgeCommand(command, args);
    } else if (command == "intro") {
        action.type = Action::kIntro;
        return kAction;
    } else if (command == "movie") {
        action.type = Action::kMovie;
        action.number = toInt(words, a);
        return kAction;
    } else if (command == "scrolltext") {
        action.type = Action::kScrollText;
        action.number = toInt(words, a);
        return kAction;
    } else if (command == "autosave") {
        action.type = Action::kAutoSave;
        return kAction;
    } else if (command == "options" || command == "endgame") {
        // Both end the game: the menu with nothing to go back to, or the credits.
        action.type = command == "options" ? Action::kOptions : Action::kEndGame;
        _running = false;
        return kAction;
    } else if (_world && command != "sectimer") {
        // Anything else drives the bridge: the map, its sounds and timers.
        _world->bridgeCommand(command, args);
    }
    return kNext;
}

void Campaign::sync(Serializer &s) {
    if (s.isLoading())
        _replayCombat = false;
    s.syncString(_section);
    uint32_t line = (uint32_t)_line;
    s.syncAsUint32(line);
    _line = line;
    s.syncAsBool(_running);
    s.syncAsInt(_mission);
    s.syncAsInt(_combat);
    s.syncString(_wonSection);
    s.syncString(_lostSection);
    syncSet<int>(s, _flags, syncInt);
    syncSet<int>(s, _played, syncInt);
    syncSet<int>(s, _won, syncInt);
    syncSet<int>(s, _lost, syncInt);
    syncSet<std::string>(s, _destroyed, syncString);
    syncSet<std::string>(s, _objectFlags, syncString);
    s.syncVector(_stack, [](Serializer &ser, std::pair<std::string, int> &entry) {
        ser.syncString(entry.first);
        ser.syncAsInt(entry.second);
    });
    s.syncAsBool(_clue);
    s.syncString(_onTransport);
    s.syncString(_onTactical);
    s.syncAsDouble(_deadline);
    if (s.isLoading() && _running && !_sections.count(_section))
        s.fail();
}

} // namespace reluva::stgenerations
