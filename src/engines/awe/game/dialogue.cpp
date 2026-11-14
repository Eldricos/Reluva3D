// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/game/dialogue.h"

#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace reluva::awe {

namespace {

// threads 1, 2, 3
constexpr int kOpening = 0x60;
constexpr int kRegreet = 0x61;
constexpr int kExit = 0x62;
constexpr float kBoxSpeed = 500;   // pixels a second, sliding in and out
constexpr float kPause = 0.4f;     // between lines
constexpr float kSkipAfter = 0.5f; // a click ends a line only after this

bool isNumber(std::string_view s) {
    return !s.empty() && std::isdigit(uint8_t(s[0]));
}
int letter(char c) {
    return std::toupper(uint8_t(c)) - 'A';
}

// A cell, "" for the tables' "0" (none).
std::string cellOf(const Table &t, size_t row, int column) {
    const std::string &c = t.cell(row, column);
    return c == "0" ? std::string() : c;
}

bool contains(std::string_view hay, std::string_view needle) {
    return fs::toLower(hay).find(fs::toLower(needle)) != std::string::npos;
}

// A rule's name with the character's in place of its "%s".
std::string named(std::string_view format, std::string_view who) {
    std::string out(format);
    if (const size_t at = out.find("%s"); at != std::string::npos)
        out.replace(at, 2, who);
    return out;
}

// The code's text, as the event files are named (0x43c100).
std::string partText(int code) {
    return code >= 100 ? std::to_string(code - 99) : std::string(1, char('a' + code));
}
std::string threadText(int code) {
    if (code >= 0x60)
        return std::to_string(code - 0x5f);
    if (code >= 26)
        return "z" + std::to_string(code - 25);
    return std::string(1, char('a' + code));
}

} // namespace

int dialoguePartCode(std::string_view s) {
    if (s.empty())
        return 100;
    return isNumber(s) ? std::atoi(std::string(s).c_str()) + 99 : letter(s[0]);
}

int dialogueThreadCode(std::string_view s) {
    if (s.empty())
        return 0;
    if (isNumber(s))
        return std::atoi(std::string(s).c_str()) + 0x5f;
    return letter(s[0]) + (s.size() > 1 && std::isdigit(uint8_t(s[1])) ? s[1] - '0' : 0);
}

int dialogueNestCode(std::string_view s) {
    if (s.empty())
        return 0;
    if (isNumber(s))
        return std::atoi(std::string(s).c_str()) + 0x5f;
    return letter(s[0]) + (s.size() > 1 ? std::toupper(uint8_t(s[1])) - '@' : 0);
}

std::string DialogueLine::voiceFile() const {
    std::string f = "sound/vo/" + fs::toLower(name + act + part + thread + nest);
    if (alternative > 0)
        f += "_" + std::to_string(alternative);
    return f + ".wav";
}

void DialogueBook::add(const Table &t) {
    // By position, as the game reads them (0x438f00), whatever the
    // headings say: gen_characters.txt's first is "gen_globalhardman", not
    // Name. gen_characters.txt and gen_antoinette.txt have no Optional
    // column, so their Notes land there (and count as 0).
    const int name = 0;
    const int act = 1;
    const int part = 2;
    const int thread = 3;
    const int nest = 4;
    const int speaker = 5;
    const int target = 6;
    const int line = 7;
    const int translation = 8;
    const int condition = 9;
    const int repeat = 10;
    const int before = 11;
    const int event = 12;
    const int after = 13;
    const int duration = 14;
    const int optional = 15;
    const int notes = 16;
    DialoguePart *p = nullptr;
    for (size_t r = 0; r < t.rows.size(); r++) {
        DialogueLine l;
        l.name = fs::toLower(t.cell(r, name));
        if (l.name.empty())
            continue;
        l.act = fs::toLower(t.cell(r, act));
        l.part = fs::toLower(t.cell(r, part));
        l.thread = fs::toLower(t.cell(r, thread));
        l.nest = fs::toLower(t.cell(r, nest));
        l.speaker = fs::toLower(cellOf(t, r, speaker));
        if (tables.target)
            l.target = fs::toLower(cellOf(t, r, target));
        l.text = t.cell(r, line);
        if (tables.translation && !cellOf(t, r, translation).empty())
            l.text = t.cell(r, translation);
        l.condition = cellOf(t, r, condition);
        l.repeat = cellOf(t, r, repeat);
        if (tables.optional)
            l.optional = cellOf(t, r, optional);
        l.notes = cellOf(t, r, notes);
        l.before = cellOf(t, r, before);
        l.event = cellOf(t, r, event);
        l.after = cellOf(t, r, after);
        // "a, b": the movies' start and length; the second is a line's time.
        const std::string d = tables.duration ? t.cell(r, duration) : std::string();
        l.duration = d.find(',') != std::string::npos ? float(std::atof(d.c_str() + d.find(',') + 1)) : 0;
        l.partCode = dialoguePartCode(l.part);
        l.threadCode = dialogueThreadCode(l.thread);
        l.nestCode = dialogueNestCode(l.nest);
        l.repeatCode = !l.repeat.empty() && std::isalpha(uint8_t(l.repeat[0])) ? letter(l.repeat[0]) : -1;
        if (!p || p->name != l.name || p->code != l.partCode) {
            _parts.push_back(DialoguePart{l.name, l.act, l.partCode, {}});
            p = &_parts.back();
        }
        if (p->threads.empty() || p->threads.back().code != l.threadCode) {
            p->threads.emplace_back();
            p->threads.back().code = l.threadCode;
        }
        auto &lines = p->threads.back().lines;
        if (!lines.empty() && lines.back().nestCode == l.nestCode)
            l.alternative = lines.back().alternative + 1;
        lines.push_back(std::move(l));
    }
}

