// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/sp_savegame.h"

#include "engines/idtech3/eliteforce/sp_game_host.h"

#include <map>
#include <utility>

namespace reluva::idtech3::jk2 {

namespace {

using raven::chunkId;

// The player carried over, as cvars (0x438d10): each chunk 1024 bytes.
constexpr std::pair<uint32_t, const char *> kPlayerCvars[] = {{chunkId("CVSV"), "playersave"},
                                                              {chunkId("AMMO"), "playerammo"},
                                                              {chunkId("IVTY"), "playerinv"},
                                                              {chunkId("FPLV"), "playerfplvl"}};
constexpr size_t kCvarChunk = 1024;
constexpr int32_t kVersion = 1;

} // namespace

void writeSaveHeader(raven::SaveWriter &w, const CvarStore &cvars, const SaveHeader &header) {
    w.compress = true; // sv_compress_saved_games, 1 unless changed
    w.addInt(chunkId("_VER"), kVersion);
    w.addText(chunkId("COMM"), header.comment, 64);
    w.addInt(chunkId("CMTM"), header.time);
    w.addInt(chunkId("SHLN"), int32_t(header.shot.size()));
    w.add(chunkId("SHOT"), header.shot);
    w.addText(chunkId("MPCM"), header.map, 1024);
    // The cvars flagged 2, their names and values with their NULs.
    std::vector<const CvarStore::Cvar *> kept;
    for (const CvarStore::Cvar &cv : cvars.all())
        if (cv.flags & 2)
            kept.push_back(&cv);
    w.addInt(chunkId("CVCN"), int32_t(kept.size()));
    for (const CvarStore::Cvar *cv : kept) {
        w.addText(chunkId("CVAR"), cv->name);
        w.addText(chunkId("VALU"), cv->value);
    }
    w.addInt(chunkId("GAME"), header.autosave ? 1 : 0);
    if (header.autosave)
        for (const auto &[id, name] : kPlayerCvars)
            w.addText(id, cvars.string(name), kCvarChunk);
}

Error readSaveHeader(raven::SaveReader &r, CvarStore &cvars, SaveHeader &out) {
    int32_t version = 0;
    int32_t count = 0;
    int32_t autosave = 0;
    int32_t shotLength = 0;
    if (Error e = r.nextInt(chunkId("_VER"), version))
        return e;
    if (version != kVersion)
        return Error::fail("It's a saved game of version " + std::to_string(version) + ", not " +
                           std::to_string(kVersion));
    if (Error e = r.nextText(chunkId("COMM"), out.comment, 64))
        return e;
    if (Error e = r.nextInt(chunkId("CMTM"), out.time))
        return e;
    if (Error e = r.nextInt(chunkId("SHLN"), shotLength))
        return e;
    if (Error e = r.next(chunkId("SHOT"), out.shot, size_t(shotLength)))
        return e;
    if (Error e = r.nextText(chunkId("MPCM"), out.map, 1024))
        return e;
    if (Error e = r.nextInt(chunkId("CVCN"), count))
        return e;
    for (int32_t i = 0; i < count; ++i) {
        std::string cvar;
        std::string value;
        if (Error e = r.nextText(chunkId("CVAR"), cvar))
            return e;
        if (Error e = r.nextText(chunkId("VALU"), value))
            return e;
        cvars.set(cvar, value, true);
    }
    if (Error e = r.nextInt(chunkId("GAME"), autosave))
        return e;
    out.autosave = autosave != 0;
    if (out.autosave)
        for (const auto &[id, name] : kPlayerCvars) {
            std::string text;
            if (Error e = r.nextText(id, text, kCvarChunk))
                return e;
            cvars.set(name, text);
        }
    return Error::success();
}

Error writeSaveLevel(raven::SaveWriter &w, eliteforce::SpGameHost &game, bool autosave) {
    if (!autosave) {
        w.addInt(chunkId("TIME"), game.levelTime());
        w.addInt(chunkId("TIMR"), 0);
        w.add(chunkId("PRTS"), game.areaPortalState());
        // Every configstring with text but the system info (1).
        std::vector<std::pair<int32_t, const std::string *>> strings;
        for (const auto &[index, text] : game.configstrings())
            if (index != 1 && !text.empty())
                strings.emplace_back(index, &text);
        w.addInt(chunkId("CSCN"), int32_t(strings.size()));
        for (const auto &[index, text] : strings) {
            w.addInt(chunkId("CSIN"), index);
            w.addText(chunkId("CSDA"), *text);
        }
    }
    game.setSaveStream(&w, nullptr);
    const Error err = game.writeLevel(autosave);
    game.setSaveStream(nullptr, nullptr);
    return err;
}

Error readSaveLevel(raven::SaveReader &r, eliteforce::SpGameHost &game, bool autosave) {
    if (!autosave) {
        int32_t time = 0;
        int32_t residual = 0;
        int32_t count = 0;
        std::vector<uint8_t> portals;
        if (Error e = r.nextInt(chunkId("TIME"), time))
            return e;
        if (Error e = r.nextInt(chunkId("TIMR"), residual))
            return e;
        if (Error e = r.next(chunkId("PRTS"), portals))
            return e;
        if (!game.setAreaPortalState(portals))
            return Error::fail("The saved game's area portals don't fit the level");
        if (Error e = r.nextInt(chunkId("CSCN"), count))
            return e;
        std::map<int32_t, std::string> strings;
        for (int32_t i = 0; i < count; ++i) {
            int32_t at = 0;
            std::string text;
            if (Error e = r.nextInt(chunkId("CSIN"), at))
                return e;
            if (Error e = r.nextText(chunkId("CSDA"), text))
                return e;
            strings[at] = text;
        }
        game.setLevelTime(time);
        game.replaceConfigstrings(strings);
    }
    return game.readLevel(autosave);
}

} // namespace reluva::idtech3::jk2
