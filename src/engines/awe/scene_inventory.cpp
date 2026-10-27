// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's inventory: an item in hand used on a hotspot or a
// character (0x41c050, Text\uselist.txt), and items combined
// (Text\combinelist.txt, 0x418cf0). See docs/formats/awe/script.md.

#include "engines/awe/viewers.h"

#include "common/fs.h"
#include "engines/awe/tables.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace reluva::awe {

namespace {

std::string unquoted(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

// "a, b, c": the names, lower case.
std::vector<std::string> names(const std::string& list) {
    std::vector<std::string> out;
    for (const Command& c : parseCommands(unquoted(list)))
        if (!c.words.empty())
            out.push_back(fs::toLower(c.words[0]));
    return out;
}

} // namespace

const Table& ScenePage::itemTable(const char* file) const {
    Table& t = itemTables_[file];
    if (t.headings.empty()) {
        std::vector<uint8_t> data;
        if (!ctx_.files.read(std::string("text/") + file, data))
            t = parseTable(decodeText(data));
    }
    return t;
}

bool ScenePage::useItem(const std::string& item, const Ac2Record& target) {
    // In hand (state 3) while the target's scripts are looked at; what
    // matches runs as the inventory's scripts do (0x419f20).
    const std::string name = fs::toLower(item);
    if (state_.held != name) {
        if (!state_.itemAvailable(name))
            return false;
        holdItem(name);
    }
    auto release = [&](int to) {
        state_.held.clear();
        if (to == 2)
            state_.itemStates.erase(name);
        else
            state_.itemStates[name] = to;
    };
    // The first of its conditional scripts whose condition has "use" in it
    // and holds (0x40e7e0 in use mode): the item is used up, unless the
    // script has "restore this".
    if (const Ac2Value* alternatives = target.field("listB"))
        for (const auto& [condition, run] : alternatives->pairs)
            if (fs::toLower(condition).find("use") != std::string::npos && testCondition(condition, state_)) {
                release(fs::toLower(run).find("restore this") != std::string::npos ? 2 : 4);
                logScript("use " + name + ": " + run);
                scriptObject_ = &target;
                runFromInventory(run, false);
                return true;
            }
    // A character, Antoinette among them (NPCX or PCXX, 0x41c0bb): uselist's
    // row for the item and its model ("antoinette" in
    // ...\antoinette_model.gr2), if its condition holds (0x41c4b0).
    if (target.tag == "NPCX" || target.tag == "PCXX") {
        const Table& t = itemTable("uselist.txt");
        const std::string model = fs::toLower(target.text("file"));
        for (size_t r = 0; r < t.rows.size(); r++) {
            if (fs::toLower(unquoted(t.cell(r, 0))) != name ||
                model.find(fs::toLower(unquoted(t.cell(r, 1))) + "_model") == std::string::npos ||
                !testCondition(unquoted(t.cell(r, 2)), state_))
                continue;
            release(4);
            logScript("use " + name + ": " + unquoted(t.cell(r, 3)));
            runFromInventory(unquoted(t.cell(r, 3)), false);
            return true;
        }
    }
    // Nothing for it: Antoinette says so, and the item stays in hand.
    logScript("use " + name + ": (can't)");
    runFromInventory("talk gen_cantuse 1", true);
    return false;
}

bool ScenePage::combineItems(const std::vector<std::string>& parts) {
    // As the combine page does with the parts in its boxes (0x418a50).
    for (std::string& box : ui_.boxes)
        box.clear();
    for (size_t i = 0; i < parts.size() && i < 4; i++)
        ui_.boxes[i] = fs::toLower(parts[i]);
    return combine();
}

std::vector<std::vector<std::string>> ScenePage::combinations() {
    // Each row of combinelist.txt that can be combined now: its parts all
    // in the bag, its condition holding.
    std::vector<std::vector<std::string>> out;
    const Table& t = itemTable("combinelist.txt");
    for (size_t r = 0; r < t.rows.size(); r++) {
        const std::vector<std::string> want = names(t.cell(r, 0));
        if (want.size() < 2 || want.size() > 4 || std::atoi(unquoted(t.cell(r, 3)).c_str()) == 0 ||
            !testCondition(unquoted(t.cell(r, 4)), state_))
            continue;
        if (std::all_of(want.begin(), want.end(), [&](const std::string& p) { return state_.itemAvailable(p); }))
            out.push_back(want);
    }
    return out;
}

std::vector<std::string> ScenePage::itemsFor(size_t i) {
    // The items its conditional scripts ask to be used on it ("use
    // waterglass"), or uselist.txt has for it, that Poirot has.
    std::vector<std::string> out;
    const std::vector<const Ac2Record*> objects = clickableObjects();
    if (i >= objects.size())
        return out;
    if (const Ac2Value* alternatives = objects[i]->field("listB"))
        for (const auto& [condition, run] : alternatives->pairs) {
            const std::string c = fs::toLower(condition);
            for (size_t at = c.find("use "); at != std::string::npos; at = c.find("use ", at + 4)) {
                if (at > 0 && std::isalpha(uint8_t(c[at - 1])))
                    continue;
                std::string item = c.substr(at + 4);
                item = item.substr(0, item.find_first_of(" ,|)"));
                if (state_.itemAvailable(item) && std::find(out.begin(), out.end(), item) == out.end())
                    out.push_back(item);
            }
        }
    if (objects[i]->tag == "NPCX" || objects[i]->tag == "PCXX") {
        const Table& t = itemTable("uselist.txt");
        const std::string model = fs::toLower(objects[i]->text("file"));
        for (size_t r = 0; r < t.rows.size(); r++) {
            const std::string item = fs::toLower(unquoted(t.cell(r, 0)));
            if (model.find(fs::toLower(unquoted(t.cell(r, 1))) + "_model") != std::string::npos &&
                state_.itemAvailable(item) && std::find(out.begin(), out.end(), item) == out.end())
                out.push_back(item);
        }
    }
    return out;
}

std::vector<std::string> ScenePage::usableItems(size_t i) {
    std::vector<std::string> out;
    const std::vector<const Ac2Record*> objects = clickableObjects();
    if (i >= objects.size())
        return out;
    for (const std::string& item : itemsFor(i))
        if (canUseOn(item, *objects[i]))
            out.push_back(item);
    return out;
}

bool ScenePage::useOn(const std::string& item, size_t i) {
    // The item picked up and clicked on the object.
    const std::vector<const Ac2Record*> objects = clickableObjects();
    if (i >= objects.size())
        return false;
    if (!runner_->idle())
        runner_->clear();
    releaseHeld();
    runner_->setFromCharacter(objects[i]->tag == "NPCX");
    return useItem(item, *objects[i]);
}

} // namespace reluva::awe
