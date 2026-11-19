// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Conversations: the dialogue tables and the dialogue manager.

#include "tests/engines/awe/fixtures.h"

#include <filesystem>

using namespace reluva;
using namespace reluva::awe;
using namespace reluva::awe::testing;

namespace {

void testDialogueBook() {
    const char *table =
        "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\tLine Translation\tCondition\tRepeat\tPrevent\t"
        "Event\tPovent\tDuration\tOptional\tNotes\n"
        "dia_observation\t02a\ta\t1\ta\tpoirot\t0\tOne must look.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_observation\t02a\ta\ta\ta\tantoinette\tantoinette up\t\"Well, yes.\"\t0\thas ribbon\ta\t"
        "scene restaurant_table.02\tanim no\tscene restaurant_table.07\t0\t1\t0\n"
        "gen_locked\t0\t1\ta\ta\tantoinette\t0\tIt's locked.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "gen_locked\t0\t1\ta\ta\tantoinette\t0\tLocked.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n";
    DialogueBook book;
    book.add(parseTable(table));
    CHECK(book.parts().size() == 2);
    const auto obs = book.conversation("Dia_Observation");
    CHECK(obs.size() == 2);
    if (obs.size() == 2) {
        CHECK(obs[0]->thread == "1" && obs[0]->threadCode == 0x60 && obs[0]->speaker == "poirot" &&
              obs[0]->target.empty());
        CHECK(obs[0]->voiceFile() == "sound/vo/dia_observation02aa1a.wav");
        CHECK(obs[1]->text == "Well, yes." && obs[1]->target == "antoinette up" && obs[1]->condition == "has ribbon");
        CHECK(obs[1]->before == "scene restaurant_table.02" && obs[1]->event == "anim no" &&
              obs[1]->after == "scene restaurant_table.07" && obs[1]->repeatCode == 0 && obs[1]->optional == "1");
    }
    // Part "1" is code 100; a line with the nest before it again is an
    // alternative, its recording numbered.
    DialoguePart *locked = book.find("gen_locked", 100);
    CHECK(locked && locked->threads.size() == 1 && locked->threads[0].lines.size() == 2);
    if (locked && locked->threads[0].lines.size() == 2)
        CHECK(locked->threads[0].lines[1].voiceFile() == "sound/vo/gen_locked01aa_1.wav");
    CHECK(dialoguePartCode("a") == 0 && dialoguePartCode("1") == 100 && dialogueThreadCode("z1") == 26 &&
          dialogueThreadCode("3") == 0x62 && dialogueNestCode("b") == 1);
    // Read by position, whatever the headings (gen_characters.txt's first
    // is a name), with no Optional column.
    DialogueBook gen;
    gen.add(parseTable("gen_globalhardman\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\n"
                       "gen_arbuthnot_exit\t0\t0\t3\ta\tantoinette\t0\tAu revoir.\t0\t0\t0\t0\t0\t0\t0\tadded\n"));
    const DialoguePart *exit = gen.find("gen_arbuthnot_exit", 99);
    CHECK(exit && exit->threads.size() == 1 && exit->threads[0].code == 0x62);
}

// A conversation played through: no voices, so each line lasts its
// length / 15 seconds (at least one).
struct QuietHost : DialogueHost {
    bool playVoice(const std::string &) override { return false; }
    bool voicePlaying() override { return false; }
    void stopVoice() override {}
    std::string readText(const std::string &) override { return {}; }
    std::string sceneName() override { return "salon_01"; }
    void talkStarting(const std::string &name) override { started.push_back(name); }
    std::vector<std::string> started;
};

void testDialogue() {
    const char *table =
        "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\tLine Translation\tCondition\tRepeat\tPrevent\t"
        "Event\tPovent\tDuration\tOptional\tNotes\n"
        "dia_obs\t02a\ta\t1\ta\tpoirot\t0\tLook.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_obs\t02a\ta\ta\ta\tantoinette\t0\tThe ladies?\t0\t0\t0\t0\ttag sawladies 1\t0\t0\t0\t0\n"
        "dia_obs\t02a\ta\ta\tb\tpoirot\t0\tYes.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_obs\t02a\ta\tb\ta\tantoinette\t0\tThe man?\t0\t0\t0\t0\t0\t0\t0\t0\tinventory\\items\\gun\\invgun.png\n"
        "dia_obs\t02a\ta\tc\ta\tantoinette\t0\tSecret.\t0\ttag never\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_obs\t02a\tb\t1\ta\tpoirot\t0\tNever said.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_obs\t02a\tc\ta\ta\tpoirot\t0\tNor this.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n";
    GameState state;
    FakeHost scripts;
    QuietHost host;
    DialogueManager d(state, scripts, host);
    d.book().add(parseTable(table));
    ScriptRunner main(state, scripts);
    main.run("talk dia_obs a+");
    // The runner waits on "talk": a stand-in starts the dialogue for it.
    struct TalkHost : ScriptHost {
        DialogueManager *d = nullptr;
        bool start(ScriptRunner &r, const ScriptEvent &e, bool) override {
            return e.verb() == "talk" && d->talk(r, e.arg(1), 0, -1, -1, false, -1, true, true);
        }
    } talker;
    talker.d = &d;
    ScriptRunner runner(state, talker);
    runner.run("talk dia_obs a+, tag after 1");
    runner.update(0.01f);
    CHECK(d.active() && !runner.idle());
    // The inventory's changes, once the conversation is found (0x43a670).
    CHECK(host.started.size() == 1 && host.started[0] == "dia_obs");
    auto run = [&](float seconds) {
        for (float t = 0; t < seconds; t += 0.05f) {
            d.update(0.05f);
            runner.update(0.05f);
        }
    };
    run(1.0f); // the box slides in
    CHECK(d.line() && d.line()->text == "Look.");
    run(1.6f); // the opening, then the choices (not "Secret.": its condition fails)
    CHECK(d.choosing() && d.choices().size() == 2);
    if (d.choices().size() == 2) {
        CHECK(d.choices()[0].text == "The ladies?" && d.choices()[1].text == "The man?");
        // A line whose Notes name a picture shows it too; the pictures are
        // tried before the rows (0x43c810).
        CHECK(d.choices()[0].picture.empty() && d.choices()[1].picture == "inventory\\items\\gun\\invgun.png");
        d.setChoiceAreas({{0, 640, 1024, 20, 0, false}, {600, 630, 112, 96, 1, true}});
        d.mouseMove(650, 645);
        CHECK(d.highlighted() == 1);
        d.mouseMove(100, 645);
        CHECK(d.highlighted() == 0);
        d.setChoiceAreas({{0, 640, 1024, 20, 0, false}, {0, 664, 1024, 20, 1, false}});
        d.mouseMove(100, 300);
        // The wheel: towards the user to the next, round through nothing
        // (no goodbye); away to the one before. Its choice stays, the
        // cursor hidden, till the mouse moves.
        d.wheel(-1, 100, 300);
        CHECK(d.highlighted() == 0 && d.wheelChoosing());
        d.wheel(-2, 100, 300);
        CHECK(d.highlighted() == -1);
        d.wheel(1, 100, 300); // back from none, with no goodbye: still none (0x43cc68)
        CHECK(d.highlighted() == -1);
        d.wheel(-1, 100, 300);
        d.wheel(-1, 100, 300);
        d.wheel(1, 100, 300);
        CHECK(d.highlighted() == 0);
        d.wheel(-1, 100, 300);
        CHECK(d.highlighted() == 1);
        d.mouseMove(100, 300);
        CHECK(d.highlighted() == 1 && d.wheelChoosing());
        d.mouseMove(101, 300);
        CHECK(d.highlighted() == -1 && !d.wheelChoosing());
        d.middleClick(); // nothing highlighted: nothing taken
        CHECK(d.choosing());
        d.click(100, 645);
    }
    CHECK(d.line() && d.line()->text == "The ladies?");
    run(1.0f);
    CHECK(state.tags["sawladies"] == 1); // its Event ran
    run(1.6f);
    // Heard to its end: kept for the "talk" condition.
    CHECK(testCondition("talk dia_obs 02a aa", state) && !testCondition("talk dia_obs 02a ab", state));
    // One choice left: played without asking. Then on to part b, which
    // has only an opening: Start looks for its thread a (0x43a835), finds
    // none and fails outright (0x43aaa3, no "+" from there), so part c
    // isn't reached either and the talk ends.
    CHECK(d.line() && d.line()->text == "The man?");
    run(1.6f);
    CHECK(!d.line() || (d.line()->text != "Never said." && d.line()->text != "Nor this."));
    run(4.0f); // the box slides out, the talk is done
    CHECK(!d.active());
    run(0.2f);
    CHECK(state.tags["after"] == 1 && runner.idle());

    // A generic extra with no line to offer (its first line's condition
    // fails, though a later one has none) doesn't count (0x43a470): part a,
    // heard, is exhausted, and "+" goes on to part b.
    d.book().add(parseTable(
        "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\tLine Translation\tCondition\tRepeat\tPrevent\t"
        "Event\tPovent\tDuration\tOptional\tNotes\n"
        "dia_hub\t06b\ta\ta\ta\thub\t0\tPassport.\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "dia_hub\t06b\tb\ta\ta\tantoinette\t0\tWas it a woman?\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "gen_hub_print\t0\ta\ta\ta\tantoinette\t0\tPrints?\t0\ttag never\t0\t0\t0\t0\t0\t0\t0\n"
        "gen_hub_print\t0\ta\ta\tb\thub\t0\tA ribbon?\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"));
    state.talks[talkKey("dia_hub", "06b", 0, 0)] = TalkProgress{1, -1};
    CHECK(d.talk(runner, "dia_hub", 0, -1, -1, true, -1, true, true));
    run(1.0f);
    CHECK(d.line() && d.line()->text == "Was it a woman?");

    // A line's Prevent runs while the box slides in (0x43d430 updates the
    // Prevent runner before the slide): the line shows before the box is
    // up.
    DialogueManager pre(state, scripts, host);
    pre.book().add(parseTable(
        "Name\tAct\tConver.\tThread\tNest\tSpeaker\tTarget\tLine\tLine Translation\tCondition\tRepeat\tPrevent\t"
        "Event\tPovent\tDuration\tOptional\tNotes\n"
        "dia_pre\t0\ta\t1\ta\tpoirot\t0\tSoon.\t0\t0\t0\twait .1\t0\t0\t0\t0\t0\n"
        "dia_pre\t0\ta\ta\ta\tantoinette\t0\tAnd?\t0\t0\t0\t0\t0\t0\t0\t0\t0\n"));
    ScriptRunner other(state, talker);
    CHECK(pre.talk(other, "dia_pre", 0, -1, -1, false, -1, true, true));
    for (int i = 0; i < 4; i++)
        pre.update(0.05f);
    CHECK(pre.lineShown() && pre.boxTop() > 768 - 160);
}

} // namespace

static const reluva::test::Suite kSuite("awe.unit.dialogue",
                                        {
                                            {"book", [](const std::filesystem::path &) { testDialogueBook(); }},
                                            {"manager", [](const std::filesystem::path &) { testDialogue(); }},
                                        });
