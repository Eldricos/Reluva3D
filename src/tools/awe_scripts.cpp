// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool awe-scripts: every script command in an install, counted by
// verb: the scenes' scripts (their objects' scripts and conditional
// scripts, and their event lists), the text tables' cells and the dialogue
// event files. What the script runner has to do, from the data.

#include "tools/awe_tool.h"

#include "common/archive.h"
#include "common/fs.h"
#include "engines/awe/ac2.h"
#include "engines/awe/tables.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reluva::awe {

namespace {

// The verbs the game's runner knows (its dispatcher, 0x409ce0).
const std::set<std::string> kVerbs = {
    "accumulation", "act",      "action",    "add",       "advance",    "anim",       "anima",     "animate",
    "attach",       "clear",    "detach",    "fade",      "fadea",      "globalhide", "globalshow", "hide",
    "hideshadow",   "interface", "juncture", "leavebox",  "letterbox",  "light",      "load",      "preload",
    "lock",         "look",     "loop",      "mainloop",  "menu",       "movie",      "musica",    "nobox",
    "pause",        "play",     "playa",     "previous",  "quit",       "remove",     "removetag", "removestate",
    "reset",        "restore",  "resume",    "rotate",    "rotatea",    "run",        "runa",      "savepos",
    "savescene",    "scene",    "gotoscene", "setchar",   "show",       "showshadow", "sound",     "sounda",
    "startpass",    "endpass",  "startprint", "endprint", "stop",       "tag",        "take",      "talk",
    "talka",        "terminate", "test",     "texture",   "lasttexture", "time",      "timer",     "turn",
    "turna",        "unlock",   "update",    "updatemusic", "use",      "wait",       "walk",      "walka",
    "wear",         "weather",  "web",       "yesno",     "head",       "nogen",      "forcegen",  "nogreet",
    // expanded or read as they're added (0x409024)
    "dooropen",     "doorlocked", "script"};

// The words the game's condition evaluator knows (0x407cb0).
const std::set<std::string> kConditionWords = {
    "0",        "1",      "act",     "action", "beenthere", "beenhere", "cantalk", "chapter", "debug", "demo",
    "has",      "lastscene", "location", "master", "moving", "rand",     "scene",   "tag",     "talk",  "test",
    "time",     "timer",  "use",     "used",   "wearing",   "weather"};

struct Usage {
    size_t count = 0;
    std::map<size_t, size_t> argCounts;
    std::set<std::string> where; // the kinds of source
    std::vector<std::string> examples;
};

std::string join(const Command& c) {
    std::string s;
    for (const std::string& w : c.words)
        s += (s.empty() ? "" : " ") + w;
    return s;
}

} // namespace