DialoguePart *DialogueBook::find(std::string_view name, int code) {
    const std::string n = fs::toLower(name);
    for (DialoguePart &p : _parts)
        if (p.name == n && p.code == code)
            return &p;
    return nullptr;
}

std::vector<const DialogueLine *> DialogueBook::conversation(std::string_view name) const {
    const std::string n = fs::toLower(name);
    std::vector<const DialogueLine *> out;
    for (const DialoguePart &p : _parts)
        if (p.name == n)
            for (const auto &t : p.threads)
                for (const DialogueLine &l : t.lines)
                    out.push_back(&l);
    return out;
}

TalkArgs parseTalkArgs(const std::vector<std::string> &words, bool gen, const DialogueRules &rules) {
    TalkArgs t;
    t.gen = gen;
    auto word = [&](size_t i) { return i < words.size() ? fs::toLower(words[i]) : std::string(); };
    t.name = words.empty() ? std::string() : words[0];
    const std::string code = word(1);
    if (!code.empty() && std::isalpha(uint8_t(code[0]))) {
        t.part = std::toupper(uint8_t(code[0])) - 'A';
        if (code.size() > 1 && (code[1] != '+' || !rules.greetAndPlus))
            t.thread = std::toupper(uint8_t(code[1])) - 'A';
        if (code.size() > 2 && (code[2] != '+' || !rules.greetAndPlus))
            t.nest = std::toupper(uint8_t(code[2])) - 'A';
    } else if (!code.empty()) {
        t.part = std::atoi(code.c_str()) + 0x63;
    }
    t.plus = rules.greetAndPlus && !code.empty() && code.back() == '+';
    for (int k = 0; k < rules.talkOptions; k++) {
        const std::string opt = word(size_t(2 + k));
        if (opt.empty())
            continue;
        if (opt == "nogen")
            t.gen = false;
        else if (opt == "forcegen")
            t.gen = true;
        else if (opt == "nogreet" && rules.greetAndPlus)
            t.greet = false;
        else
            t.number = std::atoi(opt.c_str());
    }
    return t;
}

// --- The manager -----------------------------------------------------------------

DialogueManager::DialogueManager(GameState &state, ScriptHost &scripts, DialogueHost &host)
    : _state(state), _host(host), _prevent(state, scripts), _events(state, scripts), _povent(state, scripts) {
    // "talk <name> <act> <part>": every line not Optional said (0x439e40).
    _state.partTold = [this](const std::string &name, const std::string &act, int part) {
        DialoguePart *p = _book.find(name, part);
        if (!p)
            return false;
        for (int code = 0;; code++) {
            const auto t = std::find_if(p->threads.begin(), p->threads.end(),
                                        [code](const Thread &th) { return th.code == code; });
            if (t == p->threads.end())
                return true;
            const auto rec = _state.talks.find(talkKey(name, act, part, code));
            for (const DialogueLine &l : t->lines) {
                if (std::atoi(l.optional.c_str()) != 0)
                    continue;
                const bool passed = rec != _state.talks.end() && (rec->second.status != 0 || rec->second.nest == -1 ||
                                                                  rec->second.nest > l.nestCode);
                if (!passed) {
                    log("required line not said: " + l.text);
                    return false;
                }
            }
        }
    };
}

void DialogueManager::setGame(const GameInfo &game) {
    _book.tables = game.dialogue;
    _rules = game.talk;
    _frameH = game.height;
    _boxY = float(_frameH);
}

int DialogueManager::rand() {
    _random = _random * 214013u + 2531011u;
    return int((_random >> 16) & 0x7fff);
}

const DialogueLine *DialogueManager::line() const {
    if (!_part || _thread < 0 || _line < 0 || _choosing)
        return nullptr;
    return &_part->threads[size_t(_thread)].lines[size_t(_line)];
}

bool DialogueManager::boxShown() const {
    if (!_active)
        return false;
    if (_povent.idle() || _showWhilePovent)
        return true;
    return _choosing;
}

