// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/files.h"

#include "common/fs.h"

#include <array>
#include <utility>

namespace reluva::awe::attwn {

namespace {

// The kinds the game opens by their "ac" names, as it asks for them and as
// the install has them.
constexpr std::array<std::pair<const char *, const char *>, 4> kKinds = {{
    {"txt", "act"},
    {"tga", "aca"},
    {"jpg", "acg"},
    {"wav", "acv"},
}};

// `name` with its three-letter extension `from` (any case) made `to`, or ""
// if it hasn't that extension.
std::string swapExtension(std::string_view name, const char *from, const char *to) {
    if (name.size() < 5 || name[name.size() - 4] != '.' || fs::toLower(name.substr(name.size() - 3)) != from)
        return {};
    return std::string(name.substr(0, name.size() - 3)) + to;
}

} // namespace

Error unscrambleText(const std::vector<uint8_t> &act, std::vector<uint8_t> &text) {
    const auto u16 = [&](size_t at) { return size_t(act[at] | act[at + 1] << 8); };
    if (act.size() < 2)
        return Error::fail("no line count");
    const size_t lines = u16(0);
    text.clear();
    size_t at = 2;
    for (size_t i = 0; i < lines; i++) {
        if (at + 2 > act.size())
            return Error::fail("line " + std::to_string(i + 1) + " of " + std::to_string(lines) + " missing");
        const size_t length = u16(at);
        at += 2;
        if (at + length > act.size())
            return Error::fail("line " + std::to_string(i + 1) + " cut short");
        for (size_t k = 0; k < length; k++)
            text.push_back(uint8_t(act[at + k] ^ 0x30));
        at += length;
    }
    return Error::success();
}

std::string acName(std::string_view name) {
    for (const auto &[plain, ac] : kKinds)
        if (std::string swapped = swapExtension(name, plain, ac); !swapped.empty())
            return swapped;
    return {};
}

std::string plainName(std::string_view name) {
    for (const auto &[plain, ac] : kKinds)
        if (std::string swapped = swapExtension(name, ac, plain); !swapped.empty())
            return swapped;
    return {};
}

Error GameFiles::open(const std::filesystem::path &dir) {
    return _dir.open(dir);
}

std::vector<std::string> GameFiles::list() const {
    std::vector<std::string> names = _dir.list();
    for (std::string &n : names)
        if (std::string plain = plainName(n); !plain.empty())
            n = std::move(plain);
    return names;
}

bool GameFiles::has(std::string_view name) const {
    const std::string ac = acName(name);
    return (!ac.empty() && _dir.has(ac)) || _dir.has(name);
}

Error GameFiles::read(std::string_view name, std::vector<uint8_t> &out) const {
    // The "ac" file first, as the game tries it (0x403c20), else the name
    // as it is.
    const std::string ac = acName(name);
    if (ac.empty() || !_dir.has(ac))
        return _dir.read(name, out);
    if (Error err = _dir.read(ac, out))
        return err;
    if (swapExtension(name, "txt", "act").empty())
        return Error::success();
    std::vector<uint8_t> text;
    if (Error err = unscrambleText(out, text))
        return Error::fail(ac + ": " + err.message);
    out = std::move(text);
    return Error::success();
}

Error openFiles(const std::filesystem::path &dir, std::unique_ptr<Archive> &out) {
    auto files = std::make_unique<GameFiles>();
    if (Error err = files->open(dir))
        return err;
    out = std::move(files);
    return Error::success();
}

} // namespace reluva::awe::attwn
