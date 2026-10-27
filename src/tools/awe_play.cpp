// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool awe-acts -solve and -play: a player that looks for a way
// through the game, and plays a way found back. See
// docs/formats/awe/acts.md ("Finding a playthrough").
//
// A playthrough is a list of what a player does, one a line:
//
//   new                                   a new game, at the main menu
//   click|<scene>|<object>|<n>|<choices>  the n-th clickable of that name clicked
//   use|<scene>|<object>|<n>|<choices>|<item>
//   combine|<scene>|||<choices>|<item>+<item>...
//   flip|<scene>|||<choices>|<item>       an item's back looked at
//   travel|<scene>|<car>||<choices>       the bar's car (1 baggage .. 5 Athens)
//   solve|<scene>|<puzzle>||<choices>     the puzzle screen's inputs that solve it
//   run|<scene>|<script>||<choices>       a script run (not a player's: for looking into things)
//
// A seventh field, "<object>#<n>", clicks that object as soon as it can
// be clicked while what the action started still runs (the game lets a
// click in between a script's "unlock" and "lock").
//
// <choices> are the dialogue's choices taken while it ran ("2.1.1": the
// second offered, then the first...); past them, the choice taken least
// in that action. Each action is followed by the game running till it
// settles.

#include "tools/awe_tool.h"

#include "common/archive.h"
#include "common/fs.h"
#include "engines/awe/story.h"
#include "engines/awe/tables.h"
#include "engines/awe/viewers.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace reluva::awe {

namespace {

struct Action {
    std::string kind, scene, target, item;
    int n = 0;
    std::vector<int> choices;   // taken (or to take) at each choice
    std::vector<int> offered;   // how many there were at each (not written)
    std::string then;           // clicked while it runs ("name#n")
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::string toLine(const Action& a) {
    if (a.kind == "new")
        return "new";
    std::string choices;
    for (int c : a.choices)
        choices += (choices.empty() ? "" : ".") + std::to_string(c + 1);
    std::string line = a.kind + "|" + a.scene + "|" + a.target + "|" + (a.target.empty() ? "" : std::to_string(a.n)) +
                       "|" + choices;
    if (!a.item.empty() || !a.then.empty())
        line += "|" + a.item;
    if (!a.then.empty())
        line += "|" + a.then;
    return line;
}

bool fromLine(const std::string& line, Action& a) {
    const std::vector<std::string> f = split(line, '|');
    if (f.empty() || f[0].empty() || f[0][0] == '#')
        return false;
    a = Action();
    a.kind = f[0];
    if (f.size() > 1)
        a.scene = f[1];
    if (f.size() > 2)
        a.target = f[2];
    if (f.size() > 3 && !f[3].empty())
        a.n = std::atoi(f[3].c_str());
    if (f.size() > 4 && !f[4].empty())
        for (const std::string& c : split(f[4], '.'))
            a.choices.push_back(std::atoi(c.c_str()) - 1);
    if (f.size() > 5)
        a.item = f[5];
    if (f.size() > 6)
        a.then = f[6];
    return true;
}

// The game played without a window by a player who acts, waits for the
// game to settle, and looks at the story state.
class Player {
public:
    Player(DirectoryArchive& files, double limit) : limit_(limit) {
        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);
        ctx_ = std::make_unique<ViewerContext>(ViewerContext{
            sys_, files, nullptr, screen_, std::filesystem::temp_directory_path() / "reluva-awe-solve", nullptr});
        page_ = std::make_unique<ScenePage>(*ctx_);
        page_->onLog = [this](const std::string& l) {
            log_.push_back(l);
            if (l.find("movie credits") != std::string::npos)
                credits_ = true;
            // A script's own "unlock" (till its "lock"): clicks taken in
            // between are a player's way in.
            const size_t at = l.find_first_not_of(' ');
            if (at != std::string::npos && (l.compare(at, std::string::npos, "unlock") == 0))
                unlocked_ = true;
            else if (at != std::string::npos && l.compare(at, std::string::npos, "lock") == 0)
                unlocked_ = false;
        };
        page_->setDrawing(false);
        page_->activate();
        page_->chooser = [this](const std::vector<std::string>& choices) { return choose(choices); };
        page_->watchConditions([this](std::string_view clause, bool holds) {
            if (watching_)
                tested.emplace(std::string(clause), holds);
        });
    }