// The line of a thread at `nest` whose condition holds; several: an
// alternative, the one asked for or another at random (0x439ca0).
int DialogueManager::pickLine(Thread &t, int nest, bool forward) {
    for (;; nest++) {
        bool any = false;
        std::vector<int> ok;
        for (size_t i = 0; i < t.lines.size(); i++)
            if (t.lines[i].nestCode == nest) {
                any = true;
                if (testCondition(t.lines[i].condition, _state))
                    ok.push_back(int(i));
            }
        if (!any)
            return -1;
        if (ok.size() == 1)
            return ok[0];
        if (ok.size() > 1 && !_rules.alternativeKept) {
            // And Then There Were None's (0x431f48): the one asked for, else
            // one at random other than the last, kept no longer; its place
            // counts among all the nest's lines, said or not.
            int i = _number;
            if (i == -1) {
                do
                    i = rand() % int(ok.size());
                while (i == _lastAlternative);
            } else {
                i--;
            }
            _lastAlternative = i;
            int seen = 0;
            for (size_t k = 0; k < t.lines.size(); k++)
                if (t.lines[k].nestCode == nest && seen++ == i)
                    return int(k);
            return ok.back(); // past them: the last that may be said
        }
        if (ok.size() > 1) {
            int i = _number;
            if (i == -1 || i != _lastAlternative) {
                if (i == -1) {
                    do
                        i = rand() % int(ok.size());
                    while (ok.size() > 1 && i == _lastAlternative);
                } else {
                    i = std::clamp(i - 1, 0, int(ok.size()) - 1);
                }
            }
            _number = _lastAlternative = i;
            return ok[size_t(std::clamp(i, 0, int(ok.size()) - 1))];
        }
        if (!forward)
            return -1;
    }
}

// Whether a thread is offered (0x43b870).
bool DialogueManager::available(Thread &t) {
    if (t.code == kExit)
        return true;
    if (!_rules.offerLooksOn) {
        // And Then There Were None's (0x433040): a finished thread if it
        // has a line to come back to, in the first menu; then its first
        // nest's line, the alternative the asking picks put back.
        if (t.status == 1 && (t.current < 0 || !_firstOffer))
            return false;
        const int number = _number;
        const int last = _lastAlternative;
        const bool ok = pickLine(t, 0, false) >= 0;
        _number = number;
        _lastAlternative = last;
        return ok;
    }
    if (t.status == 1) {
        if (t.current < 0 || !_firstOffer || !testCondition(t.lines[size_t(t.current)].condition, _state))
            return false;
    }
    return pickLine(t, 0, true) >= 0;
}

bool DialogueManager::offered(Thread &t) {
    if (t.current < 0)
        t.current = pickLine(t, 0, false);
    return t.current >= 0;
}

void DialogueManager::loadProgress(DialoguePart &p) {
    for (Thread &t : p.threads) {
        t.status = 0;
        t.current = -1;
        const auto rec = _state.talks.find(talkKey(p.name, p.act, p.code, t.code));
        if (rec == _state.talks.end())
            continue;
        t.status = rec->second.status;
        if (rec->second.nest != -1)
            t.current = pickLine(t, rec->second.nest, true);
    }
}

void DialogueManager::saveProgress(const DialoguePart &p, const Thread &t) {
    if (p.code >= 99 || t.code == kExit)
        return;
    TalkProgress rec{t.status, t.current >= 0 ? t.lines[size_t(t.current)].nestCode : -1};
    if (t.borrowed)
        _state.talks[talkKey(t.fromName, "0", t.fromCode, t.code)] = rec;
    else
        _state.talks[talkKey(p.name, p.act, p.code, t.code)] = rec;
}

// The first speaker of the part who isn't the player (0x43a3d0).
std::string DialogueManager::npcOf(const DialoguePart &p) const {
    for (const Thread &t : p.threads)
        for (const DialogueLine &l : t.lines)
            if (!l.speaker.empty() && l.speaker != _rules.player)
                return l.speaker;
    return {};
}

// A new exit thread: one of the NPC's goodbyes (0x43af20).
void DialogueManager::addExit(DialoguePart &p) {
    p.threads.erase(std::remove_if(p.threads.begin(), p.threads.end(), [](const Thread &t) { return t.code == kExit; }),
                    p.threads.end());
    std::string name = named(_rules.goodbyes, _npc);
    int r = rand() % 4;
    // After dark (the time of day, word[0x4986f8], above 0) the fourth
    // becomes the fifth, "Good evening" (0x43af67).
    if (const auto t = _state.values.find("time"); t != _state.values.end() && t->second > 0 && r == 3)
        r = 4;
    if (_rules.finale && testCondition(_rules.finale, _state))
        name = "gen_solutionexit", r = 0;
    auto nth = [&](const std::string &n) -> const Thread * {
        int seen = 0;
        for (const DialoguePart &q : _book.parts())
            if (q.name == n)
                for (const Thread &t : q.threads)
                    if (t.code == kExit && seen++ == r)
                        return &t;
        return nullptr;
    };
    const Thread *from = nth(name);
    if (!from && _rules.goodbyesElse)
        from = nth(_rules.goodbyesElse);
    if (!from)
        return;
    Thread t = *from;
    t.borrowed = true;
    t.status = 0;
    t.current = -1;
    p.threads.push_back(std::move(t));
}

// A new "again" thread from gen_regreet1 (this visit) or 2 (0x43b180).
void DialogueManager::addRegreet(DialoguePart &p, int visit) {
    p.threads.erase(
        std::remove_if(p.threads.begin(), p.threads.end(), [](const Thread &t) { return t.code == kRegreet; }),
        p.threads.end());
    const std::string name = "gen_regreet" + std::to_string(visit);
    const int r = rand() % 4;
    int seen = 0;
    for (const DialoguePart &q : _book.parts())
        if (q.name == name)
            for (const Thread &t : q.threads)
                if (t.code == kRegreet && seen++ == r) {
                    Thread copy = t;
                    copy.borrowed = true;
                    copy.status = 0;
                    copy.current = -1;
                    p.threads.push_back(std::move(copy));
                    return;
                }
}

