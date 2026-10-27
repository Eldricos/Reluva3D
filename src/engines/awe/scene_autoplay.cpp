// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page driven by a tool instead of a player (reluva-tool
// awe-acts): a game started at an act or a developer's save, moved on by a
// player who only waits, skips the movies and takes the first choice, and
// the scene's hotspots tried one by one. What it logs is how the acts are
// checked; see docs/formats/awe/acts.md.

#include "engines/awe/viewers.h"

#include "common/fs.h"
#include "engines/awe/story.h"

namespace reluva::awe {

void ScenePage::startAt(const std::string& where) {
    // As a new game, but straight to the act ("act 04a" as 03A's Event
    // has it) or the save ("preload slot7, load" as the load screen does).
    newGame();
    runner_->clear();
    autoChoices_.clear();
    const std::string w = fs::toLower(where);
    if (w.rfind("slot", 0) == 0)
        runner_->run("preload " + w + ", load");
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
        if (dialogue_->choosing()) {
            // The first choice taken least often so far: the first, unless
            // it's offered again and again (a wrong answer that can be
            // tried once more), so every one is tried in turn.
            const int n = int(dialogue_->choices().size());
            int pick = -1;
            for (int i = 0; i < n; i++)
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
    // The hotspots and characters a player could click now, in the scene's
    // order.
    std::vector<const Ac2Record*> out;
    for (const Ac2Record* o : scene_.objects()) {
        if ((o->tag != "SPOT" && o->tag != "NPCX") || hidden(*o) || !testCondition(o->text("condition"), state_))
            continue;
        const Ac2Value* alternatives = o->field("listB");
        if (o->text("script").empty() && (!alternatives || alternatives->pairs.empty()))
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

bool ScenePage::click(size_t i) {
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

} // namespace reluva::awe