    ScenePage& page() { return *page_; }
    std::vector<std::string>& log() { return log_; }
    bool credits() const { return credits_; }
    std::string problem; // why the last action failed, if it did
    // What could be clicked while the last action's scripts ran
    // ("scene|name#n").
    std::set<std::string> inBetween;
    // The clauses of conditions the last action had tested, and what each
    // was the first time.
    std::map<std::string, bool> tested;

    // Runs the game till it settles: nothing running, nothing said, for 3
    // s. False if it sticks (an event that doesn't end) or runs past the
    // limit.
    bool settle() {
        const uint64_t start = sys_.now;
        uint64_t quietSince = 0, eventSince = sys_.now;
        std::string lastEvent;
        while (sys_.now - start < uint64_t(limit_ * 1000)) {
            sys_.now += 40;
            page_->update(sys_.now);
            page_->autoplay();
            const ScenePage::GameStatus st = page_->status();
            if (st.idle)
                unlocked_ = false;
            if (!st.idle && unlocked_ && page_->clickable()) {
                // Clicks are taken while this runs, after its "unlock" (a
                // walk's or a turn's clicks are the same as clicks once
                // it's done): what could be clicked is noted, and the
                // click waited for made.
                const std::vector<std::string> names = page_->clickables();
                std::map<std::string, int> seen;
                for (size_t i = 0; i < names.size(); i++) {
                    const std::string id = names[i] + "#" + std::to_string(seen[names[i]]++);
                    if (id == then_) {
                        then_.clear();
                        page_->click(i);
                        break;
                    }
                    inBetween.insert(st.scene + "|" + id);

                }
            }
            if (st.event != lastEvent || st.idle)
                lastEvent = st.event, eventSince = sys_.now;
            if (!st.idle && sys_.now - eventSince > 120000) {
                problem = "stuck: " + st.event + " (in " + st.scene + ")";
                return false;
            }
            const bool quiet = st.idle && !st.talking && !st.movie && !st.loading && !st.fading;
            if (!quiet)
                quietSince = 0;
            else if (!quietSince)
                quietSince = sys_.now;
            else if (sys_.now - quietSince > 3000)
                return true;
        }
        problem = "still going after " + std::to_string(int(limit_)) + " s";
        return false;
    }

    // What a player could do now, each with no choices yet.
    std::vector<Action> actions() {
        std::vector<Action> out;
        ScenePage& p = *page_;
        const std::string scene = p.status().scene;
        if (scene.rfind("menu.", 0) == 0)
            return out;
        if (!p.puzzleOpen().empty() && !p.puzzleSolved()) {
            Action a;
            a.kind = "solve";
            a.scene = scene;
            a.target = p.puzzleOpen();
            out.push_back(a);
        }
        const std::vector<std::string> names = p.clickables();
        std::map<std::string, int> seen;
        for (size_t i = 0; i < names.size(); i++) {
            Action a;
            a.scene = scene;
            a.target = names[i];
            a.n = seen[names[i]]++;
            a.kind = "click";
            if (p.clickRuns(i))
                out.push_back(a);
            for (const std::string& item : p.itemsFor(i)) {
                Action u = a;
                u.kind = "use";
                u.item = item;
                out.push_back(u);
            }
        }
        for (const std::vector<std::string>& parts : p.combinations()) {
            Action a;
            a.kind = "combine";
            a.scene = scene;
            for (const std::string& part : parts)
                a.item += (a.item.empty() ? "" : "+") + part;
            out.push_back(a);
        }
        for (const std::string& item : p.flippableItems()) {
            Action a;
            a.kind = "flip";
            a.scene = scene;
            a.item = item;
            out.push_back(a);
        }
        for (int car : p.travelCars()) {
            Action a;
            a.kind = "travel";
            a.scene = scene;
            a.target = std::to_string(car);
            out.push_back(a);
        }
        return out;
    }