// The NPC's generic extra threads (gen_<npc>_print...), offered beside the
// conversation's own (0x43a300, 0x43a470).
std::vector<std::pair<DialoguePart *, int>> DialogueManager::extras() {
    std::vector<std::pair<DialoguePart *, int>> out;
    if (!_gen || _prefix.empty() || !_part || _part->code >= 100)
        return out;
    // Each available one with a line to offer now (0x43a470: its current
    // line, else its first one whose condition holds; none, it's left out).
    for (DialoguePart &q : _book.parts())
        if (&q != _part && q.code < 99 && isGeneric(q.name))
            for (size_t i = 0; i < q.threads.size(); i++)
                if (Thread &t = q.threads[i]; t.code < 0x60 && available(t) && offered(t))
                    out.push_back({&q, int(i)});
    return out;
}

bool DialogueManager::talk(ScriptRunner &runner, const std::string &name, int part, int thread, int nest, bool gen,
                           int number, bool plus, bool greet) {
    _gen = gen;
    if (!start(name, part, thread, nest, number, plus, greet, false))
        return false;
    _caller = &runner;
    return true;
}

bool DialogueManager::canTalk(const std::string &name, int part, int thread, int nest, bool gen, int number, bool plus,
                              bool greet) {
    // Start in a dry run (+0x61c), nothing shown or said, and what it
    // changed put back (0x43a600).
    if (_active)
        return false;
    const bool genBefore = _gen;
    const bool plusBefore = _plus;
    const bool greetBefore = _greet;
    const int numberBefore = _number;
    const int alternativeBefore = _lastAlternative;
    const int height = _boxHeight;
    const int win = _window;
    const std::string prefixBefore = _prefix;
    const std::string npcBefore = _npc;
    _dry = true;
    _gen = gen;
    const bool would = start(name, part, thread, nest, number, plus, greet, false);
    _dry = false;
    _active = false;
    _part = nullptr;
    _gen = genBefore, _plus = plusBefore, _greet = greetBefore;
    _number = numberBefore, _lastAlternative = alternativeBefore, _boxHeight = height, _window = win;
    _prefix = prefixBefore;
    _npc = npcBefore;
    return would;
}

bool DialogueManager::noTalk(ScriptRunner &runner) {
    // gen_<npc>_notalk; with no NPC, Antoinette's own "nothing to say"
    // (the game's test makes that always gen_canttalkf). The character's
    // name is the last talk's that found its part, whoever this is.
    int part = 100;
    const std::string name = _rules.noTalk      ? _rules.noTalk(_prefix, part)
                             : !_prefix.empty() ? _prefix + "notalk"
                                                : "gen_canttalkf";
    return talk(runner, name, part, -1, -1, true, -1, false, true);
}

bool DialogueManager::isGeneric(const std::string &name) const {
    return _rules.genericExact ? name == _prefix : contains(name, _prefix);
}

