// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Scripts: the runner, the inventory's slots and the story (acts.txt).

#include "tests/engines/awe/fixtures.h"

#include <filesystem>

using namespace reluva;
using namespace reluva::awe;
using namespace reluva::awe::testing;

namespace {

void testScript() {
    GameState state;
    FakeHost host;
    ScriptRunner r(state, host);
    r.run("walk 31 0, scene salon_02 1, tag act04a 2, tag act04a +3, add ticket");
    CHECK(!r.idle());
    r.update(0.01f);
    CHECK(host.started.size() == 1 && host.started[0] == "walk 31 0");
    r.update(0.01f); // still walking
    CHECK(host.started.size() == 1);
    r.finished("talk"); // not what it waits for
    r.update(0.01f);
    CHECK(host.started.size() == 1);
    r.finished("walk");
    r.update(0.01f);
    CHECK(host.started.size() == 2 && host.started[1] == "scene salon_02 1");
    for (int i = 0; i < 4; i++)
        r.update(0.01f);
    CHECK(state.tags["act04a"] == 5 && state.itemState("ticket") == 2 && r.idle());

    // The "a" forms don't wait; "wait" counts down; a fade after a walk
    // starts as the walk ends.
    host.started.clear();
    r.run("walka sp1, talka dia_test a, wait 0.5, walk 3 4\nfade out");
    r.update(0.01f);
    CHECK(host.started.size() == 1 && host.started[0] == "walk sp1 (no wait)");
    r.update(0.01f);
    CHECK(host.started.size() == 2 && host.started[1] == "talk dia_test a (no wait)");
    r.update(0.01f); // the wait starts
    r.update(0.3f);
    r.update(0.01f);
    CHECK(host.started.size() == 2);
    r.update(0.3f); // over
    r.update(0.01f);
    CHECK(host.started.size() == 3 && host.started[2] == "walk 3 4");
    r.finished("walk");
    CHECK(host.started.size() == 4 && host.started[3] == "fade out");
    r.finished("fade");
    CHECK(r.idle());
    // removetag, setchar, remove.
    r.run("removetag act04a, setchar debenham, remove ticket");
    for (int i = 0; i < 3; i++)
        r.update(0.01f);
    CHECK(!state.tags.count("act04a") && r.character() == "debenham" && !state.itemState("ticket"));
    // The next script given acts on the player again (0x408cd8).
    r.run("tag after 1");
    CHECK(r.character().empty());
    // A character's own runner goes back to that character (0x40598f).
    r.setCharacter("klaus");
    r.run("setchar antoinette");
    for (int i = 0; i < 3; i++)
        r.update(0.01f);
    CHECK(r.character() == "antoinette");
    r.run("tag after 2");
    CHECK(r.character() == "klaus");
    // Two of a name (0x41bc10 adds one each time): "use" the first in the
    // bag, "restore" the first used up, "remove" the first whatever it is.
    r.setCharacter("");
    state.items.clear();
    r.run("add waterglass, add waterglass, use waterglass");
    for (int i = 0; i < 5; i++)
        r.update(0.01f);
    CHECK(state.items.size() == 2 && state.items[0].state == 4 && state.items[1].state == 2);
    CHECK(testCondition("used waterglass", state) && testCondition("has waterglass available", state));
    r.run("use waterglass, restore waterglass, remove waterglass");
    for (int i = 0; i < 5; i++)
        r.update(0.01f);
    CHECK(state.items.size() == 1 && state.items[0].state == 4 && state.items[0].slot == 1);

    // The door macros, and a script file.
    struct FileHost : FakeHost {
        std::string scriptText(const std::string &name) override {
            return name == "door" ? "walk 40 26\nscript nested" : name == "nested" ? "tag nested 1" : "";
        }
    } files;
    GameState state2;
    ScriptRunner r2(state2, files);
    r2.run("dooropen sp3 L 2, script door");
    for (int i = 0; i < 40; i++) {
        r2.update(0.5f);
        r2.finished("walk");
        r2.finished("fade");
    }
    CHECK(files.started.size() == 9);
    if (files.started.size() == 9) {
        CHECK(files.started[0] == "walk sp3" && files.started[1] == "rotate -sp3");
        CHECK(files.started[2] == "anim doorknobL (no wait)" && files.started[3] == "turn down");
        CHECK(files.started[4] == "fade out" && files.started[5] == "sound dooropen2 (no wait)");
        CHECK(files.started[6] == "turn home" && files.started[7] == "animate 2" && files.started[8] == "walk 40 26");
    }
    CHECK(state2.tags["nested"] == 1 && r2.idle());

    // A condition in brackets decides whether what follows is added, when
    // it's added (0x408c80).
    GameState state3;
    FakeHost host3;
    ScriptRunner r3(state3, host3);
    state3.tags["hardman06bend"] = 4;
    r3.run("tag a, (tag hardman06bend >3, has printhardman), tag b, tag c, (tag hardman06bend 4) tag d, tag e");
    r3.fastForward();
    CHECK(state3.tags.count("a") && !state3.tags.count("b") && !state3.tags.count("c") && state3.tags.count("e"));
    // The inventory's verbs.
    r3.run("add waterglass, add passmasterman, add hammer, use waterglass");
    r3.fastForward();
    CHECK(state3.itemState("waterglass") == 4 && state3.itemState("passmasterman") == 5);
    r3.run("restore waterglass, remove all");
    r3.fastForward();
    CHECK(state3.itemState("waterglass") == 7 && state3.itemState("hammer") == 7 &&
          state3.itemState("passmasterman") == 5);
    state3.tags["securityunlocked"] = 1;
    r3.run("add keysmall, restore all, remove hammer");
    r3.fastForward();
    CHECK(state3.itemState("waterglass") == 2 && state3.itemState("hammer") == 0 && state3.itemState("keysmall") == 0);
    CHECK(!state3.tags.count("securityunlocked"));
    // Clicks are refused while it runs, but during a walk, or after
    // "unlock".
    host3.started.clear();
    r3.run("walk 3 4, wait 1, unlock, wait 1, lock, wait 1");
    r3.update(0.01f);
    CHECK(!r3.locked());
    r3.finished("walk");
    r3.update(0.01f);
    CHECK(r3.locked());
    r3.update(1.0f);
    r3.update(0.01f);
    r3.update(0.01f);
    CHECK(!r3.locked());
    r3.update(1.0f);
    r3.update(0.01f);
    r3.update(0.01f);
    CHECK(r3.locked());
    for (int i = 0; i < 4; i++)
        r3.update(1.0f);
    CHECK(r3.idle() && !r3.locked());
}

void testItemSlots() {
    // Items take the lowest free slot of the inventory window (16 a page)
    // and the window turns to its page; passports and fingerprint cards
    // have their own pages; the item just taken blinks, one at a time
    // (0x41bc10, 0x41bfb0, 0x41ba80).
    GameState s;
    auto slotOf = [&](const std::string &name) {
        const GameState::Item *i = s.findItem(name, {1, 2, 3});
        return i ? i->slot : -1;
    };
    addItem(s, "hammer", 2);
    addItem(s, "pliers", 1);
    addItem(s, "passmichel", 2);
    addItem(s, "printmichel", 1);
    CHECK(slotOf("hammer") == 0 && slotOf("pliers") == 1 && slotOf("passmichel") == -1);
    CHECK(s.itemState("passmichel") == 5 && s.itemState("printmichel") == 6 && s.fresh == "pliers");
    addItem(s, "nails", 1);
    CHECK(s.fresh == "nails" && s.itemState("pliers") == 2 && s.itemState("nails") == 1 && slotOf("nails") == 2);
    s.findItem("hammer")->state = 4; // used up: its slot is free
    addItem(s, "coal", 2);
    CHECK(slotOf("coal") == 0);
    addItem(s, "hammer", 2); // another: the next free slot
    CHECK(slotOf("hammer") == 3 && s.itemPage == 0 && s.itemState("hammer") == 4);
    for (int i = 0; i < 13; i++)
        addItem(s, "thing" + std::to_string(i), 2);
    CHECK(slotOf("thing12") == 16 && s.itemPage == 1);
    CHECK(s.items.size() == 20 && s.items[0].name == "hammer" && s.items[2].name == "passmichel");

    // Two of a name, as the game keeps them: "use" takes the first in the
    // bag, "restore" the first used up, "remove" the first of either.
    GameState g;
    addItem(g, "waterglass", 2);
    addItem(g, "waterglass", 2);
    CHECK(g.items.size() == 2 && g.items[1].slot == 1);
    g.findItem("waterglass", {1, 2})->state = 4;
    CHECK(g.itemAvailable("waterglass") && g.findItem("waterglass", {4}) == &g.items[0]);
    CHECK(g.freeSlot() == 0);
}

void testStory() {
    int major = 0;
    int minor = 0;
    CHECK(parseActName("06a", major, minor) && major == 6 && minor == 1 && actName(major, minor) == "06a");
    CHECK(!parseActName("a", major, minor));
    const Table t =
        parseTable("Act\tPreLoad\tLocation to Load\tPost Load\tTime of Day\tWeather\tTrain Moving\t"
                   "Completion Conditions\tEvent\tMusic\tWhat to do\n"
                   "01A\t\"fade out, movie intro\"\tsirkeci.market\t\"scene sirkeci.market_01 1 0, add ticket\"\t"
                   "day\tsnow\tNo\t0\tact 01b\t0\t0\n"
                   "05A\t0\tSalon\tscene salon_01\tnight\tBlizzard\tYes\ttag act05a\tact 05b\t0\t0\n"
                   "05B\t0\tsalon\t0\tday\tnone\tyes\t0\t0\t0\t0\n");
    const std::vector<ActRow> acts = readActs(t);
    CHECK(acts.size() == 3 && acts[0].act == "01a" && acts[0].preLoad == "fade out, movie intro");
    CHECK(acts[0].weather == 1 && acts[0].time == 0 && acts[0].moving == 0 && acts[1].location == "salon");
    CHECK(acts[1].weather == 2 && acts[1].time == 1 && acts[1].moving == 1);

    GameState state;
    FakeHost host;
    ScriptRunner main(state, host);
    Story story(state, main);
    story.setActs(acts);
    std::vector<std::string> loaded;
    story.onLoadLocation = [&](const std::string &l, bool flush) {
        loaded.push_back(l + (flush ? " flush" : ""));
        story.locationLoaded();
    };
    // reset, then a bare act: 01A. A conversation's progress goes too
    // (0x411aa0 empties +0x2b4).
    state.talks[talkKey("klaus", "01a", 1, 1)] = TalkProgress{2, -1};
    main.run("tag x 1, timer clock, reset");
    for (int i = 0; i < 4; i++)
        main.update(0.01f);
    CHECK(state.act == "01a" && state.tags.empty() && state.timers.empty() && state.talks.empty());
    CHECK(story.startAct("") && story.loading());
    // PreLoad runs first: the fade waits.
    story.update(0.01f);
    CHECK(loaded.empty());
    main.update(0.01f);
    CHECK(host.started.back() == "fade out");
    story.update(0.01f);
    CHECK(loaded.empty());
    main.finished("fade");
    main.update(0.01f); // the movie (FakeHost: done at once)
    story.update(0.01f);
    CHECK(loaded.size() == 1 && loaded[0] == "sirkeci.market" && state.values["weather"] == 1);
    for (int i = 0; i < 3; i++)
        main.update(0.01f);
    CHECK(host.started.back() == "scene sirkeci.market_01 1 0" && state.itemState("ticket") && main.idle());
    // The main menu is a place of its own: to it, and back with Play,
    // the scenes kept are dropped each time (0x422640, 0x4229a5).
    CHECK(story.sceneLocation("menu.main"));
    CHECK(!story.sceneLocation("menu.main"));
    story.startAct("01a");
    for (int i = 0; i < 4; i++)
        main.update(0.01f), story.update(0.01f);
    main.finished("fade");
    for (int i = 0; i < 4; i++)
        main.update(0.01f), story.update(0.01f);
    CHECK(loaded.back() == "sirkeci.market flush");
    // 05A: its Event once its conditions hold and the queue runs dry.
    story.startAct("05a");
    story.update(0.01f);
    CHECK(loaded.back() == "salon" && state.values["moving"] == 1); // inside the train: kept
    for (int i = 0; i < 3; i++)
        main.update(0.01f);
    host.started.clear();
    main.run("tag act05a 1");
    for (int i = 0; i < 3; i++)
        main.update(0.01f);
    CHECK(!host.started.empty() && host.started[0] == "act 05b");
    // (The page's "act" starts 05B, whose row has no conditions.)
    story.startAct("05b");
    story.update(0.01f);
    main.clear();
    // An act without a row: only the act changes, the row stays.
    CHECK(!story.startAct("05c") && state.act == "05c" && story.row() && story.row()->act == "05b");

    // Timers: a second at a time, "time n", the condition.
    main.run("timer ham, time 50");
    for (int i = 0; i < 3; i++)
        main.update(0.01f);
    story.update(0.6f);
    story.update(0.6f);
    CHECK(testCondition("timer ham >50", state) && !testCondition("timer ham >51", state));
    CHECK(testCondition("timer ham", state) && !testCondition("timer radio", state) &&
          testCondition("timer radio <0", state));
    // Visits and savescene.
    state.scene = "salon_01_06a.ac2";
    state.texts["salon_01_06a.ac2"] = "02a 05c";
    CHECK(testCondition("beenhere this", state) && testCondition("beenthere this ever", state));
    CHECK(testCondition("beenthere salon_01_06a 02a", state) && !testCondition("beenthere salon_02", state));
    state.lastScene = "exterior.forest_11.ac2";
    CHECK(testCondition("lastscene exterior.forest_11", state) &&
          !testCondition("lastscene exterior.forest_13", state));
    main.run("savescene this, savescene calais.hall_03 2 1");
    main.update(0.01f);
    CHECK(state.texts["savedscene"] == "salon_01_06a.ac2 -1 0");
    main.update(0.01f);
    CHECK(state.texts["savedscene"] == "calais.hall_03.ac2 2 1");
}

// A skeleton of four bones: Bip01, its spine, the head, the jaw's master.

} // namespace

static const reluva::test::Suite kSuite("awe.unit.script",
                                        {
                                            {"runner", [](const std::filesystem::path &) { testScript(); }},
                                            {"item slots", [](const std::filesystem::path &) { testItemSlots(); }},
                                            {"story", [](const std::filesystem::path &) { testStory(); }},
                                        });
