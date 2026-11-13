// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool stharbinger dialogue: the game's dialogue script as readable
// text (dialogue.txt) and a table (dialogue.csv). A port of STDS9H's
// harbinger_dialogue.py, with the same output, except that it takes a game
// folder (a CD, both CDs side by side, an install, or Reluva's import of
// them) and reads CCQ.ALL and CCF.ALL through the engine's Resources, each
// archive's entries one after another (the whole .ALL, as the Python reads
// it), rather than the two files' paths.
//
// CCQ.ALL holds every spoken line as NUL-terminated text. CCF.ALL holds the
// voice clip name for each line (CRLF separated), in the same order, so
// line i of one belongs to line i of the other. Player choices start with
// their menu label in [brackets] (CCR.ALL is just those labels collected),
// and recording notes from the voice sessions are kept in {braces}.
//
// Clip names are <conversation:3><speaker:3><index:2>, e.g. kdkdax42 =
// conversation kdk, Dax, line 42. 'pro' is the protagonist (Ambassador
// Bannik).

#include "common/fs.h"
#include "common/text.h"
#include "engines/stharbinger/detection.h"
#include "engines/stharbinger/formats/resources.h"
#include "devtools/tool.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::stharbinger;

// Codes not listed here (set, wey, car, dev ...) are printed as they are.
const std::map<std::string, std::string> kSpeakers = {
    {"pro", "Bannik"}, {"com", "Computer"}, {"kir", "Kira"},     {"dax", "Dax"},   {"sis", "Sisko"}, {"odo", "Odo"},
    {"qrk", "Quark"},  {"jan", "Jani"},     {"kar", "Karrig"},   {"hvk", "Havik"}, {"syr", "Syr"},   {"kee", "Keel"},
    {"yar", "Yarrow"}, {"ras", "Rasmus"},   {"lyk", "Lykotian"}, {"hav", "Havik"},
};

// The game's text is Latin-1; the files written are UTF-8.
std::string latin1(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x80) {
            out += char(c);
        } else {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// Python's str.strip() and \s, for what Latin-1 text holds.
bool isSpace(unsigned char c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0;
}

std::string strip(const std::string &s, bool asciiOnly = false) {
    auto space = [&](unsigned char c) { return asciiOnly ? (c == ' ' || (c >= 0x09 && c <= 0x0D)) : isSpace(c); };
    size_t b = 0;
    size_t e = s.size();
    while (b < e && space(static_cast<unsigned char>(s[b])))
        b++;
    while (e > b && space(static_cast<unsigned char>(s[e - 1])))
        e--;
    return s.substr(b, e - b);
}

std::vector<std::string> splitOn(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t at = s.find(sep, start);
        if (at == std::string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, at - start));
        start = at + 1;
    }
}

struct Line {
    std::string label; // the menu [label], "" if none
    std::string spoken;
    std::vector<std::string> notes;
};

Line parseLine(const std::string &t) {
    Line out;
    std::string text = t;
    // \s*\[([^\]]*)\] at the start
    size_t p = 0;
    while (p < text.size() && isSpace(static_cast<unsigned char>(text[p])))
        p++;
    if (p < text.size() && text[p] == '[') {
        const size_t close = text.find(']', p + 1);
        if (close != std::string::npos) {
            out.label = text.substr(p + 1, close - p - 1);
            text = text.substr(close + 1);
        }
    }
    std::string spoken;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i + 1);
            if (close != std::string::npos) {
                out.notes.push_back(text.substr(i + 1, close - i - 1));
                i = close;
                continue;
            }
        }
        spoken += text[i];
    }
    out.spoken = strip(spoken);
    return out;
}

// The archive's entries one after another: the whole .ALL.
bool wholeArchive(Resources &res, const char *name, std::string &out) {
    auto archive = res.getArchive(name);
    if (!archive)
        return false;
    for (uint32_t i = 0; i < archive->size(); i++) {
        auto e = archive->getEntry(i);
        out.append(reinterpret_cast<const char *>(e->data()), size_t(e->size()));
    }
    return true;
}

// A CSV field as Python's csv module writes it (quoted when it must be).
std::string csvField(const std::string &s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + "\"";
}