// 0x43a630.
bool DialogueManager::start(const std::string &name, int part, int thread, int nest, int number, bool plus, bool greet,
                            bool retry) {
    DialoguePart *p = _book.find(name, part);
    if (!p)
        return false;
    if (!_dry) {
        log("talk " + name + " " + partText(part));
        if (_rules.closesInventory)
            _host.talkStarting(name);
    }
    _part = p;
    _boxHeight = _rules.box, _window = 0;
    if (_rules.finale && testCondition(_rules.finale, _state))
        _boxHeight = _rules.finaleBox, _window = 1;
    _firstOffer = true;
    _active = true;
    _plus = plus;
    _greet = greet;
    _number = number;
    if (number != -1)
        _lastAlternative = -1;
    _lastSpeaker.clear();
    _goOn = false;
    loadProgress(*p);
    _npc = npcOf(*p);
    _prefix = _npc.empty() ? std::string() : named(_rules.generic, _npc);
    if (!_prefix.empty() && part < 100)
        for (DialoguePart &q : _book.parts())
            if (q.code < 99 && isGeneric(q.name))
                loadProgress(q);
    const bool hasExtras = !extras().empty();

    // A thread asked for, or a part of one thread and no extras: thread a
    // then (0x43a835), whatever the one thread is. dia_hubbardsalon and
    // dia_arbuthnotshoes (6B) have only an opening, so they never play:
    // the click falls back to the character's "nothing to say", as the
    // walkthrough recordings have it (Part 7 at 0:52 and 6:20).
    const bool single = p->threads.size() == 1 && !hasExtras;
    if (thread != -1 || single) {
        const int code = thread == -1 ? 0 : thread;
        int ti = -1;
        for (size_t i = 0; i < p->threads.size(); i++)
            if (p->threads[i].code == code) {
                ti = int(i);
                break;
            }
        if (ti < 0)
            return false;
        Thread &t = p->threads[size_t(ti)];
        const bool exhausted = (t.status != 0 && t.current < 0 && !_prefix.empty()) || !available(t);
        if (!exhausted) {
            int li = nest >= 0 ? pickLine(t, nest, true) : t.current >= 0 ? t.current : pickLine(t, 0, true);
            if (li >= 0) {
                if (_dry)
                    return true;
                showBox();
                play(p, ti, li);
                return true;
            }
        }
    } else {
        // The rows the choices would take (0x43a9ae): the available
        // lettered threads with a line to offer.
        int rows = 0;
        int lines = 0; // their text's rows, for a box sized by them
        for (Thread &t : p->threads)
            if (t.code < 0x60 && available(t) && offered(t)) {
                rows++;
                lines += _host.textRows(t.lines[size_t(t.current)].text);
            }
        for (const auto &[q, ti] : extras()) {
            const Thread &t = q->threads[size_t(ti)];
            rows++;
            lines += _host.textRows(t.lines[size_t(t.current)].text);
        }
        // "Met them here": 1 this visit, 2 an earlier one. A dry run
        // (cantalk) sets it too, as the game's goes on to here (+0x61c is
        // only tested by the inventory's changes, the required-line
        // message and the line's playing).
        const std::string key = fs::toLower(_host.sceneName()) + " " + p->name + " " + std::to_string(p->code);
        if (rows > 0 && _dry) {
            _state.tags[key] = 1;
            return true;
        }
        if (rows > 0) {
            if (_gen) {
                // The box's rows lose one for the part's own goodbye and gain
                // one for the new (0x432801, 0x43285e), though neither was
                // counted.
                const auto exits = [&] {
                    return int(std::count_if(p->threads.begin(), p->threads.end(),
                                             [](const Thread &t) { return t.code == kExit; }));
                };
                lines -= exits();
                addExit(*p);
                lines += exits();
            }
            sizeBox(lines);
            int start = -1;
            if (_rules.regreet) {
                const auto tag = _state.tags.find(key);
                const int visit = tag == _state.tags.end() ? -99 : tag->second;
                if (_gen && greet && part < 100 && visit > 0)
                    addRegreet(*p, std::min(visit, 2));
                _state.tags[key] = 1;
                for (size_t i = 0; i < p->threads.size(); i++)
                    if (p->threads[i].code == kOpening && !(p->threads[i].status == 1 && p->threads[i].current < 0))
                        start = int(i);
                if (start < 0)
                    for (size_t i = 0; i < p->threads.size(); i++)
                        if (p->threads[i].code == kRegreet)
                            start = int(i);
            } else {
                // The opening until it's been heard, then thread 2: a part's
                // own, none in the English release (0x4328fa).
                auto find = [&](int code) {
                    for (size_t i = 0; i < p->threads.size(); i++)
                        if (p->threads[i].code == code)
                            return int(i);
                    return -1;
                };
                start = find(kOpening);
                if (start >= 0 && p->threads[size_t(start)].status == 1)
                    start = find(kRegreet);
            }
            showBox();
            if (start >= 0 && greet) {
                Thread &t = p->threads[size_t(start)];
                const int li = t.current >= 0 ? t.current : pickLine(t, 0, true);
                if (li >= 0) {
                    play(p, start, li);
                    return true;
                }
            }
            menu(true, false);
            return true;
        }
    }
    // Nothing left: on to the next part, with "+".
    if (plus && !retry) {
        for (int k = part + 1;; k++) {
            if (!_book.find(name, k))
                break;
            if (start(name, k, -1, -1, -1, plus, greet, true))
                return true;
        }
    }
    _active = false;
    _part = nullptr;
    return false;
}

void DialogueManager::showBox() {
    if (_slide == 0 && _boxY >= float(_frameH))
        _slide = 1;
}

// A box sized by the rows of its choices' text, when the game's is
// (0x4328b6, 0x432efd): up to 3, 4, 5 or more.
void DialogueManager::sizeBox(int rows) {
    if (_rules.boxByRows[0] == 0)
        return;
    _window = rows > 4 ? 2 : rows > 3 ? 1 : 0;
    _boxHeight = _rules.boxByRows[size_t(_window)];
}

