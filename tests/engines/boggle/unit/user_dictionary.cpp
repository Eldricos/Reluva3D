// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/user_dictionary.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/support/test.h"
#include <fstream>
#include <iterator>

using namespace reluva;
using namespace reluva::boggle;
namespace {
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}};
}
void persistence(const std::filesystem::path& tmp) {
    const auto save = tmp / "save", game = tmp / "install", file = save / "EnglishUser.dic";
    std::filesystem::create_directories(game);
    fixtures::write(game / "EnglishUser.dic", "UNTOUCHED\r\n");
    UserDictionary words; CHECK(!words.load(save, game)); CHECK(words.words().empty());
    CHECK(!words.add({"cat","CAT","CATS"})); CHECK(words.words().size() == 2);
    CHECK(read(file) == "CAT\r\nCATS\r\n");
    UserDictionary restarted; CHECK(!restarted.load(save, game));
    CHECK(restarted.contains("CAT") && restarted.contains("CATS"));
    CHECK(!restarted.add({"TAC"})); CHECK(restarted.words().size() == 3);
    CHECK(read(save / "EnglishUser.dic.bak") == "CAT\r\nCATS\r\n");
    // Simulate interruption between moving the old snapshot and installing new.
    std::filesystem::remove(save / "EnglishUser.dic.bak");
    std::filesystem::rename(file, save / "EnglishUser.dic.bak");
    UserDictionary recovered; CHECK(!recovered.load(save, game)); CHECK(recovered.contains("TAC"));
    CHECK(!recovered.add({"ACT"})); CHECK(read(file) == "ACT\r\nCAT\r\nCATS\r\nTAC\r\n");
    CHECK(read(game / "EnglishUser.dic") == "UNTOUCHED\r\n");
    CHECK(bool(recovered.load(game / "saves", game))); CHECK(recovered.words().empty());
    CHECK(bool(recovered.add({"CAT"}))); // failed load must disable writes
}
void failureAndInput(const std::filesystem::path& tmp) {
    const auto save = tmp / "save", file = save / "EnglishUser.dic";
    UserDictionary words; CHECK(!words.load(save)); CHECK(!words.add({"CAT"}));
    const auto before = read(file);
    std::filesystem::create_directory(save / "EnglishUser.dic.tmp");
    CHECK(bool(words.add({"CATS"}))); CHECK(!words.contains("CATS")); CHECK(read(file) == before);
    std::filesystem::remove(save / "EnglishUser.dic.tmp");
    CHECK(!words.add({"CATS"}));
    CHECK(bool(words.add({"A B"}))); CHECK(bool(words.add({"AB"}))); CHECK(bool(words.add({std::string(65,'A')})));
    for (const auto& bad : {std::string("BAD WORD\n"),std::string("A\0B\n",4),std::string(65,'A'),std::string(2*1024*1024+1,'A')}) {
        fixtures::write(file, bad); UserDictionary corrupt; CHECK(bool(corrupt.load(save))); CHECK(corrupt.words().empty());
        CHECK(bool(corrupt.add({"CAT"}))); CHECK(read(file) == bad);
    }
    fixtures::write(file, "cat\r\n\nCATS\ncat\nTAC");
    UserDictionary normalized; CHECK(!normalized.load(save)); CHECK(normalized.words().size() == 3);
    UserDictionary unavailable; CHECK(!unavailable.load({})); CHECK(bool(unavailable.add({"CAT"})));
    // Symlinks are refused instead of following a dictionary into game data.
    std::error_code ec; std::filesystem::remove(file);
    fixtures::write(tmp / "other", "CAT\n"); std::filesystem::create_symlink(tmp / "other", file, ec);
    if (!ec) { CHECK(bool(normalized.load(save))); CHECK(read(tmp / "other") == "CAT\n"); }
}
}
static const test::Suite suite("boggle.unit.user_dictionary", {
    {"isolated snapshots, restart, recovery and original data protection", [](const auto& tmp) { persistence(tmp); }},
    {"failed writes, malformed text, normalization and unavailable saves", [](const auto& tmp) { failureAndInput(tmp); }},
});
