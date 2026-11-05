// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/competition.h"
#include "engines/boggle/classic/results.h"
#include <limits>
#include <map>
#include <set>
namespace reluva::boggle {
Error ClassicCompetition::build(const std::vector<const ClassicRound*>& rounds) {
    clear();
    if(rounds.empty() || rounds.size()>maximumPlayers)return Error::fail("Classic needs one to four player results");
    std::vector<CompetitionPlayer> players;
    std::map<std::string,uint8_t> owners;std::set<const ClassicRound*> seen;
    for(size_t i=0;i<rounds.size();++i) {
        const auto* round=rounds[i];
        if(!round || !seen.insert(round).second)return Error::fail("Each Classic player needs a distinct round");
        if((round->board().side()!=4 && round->board().side()!=5) || round->board().side()!=rounds[0]->board().side() || round->board().faces()!=rounds[0]->board().faces())
            return Error::fail("Classic players must use the same board");
        RoundResults final;
        if(auto error=final.build(*round))return error;
        CompetitionPlayer player;
        for(const auto& word:final.words()) {
            owners[word.entry.word]|=uint8_t(1u<<i);
            player.words.push_back({word,0,0});
        }
        players.push_back(std::move(player));
    }
    for(auto& player:players) {
        int64_t total=0;
        for(auto& row:player.words) {
            row.owners=owners.at(row.word.entry.word);
            row.points=row.shared()?0:row.word.entry.points;total+=row.points;
        }
        if(total>std::numeric_limits<int>::max())return Error::fail("Classic competition score exceeds its limit");
        player.score=int(total);
    }
    players_=std::move(players);return Error::success();
}
}