// The choices (0x43b560).
void DialogueManager::menu(bool first, bool keepBox) {
    _firstOffer = first;
    _choices.clear();
    int count = 0;
    int rows = 0; // their text's rows, for a box sized by them
    bool hasExit = false;
    for (size_t i = 0; i < _part->threads.size(); i++) {
        Thread &t = _part->threads[i];
        if (t.code == kOpening || t.code == kRegreet)
            continue;
        if (t.code == kExit)
            hasExit = true;
        if (!available(t))
            continue;
        if (t.current < 0)
            t.current = pickLine(t, 0, false);
        if (t.current < 0)
            continue;
        if (first && t.status != 0 && _rules.repeatsStayOffered)
            t.status = 0;
        count++;
        rows += _host.textRows(t.lines[size_t(t.current)].text);
    }
    const auto more = extras();
    count += int(more.size());
    for (const auto &[q, ti] : more)
        rows += _host.textRows(q->threads[size_t(ti)].lines[size_t(q->threads[size_t(ti)].current)].text);
    auto nextPart = [&] {
        // A next part with nothing to say leaves this one going (start()
        // drops the part it fails on).
        if (!_goOn)
            return false;
        DialoguePart *keep = _part;
        if (start(_part->name, _part->code + 1, -1, -1, -1, _plus, _greet, false))
            return true;
        _part = keep;
        _active = true;
        return false;
    };
    if (count == 0) {
        if (!nextPart())
            end(keepBox);
        return;
    }
    if (count == 1) {
        if (hasExit) {
            if (nextPart())
                return;
            for (size_t i = 0; i < _part->threads.size(); i++)
                if (_part->threads[i].code == kExit)
                    return play(_part, int(i), _part->threads[i].current);
        }
    }
    if (count == 1 && _rules.loneChoicePlays) {
        for (size_t i = 0; i < _part->threads.size(); i++) {
            Thread &t = _part->threads[i];
            if (t.code < 0x60 && t.current >= 0 && available(t))
                return play(_part, int(i), t.current);
        }
        if (!more.empty()) {
            Thread &t = more[0].first->threads[size_t(more[0].second)];
            return play(more[0].first, more[0].second, t.current >= 0 ? t.current : pickLine(t, 0, true));
        }
        return end(keepBox);
    }
    // Several: offered by their current lines, the exit last.
    int exitIndex = -1;
    for (size_t i = 0; i < _part->threads.size(); i++) {
        Thread &t = _part->threads[i];
        if (t.code == kExit) {
            exitIndex = int(i);
            continue;
        }
        if (t.code < 0x60 && t.current >= 0 && available(t)) {
            // A line whose Notes name a picture shows it too (0x4398d5:
            // ".jpg", ".png" or ".tga" in them).
            const DialogueLine &l = t.lines[size_t(t.current)];
            const std::string n = fs::toLower(l.notes);
            const bool picture = n.find(".jpg") != std::string::npos || n.find(".png") != std::string::npos ||
                                 n.find(".tga") != std::string::npos;
            _choices.push_back({l.text, _part, int(i), picture ? l.notes : std::string()});
        }
    }
    for (const auto &[q, ti] : more) {
        Thread &t = q->threads[size_t(ti)];
        if (t.current < 0)
            t.current = pickLine(t, 0, false);
        if (t.current >= 0)
            _choices.push_back({t.lines[size_t(t.current)].text, q, ti, {}});
    }
    if (exitIndex >= 0 && _part->threads[size_t(exitIndex)].current >= 0)
        _choices.push_back(
            {_part->threads[size_t(exitIndex)].lines[size_t(_part->threads[size_t(exitIndex)].current)].text,
             _part,
             exitIndex,
             {}});
    _choosing = true;
    _colour = 6;
    _highlighted = -1;
    _wheelX = _wheelY = -1;
    // A box sized by its choices takes its new height at once (0x43301a).
    sizeBox(rows);
    if (_rules.boxByRows[0] != 0 && _slide == 0 && _boxY < float(_frameH))
        _boxY = float(_frameH - _boxHeight);
    showBox();
}

void DialogueManager::play(DialoguePart *p, int thread, int line) {
    if (!p || line < 0)
        return end(false);
    _choosing = false;
    _choices.clear();
    _part = p;
    _thread = thread;
    _line = line;
    startLine();
}

// 0x43c070.
void DialogueManager::startLine() {
    const DialogueLine &l = _part->threads[size_t(_thread)].lines[size_t(_line)];
    // The player 8; the others 9, then 10 when the speaker changes. In And
    // Then There Were None the player's line starts the others' again
    // (0x433254).
    if (l.speaker.find(_rules.player) != std::string::npos) {
        _colour = 8;
        if (_rules.playerResetsColour)
            _lastSpeaker.clear();
    } else {
        if (!_lastSpeaker.empty() && l.speaker != _lastSpeaker)
            _colour = _colour == 9 ? 10 : 9;
        else if ((_colour != 9 && _colour != 10) || (_rules.playerResetsColour && _lastSpeaker.empty()))
            _colour = 9;
        _lastSpeaker = l.speaker;
    }
    _playing = true;
    _lineTime = 0;
    _prevent.clear();
    if (!l.before.empty()) {
        _prevent.setCharacter(l.speaker);
        _prevent.run(l.before);
        _waitPrevent = true;
        return;
    }
    speak();
}

// 0x43bd30, 0x43c100, 0x43c260.
void DialogueManager::speak() {
    const DialogueLine &l = _part->threads[size_t(_thread)].lines[size_t(_line)];
    log(l.speaker + ": " + l.text);
    if (!l.target.empty()) {
        _host.turnHead(l.speaker, l.target);
        if (std::find(_turned.begin(), _turned.end(), l.speaker) == _turned.end())
            _turned.push_back(l.speaker);
    }
    _speaker = l.speaker;
    _host.lineStarted(_speaker);
    _host.stopVoice();
    _voice = _host.playVoice(l.voiceFile());
    _level = -1;
    // Without a voice the mouth moves all along (0x4046c0); with one, as
    // its level does (0x43d3c0).
    if (!_voice) {
        _textLeft = l.duration > 0 ? l.duration : std::max(_rules.textMin, float(int(l.text.size()) / _rules.textRate));
        _host.mouth(_speaker, true);
    }
    // The Event column, else the line's event file, alongside the line.
    _events.clear();
    _events.setCharacter(l.speaker);
    if (!l.event.empty()) {
        _events.run(l.event);
    } else {
        const Thread &t = _part->threads[size_t(_thread)];
        const std::string file = _part->name + "_" + _part->act + "_" + partText(_part->code) + threadText(t.code) +
                                 std::string(1, char('a' + l.nestCode)) + ".txt";
        for (const char *folder : _book.tables.events) {
            const std::string text = _host.readText(folder + file);
            if (!text.empty()) {
                _events.run(text);
                break;
            }
        }
    }
}

