// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/save_game.h"

#include "common/text.h"
#include "engines/botf/formats/alt_res.h"

#include <string_view>
#include <vector>

namespace reluva::botf {

namespace {

// gameInfo's fields used here, as the Ultimate Editor's GameInfo.java
// gives them.
constexpr size_t kDescriptionAt = 0x0D;
constexpr size_t kDescriptionSize = 51;
constexpr size_t kTurnAt = 0x40;
constexpr size_t kMultiplayerAt = 0x4C;
constexpr size_t kUsed = kMultiplayerAt + 4;
constexpr size_t kEmpireAt = 0x252;

// The saved game's text files are compressed with the window full of
// spaces; gameInfo is one of them.
constexpr uint8_t kTextWindowFill = 0x20;

std::string field(const std::vector<uint8_t> &data, size_t at, size_t size) {
    size_t len = 0;
    while (len < size && data[at + len])
        ++len;
    return text::windows1252ToUtf8(std::string_view(reinterpret_cast<const char *>(&data[at]), len));
}

} // namespace

Error readSaveSummary(const std::filesystem::path &path, SaveSummary &summary) {
    AltResArchive save;
    if (Error err = save.open(path))
        return err;
    summary = SaveSummary{};
    summary.name = text::windows1252ToUtf8(save.name());
    for (const AltResArchive::Entry &e : save.entries()) {
        if (normalizeArchiveName(e.name) != normalizeArchiveName("gameInfo"))
            continue;
        std::vector<uint8_t> info;
        if (Error err = save.read(e, info, kTextWindowFill))
            return err;
        if (info.size() < kUsed)
            return Error::fail(path.string() + ": gameInfo is too small");
        summary.description = field(info, kDescriptionAt, kDescriptionSize);
        const uint8_t *t = &info[kTurnAt];
        summary.turn = int(uint32_t(t[0] | t[1] << 8 | t[2] << 16) | uint32_t(t[3]) << 24);
        summary.multiplayer = info[kMultiplayerAt] != 0;
        if (info.size() > kEmpireAt && info[kEmpireAt] < 5)
            summary.empire = info[kEmpireAt];
        return Error::success();
    }
    return Error::fail(path.string() + " has no gameInfo");
}

} // namespace reluva::botf