int aweCommandScripts(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool awe-scripts <game folder> [-v]\n");
        return 2;
    }
    const bool verbose = argc > 3 && std::string(argv[3]) == "-v";
    DirectoryArchive files;
    if (Error e = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", e.message.c_str());
        return 1;
    }
    std::map<std::string, Usage> verbs, unknown, words;
    size_t commands = 0, conditions = 0, mixed = 0;
    // A condition's clauses, by their word (0x407ae0: split at ',' and
    // '|', a clause's '!' and the words after it).
    auto takeCondition = [&](std::string_view text, const std::string& source) {
        const std::string all(text);
        if (all.empty() || all == "0" || all == "1")
            return;
        conditions++;
        if (all.find(',') != std::string::npos && all.find('|') != std::string::npos)
            mixed++;
        size_t start = 0;
        for (;;) {
            const size_t at = all.find_first_of(",|", start);
            std::string clause = all.substr(start, at == std::string::npos ? at : at - start);
            while (!clause.empty() && (clause[0] == ' ' || clause[0] == '!' || clause[0] == '('))
                clause.erase(0, 1);
            if (!clause.empty()) {
                const std::string word = fs::toLower(clause.substr(0, clause.find(' ')));
                Usage& u = words[kConditionWords.count(word) ? word : "?" + word];
                u.count++;
                u.where.insert(source);
                if (u.examples.size() < 4 && std::find(u.examples.begin(), u.examples.end(), all) == u.examples.end())
                    u.examples.push_back(all);
            }
            if (at == std::string::npos)
                break;
            start = at + 1;
        }
    };
    auto take = [&](std::string_view text, const std::string& source, bool onlyKnown) {
        for (const Command& c : parseCommands(text)) {
            if (c.words.empty())
                continue;
            const std::string verb = fs::toLower(c.verb());
            const bool known = kVerbs.count(verb) != 0;
            if (!known && onlyKnown)
                continue;
            Usage& u = (known ? verbs : unknown)[verb];
            u.count++;
            commands++;
            u.argCounts[c.words.size() - 1]++;
            u.where.insert(source);
            if (u.examples.size() < 4 && std::find(u.examples.begin(), u.examples.end(), join(c)) == u.examples.end())
                u.examples.push_back(join(c));
        }
    };
    std::function<void(const Ac2Record&)> takeRecord = [&](const Ac2Record& r) {
        take(r.text("script"), "scene scripts", false);
        takeCondition(r.text("condition"), "scene " + r.tag + " condition");
        for (const auto& [field, v] : r.fields)
            for (const auto& [cond, run] : v.pairs) {
                take(run, r.tag == "MUSC" ? "music" : "scene " + r.tag + " " + field, true);
                takeCondition(cond, "scene " + r.tag + " " + field);
            }
        for (const Ac2Record& part : r.parts)
            takeRecord(part);
    };
    for (const std::string& name : files.list()) {
        const std::string lower = fs::toLower(name);
        std::vector<uint8_t> data;
        if (files.read(name, data))
            continue;
        if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".ac2") == 0 &&
            lower.rfind("text/scripts/", 0) != 0) {
            Ac2Scene scene;
            if (parseAc2(data, scene))
                continue;
            for (const Ac2Record& r : scene.records)
                takeRecord(r);
        } else if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".txt") == 0 &&
                   lower.rfind("text/", 0) == 0) {
            const std::string text = decodeText(data);
            if (lower.rfind("text/dialogue/events/", 0) == 0) {
                take(text, "dialogue events", false);
                continue;
            }
            if (lower.rfind("text/movies/", 0) == 0)
                continue;
            const Table t = parseTable(text);
            const std::string kind = lower.rfind("text/dialogue/", 0) == 0 ? "dialogue tables" : name;
            for (const auto& row : t.rows)
                for (size_t col = 0; col < row.size(); col++) {
                    // Not the columns of text to read (lines, descriptions).
                    const std::string heading = col < t.headings.size() ? fs::toLower(t.headings[col]) : "";
                    if (heading.find("condition") != std::string::npos) {
                        takeCondition(row[col], kind + " [" + t.headings[col] + "]");
                        continue;
                    }
                    if (heading.find("line") != std::string::npos || heading.find("text") != std::string::npos ||
                        heading.find("desc") != std::string::npos || heading.find("translation") != std::string::npos ||
                        heading.find("notes") != std::string::npos || heading == "tag" || heading == "name")
                        continue;
                    take(row[col], kind + (col < t.headings.size() ? " [" + t.headings[col] + "]" : ""), true);
                }
        }
    }
    std::printf("%zu commands of %zu verbs\n", commands, verbs.size());
    std::vector<std::pair<std::string, Usage>> sorted(verbs.begin(), verbs.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.count > b.second.count; });
    for (const auto& [verb, u] : sorted) {
        std::printf("%-12s %6zu  args:", verb.c_str(), u.count);
        for (const auto& [n, k] : u.argCounts)
            std::printf(" %zu (%zu)", n, k);
        std::printf("  e.g. %s", u.examples.empty() ? "" : u.examples[0].c_str());
        for (size_t i = 1; i < u.examples.size(); i++)
            std::printf(" | %s", u.examples[i].c_str());
        std::printf("\n");
        if (verbose)
            for (const std::string& w : u.where)
                std::printf("               in %s\n", w.c_str());
    }
    std::printf("verbs the runner doesn't know, in scene scripts and event files:\n");
    for (const auto& [verb, u] : unknown)
        std::printf("  %-12s %4zu  e.g. %s\n", verb.c_str(), u.count, u.examples[0].c_str());
    const std::string none;
    std::printf("verbs the runner knows that nothing uses:");
    for (const std::string& v : kVerbs)
        if (!verbs.count(v))
            std::printf(" %s", v.c_str());
    std::printf("\n");
    std::printf("%zu conditions (%zu with both ',' and '|'), by word (? the evaluator doesn't know it):\n", conditions,
                mixed);
    std::vector<std::pair<std::string, Usage>> byWord(words.begin(), words.end());
    std::sort(byWord.begin(), byWord.end(), [](const auto& a, const auto& b) { return a.second.count > b.second.count; });
    for (const auto& [word, u] : byWord) {
        std::printf("  %-12s %5zu  e.g. %s", word.c_str(), u.count, u.examples[0].c_str());
        for (size_t i = 1; i < u.examples.size(); i++)
            std::printf(" | %s", u.examples[i].c_str());
        std::printf("\n");
        if (verbose)
            for (const std::string& w : u.where)
                std::printf("               in %s\n", w.c_str());
    }
    return 0;
}

} // namespace reluva::awe
