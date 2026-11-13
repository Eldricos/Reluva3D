// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "common/text.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

// What the campaign drives between activities: the bridge (the star map).
// The campaign hands it the script's bridge commands and asks it what
// the script's conditions and waits need to know.
class CampaignWorld {
public:
    virtual ~CampaignWorld() {}
    // Run a bridge command: its name in lower case and its arguments as
    // written (SetText's text is the rest of the line). Most bring the
    // bridge up.
    virtual void bridgeCommand(const std::string &command, const std::vector<std::string> &args) = 0;
    // The bridge is up: a bridge command ran since the last activity.
    virtual bool isBridgeShown() const = 0;
    virtual bool isEnterpriseAt(const std::string &sun) const = 0;
    virtual bool getObjectFlag(const std::string &object, int flag) const = 0;
    // The stardate shown.
    virtual double getStardate() const = 0;
    virtual bool isAudioQueueEmpty() const = 0;
    virtual bool isWarping() const = 0;
    virtual bool isZooming() const = 0;
    // The Enterprise is at a star, not travelling.
    virtual bool isEnterpriseIdle() const = 0;
};

// The campaign: sgens.exe's game manager (0x419250), which runs
// DESIGN/STATES.INI from STELLAR.DAT. Each [section] is a list of
// commands; some start a full-screen activity (a movie, an away mission,
// a space combat), and the script carries on from the next line, or from
// the section the outcome names, when it ends. The rest drive the bridge:
// while it is up the script runs one command a frame, and Wait holds it
// until what it waits for has happened.
//
// With no world (setWorld), the bridge commands are passed over:
// OnTransport is taken at once and OnTactical never is.
class Campaign {
public:
    // What the engine is asked to do next.
    struct Action {
        enum Type {
            kNone,        // the script has stopped, or waits on an activity
            kBridge,      // show the bridge for a frame, then call next() again
            kIntro,       // INTRO1.BMP
            kMovie,       // MOVIE/MOV_nn.AVI
            kActionGame,  // away mission n (a location's type); call missionEnded() after
            kSpaceCombat, // space combat n; call combatEnded() after
            kScrollText,  // TEXTn.TXT
            kOptions,     // the menu, with nothing to return to
            kEndGame,     // credits, then quit
            kAutoSave     // save to the autosave slot
        };
        Type type = kNone;
        int number = 0;
    };
    // Space combat results (0x418F7A).
    enum CombatResult { kCombatLost = 0, kCombatWon = 1, kEnterpriseDestroyed = 2 };

    // Read the script's text; false if it has no [Start].
    bool load(const std::string &text);
    bool isLoaded() const { return !_sections.empty(); }
    bool hasSection(const std::string &name) const { return _sections.count(text::toLower(name)) != 0; }
    // The sections, in lower case.
    std::vector<std::string> getSectionNames() const;
    void setWorld(CampaignWorld *world) { _world = world; }

    // Start the script at a section (e.g. "Start" or "NewGame"), with everything reset.
    void start(const std::string &section);
    // Run up to the next thing the engine must do.
    Action next();
    // An away mission asked for by next() ended; clue: it left the story a
    // clue (any SET_MISSION_OUTCOME code), which IfClue tests.
    void missionEnded(bool accomplished, bool clue = false);
    // A space combat asked for by next() ended.
    void combatEnded(CombatResult result);

    // The bridge's TRANSPORT button: beam down where OnTransport said.
    void transport();
    // Engage a fleet (GM 0x419610): -1 the one OnTactical names, a roaming
    // fleet (13..24) for a fight the script returns from, or a scripted one.
    void startCombat(int fleet);

    bool isRunning() const { return _running; }
    // The section running, for saved games.
    const std::string &getSection() const { return _section; }
    // Start the section again from its first line, as loading a game does
    // (0x419703); or, if it was saved in a space combat, that combat again
    // from its start, the original's "save game at start of current
    // encounter".
    void restartSection();
    // The space combat being fought, or 0.
    int getCombat() const { return _combat; }

    bool getFlag(int flag) const { return _flags.count(flag) != 0; }
    void setFlag(int flag, bool value);

    // Random choices (JumpRandom); returns 0..count-1. Defaults to the first.
    void setRandom(std::function<int(int count)> random) { _random = std::move(random); }

    void sync(Serializer &s);

private:
    enum Result {
        kNext,    // go on to the next line
        kAction,  // the line asks the engine for something
        kBlocked, // a Wait that isn't over: run the line again next frame
    };

    void jump(const std::string &section);
    Result execute(const std::vector<std::string> &words, Action &action);
    // The conditions in front of a command (IfFlag 5 IfNotFlag 15 Jump X); false if one fails.
    bool test(const std::vector<std::string> &words, size_t &i) const;
    bool isWaitOver(const std::string &kind) const;

    std::map<std::string, std::vector<std::vector<std::string>>> _sections;
    CampaignWorld *_world = nullptr;
    std::string _section;
    size_t _line = 0;
    bool _running = false;
    std::set<int> _flags;
    std::set<int> _played, _won, _lost;
    std::set<std::string> _destroyed;
    std::set<std::string> _objectFlags;
    std::vector<std::pair<std::string, int>> _stack;
    // The activity running, with where to go after it.
    std::string _wonSection, _lostSection;
    int _mission = 0;
    int _combat = 0;
    // Set by restartSection() in a combat: next() starts it again.
    bool _replayCombat = false;
    bool _clue = false;
    // The states the bridge's context button leads to.
    std::string _onTransport, _onTactical;
    // SDTimer's deadline, a stardate.
    double _deadline = 0;
    std::function<int(int)> _random;
};

} // namespace reluva::stgenerations
