// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page driven by a tool instead of a player (reluva-tool
// awe-acts): a game started at an act or a developer's save, moved on by a
// player who only waits, skips the movies and takes the first choice, and
// the scene's hotspots tried one by one. What it logs is how the acts are
// checked; see docs/formats/awe/acts.md.

#include "engines/awe/viewers.h"

#include "common/fs.h"
#include "engines/awe/savegame.h"
#include "engines/awe/story.h"

#include <algorithm>
#include <utility>

namespace reluva::awe {

void ScenePage::startAt(const std::string& where) {
    // As a new game, but straight to the act ("act 04a" as 03A's Event
    // has it) or the save ("preload slot7, load, fade in" as the load screen does).
    // "new": a new game as the program starts one, at the main menu.
    newGame();
    autoChoices_.clear();
    const std::string w = fs::toLower(where);
    if (w == "new")
        return;
    runner_->clear();
    if (w.rfind("slot", 0) == 0)
        runner_->run("preload " + w + ", load, fade in");
    else
        runner_->run("act " + w);
}

std::string ScenePage::autoplay() {
    if (yesNo_) {
        yesNo_.reset();
        return "yes/no box closed";
    }
    if (movie_) {
        const std::string name = movieName_;
        endMovie();
        return "movie " + name + " skipped";
    }
    if (dialogue_->active()) {
        if (dialogue_->choosing() && dialogue_->ready()) {
            // The tool's pick, else the first choice taken least often so
            // far: the first, unless it's offered again and again (a wrong
            // answer that can be tried once more), so every one is tried in
            // turn.
            const int n = int(dialogue_->choices().size());
            int pick = -1;
            if (chooser) {
                std::vector<std::string> texts;
                for (const DialogueManager::Choice& c : dialogue_->choices())
                    texts.push_back(c.text);
                pick = chooser(texts);
                if (pick >= n)
                    pick = -1;
            }
            const bool picked = pick >= 0;
            for (int i = 0; i < n && !picked; i++)
                if (pick < 0 || autoChoices_[dialogue_->choices()[size_t(i)].text] <
                                    autoChoices_[dialogue_->choices()[size_t(pick)].text])
                    pick = i;
            if (pick < 0)
                return {};
            const std::string text = dialogue_->choices()[size_t(pick)].text;
            if (!dialogue_->choose(pick))
                return {};
            autoChoices_[text]++;
            return "choice " + std::to_string(pick + 1) + " of " + std::to_string(n) + ": " + text;
        }
        if (cutLines)
            dialogue_->choose(-1); // the line cut short, once it can be
    }
    return {};
}

ScenePage::GameStatus ScenePage::status() const {
    GameStatus s;
    s.scene = state_.scene;
    s.act = state_.act;
    s.idle = runner_->idle();
    s.event = runner_->current();
    s.talking = dialogue_->active();
    s.movie = movie_ != nullptr;
    s.loading = load_.state != 0 || story_->loading();
    s.puzzle = puzzleActive() || slots_ || optionsMenu_;
    s.fading = fade_ != fadeTarget_;
    return s;
}

std::vector<const Ac2Record*> ScenePage::clickableObjects() const {
    // The hotspots, characters and pictures (items lying about) a player
    // could click now, in the scene's order: shown, with something to run,
    // and an outline or a rectangle the pick can find; and Antoinette,
    // whom items can be used on (uselist.txt).
    std::vector<const Ac2Record*> out;
    for (const Ac2Record* o : scene_.objects()) {
        if ((o->tag != "SPOT" && o->tag != "NPCX" && o->tag != "PCXX" && o->tag != "2DOB") || hidden(*o) ||
            !testCondition(o->text("condition"), state_))
            continue;
        if (!scripted(*o) && o->tag != "PCXX")
            continue;
        const Ac2Value* outline = o->field("listD");
        int l, t, r, b;
        if (o->tag == "2DOB" && !(outline && outline->numbers.size() >= 3) && !objectRect(*o, l, t, r, b))
            continue;
        out.push_back(o);
    }
    return out;
}

std::vector<std::string> ScenePage::clickables() const {
    // Their names, or their pictures' or models' files.
    std::vector<std::string> out;
    for (const Ac2Record* o : clickableObjects()) {
        const Ac2Record* hot = o->part("HOTS");
        std::string name = !o->text("name").empty() ? o->text("name") : hot ? hot->text("name") : "";
        if (name.empty() && !o->text("file").empty())
            name = fs::toLower(o->text("file").substr(o->text("file").find_last_of("\\/") + 1));
        if (name.empty())
            name = o->tag + "@" + std::to_string(o->i("left")) + "," + std::to_string(o->i("top"));
        out.push_back(name);
    }
    return out;
}

bool ScenePage::scripted(const Ac2Record& o) {
    const Ac2Value* alternatives = o.field("listB");
    return !o.text("script").empty() || (alternatives && !alternatives->pairs.empty());
}

bool ScenePage::clickRuns(size_t i) const {
    const std::vector<const Ac2Record*> objects = clickableObjects();
    return i < objects.size() && scripted(*objects[i]);
}

bool ScenePage::pointFor(size_t i, int& x, int& y) const {
    const std::vector<const Ac2Record*> objects = clickableObjects();
    if (i >= objects.size())
        return false;
    const Ac2Record* target = objects[i];
    // Where the game's mouse can be (handleEvent: x to 1004, y to 748, or
    // 758 in a conversation).
    const int maxX = 1004, maxY = dialogue_->active() ? 758 : 748;
    auto scan = [&](int left, int top, int right, int bottom, int step) {
        for (int sy = std::max(top, 0); sy < std::min(bottom, maxY + 1); sy += step)
            for (int sx = std::max(left, 0); sx < std::min(right, maxX + 1); sx += step)
                if (pickAllowed(sy) && pickAt(sx, sy) == target) {
                    x = sx, y = sy;
                    return true;
                }
        return false;
    };
    // Only its own rectangle or outline can be hit (0x40ef70); a
    // character without either by its bones, about where it stands.
    // (The outline when it has one: 0x40efa9.)
    int l, t, r, b;
    if (const Ac2Value* outline = target->field("listD"); outline && outline->numbers.size() >= 3) {
        l = kScreenW, t = kScreenH, r = 0, b = 0;
        for (const auto& [ux, uy] : outline->numbers) {
            const int px = int32_t(ux), py = int32_t(uy);
            l = std::min(l, px), r = std::max(r, px), t = std::min(t, py), b = std::max(b, py);
        }
        return scan(l, t, r + 1, b + 1, 2);
    }
    if (objectRect(*target, l, t, r, b))
        return scan(l, t, r + 1, b + 1, 2);
    if (target->tag != "NPCX" && target->tag != "PCXX")
        return false;
    for (const Character& c : characters_)
        if (c.record == target)
            if (const Ac2Record* camr = scene_.camera()) {
                const float feet[3] = {c.x, c.y, c.z};
                float fx, fy;
                if (SceneCamera::fromRecord(*camr).project(feet, kScreenW, kScreenH, fx, fy) &&
                    scan(int(fx) - 200, int(fy) - 500, int(fx) + 200, int(fy) + 20, 2))
                    return true;
            }
    return scan(0, 0, kScreenW, kScreenH, 4);
}

bool ScenePage::pointFor(size_t i, int& x, int& y, const std::string& holding) {
    // Only what the pick sees of the hand (itemInHand), and put back.
    const std::string was = std::exchange(state_.held, holding);
    const bool found = std::as_const(*this).pointFor(i, x, y);
    state_.held = was;
    return found;
}

bool ScenePage::click(size_t i) {
    frame_++;
    const std::vector<const Ac2Record*> objects = clickableObjects();
    if (i >= objects.size())
        return false;
    if (!runner_->idle())
        runner_->clear();
    releaseHeld(); // a click with nothing in hand
    runner_->setFromCharacter(objects[i]->tag == "NPCX");
    runScript(*objects[i]);
    return true;
}

std::vector<uint8_t> ScenePage::saveToMemory() const {
    return writeSaveGame(captureSave("search"));
}

bool ScenePage::loadFromMemory(const std::vector<uint8_t>& data) {
    // As the load screen loads a save: "preload", then "load, fade in".
    newGame();
    runner_->clear();
    autoChoices_.clear();
    if (!preloadData(data, "the save in memory"))
        return false;
    runner_->run("load, fade in");
    return true;
}

std::vector<std::string> ScenePage::flippableItems() {
    // The examine page's eye (0x41a860): an item with a back to look at
    // and an Examine Event for it.
    std::vector<std::string> out;
    for (const std::string& item : bagItems())
        if (itemInfo(item).event.size() > 1 && !itemPicture(item, "ex2").empty())
            out.push_back(item);
    return out;
}

bool ScenePage::flipItem(const std::string& item) {
    const std::vector<std::string> items = flippableItems();
    if (std::find(items.begin(), items.end(), item) == items.end())
        return false;
    if (!runner_->idle())
        runner_->clear();
    releaseHeld();
    ui_.examined = item;
    ui_.back = true;
    playSound("discovery");
    runFromInventory(itemInfo(item).event, true);
    return true;
}

std::vector<int> ScenePage::travelCars() {
    // The bar's cars (0x4177f0) a click would go to now: allowed, and not
    // the one Poirot is in.
    std::vector<int> out;
    if (!interfaceOn())
        return out;
    for (int car = 1; car <= 5; car++)
        if (carOn(car) && !inCar(car))
            out.push_back(car);
    return out;
}

bool ScenePage::travel(int car) {
    const std::vector<int> cars = travelCars();
    if (std::find(cars.begin(), cars.end(), car) == cars.end())
        return false;
    if (!runner_->idle())
        runner_->clear();
    releaseHeld();
    runner_->run("fade out, scene " + carScene(car) + " 0, fade in");
    return true;
}

std::vector<std::pair<std::string, std::string>> ScenePage::matchingPrints() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const GameState::Item& card : state_.items)
        if (const std::string& b = card.name; card.state == 6 && printRow(b) == 1) {
            const std::string a = fs::toLower(itemInfo(b).flipped);
            if (state_.itemState(a) == 6 && printRow(a) == 0 && !state_.tags.count(a + "=" + b))
                out.emplace_back(a, b);
        }
    return out;
}

bool ScenePage::comparePrintCards(const std::string& person, const std::string& thing) {
    // Each row's card shown (0x41e2e0 compares the two shown), then the
    // magnifier.
    std::vector<std::string> rows[2];
    for (const GameState::Item& i : state_.items)
        if (i.state == 6)
            rows[printRow(i.name)].push_back(i.name);
    const auto a = std::find(rows[0].begin(), rows[0].end(), person);
    const auto b = std::find(rows[1].begin(), rows[1].end(), thing);
    if (a == rows[0].end() || b == rows[1].end())
        return false;
    if (!runner_->idle())
        runner_->clear();
    releaseHeld();
    ui_.printAt[0] = int(a - rows[0].begin());
    ui_.printAt[1] = int(b - rows[1].begin());
    comparePrints();
    return true;
}

} // namespace reluva::awe
