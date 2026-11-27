// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A park's state written out and read back: what play has changed of it,
// the settings left to Park::load. See
// docs/engines/tpw/games/simcoaster/formats/reluva-saves.md.

#include "engines/tpw/park/park.h"
#include "engines/tpw/park/state_io.h"

namespace reluva::tpw {

namespace {

// The state's own version: a save of another is refused.
constexpr uint32_t kStateVersion = 3;

} // namespace

using stateio::io;
using stateio::Reader;
using stateio::Writer;

template<class A>
void Park::ioState(A &a) {
    io(a, _progress);
    io(a, _obstacleResearched);
    io(a, _themesOpened);
    io(a, _won);
    io(a, _goldenTickets);
    io(a, _rank);
    io(a, _cash);
    io(a, _thisMonth);
    io(a, _lastMonth);
    io(a, _thisYear);
    io(a, _lastReport);
    io(a, _reported);
    io(a, _totalVisitors);
    io(a, _generationVisitors);
    io(a, _yearVisitors);
    io(a, _loans);
    io(a, _shares);
    io(a, _monthsInRed);
    io(a, _wasInRed);
    io(a, _beenInRed);
    io(a, _bankrupt);
    io(a, _lowWarned);
    io(a, _repayAdvised);
    io(a, _messages);
    io(a, _challenges);
    io(a, _challengesWon);
    io(a, _challengesFailed);
    io(a, _fired);
    io(a, _lastOffer);
    io(a, _sold);
    io(a, _shopProfit);
    io(a, _usesById);
    io(a, _kidPranks);
    io(a, _adultPranks);
    io(a, _heatwaves);
    io(a, _admissionFee);
    io(a, _visitors);
    io(a, _millis);
    io(a, _ground);
    io(a, _owned);
    io(a, _fence);
    io(a, _objects);
    io(a, _queues);
    io(a, _weather);
    io(a, _lastDay);
    io(a, _litter);
    io(a, _projects);
    io(a, _researched);
    io(a, _workRate);
    io(a, _strikes);
    io(a, _demands);
    io(a, _parkOpen);
    io(a, _pool);
    io(a, _staff);
    io(a, _candidatesMade);
    io(a, _nextStaffId);
    io(a, _stinkBombs);
    io(a, _people);
    io(a, _nextArrivalMs);
    io(a, _nextId);
    bool clearing = _clearing.has_value();
    io(a, clearing);
    if constexpr (A::kReading)
        _clearing = clearing ? std::optional<Clearing>(Clearing{}) : std::nullopt;
    if (clearing) {
        io(a, _clearing->staff);
        io(a, _clearing->days);
    }
}

std::vector<uint8_t> Park::saveState() const {
    std::vector<uint8_t> out;
    Writer w(out);
    uint32_t version = kStateVersion;
    io(w, version);
    // Written as read: the fields are taken by reference, not changed.
    const_cast<Park &>(*this).ioState(w);
    return out;
}

Error Park::restoreState(const std::vector<uint8_t> &data) {
    Reader r(data.data(), data.size());
    uint32_t version = 0;
    io(r, version);
    if (!r.ok() || version != kStateVersion)
        return Error::fail("a saved park of another version");
    const size_t cells = _ground.size();
    const std::vector<ChallengeState> challenges = _challenges;
    ioState(r);
    if (!r.ok() || !r.atEnd())
        return Error::fail("a saved park cut short or too long");
    if (_ground.size() != cells || _owned.size() != cells)
        return Error::fail("a saved park of another world");
    // Each challenge's settings again, by its number.
    for (ChallengeState &c : _challenges)
        for (const ChallengeState &from : challenges)
            if (from.challenge.number == c.challenge.number)
                c.challenge = from.challenge;
    // The mission as play left it, the research's results on sale again.
    const bool researched = _obstacleResearched;
    startMission(_progress);
    _obstacleResearched = researched;
    for (const auto &[id, level] : _researched)
        if (level == 0)
            _attractions.markAvailable(id);
    setResearchables();
    _removals.clear();
    return Error::success();
}

} // namespace reluva::tpw