int cmdDialogue(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger dialogue <game folder> <out folder>\n");
        return 2;
    }
    const auto parts = findGameParts(argv[2]);
    if (parts.empty()) {
        std::fprintf(stderr, "%s: no part of Harbinger found (a CD, both CDs or an install)\n", argv[2]);
        return 1;
    }
    Resources res;
    for (const GamePart &p : parts)
        res.addGamePath(p.folder.string());
    std::string ccq;
    std::string ccf;
    if (!wholeArchive(res, "CCQ", ccq) || !wholeArchive(res, "CCF", ccf)) {
        std::fprintf(stderr, "%s: CCQ.ALL and CCF.ALL (disc 1's SSE/SSE.ALL, or an install) not found\n", argv[2]);
        return 1;
    }
    std::vector<std::string> texts = splitOn(ccq, '\0');
    std::vector<std::string> clips;
    for (const std::string &c : splitOn(ccf, '\n'))
        clips.push_back(strip(c, true));
    if (!texts.empty() && texts.back().empty())
        texts.pop_back();
    if (!clips.empty() && clips.back().empty())
        clips.pop_back();
    if (texts.size() != clips.size())
        std::printf("warning: %u text lines but %u clip names\n", unsigned(texts.size()), unsigned(clips.size()));
    const std::filesystem::path out = argv[3];
    std::error_code ec;
    std::filesystem::create_directories(out, ec);

    std::string csv = "line,clip,conversation,speaker,menu_label,text,recording_notes\r\n";
    std::vector<std::string> script;
    std::string lastConv;
    bool first = true;
    std::vector<std::pair<std::string, int>> speakers; // in the order first seen, as Python's Counter
    std::set<std::string> conversations;
    size_t rows = 0;
    for (size_t i = 0; i < texts.size() && i < clips.size(); i++) {
        // Sliced and parsed as Latin-1, written as UTF-8.
        const std::string &raw = clips[i];
        const std::string c = latin1(raw);
        const std::string conv = latin1(fs::toLower(raw.substr(0, 3)));
        const std::string spk = latin1(fs::toLower(raw.size() > 3 ? raw.substr(3, 3) : ""));
        const auto known = kSpeakers.find(spk);
        const std::string who = known != kSpeakers.end() ? known->second : spk;
        auto it = std::find_if(speakers.begin(), speakers.end(), [&](const auto &e) { return e.first == who; });
        if (it == speakers.end())
            speakers.push_back({who, 1});
        else
            it->second++;
        Line line = parseLine(texts[i]);
        line.label = latin1(line.label);
        line.spoken = latin1(line.spoken);
        std::string notes;
        for (const std::string &n : line.notes)
            notes += (notes.empty() ? "" : " | ") + latin1(n);
        csv += std::to_string(i) + "," + csvField(c) + "," + csvField(conv) + "," + csvField(who) + "," +
               csvField(line.label) + "," + csvField(line.spoken) + "," + csvField(notes) + "\r\n";
        conversations.insert(conv);
        rows++;
        if (first || conv != lastConv) {
            script.push_back("\n=== conversation " + conv + " ===");
            lastConv = conv;
            first = false;
        }
        const std::string choice = line.label.empty() ? "" : "  [choice: " + line.label + "]";
        const std::string whoColon = who + ":";
        // "%5d %-9s %-8s %s%s" (widths in characters)
        auto pad = [](std::string s, size_t w) {
            size_t chars = 0;
            for (unsigned char ch : s)
                chars += (ch & 0xC0) != 0x80;
            return chars >= w ? s : s + std::string(w - chars, ' ');
        };
        script.push_back(text::format("%5u ", unsigned(i)) + pad(c, 9) + " " + pad(whoColon, 8) + " " +
                         (line.spoken.empty() ? "(no spoken text)" : line.spoken) + choice);
    }

    {
        std::ofstream f(out / "dialogue.csv", std::ios::binary);
        f << csv;
    }
    std::string joined;
    for (size_t i = 0; i < script.size(); i++)
        joined += (i ? "\n" : "") + script[i];
    joined.erase(0, joined.find_first_not_of(" \t\n\r\x0b\x0c"));
    {
        std::ofstream f(out / "dialogue.txt", std::ios::binary);
        f << joined << '\n';
    }
    std::printf("%u lines, %u conversations -> %s\n", unsigned(rows), unsigned(conversations.size()),
                out.string().c_str());
    std::stable_sort(speakers.begin(), speakers.end(),
                     [](const auto &a, const auto &b) { return a.second > b.second; });
    std::string per;
    for (const auto &[who, n] : speakers)
        per += (per.empty() ? "" : ", ") + who + " " + std::to_string(n);
    std::printf("lines per speaker: %s\n", per.c_str());
    return 0;
}

const reluva::tool::Commands
    commands("stharbinger",
             {
                 {"dialogue", "<game folder> <out folder>",
                  "write the game's dialogue (CCQ.ALL with CCF.ALL's clip names) as dialogue.txt, by conversation, and "
                  "dialogue.csv: line, clip, conversation, speaker, menu label, text and recording notes",
                  cmdDialogue},
             });

} // namespace