    // Does the action, then lets the game settle. Its choices are taken
    // as given, then the least taken; what was taken is written back.
    bool act(Action& a) {
        problem.clear();
        forced_ = a.choices;
        then_ = a.then;
        inBetween.clear();
        tested.clear();
        watching_ = true;
        taken_.clear();
        offered_.clear();
        counts_.clear();
        ScenePage& p = *page_;
        bool ok = true;
        if (a.kind == "new") {
            p.startAt("new");
            credits_ = false;
        } else if (a.kind == "start") {
            p.startAt(a.target);
            credits_ = false;
        } else if (a.kind == "click" || a.kind == "use") {
            const std::vector<std::string> names = p.clickables();
            int index = -1;
            for (int i = 0, k = 0; i < int(names.size()); i++)
                if (names[size_t(i)] == a.target && k++ == a.n) {
                    index = i;
                    break;
                }
            if (index < 0)
                ok = false, problem = "no " + a.target + " to click in " + p.status().scene;
            else if (a.kind == "click")
                ok = p.click(size_t(index));
            else
                p.useOn(a.item, size_t(index));
        } else if (a.kind == "combine") {
            ok = p.combineItems(split(a.item, '+'));
            if (!ok)
                problem = "can't combine " + a.item;
        } else if (a.kind == "flip") {
            ok = p.flipItem(a.item);
            if (!ok)
                problem = "can't look at the back of " + a.item;
        } else if (a.kind == "run") {
            // A script run as a scene's (for looking into things; not a
            // player's).
            p.runCommands(a.target);
        } else if (a.kind == "solve") {
            // The puzzle's inputs, each with the game run on till its
            // scripts are done.
            ok = p.puzzleOpen() == a.target;
            for (int n = 0; ok && n < 1000 && !p.puzzleSolved(); n++) {
                if (!p.puzzleStep()) {
                    ok = false;
                    break;
                }
                for (int k = 0; k < 2 || (!p.status().idle && k < 3000); k++) {
                    sys_.now += 40;
                    p.update(sys_.now);
                    p.autoplay();
                }
            }
            if (!ok)
                problem = "can't solve the " + a.target;
        } else if (a.kind == "travel") {
            ok = p.travel(std::atoi(a.target.c_str()));
            if (!ok)
                problem = "can't travel to car " + a.target;
        } else {
            ok = false, problem = "unknown action " + a.kind;
        }
        if (ok)
            ok = settle();
        watching_ = false;
        if (ok && !then_.empty())
            ok = false, problem = then_ + " couldn't be clicked while it ran";
        a.choices = taken_;
        a.offered = offered_;
        return ok;
    }

    // The story state as words: what the search tells states apart by
    // (the scene, act, tags, items and conversations) and, separately, the
    // scenes seen (beenhere).
    std::set<std::string> features(bool withSeen) const {
        std::set<std::string> out;
        const GameState& s = page_->gameState();
        out.insert("act " + s.act);
        out.insert("scene " + s.scene);
        for (const auto& [name, v] : s.tags)
            out.insert("tag " + name + " " + std::to_string(v));
        for (const std::string& item : s.items)
            // (Just taken or in hand: in the bag.)
            out.insert("item " + item + " " +
                       std::to_string(s.itemState(item) == 1 || s.itemState(item) == 3 ? 2 : s.itemState(item)));
        for (const auto& [key, t] : s.talks)
            out.insert("talk " + key + " " + std::to_string(t.status) + " " + std::to_string(t.nest));
        for (const auto& [name, v] : s.texts)
            if (name.rfind("savedscene", 0) != 0 && withSeen)
                out.insert("seen " + name);
        return out;
    }

private:
    int choose(const std::vector<std::string>& choices) {
        int pick = -1;
        const size_t at = taken_.size();
        if (at < forced_.size() && forced_[at] >= 0 && forced_[at] < int(choices.size()))
            pick = forced_[at];
        for (int i = 0; i < int(choices.size()) && pick < 0; i++) {
            int best = 0;
            for (int j = 1; j < int(choices.size()); j++)
                if (counts_[choices[size_t(j)]] < counts_[choices[size_t(best)]])
                    best = j;
            pick = best;
        }
        if (pick < 0)
            return -1;
        counts_[choices[size_t(pick)]]++;
        taken_.push_back(pick);
        offered_.push_back(int(choices.size()));
        return pick;
    }

