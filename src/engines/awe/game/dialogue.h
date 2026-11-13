// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/game/conditions.h"
#include "engines/awe/game/script.h"
#include "engines/awe/formats/tables.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The game's dialogue (motoe.exe's dialogue manager, 0x438f00..0x43e000):
// its tables (Text/Dialogue/act<NN><x>.txt for an act, and the gen_ tables
// for all), and how a conversation plays: the opening, the choices, each
// line's commands, voice and text, its progress kept for the conditions.
// See docs/engines/awe/games/motoe/systems/dialogue.md.

namespace reluva::awe {

// A row of a dialogue table.
struct DialogueLine {
    // as written ("02a", "a", "1", "a")
    std::string name;
    std::string act;
    std::string part;
    std::string thread;
    std::string nest;
    // "antoinette"; whom they face ("antoinette up")
    std::string speaker;
    std::string target;
    std::string text;
    std::string condition; // when it can be said
    std::string repeat;
    std::string optional;
    std::string notes;
    // commands: Prevent, Event, Povent
    std::string before;
    std::string event;
    std::string after;
    float duration = 0;

    // Codes as the loader makes them (0x438f00). A part: a letter from 0
    // (a), a number n as n + 99. A thread: a letter from 0, a second
    // character's digit added (z1: 26); a number n as n + 0x5f (1: 0x60).
    // A nest: a letter from 0, a second letter's place added; a number
    // n + 0x5f.
    int partCode = 0;
    int threadCode = 0;
    int nestCode = 0;
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
    int part = 100;
    int thread = -1;
    int nest = -1;
    int number = -1;
    bool gen = false;
    bool plus = false;
    bool greet = true;
};
TalkArgs parseTalkArgs(const std::vector<std::string> &words, bool gen);

// A conversation's part (one Name and Conver.), its threads in order.
struct DialoguePart {
    std::string name;
    std::string act;
    int code = 0;
    struct Thread {
        int code = 0;
        std::vector<DialogueLine> lines;
        int status = 0;        // 1: heard to its end
        int current = -1;      // the line it resumes at (and is offered by), -1 none
        bool borrowed = false; // a generic thread copied in (exit, regreet, extra)
        std::string fromName;  // borrowed: where from, for its progress
        int fromCode = 0;
    };
    std::vector<Thread> threads;
};

class DialogueBook {
public:
    // Adds a table's rows: a new part whenever the Name or Conver.
    // changes, a new thread whenever the Thread does (0x438f00).
    void add(const Table &table);
    void clear() { _parts.clear(); }
    // The first part of that name and code; null if none.
    DialoguePart *find(std::string_view name, int code);
    std::vector<DialoguePart> &parts() { return _parts; }
    const std::vector<DialoguePart> &parts() const { return _parts; }
    // All the lines of a conversation, in table order.
    std::vector<const DialogueLine *> conversation(std::string_view name) const;

private:
    std::vector<DialoguePart> _parts;
};

// What a conversation needs from the game around it.
class DialogueHost {
public:
    virtual ~DialogueHost() = default;
    // Starts a line's recording; false if there's none.
    virtual bool playVoice(const std::string &file) = 0;
    virtual bool voicePlaying() = 0;
    virtual void stopVoice() = 0;
    // A file's text ("" if there's none): the lines' event files.
    virtual std::string readText(const std::string &file) = 0;
    // The scene's name ("salon_01"), for the "met them here" tags.
    virtual std::string sceneName() = 0;
    // A speaker turns to look as its line's Target says ("antoinette up"),
    // or (empty) back home at the end.
    virtual void turnHead(const std::string &speaker, const std::string &target) {
        (void)speaker;
        (void)target;
    }
    // The speaker's talking: a line starts (talkidle1: 0x404720) and ends
    // (0x404760); the mouth moves or stops (0x4046c0, 0x4046f0).
    virtual void lineStarted(const std::string &speaker) { (void)speaker; }
    virtual void lineEnded(const std::string &speaker) { (void)speaker; }
    virtual void mouth(const std::string &speaker, bool on) {
        (void)speaker;
        (void)on;
    }
    // The voice's level now (0x425140): 0 silent, more when it's loud; -1
    // if unknown.
    virtual int voiceLevel() { return -1; }
    // A conversation found and starting (not a dry run): the inventory
    // window closes and what's in hand goes back (0x43a670).
    virtual void talkStarting(const std::string &name) { (void)name; }
};

class DialogueManager {
public:
    DialogueManager(GameState &state, ScriptHost &scripts, DialogueHost &host);

    DialogueBook &book() { return _book; }

