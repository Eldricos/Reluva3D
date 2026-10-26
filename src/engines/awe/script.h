// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/conditions.h"

#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The game's script runner (motoe.exe 0x409ce0, "event done" 0x409a80):
// commands queued as events, run one at a time. An event either finishes
// at once or waits for what it started (a walk, a fade, a line of
// dialogue) to say it's over; then the next one runs, on the next update
// (a fade at once). A verb ending in "a" (walka, talka, fadea...) starts
// the same thing without waiting. See docs/formats/awe/script.md.

namespace reluva::awe {

struct ScriptEvent {
    std::vector<std::string> words; // the verb, then its arguments
    const std::string& verb() const { return words.front(); }
    // An argument from 1, "" if there isn't one.
    const std::string& arg(size_t i) const;
};

class ScriptRunner;

// Adds `seconds` to every timer ("time <n>", and each second that passes).
void addTime(GameState& state, int seconds);

// What scripts do to the world, for the runner to call.
class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    // Starts what `e` asks (its verb without the "a" of the verbs that
    // have one). Returns true if it goes on, to tell the runner with
    // ScriptRunner::finished() when it's over; false if it's done already.
    // `wait` is false for the "a" verbs: their finishing isn't waited for.
    // `runner` is the one to tell.
    virtual bool start(ScriptRunner& runner, const ScriptEvent& e, bool wait) = 0;
    // The text of text\scripts\<name>.txt, for "script <name>"; "" if
    // there's none.
    virtual std::string scriptText(const std::string& name) {
        (void)name;
        return {};
    }
};

class ScriptRunner {
public:
    ScriptRunner(GameState& state, ScriptHost& host) : state_(state), host_(host) {}

    // Queues commands: a script cell's ("walk 31 0, scene salon_02 1") or
    // an event file's, one a line. As the game adds them (0x409024), two
    // are macros, expanded here, and "script <name>" adds a script file's:
    //   dooropen <point> <L|R> <n>: walk to the point, turn from it, the
    //     door knob's animation, fade out, the door's sound n...
    //   doorlocked <point> <L|R> <n>: the same, but the door stays shut.
    void run(std::string_view commands);
    // Queues commands right after the event under way (the dialogue's
    // terminating Povent: 0x409440).
    void runNext(std::string_view commands);
    // Runs what's queued through at once: each event started and ended,
    // until none is left (0x409810, a line's events when it ends).
    void fastForward();
    // Whether the commands came from clicking a character (the game's
    // runner +0xf4, set by 0x408c80): "talk" offers its generic lines then.
    void setFromCharacter(bool from) { fromCharacter_ = from; }
    bool fromCharacter() const { return fromCharacter_; }
    // Runs the next event if none is under way, and counts down a wait.
    void update(float seconds);
    // What the event under way started is over (its verb, as start() had
    // it). Anything else is ignored, as the game ignores it.
    void finished(std::string_view verb);
    bool idle() const { return queue_.empty() && !running_; }
    // The event under way is dropped from being under way, kept at the
    // front: the next update starts it again ("pause" of a character's
    // routine: 0x4057f0).
    void restart() {
        running_ = false;
        waitingFor_.clear();
        waitLeft_ = -1;
    }
    void clear();

    // The character the events act on (setchar), "" for the player's.
    const std::string& character() const { return character_; }
    void setCharacter(const std::string& who) { character_ = who; }
    // When an event ends and leaves nothing queued (the main runner's
    // acts check their completion then: 0x409a80).
    std::function<void()> onEmpty;
    // Each event as it starts, for a log.
    std::function<void(const ScriptEvent& e)> onEvent;

    // The verbs whose "a" form doesn't wait: walk and walka, talk and
    // talka... "" if `verb` isn't one of those forms.
    static std::string_view baseOfAsync(std::string_view verb);

private:
    void start();
    void add(std::vector<std::string> words, int depth);
    // The verbs the runner does itself (the game's state); false if
    // `e` isn't one.
    bool runOwn(const ScriptEvent& e);

    GameState& state_;
    ScriptHost& host_;
    std::deque<ScriptEvent> queue_;
    bool running_ = false;      // the front event is under way
    std::string waitingFor_;    // the verb it waits on
    float waitLeft_ = -1;       // a "wait"'s seconds left
    std::string character_;
    bool fromCharacter_ = false;
};

} // namespace reluva::awe
