// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/resources.h"
#include "tools/tool.h"
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
using namespace reluva::boggle;
const char* status(Submission result) {
    switch (result) {
    case Submission::Accepted: return "accepted";
    case Submission::NotPlaying: return "not-playing";
    case Submission::InvalidLetters: return "invalid-letters";
    case Submission::TooShort: return "too-short";
    case Submission::UnknownWord: return "unknown-word";
    case Submission::NotOnBoard: return "not-on-board";
    case Submission::Duplicate: return "duplicate";
    case Submission::SelectPath: return "select-path";
    }
    return "invalid";
}
struct Step { uint64_t elapsed; std::string word; };
int cmdReplay(int argc, char** argv) {
    if (argc < 4 || argc > 6) {
        std::fprintf(stderr, "usage: reluva-tool boggle classic-replay <game folder> <external scenario file> [original|master] [seconds]\n"); return 2;
    }
    ClassicOptions options;
    if (auto err = readClassicOptions({{"classic_variant", argc >= 5 ? argv[4] : "original"},
                                       {"classic_seconds", argc == 6 ? argv[5] : "180"}}, options)) {
        std::fprintf(stderr, "%s\n", err.message.c_str()); return 2;
    }
    // Validate the complete bounded scenario before running any submissions.
    // Neither diagnostics nor results echo observed faces or word lists.
    std::ifstream file(argv[3], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > 65536) {
        std::fprintf(stderr, "Cannot read scenario, or it exceeds 64 KiB\n"); return 2;
    }
    const size_t length = size_t(file.tellg()); file.seekg(0);
    std::string text(length, '\0');
    if (!file.read(text.data(), std::streamsize(length))) { std::fprintf(stderr, "Cannot read scenario\n"); return 2; }
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3); // Windows UTF-8 text editors
    std::istringstream lines(text); std::string line;
    std::vector<std::string> faces; std::vector<Step> steps; size_t row = 0, lineNumber = 0;
    auto invalid = [&] { std::fprintf(stderr, "Invalid scenario at line %zu; see the Classic comparison guide\n", lineNumber); return 2; };
    while (std::getline(lines, line)) {
        ++lineNumber;
        if (line.size() > 256 || line.find('\0') != std::string::npos) return invalid();
        std::istringstream fields(line); std::string first, extra;
        if (!(fields >> first) || first.starts_with('#')) continue;
        if (row < options.side()) {
            faces.push_back(first);
            for (size_t i = 1; i < options.side(); ++i) { if (!(fields >> first)) return invalid(); faces.push_back(first); }
            if (fields >> extra) return invalid();
            ++row;
        } else {
            uint64_t elapsed = 0;
            const auto parsed = std::from_chars(first.data(), first.data() + first.size(), elapsed);
            std::string word;
            if (parsed.ec != std::errc{} || parsed.ptr != first.data() + first.size() || elapsed > 3600000 ||
                (!steps.empty() && elapsed < steps.back().elapsed) || !(fields >> word) ||
                word.size() > 64 || (fields >> extra) || steps.size() >= 256) return invalid();
            steps.push_back({elapsed, std::move(word)});
        }
    }
    if (row != options.side() || steps.empty()) return invalid();
    Board board;
    if (auto err = board.set(options.side(), faces)) { std::fprintf(stderr, "%s\n", err.message.c_str()); return 2; }
    Resources resources; Dictionary dictionary;
    auto err = resources.open(argv[2]);
    if (!err) err = resources.dictionary("Dicts/English.dic", dictionary);
    if (err) { std::fprintf(stderr, "%s\n", err.message.c_str()); return 1; }
    ClassicRound round(dictionary);
    if ((err = round.board(std::move(board), options.duration)) || (err = round.start(0, options.duration))) {
        std::fprintf(stderr, "%s\n", err.message.c_str()); return 1;
    }
    std::printf("Native replay only; pending words carry provisional points. Dictionary approvals are not resolved here.\n");
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i]; const int before = round.score();
        round.tick(step.elapsed);
        const bool tick = step.word == "-";
        const char* result = tick ? "tick" : status(round.submit(step.word, step.elapsed));
        size_t pending = 0; for (const auto& word : round.words()) if (word.state == WordState::Pending) ++pending;
        std::printf("step %zu: ms %llu, %s, points %+d, score %d, words %zu, %s, remaining %llu, pending %zu\n",
            i + 1, static_cast<unsigned long long>(step.elapsed), result, round.score() - before,
            round.score(), round.accepted().size(), round.phase() == RoundPhase::Results ? "results" : "playing",
            static_cast<unsigned long long>(round.remaining(step.elapsed)), pending);
    }
    // No automatic expiry: a trace may intentionally stop before the deadline.
    return 0;
}
const reluva::tool::Commands commands("boggle", {
    {"classic-replay", "<game folder> <external scenario file> [original|master] [seconds]", "replay an observed board and timed submissions; print numbered native results", cmdReplay},
});
}
