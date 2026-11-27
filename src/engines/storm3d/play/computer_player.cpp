// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/computer_player.h"

#include "common/text.h"

namespace reluva::storm3d {

std::string ComputerPlayer::buildListFileOf(const std::string &race, Stance stance) {
    static const std::map<std::string, std::string> kNames = {
        {"fed", "federation"}, {"kling", "klingon"}, {"rom", "romulan"}, {"borg", "borg"}};
    auto it = kNames.find(text::toLower(race));
    const char *list = stance == Stance::kDefensive   ? "_defensive_list.aip"
                       : stance == Stance::kOffensive ? "_offensive_list.aip"
                                                      : "_build_list.aip";
    return (it == kNames.end() ? std::string("federation") : it->second) + list;
}

std::string ComputerPlayer::personalityFileOf(Stance stance) {
    return stance == Stance::kDefensive   ? "default_defensive.aip"
           : stance == Stance::kOffensive ? "default_offensive.aip"
                                          : "newdefault.aip";
}

ComputerPlayer::Stance ComputerPlayer::stanceFor(float strength, float average) {
    if (strength >= average * 1.1f)
        return Stance::kOffensive;
    return strength <= average * 0.7f ? Stance::kDefensive : Stance::kNormal;
}

std::vector<ComputerPlayer::Element> ComputerPlayer::buildListOf(const std::string &file,
                                                                 const ConfigFile::Loader &loader) {
    std::vector<Element> list;
    ConfigFile config;
    if (config.load(file, loader))
        return list;
    const ConfigFile::Value *all = config.find("the_build_list");
    if (!all || all->kind != ConfigFile::Value::Kind::kList)
        return list;
    for (const ConfigFile::Value &item : all->items) {
        if (item.kind != ConfigFile::Value::Kind::kList || item.items.size() < 2 ||
            item.items[0].kind != ConfigFile::Value::Kind::kString)
            continue;
        Element e;
        e.type = text::toLower(item.items[0].text);
        e.count = int(item.items[1].number);
        e.base = item.items.size() > 2 ? int(item.items[2].number) : 0;
        list.push_back(e);
    }
    return list;
}

std::optional<size_t> ComputerPlayer::nextOf(const std::vector<Element> &list,
                                             const std::map<std::string, int> &owned) {
    // "fbase 1, fconst 1, fmining 1, ffreight 2, ... fmining 1": the second
    // mining station is wanted once there are two in all.
    std::map<std::string, int> wanted;
    for (size_t i = 0; i < list.size(); ++i) {
        const int want = wanted[list[i].type] += list[i].count;
        auto have = owned.find(list[i].type);
        if ((have == owned.end() ? 0 : have->second) < want)
            return i;
    }
    return std::nullopt;
}

} // namespace reluva::storm3d
