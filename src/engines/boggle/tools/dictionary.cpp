// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/resources.h"
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/options.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>

namespace {
int cmdDictionaryCheck(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: reluva-tool boggle dictionary-check <game folder>\n"); return 2; }
    reluva::boggle::Resources resources;
    if (auto err = resources.open(argv[2])) { std::fprintf(stderr, "%s\n", err.message.c_str()); return 1; }
    reluva::Md5 corpus;
    size_t count = 0, total = 0;
    for (const char* name : {"AI", "British", "Dutch", "English", "French", "German", "Spanish", "UK"}) {
        reluva::boggle::Dictionary dictionary;
        if (auto err = resources.dictionary(std::string("Dicts/") + name + ".dic", dictionary)) {
            std::fprintf(stderr, "%s.dic: %s\n", name, err.message.c_str()); return 1;
        }
        reluva::Md5 digest;
        for (const auto& word : dictionary.words()) {
            digest.update(word.data(), word.size()); digest.update("\0", 1);
            corpus.update(word.data(), word.size()); corpus.update("\0", 1);
        }
        std::printf("%s: entries %zu, unique %zu, decoded %s\n", name, dictionary.entries(), dictionary.words().size(), digest.hexDigest().c_str());
        ++count; total += dictionary.words().size();
    }
    std::printf("dictionaries %zu, unique words summed %zu, decoded dictionaries: %s\n", count, total, corpus.hexDigest().c_str());
    return 0;
}
int cmdDictionaryQuery(int argc, char** argv) {
    if (argc != 4) { std::fprintf(stderr, "usage: reluva-tool boggle dictionary-query <game folder> <ASCII word>\n"); return 2; }
    std::string word = argv[3];
    if (word.empty() || word.size() > 64) return 2;
    for (char& c : word) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (c < 'A' || c > 'Z') { std::fprintf(stderr, "This query command uses U.S. English ASCII letters\n"); return 2; }
    }
    reluva::boggle::Resources resources;
    reluva::boggle::Dictionary dictionary;
    auto err = resources.open(argv[2]);
    if (!err) err = resources.dictionary("Dicts/English.dic", dictionary);
    if (err) { std::fprintf(stderr, "%s\n", err.message.c_str()); return 1; }
    std::printf("word %s, prefix %s\n", dictionary.contains(word) ? "present" : "absent", dictionary.hasPrefix(word) ? "present" : "absent");
    return 0;
}
int cmdClassicCheck(int argc, char** argv) {
    using namespace reluva::boggle;
    if (argc != 3 && argc != 4) { std::fprintf(stderr, "usage: reluva-tool boggle classic-check <game folder> [original|master]\n"); return 2; }
    ClassicOptions options;
    if (auto err = readClassicOptions({{"classic_variant", argc == 4 ? argv[3] : "original"}}, options)) { std::fprintf(stderr,"%s\n",err.message.c_str()); return 2; }
    Resources resources; Dictionary dictionary; DiceSet dice; Board board;
    auto err = resources.open(argv[2]);
    if (!err) err = resources.dictionary("Dicts/English.dic", dictionary);
    if (!err) err = resources.diceSet(options.dice(), dice);
    if (!err) err = Board::generate(dice, 0x1997, board);
    if (err) { std::fprintf(stderr, "%s\n", err.message.c_str()); return 1; }
    ClassicRound round(dictionary);
    if ((err = round.board(board)) || (err = round.start(1000))) {
        std::fprintf(stderr, "%s\n", err.message.c_str()); return 1;
    }
    reluva::Md5 digest;
    size_t count = 0; int points = 0;
    for (const auto& word : dictionary.words()) {
        if (word.size() < round.minimum()) continue;
        std::vector<size_t> path;
        const auto found = board.find(word, path);
        if (found == PathResult::Limit) { std::fprintf(stderr, "Path search budget exceeded\n"); return 1; }
        if (found != PathResult::Found) continue;
        if (!board.matches(word, path) || round.submit(word, 1000) != Submission::Accepted ||
            round.submit(word, 1000) != Submission::Duplicate) {
            std::fprintf(stderr, "Round acceptance mismatch\n"); return 1;
        }
        ++count; points += classicPoints(word.size(), round.minimum());
        digest.update(word.data(), word.size()); digest.update("\0", 1);
    }
    round.tick(181000);
    if (!count || round.score() != points || round.phase() != RoundPhase::Results || round.remaining(181000)) {
        std::fprintf(stderr, "Round result mismatch\n"); return 1;
    }
    std::printf("Classic %s: seed 6551, accepted %zu, score %d, duration 180000 ms\n", options.side() == 5 ? "Master" : "Original", count, points);
    std::printf("decoded Classic: %s\n", digest.hexDigest().c_str());
    return 0;
}
const reluva::tool::Commands commands("boggle", {
    {"classic-check", "<game folder> [original|master]", "validate a deterministic Classic round; report counts/checksum only", cmdClassicCheck},
    {"dictionary-check", "<game folder>", "decode all eight binary dictionaries; report counts/checksums only", cmdDictionaryCheck},
    {"dictionary-query", "<game folder> <ASCII word>", "check U.S. English membership and prefix lookup", cmdDictionaryQuery},
});
}