    double limit_;
    HeadlessSystem sys_;
    Surface screen_;
    std::unique_ptr<ViewerContext> ctx_;
    std::unique_ptr<ScenePage> page_;
    std::vector<std::string> log_;
    bool credits_ = false;
    std::vector<int> forced_, taken_, offered_;
    std::string then_;
    bool watching_ = false, unlocked_ = false;
    std::map<std::string, int> counts_;
};

std::string joinFeatures(const std::set<std::string>& f) {
    std::string out;
    for (const std::string& s : f)
        out += s + "\n";
    return out;
}

// How far on a state is: the tags, items and conversations it has.
// The story tags: not the dialogue's "met them here" ones ("<scene>
// <conversation>", a space in the name), which change whenever a scene is
// left after a talk.
bool storyTag(const std::string& feature) {
    return feature.rfind("tag ", 0) == 0 && std::count(feature.begin(), feature.end(), ' ') == 2;
}

// The story so far: its story tags and items (for the climb's dead ends).
std::string outlineOf(const std::set<std::string>& f) {
    std::string out;
    for (const std::string& s : f)
        if (storyTag(s) || s.rfind("item ", 0) == 0 || s.rfind("act ", 0) == 0)
            out += s + "\n";
    return out;
}

// How far on a state is: the story tags and items it has, then the
// conversations heard.
int progressOf(const std::set<std::string>& f) {
    int n = 0;
    for (const std::string& s : f)
        if (storyTag(s) || s.rfind("item ", 0) == 0)
            n += 2;
        else if (s.rfind("talk ", 0) == 0 && s.find(" 1 ", 5) != std::string::npos)
            n += 1;
    return n;
}

int actRank(const std::string& act) {
    int major = 0, minor = 0;
    return parseActName(act, major, minor) ? major * 10 + minor : 0;
}

bool readPlaythrough(const std::string& file, std::vector<Action>& out) {
    std::ifstream in(file);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        Action a;
        if (fromLine(line, a))
            out.push_back(a);
    }
    return true;
}

void writePlaythrough(const std::string& file, const std::vector<Action>& path) {
    std::ofstream out(file);
    for (const Action& a : path)
        out << toLine(a) << "\n";
}

// Plays a playthrough from its start; false if an action can't be done
// or the game sticks. `print` shows where each action leaves the game.
bool replay(Player& player, std::vector<Action>& path, bool print, size_t& done, size_t logFrom = SIZE_MAX) {
    done = 0;
    for (Action& a : path) {
        const size_t logAt = player.log().size();
        const bool ok = player.act(a);
        const ScenePage::GameStatus st = player.page().status();
        if (print)
            std::printf("%4zu %-60s -> act %s, %s\n", done + 1, toLine(a).substr(0, 60).c_str(), st.act.c_str(),
                        st.scene.c_str());
        if (done + 1 >= logFrom)
            for (size_t i = logAt; i < player.log().size(); i++)
                std::printf("      %s\n", player.log()[i].c_str());
        if (!ok) {
            std::printf("  failed: %s\n", player.problem.c_str());
            return false;
        }
        done++;
    }
    return true;
}

bool gTiers = false; // order the search by tier first

