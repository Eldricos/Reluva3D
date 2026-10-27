// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/dialogue.h"

#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace reluva::awe {

namespace {

constexpr int kOpening = 0x60, kRegreet = 0x61, kExit = 0x62; // threads 1, 2, 3
constexpr float kBoxSpeed = 500; // pixels a second, sliding in and out
constexpr float kPause = 0.4f;   // between lines
constexpr float kSkipAfter = 0.5f; // a click ends a line only after this

bool isNumber(std::string_view s) { return !s.empty() && std::isdigit(uint8_t(s[0])); }
int letter(char c) { return std::toupper(uint8_t(c)) - 'A'; }

// A cell, "" for the tables' "0" (none).
std::string cellOf(const Table& t, size_t row, int column) {
    const std::string& c = t.cell(row, column);
    return c == "0" ? std::string() : c;
}

bool contains(std::string_view hay, std::string_view needle) {
    return fs::toLower(hay).find(fs::toLower(needle)) != std::string::npos;
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

void DialogueBook::add(const Table& t) {
    // By position, as the game reads them (0x438f00), whatever the
    // headings say: gen_characters.txt's first is "gen_globalhardman", not
    // Name. gen_characters.txt and gen_antoinette.txt have no Optional
    // column, so their Notes land there (and count as 0).
    const int name = 0, act = 1, part = 2, thread = 3, nest = 4, speaker = 5, target = 6, line = 7, translation = 8,
              condition = 9, repeat = 10, before = 11, event = 12, after = 13, duration = 14, optional = 15, notes = 16;
    DialoguePart* p = nullptr;
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
        l.target = fs::toLower(cellOf(t, r, target));
        l.text = t.cell(r, line);
        if (!cellOf(t, r, translation).empty())
            l.text = t.cell(r, translation);
        l.condition = cellOf(t, r, condition);
        l.repeat = cellOf(t, r, repeat);
        l.optional = cellOf(t, r, optional);
        l.notes = cellOf(t, r, notes);
        l.before = cellOf(t, r, before);
        l.event = cellOf(t, r, event);
        l.after = cellOf(t, r, after);
        // "a, b": the movies' start and length; the second is a line's time.
        const std::string d = t.cell(r, duration);
        l.duration = d.find(',') != std::string::npos ? float(std::atof(d.c_str() + d.find(',') + 1)) : 0;
        l.partCode = dialoguePartCode(l.part);
        l.threadCode = dialogueThreadCode(l.thread);
        l.nestCode = dialogueNestCode(l.nest);
        l.repeatCode = !l.repeat.empty() && std::isalpha(uint8_t(l.repeat[0])) ? letter(l.repeat[0]) : -1;
        if (!p || p->name != l.name || p->code != l.partCode) {
            parts_.push_back(DialoguePart{l.name, l.act, l.partCode, {}});
            p = &parts_.back();
        }
        if (p->threads.empty() || p->threads.back().code != l.threadCode) {
            p->threads.emplace_back();
            p->threads.back().code = l.threadCode;
        }
        auto& lines = p->threads.back().lines;
        if (!lines.empty() && lines.back().nestCode == l.nestCode)
            l.alternative = lines.back().alternative + 1;
        lines.push_back(std::move(l));
    }
}

DialoguePart* DialogueBook::find(std::string_view name, int code) {
    const std::string n = fs::toLower(name);
    for (DialoguePart& p : parts_)
        if (p.name == n && p.code == code)
            return &p;
    return nullptr;
}

std::vector<const DialogueLine*> DialogueBook::conversation(std::string_view name) const {
    const std::string n = fs::toLower(name);
    std::vector<const DialogueLine*> out;
    for (const DialoguePart& p : parts_)
        if (p.name == n)
            for (const auto& t : p.threads)
                for (const DialogueLine& l : t.lines)
                    out.push_back(&l);
    return out;
}

TalkArgs parseTalkArgs(const std::vector<std::string>& words, bool gen) {
    TalkArgs t;
    t.gen = gen;
    auto word = [&](size_t i) { return i < words.size() ? fs::toLower(words[i]) : std::string(); };
    t.name = words.empty() ? std::string() : words[0];
    const std::string code = word(1);
    if (!code.empty() && std::isalpha(uint8_t(code[0]))) {
        t.part = std::toupper(uint8_t(code[0])) - 'A';
        if (code.size() > 1 && code[1] != '+')
            t.thread = std::toupper(uint8_t(code[1])) - 'A';
        if (code.size() > 2 && code[2] != '+')
            t.nest = std::toupper(uint8_t(code[2])) - 'A';
    } else if (!code.empty()) {
        t.part = std::atoi(code.c_str()) + 0x63;
    }
    t.plus = !code.empty() && code.back() == '+';
    for (const std::string& opt : {word(2), word(3)}) {
        if (opt.empty())
            continue;
        if (opt == "nogen")
            t.gen = false;
        else if (opt == "forcegen")
            t.gen = true;
        else if (opt == "nogreet")
            t.greet = false;
        else
            t.number = std::atoi(opt.c_str());
    }
    return t;
}

// --- The manager -----------------------------------------------------------------

DialogueManager::DialogueManager(GameState& state, ScriptHost& scripts, DialogueHost& host)
    : state_(state), host_(host), prevent_(state, scripts), events_(state, scripts), povent_(state, scripts) {
    // "talk <name> <act> <part>": every line not Optional said (0x439e40).
    state_.partTold = [this](const std::string& name, const std::string& act, int part) {
        DialoguePart* p = book_.find(name, part);
        if (!p)
            return false;
        for (int code = 0;; code++) {
            const auto t = std::find_if(p->threads.begin(), p->threads.end(),
                                        [code](const Thread& th) { return th.code == code; });
            if (t == p->threads.end())
                return true;
            const auto rec = state_.talks.find(talkKey(name, act, part, code));
            for (const DialogueLine& l : t->lines) {
                if (std::atoi(l.optional.c_str()) != 0)
                    continue;
                const bool passed = rec != state_.talks.end() &&
                                    (rec->second.status != 0 || rec->second.nest == -1 || rec->second.nest > l.nestCode);
                if (!passed) {
                    log("required line not said: " + l.text);
                    return false;
                }
            }
        }
    };
}

int DialogueManager::rand() {
    random_ = random_ * 214013u + 2531011u;
    return int((random_ >> 16) & 0x7fff);
}

const DialogueLine* DialogueManager::line() const {
    if (!part_ || thread_ < 0 || line_ < 0 || choosing_)
        return nullptr;
    return &part_->threads[size_t(thread_)].lines[size_t(line_)];
}

bool DialogueManager::boxShown() const {
    if (!active_)
        return false;
    if (povent_.idle() || showWhilePovent_)
        return true;
    return choosing_;
}

// The line of a thread at `nest` whose condition holds; several: an
// alternative, the one asked for or another at random (0x439ca0).
int DialogueManager::pickLine(Thread& t, int nest, bool forward) {
    for (;; nest++) {
        bool any = false;
        std::vector<int> ok;
        for (size_t i = 0; i < t.lines.size(); i++)
            if (t.lines[i].nestCode == nest) {
                any = true;
                if (testCondition(t.lines[i].condition, state_))
                    ok.push_back(int(i));
            }
        if (!any)
            return -1;
        if (ok.size() == 1)
            return ok[0];
        if (ok.size() > 1) {
            int i = number_;
            if (i == -1 || i != lastAlternative_) {
                if (i == -1) {
                    do
                        i = rand() % int(ok.size());
                    while (ok.size() > 1 && i == lastAlternative_);
                } else {
                    i = std::clamp(i - 1, 0, int(ok.size()) - 1);
                }
            }
            number_ = lastAlternative_ = i;
            return ok[size_t(std::clamp(i, 0, int(ok.size()) - 1))];
        }
        if (!forward)
            return -1;
    }
}

// Whether a thread is offered (0x43b870).
bool DialogueManager::available(Thread& t) {
    if (t.code == kExit)
        return true;
    if (t.status == 1) {
        if (t.current < 0 || !firstOffer_ || !testCondition(t.lines[size_t(t.current)].condition, state_))
            return false;
    }
    return pickLine(t, 0, true) >= 0;
}

void DialogueManager::loadProgress(DialoguePart& p) {
    for (Thread& t : p.threads) {
        t.status = 0;
        t.current = -1;
        const auto rec = state_.talks.find(talkKey(p.name, p.act, p.code, t.code));
        if (rec == state_.talks.end())
            continue;
        t.status = rec->second.status;
        if (rec->second.nest != -1)
            t.current = pickLine(t, rec->second.nest, true);
    }
}

void DialogueManager::saveProgress(const DialoguePart& p, const Thread& t) {
    if (p.code >= 99 || t.code == kExit)
        return;
    TalkProgress rec{t.status, t.current >= 0 ? t.lines[size_t(t.current)].nestCode : -1};
    if (t.borrowed)
        state_.talks[talkKey(t.fromName, "0", t.fromCode, t.code)] = rec;
    else
        state_.talks[talkKey(p.name, p.act, p.code, t.code)] = rec;
}

// "gen_<speaker>_": the first speaker of the part who isn't Antoinette.
std::string DialogueManager::genPrefix(const DialoguePart& p) const {
    for (const Thread& t : p.threads)
        for (const DialogueLine& l : t.lines)
            if (!l.speaker.empty() && l.speaker != "antoinette")
                return "gen_" + l.speaker + "_";
    return {};
}

// A new exit thread: one of the NPC's goodbyes (0x43af20).
void DialogueManager::addExit(DialoguePart& p) {
    p.threads.erase(std::remove_if(p.threads.begin(), p.threads.end(), [](const Thread& t) { return t.code == kExit; }),
                    p.threads.end());
    std::string name = prefix_ + "exit";
    int r = rand() % 4;
    if (testCondition("act >07a", state_))
        name = "gen_solutionexit", r = 0;
    auto nth = [&](const std::string& n) -> const Thread* {
        int seen = 0;
        for (const DialoguePart& q : book_.parts())
            if (q.name == n)
                for (const Thread& t : q.threads)
                    if (t.code == kExit && seen++ == r)
                        return &t;
        return nullptr;
    };
    const Thread* from = nth(name);
    if (!from)
        from = nth("gen_exit");
    if (!from)
        return;
    Thread t = *from;
    t.borrowed = true;
    t.status = 0;
    t.current = -1;
    p.threads.push_back(std::move(t));
}

// A new "again" thread from gen_regreet1 (this visit) or 2 (0x43b180).
void DialogueManager::addRegreet(DialoguePart& p, int visit) {
    p.threads.erase(
        std::remove_if(p.threads.begin(), p.threads.end(), [](const Thread& t) { return t.code == kRegreet; }),
        p.threads.end());
    const std::string name = "gen_regreet" + std::to_string(visit);
    const int r = rand() % 4;
    int seen = 0;
    for (const DialoguePart& q : book_.parts())
        if (q.name == name)
            for (const Thread& t : q.threads)
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
std::vector<std::pair<DialoguePart*, int>> DialogueManager::extras() {
    std::vector<std::pair<DialoguePart*, int>> out;
    if (!gen_ || prefix_.empty() || !part_ || part_->code >= 100)
        return out;
    for (DialoguePart& q : book_.parts())
        if (&q != part_ && q.code < 99 && contains(q.name, prefix_))
            for (size_t i = 0; i < q.threads.size(); i++)
                if (q.threads[i].code < 0x60 && available(q.threads[i]))
                    out.push_back({&q, int(i)});
    return out;
}

bool DialogueManager::talk(ScriptRunner& runner, const std::string& name, int part, int thread, int nest, bool gen,
                           int number, bool plus, bool greet) {
    gen_ = gen;
    if (!start(name, part, thread, nest, number, plus, greet, false))
        return false;
    caller_ = &runner;
    return true;
}

bool DialogueManager::canTalk(const std::string& name, int part, int thread, int nest, bool gen, int number, bool plus,
                              bool greet) {
    // Start in a dry run (+0x61c), nothing shown or said, and what it
    // changed put back (0x43a600).
    if (active_)
        return false;
    const bool genBefore = gen_, plusBefore = plus_, greetBefore = greet_;
    const int numberBefore = number_, alternativeBefore = lastAlternative_, height = boxHeight_, win = window_;
    const std::string prefixBefore = prefix_;
    dry_ = true;
    gen_ = gen;
    const bool would = start(name, part, thread, nest, number, plus, greet, false);
    dry_ = false;
    active_ = false;
    part_ = nullptr;
    gen_ = genBefore, plus_ = plusBefore, greet_ = greetBefore;
    number_ = numberBefore, lastAlternative_ = alternativeBefore, boxHeight_ = height, window_ = win;
    prefix_ = prefixBefore;
    return would;
}

bool DialogueManager::noTalk(ScriptRunner& runner) {
    // gen_<npc>_notalk; with no NPC, Antoinette's own "nothing to say"
    // (the game's test makes that always gen_canttalkf).
    const std::string name = !prefix_.empty() ? prefix_ + "notalk" : "gen_canttalkf";
    return talk(runner, name, 100, -1, -1, true, -1, false, true);
}

// 0x43a630.
bool DialogueManager::start(const std::string& name, int part, int thread, int nest, int number, bool plus, bool greet,
                            bool retry) {
    DialoguePart* p = book_.find(name, part);
    if (!p)
        return false;
    if (!dry_)
        log("talk " + name + " " + partText(part));
    part_ = p;
    boxHeight_ = 160, window_ = 0;
    if (testCondition("act >07a", state_))
        boxHeight_ = 180, window_ = 1;
    firstOffer_ = true;
    active_ = true;
    plus_ = plus;
    greet_ = greet;
    number_ = number;
    if (number != -1)
        lastAlternative_ = -1;
    lastSpeaker_.clear();
    goOn_ = false;
    loadProgress(*p);
    prefix_ = genPrefix(*p);
    if (!prefix_.empty() && part < 100)
        for (DialoguePart& q : book_.parts())
            if (q.code < 99 && contains(q.name, prefix_))
                loadProgress(q);
    const bool hasExtras = !extras().empty();

    // A thread asked for, or the only one: played as it is.
    const bool single = p->threads.size() == 1 && !hasExtras;
    if (thread != -1 || single) {
        const int code = thread == -1 ? 0 : thread;
        int ti = -1;
        for (size_t i = 0; i < p->threads.size(); i++)
            if (p->threads[i].code == (single && thread == -1 ? p->threads[i].code : code)) {
                ti = int(i);
                break;
            }
        if (ti < 0)
            return false;
        Thread& t = p->threads[size_t(ti)];
        const bool exhausted = (t.status != 0 && t.current < 0 && !prefix_.empty()) || !available(t);
        if (!exhausted) {
            int li = nest >= 0 ? pickLine(t, nest, true) : t.current >= 0 ? t.current : pickLine(t, 0, true);
            if (li >= 0) {
                if (dry_)
                    return true;
                showBox();
                play(p, ti, li);
                return true;
            }
        }
    } else {
        int rows = 0;
        for (Thread& t : p->threads)
            if (t.code < 0x60 && available(t))
                rows++;
        rows += int(extras().size());
        if (rows > 0 && dry_)
            return true;
        if (rows > 0) {
            if (gen_)
                addExit(*p);
            // "Met them here": 1 this visit, 2 an earlier one.
            const std::string key = fs::toLower(host_.sceneName()) + " " + p->name + " " + std::to_string(p->code);
            const auto tag = state_.tags.find(key);
            const int visit = tag == state_.tags.end() ? -99 : tag->second;
            if (gen_ && greet && part < 100 && visit > 0)
                addRegreet(*p, std::min(visit, 2));
            state_.tags[key] = 1;
            int start = -1;
            for (size_t i = 0; i < p->threads.size(); i++)
                if (p->threads[i].code == kOpening && !(p->threads[i].status == 1 && p->threads[i].current < 0))
                    start = int(i);
            if (start < 0)
                for (size_t i = 0; i < p->threads.size(); i++)
                    if (p->threads[i].code == kRegreet && (visit > 0 || true))
                        start = int(i);
            showBox();
            if (start >= 0 && greet) {
                Thread& t = p->threads[size_t(start)];
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
            if (!book_.find(name, k))
                break;
            if (start(name, k, -1, -1, -1, plus, greet, true))
                return true;
        }
    }
    active_ = false;
    part_ = nullptr;
    return false;
}

void DialogueManager::showBox() {
    if (slide_ == 0 && boxY_ >= 768)
        slide_ = 1;
}

// The choices (0x43b560).
void DialogueManager::menu(bool first, bool keepBox) {
    firstOffer_ = first;
    choices_.clear();
    int count = 0;
    bool hasExit = false;
    for (size_t i = 0; i < part_->threads.size(); i++) {
        Thread& t = part_->threads[i];
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
        if (first && t.status != 0)
            t.status = 0;
        count++;
    }
    const auto more = extras();
    count += int(more.size());
    auto nextPart = [&] {
        // A next part with nothing to say leaves this one going (start()
        // drops the part it fails on).
        if (!goOn_)
            return false;
        DialoguePart* keep = part_;
        if (start(part_->name, part_->code + 1, -1, -1, -1, plus_, greet_, false))
            return true;
        part_ = keep;
        active_ = true;
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
            for (size_t i = 0; i < part_->threads.size(); i++)
                if (part_->threads[i].code == kExit)
                    return play(part_, int(i), part_->threads[i].current);
        }
        for (size_t i = 0; i < part_->threads.size(); i++) {
            Thread& t = part_->threads[i];
            if (t.code < 0x60 && t.current >= 0 && available(t))
                return play(part_, int(i), t.current);
        }
        if (!more.empty()) {
            Thread& t = more[0].first->threads[size_t(more[0].second)];
            return play(more[0].first, more[0].second, t.current >= 0 ? t.current : pickLine(t, 0, true));
        }
        return end(keepBox);
    }
    // Several: offered by their current lines, the exit last.
    int exitIndex = -1;
    for (size_t i = 0; i < part_->threads.size(); i++) {
        Thread& t = part_->threads[i];
        if (t.code == kExit) {
            exitIndex = int(i);
            continue;
        }
        if (t.code < 0x60 && t.current >= 0 && available(t))
            choices_.push_back({t.lines[size_t(t.current)].text, part_, int(i)});
    }
    for (const auto& [q, ti] : more) {
        Thread& t = q->threads[size_t(ti)];
        if (t.current < 0)
            t.current = pickLine(t, 0, false);
        if (t.current >= 0)
            choices_.push_back({t.lines[size_t(t.current)].text, q, ti});
    }
    if (exitIndex >= 0 && part_->threads[size_t(exitIndex)].current >= 0)
        choices_.push_back({part_->threads[size_t(exitIndex)].lines[size_t(part_->threads[size_t(exitIndex)].current)].text,
                            part_, exitIndex});
    choosing_ = true;
    colour_ = 6;
    highlighted_ = -1;
    wheelX_ = wheelY_ = -1;
    showBox();
}

void DialogueManager::play(DialoguePart* p, int thread, int line) {
    if (!p || line < 0)
        return end(false);
    choosing_ = false;
    choices_.clear();
    part_ = p;
    thread_ = thread;
    line_ = line;
    startLine();
}

// 0x43c070.
void DialogueManager::startLine() {
    const DialogueLine& l = part_->threads[size_t(thread_)].lines[size_t(line_)];
    // Antoinette 8; the others 9, then 10 when the speaker changes.
    if (l.speaker.find("antoinette") != std::string::npos) {
        colour_ = 8;
    } else {
        if (!lastSpeaker_.empty() && l.speaker != lastSpeaker_)
            colour_ = colour_ == 9 ? 10 : 9;
        else if (colour_ != 9 && colour_ != 10)
            colour_ = 9;
        lastSpeaker_ = l.speaker;
    }
    playing_ = true;
    lineTime_ = 0;
    prevent_.clear();
    if (!l.before.empty()) {
        prevent_.setCharacter(l.speaker);
        prevent_.run(l.before);
        waitPrevent_ = true;
        return;
    }
    speak();
}

// 0x43bd30, 0x43c100, 0x43c260.
void DialogueManager::speak() {
    const DialogueLine& l = part_->threads[size_t(thread_)].lines[size_t(line_)];
    log(l.speaker + ": " + l.text);
    if (!l.target.empty()) {
        host_.turnHead(l.speaker, l.target);
        if (std::find(turned_.begin(), turned_.end(), l.speaker) == turned_.end())
            turned_.push_back(l.speaker);
    }
    speaker_ = l.speaker;
    host_.lineStarted(speaker_);
    host_.stopVoice();
    voice_ = host_.playVoice(l.voiceFile());
    level_ = -1;
    // Without a voice the mouth moves all along (0x4046c0); with one, as
    // its level does (0x43d3c0).
    if (!voice_) {
        textLeft_ = l.duration > 0 ? l.duration : std::max(1.0f, float(int(l.text.size()) / 15));
        host_.mouth(speaker_, true);
    }
    // The Event column, else the line's event file, alongside the line.
    events_.clear();
    events_.setCharacter(l.speaker);
    if (!l.event.empty()) {
        events_.run(l.event);
    } else {
        const Thread& t = part_->threads[size_t(thread_)];
        const std::string file = "text/dialogue/events/" + part_->name + "_" + part_->act + "_" +
                                 partText(part_->code) + threadText(t.code) + std::string(1, char('a' + l.nestCode)) +
                                 ".txt";
        const std::string text = host_.readText(file);
        if (!text.empty())
            events_.run(text);
    }
}

// 0x43c310 -> 0x43c375.
void DialogueManager::lineEnd() {
    host_.stopVoice();
    voice_ = false;
    host_.lineEnded(speaker_);
    playing_ = false;
    events_.fastForward();
    Thread& t = part_->threads[size_t(thread_)];
    const DialogueLine& l = t.lines[size_t(line_)];
    const std::string& after = l.after;
    const bool juncture = contains(after, "juncture"), terminate = contains(after, "terminate"),
               advance = contains(after, "advance");
    bool keepBox = false;
    const int next = pickLine(t, l.nestCode + 1, true);
    if (!after.empty()) {
        keepBox = (contains(after, "talk") || contains(after, "leavebox")) && !contains(after, "nobox");
        showWhilePovent_ = !(contains(after, "walk") || contains(after, "rotate"));
        // At the conversation's end it goes to the main script, after the
        // talk; else it runs while the dialogue waits.
        bool othersLeft = false;
        for (Thread& o : part_->threads)
            if (&o != &t && o.code < 0x60 && available(o))
                othersLeft = true;
        const bool runsOut =
            next < 0 && !othersLeft && !(plus_ && book_.find(part_->name, part_->code + 1));
        if ((contains(after, "mainloop") || terminate || runsOut) && caller_)
            caller_->runNext(after);
        else
            povent_.run(after);
    }
    t.current = line_;
    goOn_ = (next < 0 && plus_) || advance;
    if (next >= 0 && !juncture && !terminate && !goOn_) {
        line_ = next;
        if (!povent_.idle())
            pending_ = 2;
        else
            pause_ = contains(part_->name, "_movie") ? 0 : kPause;
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
    saveProgress(*part_, t);
    if (terminate || t.code == kExit)
        return end(keepBox);
    if (advance)
        for (Thread& o : part_->threads)
            if (o.code != kExit && !o.borrowed) {
                o.status = 1;
                o.current = -1;
                saveProgress(*part_, o);
            }
    if (!povent_.idle()) {
        pending_ = 3;
        return;
    }
    menu(t.code == kOpening || t.code == kRegreet, false);
}

// 0x43b8f0.
void DialogueManager::end(bool keepBox) {
    wheelX_ = wheelY_ = -1;
    choosing_ = false;
    choices_.clear();
    if (active_ && !keepBox && slide_ != 2 && boxY_ < 768) {
        slide_ = 2;
        for (const std::string& who : turned_)
            host_.turnHead(who, "");
        turned_.clear();
        return;
    }
    part_ = nullptr;
    thread_ = line_ = -1;
    if (!keepBox)
        active_ = false;
    if (povent_.idle())
        finishTalk();
    else
        pending_ = 1;
}

void DialogueManager::finishTalk() {
    log("talk done");
    if (ScriptRunner* r = caller_) {
        caller_ = nullptr;
        r->finished("talk");
    }
}

void DialogueManager::leaveScene(const std::string& scene) {
    const std::string s = fs::toLower(scene);
    for (auto& [name, value] : state_.tags)
        if (name.find(s + " ") == 0)
            value = 2;
}

void DialogueManager::update(float dt) {
    if (!active_ && slide_ == 0)
        return;
    const float top = float(768 - boxHeight_);
    if (slide_ == 1) {
        boxY_ = std::max(top, boxY_ - kBoxSpeed * dt);
        if (boxY_ <= top)
            slide_ = 0;
        return;
    }
    if (slide_ == 2) {
        boxY_ = std::min(768.0f, boxY_ + kBoxSpeed * dt);
        if (boxY_ >= 768) {
            slide_ = 0;
            active_ = false;
            end(false);
        }
        return;
    }
    prevent_.update(dt);
    povent_.update(dt);
    if (waitPrevent_) {
        if (prevent_.idle()) {
            waitPrevent_ = false;
            speak();
        }
        return;
    }
    if (pending_ && povent_.idle()) {
        const int p = pending_;
        pending_ = 0;
        if (p == 1)
            finishTalk();
        else if (p == 2)
            pause_ = kPause;
        else if (p == 3 && part_)
            menu(false, false);
        return;
    }
    if (pause_ >= 0) {
        pause_ -= dt;
        if (pause_ < 0 && part_)
            startLine();
        return;
    }
    if (playing_) {
        events_.update(dt);
        lineTime_ += dt;
        if (voice_) {
            const int level = host_.voiceLevel();
            if (level != -1 && level != level_) {
                host_.mouth(speaker_, level > 0);
                level_ = level;
            }
        }
        if (voice_ ? !host_.voicePlaying() : (textLeft_ -= dt) <= 0)
            lineEnd();
    }
}

void DialogueManager::mouseMove(int x, int y) {
    // After the wheel, its choice stays until the mouse moves (0x43d767).
    if (wheelChoosing()) {
        if (x == wheelX_ && y == wheelY_)
            return;
        wheelX_ = wheelY_ = -1;
    }
    highlighted_ = -1;
    if (!choosing_)
        return;
    for (size_t i = 0; i < choiceRows_.size(); i++)
        if (y >= choiceRows_[i].first && y < choiceRows_[i].first + choiceRows_[i].second)
            highlighted_ = int(i);
}

void DialogueManager::wheel(int steps, int x, int y) {
    // The choices in turn, the goodbye last (0x43cb70 on, 0x43cc30 back):
    // round from the goodbye to the first; without one, nothing is
    // highlighted between the last and the first.
    const int n = int(choices_.size());
    if (!choosing_ || n == 0 || steps == 0)
        return;
    const Choice& last = choices_.back();
    const bool goodbye = last.part == part_ && part_->threads[size_t(last.thread)].code == kExit;
    wheelX_ = x, wheelY_ = y;
    for (; steps > 0; steps--)
        highlighted_ = highlighted_ < 0 ? n - 1 : highlighted_ > 0 ? highlighted_ - 1 : goodbye ? n - 1 : -1;
    for (; steps < 0; steps++)
        highlighted_ = highlighted_ < 0 ? 0 : highlighted_ + 1 < n ? highlighted_ + 1 : goodbye ? 0 : -1;
}

void DialogueManager::middleClick() {
    if (choosing_ && highlighted_ >= 0)
        take(highlighted_);
}

void DialogueManager::click(int x, int y) {
    if (slide_ != 0 || waitPrevent_ || pending_ || pause_ >= 0)
        return;
    if (choosing_) {
        mouseMove(x, y);
        take(highlighted_);
        return;
    }
    if (playing_ && lineTime_ >= kSkipAfter)
        lineEnd();
}

bool DialogueManager::choose(int i) {
    if (slide_ != 0 || waitPrevent_ || pending_ || pause_ >= 0)
        return false;
    if (choosing_)
        return take(i);
    if (playing_ && lineTime_ >= kSkipAfter) {
        lineEnd();
        return true;
    }
    return false;
}

bool DialogueManager::take(int i) {
    if (i < 0 || size_t(i) >= choices_.size())
        return false;
    highlighted_ = i;
    const Choice c = choices_[size_t(i)];
    Thread& t = c.part->threads[size_t(c.thread)];
    if (c.part != part_) {
        // A generic extra: borrowed into this conversation.
        Thread copy = t;
        copy.borrowed = true;
        copy.fromName = c.part->name;
        copy.fromCode = c.part->code;
        part_->threads.push_back(std::move(copy));
        Thread& added = part_->threads.back();
        play(part_, int(part_->threads.size()) - 1, added.current);
        return true;
    }
    play(part_, c.thread, t.current);
    return true;
}

} // namespace reluva::awe
