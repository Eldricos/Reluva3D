// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/user_dictionary.h"
#include "engines/boggle/classic/round.h"
#include <fstream>

namespace reluva::boggle {
namespace {
constexpr size_t maxBytes = 2 * 1024 * 1024, maxWords = 32768;
Error failure() { return Error::fail("Cannot save user dictionary. Check the game's save directory and retry, or choose No."); }
Error safeFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) return Error::success();
    if (ec || !std::filesystem::is_regular_file(status)) return Error::fail("User dictionary path is not a regular file");
    return Error::success();
}
bool inside(const std::filesystem::path& parent, std::filesystem::path child) {
    // Ask the filesystem too: on Windows, different casing can name the same
    // existing game directory even when its future save subfolder is absent.
    while (!child.empty()) {
        std::error_code ec;
        if (child == parent || std::filesystem::equivalent(parent, child, ec)) return true;
        const auto next = child.parent_path();
        if (next == child) break;
        child = next;
    }
    return false;
}
Error readText(const std::filesystem::path& path, std::set<std::string>& words) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || in.tellg() > std::streamoff(maxBytes)) return Error::fail("Cannot read user dictionary, or size limit exceeded");
    in.seekg(0); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string normalized;
        if (!englishWord(line, normalized)) return Error::fail("Invalid English user dictionary: expected one A-Z word per line");
        words.insert(std::move(normalized));
        if (words.size() > maxWords) return Error::fail("User dictionary word limit exceeded");
    }
    if (in.bad()) return Error::fail("Cannot read user dictionary");
    return Error::success();
}
}
Error UserDictionary::load(const std::filesystem::path& savePath, const std::filesystem::path& gamePath) {
    words_.clear(); file_.clear();
    if (savePath.empty()) return Error::success(); // no accidental working-directory fallback
    std::error_code ec;
    const auto save = std::filesystem::weakly_canonical(savePath, ec);
    if (ec) return Error::fail("Cannot resolve user dictionary save directory");
    if (!gamePath.empty()) {
        const auto game = std::filesystem::weakly_canonical(gamePath, ec);
        if (ec || inside(game, save)) return Error::fail("User dictionary save directory must be outside the original game folder");
    }
    const auto file = save / "EnglishUser.dic", backup = save / "EnglishUser.dic.bak";
    if (auto err = safeFile(file)) return err;
    if (auto err = safeFile(backup)) return err;
    bool present = std::filesystem::exists(file, ec);
    if (ec) return Error::fail("Cannot inspect user dictionary");
    auto source = file;
    if (!present) {
        present = std::filesystem::exists(backup, ec); source = backup;
        if (ec) return Error::fail("Cannot inspect user dictionary backup");
    }
    std::set<std::string> words;
    if (present) if (auto err = readText(source, words)) return err;
    file_ = file; words_ = std::move(words);
    return Error::success();
}
Error UserDictionary::add(const std::vector<std::string>& additions) {
    auto next = words_;
    for (const auto& word : additions) {
        std::string normalized;
        if (!englishWord(word, normalized) || normalized.size() < 3) return Error::fail("User words need at least three English letters");
        next.insert(std::move(normalized));
    }
    if (next == words_) return Error::success();
    if (next.size() > maxWords) return Error::fail("User dictionary word limit exceeded");
    if (file_.empty()) return Error::fail("No save directory is available for user words. Choose No to continue.");
    size_t bytes = 0; for (const auto& word : next) bytes += word.size() + 2;
    if (bytes > maxBytes) return Error::fail("User dictionary size limit exceeded");
    const auto temp = file_.parent_path() / "EnglishUser.dic.tmp";
    const auto backup = file_.parent_path() / "EnglishUser.dic.bak";
    for (const auto& path : {file_, temp, backup}) if (auto err = safeFile(path)) return err;
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (ec) return failure();
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const auto& word : next) { out.write(word.data(), std::streamsize(word.size())); out.write("\r\n", 2); }
        out.flush();
        if (!out) return failure();
        out.close(); if (!out) return failure();
    }
    const bool present = std::filesystem::exists(file_, ec);
    if (ec) return failure();
    if (present) {
        std::filesystem::remove(backup, ec); if (ec) return failure();
        std::filesystem::rename(file_, backup, ec); if (ec) return failure();
    }
    std::filesystem::rename(temp, file_, ec);
    if (ec) {
        if (present) { std::error_code restore; std::filesystem::rename(backup, file_, restore); }
        return failure(); // old snapshot remains in place or recoverable as .bak
    }
    words_ = std::move(next);
    return Error::success();
}
} // namespace reluva::boggle