struct Node {
    std::vector<uint8_t> save;
    size_t path = 0; // into the search's paths: the actions that led here
    int progress = 0, depth = 0;
    int tier = 0; // 1: nothing in it is new to its scene (no novelty)
    std::string key;
};

} // namespace

int aweCommandSolve(int argc, char** argv) {
    // reluva-tool awe-acts <game> -solve <out> [-from <file>] [-budget n] [-t s] [-v]
    // reluva-tool awe-acts <game> -play <file> [-v]
    DirectoryArchive files;
    if (Error e = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::string solveTo, playFile, fromFile, goal;
    int budget = 4000, levelBudget = 3000;
    bool climb = true;
    double limit = 900;
    bool verbose = false, list = false, reload = false;
    size_t logFrom = SIZE_MAX; // -log n: the script log from the n-th action on
    gTiers = std::getenv("AWE_NOTIERS") == nullptr;
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-solve" && i + 1 < argc)
            solveTo = argv[++i];
        else if (a == "-play" && i + 1 < argc)
            playFile = argv[++i];
        else if (a == "-from" && i + 1 < argc)
            fromFile = argv[++i];
        else if (a == "-goal" && i + 1 < argc)
            goal = argv[++i];
        else if (a == "-budget" && i + 1 < argc)
            budget = std::atoi(argv[++i]);
        else if (a == "-level" && i + 1 < argc)
            levelBudget = std::atoi(argv[++i]);
        else if (a == "-noclimb")
            climb = false;
        else if (a == "-t" && i + 1 < argc)
            limit = std::atof(argv[++i]);
        else if (a == "-v")
            verbose = true;
        else if (a == "-log" && i + 1 < argc)
            logFrom = size_t(std::atoi(argv[++i]));
        else if (a == "-list")
            list = true;
        else if (a == "-reload")
            reload = true;
    }
    Player player(files, limit);

    if (!playFile.empty()) {
        std::vector<Action> path;
        if (!readPlaythrough(playFile, path)) {
            std::fprintf(stderr, "can't read %s\n", playFile.c_str());
            return 1;
        }
        size_t done = 0;
        const bool ok = replay(player, path, true, done, logFrom);
        const ScenePage::GameStatus st = player.page().status();
        std::printf("%zu of %zu actions; act %s, scene %s; %s\n", done, path.size(), st.act.c_str(), st.scene.c_str(),
                    player.credits() ? "the credits were reached" : "no credits");
        if (verbose)
            for (const std::string& l : player.log())
                std::printf("%s\n", l.c_str());
        if (reload) {
            // Saved and loaded again, as the search does.
            const std::set<std::string> before = player.features(true);
            const std::vector<uint8_t> save = player.page().saveToMemory();
            const size_t from = player.log().size();
            player.page().loadFromMemory(save);
            player.settle();
            const std::set<std::string> after = player.features(true);
            for (const std::string& f : before)
                if (!after.count(f))
                    std::printf("  lost on loading: %s\n", f.c_str());
            for (const std::string& f : after)
                if (!before.count(f))
                    std::printf("  new on loading: %s\n", f.c_str());
            if (verbose)
                for (size_t i = from; i < player.log().size(); i++)
                    std::printf("%s\n", player.log()[i].c_str());
        }
        if (list) {
            // What could be done next, and the story state.
            for (const Action& a : player.actions())
                std::printf("  can: %s\n", toLine(a).c_str());
            for (const std::string& f : player.features(true))
                std::printf("  %s\n", f.c_str());
        }
        return ok && player.credits() ? 0 : 1;
    }

    // The search. Its start: the playthrough given, else a new game and Play.
    std::vector<Action> start;
    if (!fromFile.empty() && !readPlaythrough(fromFile, start)) {
        std::fprintf(stderr, "can't read %s\n", fromFile.c_str());
        return 1;
    }
    if (start.empty()) {
        Action n;
        n.kind = "new";
        start.push_back(n);
        Action play;
        play.kind = "click";
        play.scene = "menu.main_01.ac2";
        play.target = "playover";
        start.push_back(play);
    }
    size_t done = 0;
    if (!replay(player, start, verbose, done)) {
        std::printf("the start doesn't play (%zu of %zu)\n", done, start.size());
        return 1;
    }
    // Every path found, by index; each a parent's and one action more.
    struct Step {
        size_t parent; // SIZE_MAX: the start
        Action action;
    };
    std::vector<Step> steps;
    auto pathOf = [&](size_t i) {
        std::vector<Action> out;
        for (; i != SIZE_MAX; i = steps[i].parent)
            out.push_back(steps[i].action);
        std::reverse(out.begin(), out.end());
        return out;
    };
    std::vector<Action> committed = start; // up to the root
    auto rootNode = [&]() {
        Node n;
        n.save = player.page().saveToMemory();
        n.path = SIZE_MAX;
        const std::set<std::string> f = player.features(false);
        n.key = joinFeatures(f);
        n.progress = progressOf(f);
        return n;
    };
    // States new to the story first, then the furthest on, then the
    // nearest the start.
    auto cmp = [](const Node& a, const Node& b) {
        if (gTiers && a.tier != b.tier)
            return a.tier > b.tier;
        return a.progress != b.progress ? a.progress < b.progress : a.depth > b.depth;
    };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> frontier(cmp);
    std::set<std::string> seenKeys, seenFeatures, triedOnce, seenOutlines;
    std::map<std::string, std::map<std::string, bool>> deadRuns; // by scene and action
    Node best;
    // The climb: each state with something new to the act's story (a story
    // tag's value, an item's state, a conversation's progress never had
    // before in the act, whichever way it was reached) becomes the root,
    // the way there kept; the roots before are kept to go back to if a
    // root leads to nothing new within the level's budget.
    struct Level {
        std::vector<Action> committed;
        std::vector<uint8_t> save;
        std::string outline;
    };
    std::vector<Level> levels;
    std::set<std::string> actStory; // the story features had in the act
    auto storyFeature = [](const std::string& x) {
        return storyTag(x) || x.rfind("item ", 0) == 0 || x.rfind("talk ", 0) == 0;
    };
    std::vector<uint8_t> rootSave;
    std::string rootOutline;
    int rootProgress = 0, levelExpansions = 0;
    auto reset = [&]() {
        frontier = decltype(frontier)(cmp);
        seenKeys.clear();
        seenFeatures.clear();
        triedOnce.clear();
        seenOutlines.clear();
        deadRuns.clear();
        steps.clear();
        Node root = rootNode();
        rootSave = root.save;
        rootProgress = root.progress;
        rootOutline = outlineOf(player.features(false));
        levelExpansions = 0;
        for (const std::string& x : player.features(false))
            if (storyFeature(x))
                actStory.insert(x);
        for (const std::string& x : player.features(true))
            seenOutlines.insert(player.page().status().scene + "|" + x);
        best = root;
        seenKeys.insert(root.key);
        for (const std::string& f : player.features(true))
            seenFeatures.insert(f);
        frontier.push(root);
        writePlaythrough(solveTo, committed);
        const ScenePage::GameStatus st = player.page().status();
        std::printf("== at act %s, scene %s, %zu actions so far (progress %d, level %zu)\n", st.act.c_str(),
                    st.scene.c_str(), committed.size(), rootProgress, levels.size());
        std::fflush(stdout);
    };
    reset();
    const auto began = std::chrono::steady_clock::now();
    int expansions = 0, runs = 0, skipped = 0, deadCount = 0;
    double restoreTime = 0, actTime = 0;
    while (expansions < budget) {
        if ((frontier.empty() || (climb && levelExpansions >= levelBudget)) && !levels.empty()) {
            // A dead end: back to the root before.
            std::printf("  (nothing further from here: back)\n");
            Level l = std::move(levels.back());
            levels.pop_back();
            committed = std::move(l.committed);
            player.page().loadFromMemory(l.save);
            player.settle();
            reset();
            continue;
        }
        if (frontier.empty())
            break;
        Node node = frontier.top();
        frontier.pop();
        expansions++;
        levelExpansions++;
        player.page().loadFromMemory(node.save);
        player.settle();
        {
            const std::string key = joinFeatures(player.features(false));
            if (key != node.key && verbose)
                std::printf("  (restored state differs from the one saved)\n");
        }
        std::vector<Action> todo = player.actions();
        const std::string fromAct = player.page().status().act;
        // The story without the scene: what the inventory's actions and
        // the bar's travel do doesn't depend on the scene they're done in
        // (but the car the bar's travel is from).
        std::string story;
        for (const std::string& f : split(node.key, '\n'))
            if (f.rfind("scene ", 0) != 0)
                story += f + "\n";
        const std::string here = player.page().status().scene;
        const std::string car = here.substr(0, here.find_first_of("_."));
        bool fresh = true, advanced = false;
        for (size_t k = 0; k < todo.size() && !advanced; k++) {
            Action a = todo[k];
            if (a.kind == "flip" || a.kind == "combine" || a.kind == "travel") {
                std::string once = a.kind + "|" + a.target + "|" + a.item;
                for (int c : a.choices)
                    once += "." + std::to_string(c);
                if (a.kind == "travel")
                    once += "|" + car;
                if (!triedOnce.insert(std::to_string(std::hash<std::string>()(story)) + once).second)
                    continue;
            }
            // An action that changed nothing before, where every condition it
            // had tested comes out the same now, would change nothing again.
            const std::string memoKey = here + "|" + toLine(a);
            if (const auto dead = deadRuns.find(memoKey); dead != deadRuns.end()) {
                bool same = true;
                for (const auto& [clause, held] : dead->second)
                    if (player.page().holds(clause) != held) {
                        same = false;
                        break;
                    }
                if (same) {
                    skipped++;
                    continue;
                }
            }
            auto t0 = std::chrono::steady_clock::now();
            if (!fresh) {
                player.page().loadFromMemory(node.save);
                player.settle();
            }
            fresh = false;
            auto t1 = std::chrono::steady_clock::now();
            runs++;
            const bool ok = player.act(a);
            restoreTime += std::chrono::duration<double>(t1 - t0).count();
            actTime += std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
            if (!ok) {
                if (verbose)
                    std::printf("  %s: %s\n", toLine(a).c_str(), player.problem.c_str());
                continue;
            }
            // Its choices tried otherwise: each choice point's other
            // answers, the ones before kept (up to the eighth point).
            if (a.choices.size() && todo[k].choices.empty())
                for (size_t c = 0; c < a.choices.size() && c < 8; c++)
                    for (int alt = 0; alt < a.offered[c]; alt++) {
                        if (alt == a.choices[c])
                            continue;
                        Action v = todo[k];
                        v.choices.assign(a.choices.begin(), a.choices.begin() + long(c));
                        v.choices.push_back(alt);
                        todo.push_back(v);
                    }
            // And with a click while it ran, on each thing that could be
            // clicked then.
            if (todo[k].then.empty())
                for (const std::string& id : player.inBetween) {
                    Action v = todo[k];
                    v.choices = a.choices;
                    v.then = id.substr(id.find('|') + 1);
                    todo.push_back(v);
                }
            const ScenePage::GameStatus st = player.page().status();
            if (st.scene.rfind("menu.", 0) == 0 && !player.credits())
                continue; // a menu: not the way on
            const std::set<std::string> f = player.features(false);
            const std::string key = joinFeatures(f);
            if (std::getenv("AWE_DIFF") && key != node.key) {
                const std::vector<std::string> before = split(node.key, '\n'), after = split(key, '\n');
                std::string d;
                for (const std::string& x : before)
                    if (std::find(after.begin(), after.end(), x) == after.end())
                        d += " -[" + x + "]";
                for (const std::string& x : after)
                    if (std::find(before.begin(), before.end(), x) == before.end())
                        d += " +[" + x + "]";
                std::printf("    diff %s:%s\n", toLine(a).c_str(), d.c_str());
            }
            if (key == node.key && a.then.empty())
                deadRuns[memoKey] = player.tested, deadCount++;
            int novel = 0;
            for (const std::string& s : player.features(true))
                if (seenFeatures.insert(s).second)
                    novel++;
            if (!seenKeys.insert(key).second)
                continue;
            steps.push_back({node.path, a});
            const size_t at = steps.size() - 1;
            if (verbose || novel)
                std::printf("  [%d] %s -> %s%s (+%d new)\n", expansions, toLine(a).c_str(), st.scene.c_str(),
                            st.act != fromAct ? (" ACT " + st.act).c_str() : "", novel);
            bool reached = false;
            if (!goal.empty())
                for (const std::string& s : player.features(true))
                    reached |= s.find(goal) != std::string::npos;
            if (reached) {
                // The state asked for (-goal): the way there, and stop.
                std::vector<Action> all = committed;
                const std::vector<Action> more = pathOf(at);
                all.insert(all.end(), more.begin(), more.end());
                writePlaythrough(solveTo, all);
                std::printf("reached %s after %zu actions\n", goal.c_str(), all.size());
                return 0;
            }
            if (player.credits() || actRank(st.act) > actRank(fromAct)) {
                // On to the next act (or the end): the way here kept, the
                // search begun again from here.
                const std::vector<Action> more = pathOf(at);
                committed.insert(committed.end(), more.begin(), more.end());
                if (player.credits()) {
                    writePlaythrough(solveTo, committed);
                    std::printf("the credits, after %zu actions\n", committed.size());
                    return 0;
                }
                levels.clear();
                actStory.clear();
                reset();
                advanced = true;
                break;
            }
            int newToAct = 0;
            for (const std::string& x : f)
                if (storyFeature(x))
                    newToAct += actStory.insert(x).second;
            if (climb && newToAct) {
                // Further on: the new root.
                levels.push_back({committed, rootSave, rootOutline});
                const std::vector<Action> more = pathOf(at);
                committed.insert(committed.end(), more.begin(), more.end());
                reset();
                advanced = true;
                break;
            }
            Node n;
            n.save = player.page().saveToMemory();
            n.path = at;
            n.key = key;
            n.progress = progressOf(f);
            n.depth = node.depth + 1;
            // Its novelty: anything (a tag's value, an item, a
            // conversation's progress, a scene seen) never had before in
            // this scene; a state with none waits till the others are done.
            int novelHere = 0;
            for (const std::string& x : player.features(true))
                if (x.rfind("scene ", 0) != 0)
                    novelHere += seenOutlines.insert(st.scene + "|" + x).second;
            n.tier = novelHere ? 0 : 1;
            if (n.progress > best.progress)
                best = n;
            frontier.push(std::move(n));
        }
        if (expansions % 20 == 0) {
            const double secs =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
            std::printf("  .. %d expanded, %d runs (%d dead, %d skipped), %zu waiting, %zu states, %.0f s (%.0f loading, %.0f "
                        "playing)\n",
                        expansions, runs, deadCount, skipped, frontier.size(), seenKeys.size(), secs, restoreTime, actTime);
            std::fflush(stdout);
        }
    }
    // Where it got furthest: the way there, and its state.
    {
        const std::vector<Action> way = best.path == SIZE_MAX ? std::vector<Action>() : pathOf(best.path);
        std::printf("furthest (progress %d), after:\n", best.progress);
        for (const Action& a : way)
            std::printf("  %s\n", toLine(a).c_str());
        std::printf("%s", best.key.c_str());
        std::vector<Action> all = committed;
        all.insert(all.end(), way.begin(), way.end());
        writePlaythrough(solveTo + ".furthest", all);
    }
    std::printf("stopped: %s after %d expansions\n", frontier.empty() ? "nothing left to try" : "the budget spent",
                expansions);
    return 1;
}

} // namespace reluva::awe