    // "talk <name> <code> [options]" (0x40ba25 -> 0x43a630): part, thread
    // and nest from the code's letters (-1 for none), `gen` to offer the
    // generic lines (an NPC clicked, or forcegen), `number` to pick an
    // alternative (1 on, -1 at random), `plus` to go on to the next part
    // when one runs dry, `greet` to play the opening. True if it started;
    // then `runner` hears "talk" done when it's over.
    bool talk(ScriptRunner &runner, const std::string &name, int part, int thread, int nest, bool gen, int number,
              bool plus, bool greet);
    // Whether that "talk" would start: the "cantalk" condition's dry run
    // (0x407f24), nothing shown or said. False while a conversation is on.
    bool canTalk(const std::string &name, int part, int thread, int nest, bool gen, int number, bool plus, bool greet);
    // The "nothing to say" line (0x43a550), for an NPC with nothing left.
    bool noTalk(ScriptRunner &runner);
    // Moves things on by `seconds`.
    void update(float seconds);
    // A click: a choice, or the line cut short (after half a second).
    void click(int x, int y);
    void mouseMove(int x, int y);
    // The mouse wheel while choosing (0x43d810, 0x43d850): `steps`
    // notches, away from the user (> 0) to the choice before, towards
    // (< 0) to the one after. Its choice stays until the mouse leaves
    // (x, y).
    void wheel(int steps, int x, int y);
    // Whether the wheel chose last and the mouse hasn't moved since: the
    // cursor is hidden then (0x43d7f0).
    bool wheelChoosing() const { return _wheelX != -1 && _wheelY != -1; }
    // The middle button (the wheel pressed): the highlighted choice taken
    // (0x43d890).
    void middleClick();
    // Without a mouse (tools): choice i taken, or the line cut short as a
    // click would; false if neither can be done now.
    bool choose(int i);
    // Whether a choice or a click would be taken now (no box sliding, no
    // Prevent or pause under way).
    bool ready() const { return _slide == 0 && !_waitPrevent && !_pending && _pause < 0; }
    // Whether a click now would end the line being said (click()).
    bool lineSkippable() const;

    // A tag every "met them here" tag of the scene is set to when it's
    // left (0x43aee0).
    void leaveScene(const std::string &scene);

    // What to draw.
    bool active() const { return _active; }
    bool boxShown() const;
    int boxTop() const { return int(_boxY); }
    int boxHeight() const { return _boxHeight; }
    int window() const { return _window; } // 0: dialoguewin01, 1: dialoguewin02
    bool choosing() const { return _choosing; }
    // The line being said and its colour (fonts.txt's: 8 Antoinette, 9 and
    // 10 the others).
    const DialogueLine *line() const;
    int colour() const { return _colour; }
    // Whether the line's text is drawn now (0x43cf90): not while its
    // Prevent or a Povent runs, nothing is pending, nor in the pause before
    // the next line. The box is drawn empty meanwhile.
    bool lineShown() const { return !_waitPrevent && !_pending && _pause < 0 && _povent.idle(); }
    struct Choice {
        std::string text;
        DialoguePart *part;
        int thread;
        // A picture shown for it too (its line's Notes, an image's path:
        // act 8's evidence), else empty.
        std::string picture;
    };
    const std::vector<Choice> &choices() const { return _choices; }
    int highlighted() const { return _highlighted; }
    // Where each choice is, from the box's drawing, for the mouse: its
    // text row, and its picture if it has one; a picture is tried before
    // the rows (0x43c810).
    struct ChoiceArea {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        int choice = -1;
        bool picture = false;
    };
    void setChoiceAreas(std::vector<ChoiceArea> areas) { _choiceAreas = std::move(areas); }
    const std::vector<ChoiceArea> &choiceAreas() const { return _choiceAreas; }

    // For a log.
    std::function<void(const std::string &line)> onLog;

private:
    using Thread = DialoguePart::Thread;
    bool start(const std::string &name, int part, int thread, int nest, int number, bool plus, bool greet, bool retry);
    int pickLine(Thread &t, int nest, bool forward);
    bool available(Thread &t);
    // Whether it has a line to offer as a choice: its current line, else
    // its first nest's (set as its current line); 0x43a9d9, 0x43a4d3.
    bool offered(Thread &t);
    void loadProgress(DialoguePart &p);
    void saveProgress(const DialoguePart &p, const Thread &t);
    std::string genPrefix(const DialoguePart &p) const;
    void addExit(DialoguePart &p);
    void addRegreet(DialoguePart &p, int visit);
    std::vector<std::pair<DialoguePart *, int>> extras();
    bool take(int choice);
    void menu(bool first, bool keepBox);
    void play(DialoguePart *p, int thread, int line);
    void startLine();
    void speak();
    void lineEnd();
    void end(bool keepBox);
    void finishTalk();
    void showBox();
    void log(const std::string &s) const {
        if (onLog)
            onLog(s);
    }

    GameState &_state;
    DialogueHost &_host;
    DialogueBook _book;
    ScriptRunner _prevent;
    ScriptRunner _events;
    ScriptRunner _povent;
    ScriptRunner *_caller = nullptr; // the runner whose "talk" this is

    DialoguePart *_part = nullptr;
    // indices into part_
    int _thread = -1;
    int _line = -1;
    bool _active = false;
    bool _choosing = false;
    bool _playing = false;
    bool _firstOffer = true;
    bool _plus = false;
    bool _goOn = false;
    bool _gen = false;
    bool _greet = true;
    int _number = -1;
    int _lastAlternative = -1;
    bool _dry = false; // a dry run (cantalk)
    bool _waitPrevent = false;
    bool _voice = false;
    bool _showWhilePovent = true;
    float _textLeft = 0;
    float _lineTime = 0;
    float _pause = -1;
    int _pending = 0; // after the Povent: 1 end the talk, 2 the next line, 3 the menu
    int _colour = 8;
    std::string _lastSpeaker;
    std::string _speaker; // whose line plays
    int _level = -1;      // the voice's last level
    std::string _prefix;  // gen_<npc>_
    std::vector<std::string> _turned;
    // The box.
    float _boxY = 768;
    // slide: 1 in, 2 out
    int _boxHeight = 160;
    int _window = 0;
    int _slide = 0;
    // Choices.
    std::vector<Choice> _choices;
    std::vector<ChoiceArea> _choiceAreas;
    int _highlighted = -1;
    // where the mouse was when the wheel chose
    int _wheelX = -1;
    int _wheelY = -1;
    unsigned _random = 1;
    int rand();
};

} // namespace reluva::awe