// 0x43c310 -> 0x43c375.
void DialogueManager::lineEnd() {
    _host.stopVoice();
    _voice = false;
    _host.lineEnded(_speaker);
    _playing = false;
    _events.fastForward();
    Thread &t = _part->threads[size_t(_thread)];
    const DialogueLine &l = t.lines[size_t(_line)];
    const std::string &after = l.after;
    const bool juncture = contains(after, "juncture");
    const bool terminate = contains(after, "terminate");
    const bool advance = _rules.advance && contains(after, "advance");
    bool keepBox = false;
    const int next = pickLine(t, l.nestCode + 1, true);
    if (!after.empty()) {
        const bool talks = contains(after, _rules.keepBox);
        const bool leaves = contains(after, "leavebox");
        const bool drops = contains(after, _rules.dropBox);
        keepBox = _rules.leaveBoxWins ? (talks && !drops) || leaves : (talks || leaves) && !drops;
        _showWhilePovent = !(contains(after, "walk") || contains(after, "rotate"));
        // At the conversation's end it goes to the main script, after the
        // talk; else it runs while the dialogue waits.
        bool othersLeft = false;
        for (Thread &o : _part->threads)
            if (&o != &t && (o.code < 0x60 || (o.code == kExit && _rules.goodbyeLeft)) && available(o))
                othersLeft = true;
        const bool runsOut = next < 0 && !othersLeft && !(_plus && _book.find(_part->name, _part->code + 1));
        if ((contains(after, "mainloop") || terminate || runsOut) && _caller)
            _caller->runNext(after);
        else
            _povent.run(after);
    }
    t.current = _line;
    _goOn = (next < 0 && _plus) || advance;
    if (next >= 0 && !juncture && !terminate && !_goOn) {
        _line = next;
        if (!_povent.idle())
            _pending = 2;
        else if (contains(_part->name, "_movie"))
            startLine(); // no pause: at once (0x43c64d)
        else
            _pause = kPause;
        return;
    }
    // The thread stops here.
    t.status = juncture ? 0 : 1;
    if (next < 0) {
        if (l.repeatCode != -1)
            t.current = pickLine(t, l.repeatCode, false);
        else if (t.code == kExit)
            t.current = pickLine(t, 0, false);
        else
            t.current = -1;
    } else {
        t.current = next;
    }
    saveProgress(*_part, t);
    if (terminate || t.code == kExit)
        return end(keepBox);
    if (advance)
        for (Thread &o : _part->threads)
            if (o.code != kExit && !o.borrowed) {
                o.status = 1;
                o.current = -1;
                saveProgress(*_part, o);
            }
    if (!_povent.idle()) {
        _pending = 3;
        return;
    }
    menu(t.code == kOpening || t.code == kRegreet, false);
}

// 0x43b8f0.
void DialogueManager::end(bool keepBox) {
    _wheelX = _wheelY = -1;
    _choosing = false;
    _choices.clear();
    if (_active && !keepBox && _slide != 2 && _boxY < float(_frameH)) {
        _slide = 2;
        for (const std::string &who : _turned)
            _host.turnHead(who, "");
        _turned.clear();
        return;
    }
    _part = nullptr;
    _thread = _line = -1;
    if (!keepBox)
        _active = false;
    if (_povent.idle())
        finishTalk();
    else
        _pending = 1;
}

void DialogueManager::finishTalk() {
    log("talk done");
    if (ScriptRunner *r = _caller) {
        _caller = nullptr;
        r->finished("talk");
    }
}

void DialogueManager::leaveScene(const std::string &scene) {
    const std::string s = fs::toLower(scene);
    for (auto &[name, value] : _state.tags)
        if (name.find(s + " ") == 0)
            value = 2;
}

