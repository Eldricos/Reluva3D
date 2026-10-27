// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/conditions.h"
#include "engines/awe/script.h"
#include "engines/awe/tables.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The game's dialogue (motoe.exe's dialogue manager, 0x438f00..0x43e000):
// its tables (Text/Dialogue/act<NN><x>.txt for an act, and the gen_ tables
// for all), and how a conversation plays: the opening, the choices, each
// line's commands, voice and text, its progress kept for the conditions.
// See docs/formats/awe/dialogue.md.

namespace reluva::awe {

// A row of a dialogue table.
struct DialogueLine {
    std::string name, act, part, thread, nest; // as written ("02a", "a", "1", "a")
    std::string speaker, target;               // "antoinette"; whom they face ("antoinette up")
    std::string text;
    std::string condition;                     // when it can be said
    std::string repeat, optional, notes;
    std::string before, event, after;          // commands: Prevent, Event, Povent
    float duration = 0;

    // Codes as the loader makes them (0x438f00). A part: a letter from 0
    // (a), a number n as n + 99. A thread: a letter from 0, a second
    // character's digit added (z1: 26); a number n as n + 0x5f (1: 0x60).
    // A nest: a letter from 0, a second letter's place added; a number
    // n + 0x5f.
    int partCode = 0, threadCode = 0, nestCode = 0;
    int repeatCode = -1; // Repeat's letter, else -1
    int alternative = 0; // 0, or one more than the line before if that has the same nest

    // Its recording: sound/vo/<name><act><part><thread><nest>[_<alternative>].wav
    // ("dia_observation02aa1a.wav", "gen_locked01aa_1.wav").
    std::string voiceFile() const;
};

// The dialogue's codes, as text: for the event files' names and the
// "talk" conditions.
int dialoguePartCode(std::string_view s);
int dialogueThreadCode(std::string_view s);
int dialogueNestCode(std::string_view s);

// "talk <name> <code> [options]" (0x40ba25), and "cantalk" with the same
// words (0x407f24): the code's letters are the part, thread and nest; a
// number n the part n + 99, none part 100; a "+" at its end goes on to the
// next part. Options: nogen, forcegen, nogreet, or an alternative's
// number. `gen` is the default (a character was clicked; cantalk: on).
struct TalkArgs {
    std::string name;
    int part = 100, thread = -1, nest = -1, number = -1;
    bool gen = false, plus = false, greet = true;
};
TalkArgs parseTalkArgs(const std::vector<std::string>& words, bool gen);

// A conversation's part (one Name and Conver.), its threads in order.
struct DialoguePart {
    std::string name, act;
    int code = 0;
    struct Thread {
        int code = 0;
        std::vector<DialogueLine> lines;
        int status = 0;         // 1: heard to its end
        int current = -1;       // the line it resumes at (and is offered by), -1 none
        bool borrowed = false;  // a generic thread copied in (exit, regreet, extra)
        std::string fromName;   // borrowed: where from, for its progress
        int fromCode = 0;
    };
    std::vector<Thread> threads;
};

class DialogueBook {
public:
    // Adds a table's rows: a new part whenever the Name or Conver.
    // changes, a new thread whenever the Thread does (0x438f00).
    void add(const Table& table);
    void clear() { parts_.clear(); }
    // The first part of that name and code; null if none.
    DialoguePart* find(std::string_view name, int code);
    std::vector<DialoguePart>& parts() { return parts_; }
    const std::vector<DialoguePart>& parts() const { return parts_; }
    // All the lines of a conversation, in table order.
    std::vector<const DialogueLine*> conversation(std::string_view name) const;

private:
    std::vector<DialoguePart> parts_;
};

// What a conversation needs from the game around it.
class DialogueHost {
public:
    virtual ~DialogueHost() = default;
    // Starts a line's recording; false if there's none.
    virtual bool playVoice(const std::string& file) = 0;
    virtual bool voicePlaying() = 0;
    virtual void stopVoice() = 0;
    // A file's text ("" if there's none): the lines' event files.
    virtual std::string readText(const std::string& file) = 0;
    // The scene's name ("salon_01"), for the "met them here" tags.
    virtual std::string sceneName() = 0;
    // A speaker turns to look as its line's Target says ("antoinette up"),
    // or (empty) back home at the end.
    virtual void turnHead(const std::string& speaker, const std::string& target) {
        (void)speaker;
        (void)target;
    }
    // The speaker's talking: a line starts (talkidle1: 0x404720) and ends
    // (0x404760); the mouth moves or stops (0x4046c0, 0x4046f0).
    virtual void lineStarted(const std::string& speaker) { (void)speaker; }
    virtual void lineEnded(const std::string& speaker) { (void)speaker; }
    virtual void mouth(const std::string& speaker, bool on) {
        (void)speaker;
        (void)on;
    }
    // The voice's level now (0x425140): 0 silent, more when it's loud; -1
    // if unknown.
    virtual int voiceLevel() { return -1; }
};

class DialogueManager {
public:
    DialogueManager(GameState& state, ScriptHost& scripts, DialogueHost& host);