void DialogueManager::update(float dt) {
    // 0x43d430, called every frame whether a conversation is on or not
    // (0x416d8f), in this order. The Povent runner first, and what waited
    // on it once it's idle, in the same frame.
    _povent.update(dt);
    if (_pending && _povent.idle()) {
        const int p = std::exchange(_pending, 0);
        if (p == 1) {
            finishTalk();
        } else if (p == 2) {
            _pause = kPause; // 0 only in the movie mode, which Reluva's movies don't use
        } else if (p == 3 && _part) {
            const int code = _thread >= 0 ? _part->threads[size_t(_thread)].code : 0;
            menu(code == kOpening || code == kRegreet, false);
        }
    }
    if (!_povent.idle())
        return;
    // The pause before the next line.
    if (_pause >= 0) {
        _pause -= dt;
        if (_pause <= 0) {
            _pause = -1;
            if (_part)
                startLine();
        }
        return;
    }
    if (!_active)
        return;
    // The line's Prevent, then the line once it's idle (0x43bd30): these
    // go on while the box slides.
    _prevent.update(dt);
    if (!_prevent.idle())
        return;
    if (_waitPrevent) {
        _waitPrevent = false;
        _lineTime = 0;
        speak();
    }
    // The line's events, its lip sync and its time.
    _events.update(dt);
    if (_playing) {
        _lineTime += dt;
        if (_voice) {
            const int level = _host.voiceLevel();
            if (level != -1 && level != _level) {
                _host.mouth(_speaker, level > 0);
                _level = level;
            }
        }
    }
    // The box sliding in or out: the line's end waits.
    const float top = float(_frameH - _boxHeight);
    if (_slide == 1) {
        _boxY = std::max(top, _boxY - kBoxSpeed * dt);
        if (_boxY <= top)
            _slide = 0;
        return;
    }
    if (_slide == 2) {
        _boxY = std::min(float(_frameH), _boxY + kBoxSpeed * dt);
        if (_boxY >= float(_frameH)) {
            _slide = 0;
            _active = false;
            end(false);
        }
        return;
    }
    // The line's end: its voice over, or its time without one.
    if (_playing && (_voice ? !_host.voicePlaying() : (_textLeft -= dt) <= 0))
        lineEnd();
}

void DialogueManager::mouseMove(int x, int y) {
    // After the wheel, its choice stays until the mouse moves (0x43d767).
    if (wheelChoosing()) {
        if (x == _wheelX && y == _wheelY)
            return;
        _wheelX = _wheelY = -1;
    }
    _highlighted = -1;
    if (!_choosing)
        return;
    // The pictures first, then the rows (0x43c810).
    for (bool pictures : {true, false})
        for (const ChoiceArea &a : _choiceAreas)
            if (a.picture == pictures && _highlighted < 0 && x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h)
                _highlighted = a.choice;
}

void DialogueManager::wheel(int steps, int x, int y) {
    // The part's own choices in turn, the goodbye last (0x43cb70 on,
    // 0x43cc30 back): round from the goodbye to the first; without one,
    // nothing is highlighted between the last and the first. Both walk
    // only the part's threads (+0x10's list) and the goodbye, so the
    // generic extras (other conversations' threads, 3.3) are passed over;
    // the mouse can still pick them.
    if (!_choosing || _choices.empty() || steps == 0)
        return;
    std::vector<int> own;
    for (size_t i = 0; i < _choices.size(); i++)
        if (_choices[i].part == _part)
            own.push_back(int(i));
    if (own.empty())
        return;
    const int n = int(own.size());
    const Choice &last = _choices[size_t(own.back())];
    const bool goodbye = _part->threads[size_t(last.thread)].code == kExit;
    _wheelX = x, _wheelY = y;
    // Where the highlight is among them (an extra highlighted by the
    // mouse counts as none: 0x43cb84 finds no line of the part).
    int at = -1;
    for (int k = 0; k < n; k++)
        if (own[size_t(k)] == _highlighted)
            at = k;
    for (; steps > 0; steps--)
        at = at < 0 ? (goodbye ? n - 1 : -1) : at > 0 ? at - 1 : goodbye ? n - 1 : -1; // none: the goodbye (0x43cc68)
    for (; steps < 0; steps++)
        at = at < 0 ? 0 : at + 1 < n ? at + 1 : goodbye ? 0 : -1;
    _highlighted = at < 0 ? -1 : own[size_t(at)];
}

void DialogueManager::middleClick() {
    if (_choosing && _highlighted >= 0)
        take(_highlighted);
}

void DialogueManager::click(int x, int y) {
    if (_slide != 0 || _waitPrevent || _pending || _pause >= 0)
        return;
    if (_choosing) {
        mouseMove(x, y);
        take(_highlighted);
        return;
    }
    if (_playing && _lineTime >= kSkipAfter)
        lineEnd();
}

bool DialogueManager::lineSkippable() const {
    return ready() && !_choosing && _playing && _lineTime >= kSkipAfter;
}

bool DialogueManager::choose(int i) {
    if (!ready())
        return false;
    if (_choosing)
        return take(i);
    if (_playing && _lineTime >= kSkipAfter) {
        lineEnd();
        return true;
    }
    return false;
}

bool DialogueManager::take(int i) {
    if (i < 0 || size_t(i) >= _choices.size())
        return false;
    _highlighted = i;
    const Choice c = _choices[size_t(i)];
    Thread &t = c.part->threads[size_t(c.thread)];
    if (c.part != _part) {
        // A generic extra: borrowed into this conversation.
        Thread copy = t;
        copy.borrowed = true;
        copy.fromName = c.part->name;
        copy.fromCode = c.part->code;
        _part->threads.push_back(std::move(copy));
        Thread &added = _part->threads.back();
        play(_part, int(_part->threads.size()) - 1, added.current);
        return true;
    }
    play(_part, c.thread, t.current);
    return true;
}

} // namespace reluva::awe