    DialogueBook& book() { return book_; }

    // "talk <name> <code> [options]" (0x40ba25 -> 0x43a630): part, thread
    // and nest from the code's letters (-1 for none), `gen` to offer the
    // generic lines (an NPC clicked, or forcegen), `number` to pick an
    // alternative (1 on, -1 at random), `plus` to go on to the next part
    // when one runs dry, `greet` to play the opening. True if it started;
    // then `runner` hears "talk" done when it's over.
    bool talk(ScriptRunner& runner, const std::string& name, int part, int thread, int nest, bool gen, int number,
              bool plus, bool greet);
    // Whether that "talk" would start: the "cantalk" condition's dry run
    // (0x407f24), nothing shown or said. False while a conversation is on.
    bool canTalk(const std::string& name, int part, int thread, int nest, bool gen, int number, bool plus,
                 bool greet);
    // The "nothing to say" line (0x43a550), for an NPC with nothing left.
    bool noTalk(ScriptRunner& runner);
    // Moves things on by `seconds`.
    void update(float seconds);
    // A click: a choice, or the line cut short (after half a second).
    void click(int x, int y);
    void mouseMove(int x, int y);

    // A tag every "met them here" tag of the scene is set to when it's
    // left (0x43aee0).
    void leaveScene(const std::string& scene);

    // What to draw.
    bool active() const { return active_; }
    bool boxShown() const;
    int boxTop() const { return int(boxY_); }
    int boxHeight() const { return boxHeight_; }
    int window() const { return window_; } // 0: dialoguewin01, 1: dialoguewin02
    bool choosing() const { return choosing_; }
    // The line being said and its colour (fonts.txt's: 8 Antoinette, 9 and
    // 10 the others).
    const DialogueLine* line() const;
    int colour() const { return colour_; }
    struct Choice {
        std::string text;
        DialoguePart* part;
        int thread;
    };
    const std::vector<Choice>& choices() const { return choices_; }
    int highlighted() const { return highlighted_; }
    // Where each choice is, from the box's drawing: for clicks.
    void setChoiceAreas(std::vector<std::pair<int, int>> rows) { choiceRows_ = std::move(rows); }

    // For a log.
    std::function<void(const std::string& line)> onLog;

private:
    using Thread = DialoguePart::Thread;
    bool start(const std::string& name, int part, int thread, int nest, int number, bool plus, bool greet,
               bool retry);
    int pickLine(Thread& t, int nest, bool forward);
    bool available(Thread& t);
    void loadProgress(DialoguePart& p);
    void saveProgress(const DialoguePart& p, const Thread& t);
    std::string genPrefix(const DialoguePart& p) const;
    void addExit(DialoguePart& p);
    void addRegreet(DialoguePart& p, int visit);
    std::vector<std::pair<DialoguePart*, int>> extras();
    void menu(bool first, bool keepBox);
    void play(DialoguePart* p, int thread, int line);
    void startLine();
    void speak();
    void lineEnd();
    void end(bool keepBox);
    void finishTalk();
    void showBox();
    void log(const std::string& s) {
        if (onLog)
            onLog(s);
    }

    GameState& state_;
    DialogueHost& host_;
    DialogueBook book_;
    ScriptRunner prevent_, events_, povent_;
    ScriptRunner* caller_ = nullptr; // the runner whose "talk" this is

    DialoguePart* part_ = nullptr;
    int thread_ = -1, line_ = -1; // indices into part_
    bool active_ = false, choosing_ = false, playing_ = false;
    bool firstOffer_ = true, plus_ = false, goOn_ = false, gen_ = false, greet_ = true;
    int number_ = -1, lastAlternative_ = -1;
    bool dry_ = false; // a dry run (cantalk)
    bool waitPrevent_ = false, voice_ = false, showWhilePovent_ = true;
    float textLeft_ = 0, lineTime_ = 0, pause_ = -1;
    int pending_ = 0; // after the Povent: 1 end the talk, 2 the next line, 3 the menu
    int colour_ = 8;
    std::string lastSpeaker_;
    std::string speaker_; // whose line plays
    int level_ = -1;      // the voice's last level
    std::string prefix_; // gen_<npc>_
    std::vector<std::string> turned_;
    // The box.
    float boxY_ = 768;
    int boxHeight_ = 160, window_ = 0, slide_ = 0; // slide: 1 in, 2 out
    // Choices.
    std::vector<Choice> choices_;
    std::vector<std::pair<int, int>> choiceRows_; // y, height
    int highlighted_ = -1;
    unsigned random_ = 1;
    int rand();
};

} // namespace reluva::awe
