// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The directors' challenges: offered, taken up, prepared for, run, won or
// failed. docs/engines/tpw/games/simcoaster/systems/challenges.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr const char *kChallengeSettings = "Challenges.sam";
constexpr int kMostChallenges = 64;
constexpr int kMostInWorld = 16;
// The worlds' folders, by the missions' theme numbers.
constexpr std::array<const char *, 3> kWorldFolders{"arabian", "science", "water"};

// The challenges' types, as Challenges.sam's comments and their letters
// (CHALLENGE_SPECIFIC.str) tell them.
enum Type : int {
    kHotDogsSold = 2,
    kDrinksSold = 3,
    kBalloonsSold = 6,
    kCostumedChildren = 10, // percent of the children in the park
    kRideUsers = 17,        // of the TargetObj attraction
    kStaffHappiness = 25,   // the lowest
    kProfitKept = 34,       // last month's
    kSideshowSatisfaction = 35,
    kVisitorHappiness = 36, // the average
    kVisitorsKept = 37,
    kNoPranks = 38,
    kNoAdultPranks = 39,
    kNoChildPranks = 40,
    kNoStrikes = 41,
    kHotDogScare = 42, // hot dog stands shut, hunger not among the three commonest thoughts
    kNoLitter = 43,
    kDrinkProfit = 45,          // drinks' and ices' takings less their goods
    kRideRepair = 46,           // the lowest state of repair of the rides
    kSpecialRides = 48,         // and after: a special ride's (TargetObj), once it's activated
    kRideLoved = 48,            // its satisfaction, and no pranks
    kHappyThoughts = 50,        // happiness among the three commonest thoughts
    kCoasterLoops = 51,         // the coaster's loops: no coasters yet
    kRideSafeAndUsed = 52,      // so many users, its repair above TargetVal2
    kRideFastAndSafe = 53,      // its excitement, its repair above TargetVal2
    kRideExcitingNotMessy = 55, // its excitement, "Too messy" not among the commonest thoughts
    kRideExcitingAndLiked = 56, // its excitement, satisfaction above TargetVal2
    kRidePhotos = 57,           // its users buying photos: no photos yet
    kRideWidelyUsed = 58,       // so many percent of the park's visitors use it
};

// The shops' ShopType numbers they count.
constexpr size_t kHotDogShop = 2;
constexpr size_t kDrinkShop = 4;
constexpr size_t kIceShop = 7;
constexpr size_t kBalloonShop = 6;
constexpr int kCostume = 2; // Visitor::appearance

// TAG_SYSTEM's: the challenges' messages' titles, by event (offered, under
// way, won, failed, time running out); each sender's texts (the
// president's between the titles; the directors' five in a row from 53,
// in Sender's order, as the recording's Miss Moody, Human Resources,
// sender 5, writes 68); the senders' names.
constexpr std::array<uint32_t, 5> kTitles{42, 44, 46, 48, 50};
constexpr uint32_t kPresidentTexts = 43;
constexpr std::array<uint32_t, 5> kDirectorTexts{53, 58, 63, 68, 73};
constexpr std::array<uint32_t, 6> kSenders{173, 177, 179, 181, 183, 185};
constexpr uint32_t kPromotionTitle = 169;
constexpr uint32_t kPromotionLetter = 170;
constexpr uint32_t kGameOverTitle = 165;
constexpr uint32_t kFiredLetter = 167;
enum Event { kOffered, kUnderWay, kWon, kFailed, kTimeShort };

// Whether a type counts up to its target, won as soon as it's there;
// else it keeps a measure, failed as soon as it slips.
bool counts(int type) {
    return type == kHotDogsSold || type == kDrinksSold || type == kBalloonsSold || type == kRideUsers ||
           type == kDrinkProfit || type == kRideSafeAndUsed;
}

} // namespace

// Every challenge's settings; this world's list of them (its
// ChallengesInThisLevel); the general rules of offering and firing.
void Park::loadChallenges(const SearchSet &files, const SamFile &general, const SamFile *world) {
    _daysBetweenOffers = int(general.number("Challenges.DaysBetweenOffers", _daysBetweenOffers));
    _warningPercent = int(general.number("Challenges.ShortTimeLeftWarningAt", _warningPercent));
    for (size_t r = 0; r < _firingLine.size(); ++r)
        _firingLine[r] = int(general.number("FiringLineForRank[" + std::to_string(r) + "].Limit", _firingLine[r]));
    // Each world's list, for whose a challenge is.
    for (size_t t = 0; t < kWorldFolders.size(); ++t) {
        std::vector<uint8_t> list;
        SamFile sam;
        if (files.read(std::string("levels/") + kWorldFolders[t] + "/Standard.sam", list) || sam.parse(list))
            continue;
        for (int i = 0; i < kMostInWorld; ++i)
            if (const int n = int(sam.number("ChallengesInThisLevel[" + std::to_string(i) + "].ChallengeType", 0)))
                _challengeThemes[n] = int(t);
    }
    std::vector<uint8_t> data;
    SamFile all;
    if (!world || files.read(kChallengeSettings, data) || all.parse(data))
        return;
    for (int i = 0; i < kMostInWorld; ++i) {
        const int n = int(world->number("ChallengesInThisLevel[" + std::to_string(i) + "].ChallengeType", 0));
        if (n <= 0 || n >= kMostChallenges)
            continue;
        const std::string at = "Challenges[" + std::to_string(n) + "].";
        auto number = [&](const char *name, double fallback) { return int(all.number(at + name, fallback)); };
        Challenge c;
        c.number = n;
        c.type = number("Type", 0);
        c.time = number("TargetTime", 30);
        c.target = number("TargetVal", 0);
        c.targetObject = number("TargetObj", 0);
        c.target2 = number("TargetVal2", 0);
        c.preparation = number("GracePeriodLimit", 0);
        c.cash = number("CashPrize", 0);
        c.tickets = std::max(1, number("GTPrize", 1)); // "This has a minimum value of 1"
        c.shares = number("SharePrize", 0);
        c.promotion = number("PromoGiven", 0);
        c.missionNeeded = number("MissionNeededToActivate", 0);
        c.trigger = number("TriggerData", 0);
        c.daysDelay = number("DaysDelay", 0);
        c.sender = std::clamp(number("Sender", 1), 1, int(kSenders.size()));
        c.priceWon = number("SharePriceEffectWon", 0);
        c.priceLost = number("SharePriceEffectLost", 0);
        ChallengeState state;
        state.challenge = c;
        if (std::find(_challengesWon.begin(), _challengesWon.end(), n) != _challengesWon.end())
            state.status = ChallengeStatus::kComplete;
        _challenges.push_back(state);
    }
}

void Park::postChallenge(const Challenge &c, int event) {
    Message m;
    m.title = kTitles[size_t(event)];
    m.text =
        c.sender == 1 ? kPresidentTexts + 2 * uint32_t(event) : kDirectorTexts[size_t(c.sender - 2)] + uint32_t(event);
    m.sender = kSenders[size_t(c.sender - 1)];
    m.challenge = c.number;
    _messages.push_back(std::move(m));
}

// What sets a challenge off, as its TriggerData's comment reads for its
// type; those Reluva can't tell are set off at once (guessed). The
// special rides' wait for the rides to be opened.
bool Park::challengeTriggered(const Challenge &c) const {
    switch (c.type) {
    case kVisitorsKept:
        return _visitors >= c.trigger;
    case kDrinksSold:
        return _totalVisitors >= c.trigger; // "vistors to park ever"
    case kHotDogsSold:
        return std::count_if(_objects.begin(), _objects.end(), [&](const PlacedObject &o) {
                   const Attractions::Attraction *a = _attractions.find(o.id);
                   return a && a->shopType == int(kHotDogShop);
               }) >= c.trigger; // "nth hot dog shop built"
    case kProfitKept:
        return income(_lastMonth) - expenditure(_lastMonth) >= c.trigger;
    case kNoChildPranks:
        return _kidPranks >= c.trigger;
    case kNoAdultPranks:
        return _adultPranks >= c.trigger;
    case kNoPranks:
        return _kidPranks + _adultPranks >= c.trigger;
    case kDrinkProfit:
        return _heatwaves >= c.trigger; // "after nth heatwave"
    case kCoasterLoops:
    case kRidePhotos:
        return false;
    case kRideUsers:
    default:
        // An attraction's, once it's there and open to visitors: a special
        // ride once it's activated, as the recording's first mission, its
        // special rides boarded up, offers none.
        if (c.type != kRideUsers && c.type < kSpecialRides)
            return true;
        return std::any_of(_objects.begin(), _objects.end(),
                           [&](const PlacedObject &o) { return o.id == c.targetObject && o.active; });
    }
}

void Park::setChallengeRecord(std::vector<int> won, int failures) {
    _challengesWon = std::move(won);
    _challengesFailed = failures;
    for (ChallengeState &state : _challenges)
        if (std::find(_challengesWon.begin(), _challengesWon.end(), state.challenge.number) != _challengesWon.end())
            state.status = ChallengeStatus::kComplete;
}

int Park::challengesWonIn(int theme) const {
    return int(std::count_if(_challengesWon.begin(), _challengesWon.end(), [&](int n) {
        const auto t = _challengeThemes.find(n);
        return t != _challengeThemes.end() && t->second == theme;
    }));
}

int Park::failuresLeft() const {
    return std::max(0, _firingLine[size_t(std::clamp(_rank, 0, int(_firingLine.size()) - 1))] - _challengesFailed);
}

bool Park::acceptChallenge(size_t index) {
    if (index >= _challenges.size() || _challenges[index].status != ChallengeStatus::kAvailable)
        return false;
    for (const ChallengeState &s : _challenges)
        if (s.status == ChallengeStatus::kPreparing || s.status == ChallengeStatus::kActive)
            return false;
    ChallengeState &state = _challenges[index];
    const Challenge &c = state.challenge;
    state.warned = false;
    // "I need to close them all for a while".
    if (c.type == kHotDogScare)
        for (size_t i = 0; i < _objects.size(); ++i)
            if (const Attractions::Attraction *a = _attractions.find(_objects[i].id);
                a && a->shopType == int(kHotDogShop))
                _objects[i].open = false;
    if (c.preparation > 0) {
        state.status = ChallengeStatus::kPreparing;
        state.daysLeft = c.preparation;
    } else {
        state.status = ChallengeStatus::kActive;
        state.daysLeft = c.time;
        startCounting(index);
    }
    return true;
}

// What a challenge counts from as it begins: its count, the pranks or
// the visitors come in so far.
void Park::startCounting(size_t index) {
    ChallengeState &state = _challenges[index];
    const int type = state.challenge.type;
    state.baseline = 0;
    state.baseline2 = 0;
    const bool countsUses = type == kRideSafeAndUsed || type == kRideWidelyUsed;
    if (counts(type) || type == kNoPranks || type == kNoAdultPranks || type == kNoChildPranks) {
        state.baseline = challengeValue(index);
    } else if (countsUses) {
        const auto uses = _usesById.find(state.challenge.targetObject);
        state.baseline = uses == _usesById.end() ? 0 : uses->second;
    }
    if (type == kRideLoved)
        state.baseline2 = _kidPranks + _adultPranks;
    if (type == kRideWidelyUsed)
        state.baseline2 = _totalVisitors;
}

// A count since it began; or what it keeps watch on now.
int Park::challengeValue(size_t index) const {
    if (index >= _challenges.size())
        return 0;
    const ChallengeState &state = _challenges[index];
    const Challenge &c = state.challenge;
    const auto inPark = [&](auto &&value, auto &&reduce, int none) {
        int out = none;
        for (const Visitor &v : _people)
            if (v.inPark)
                out = reduce(out, value(v));
        return out;
    };
    switch (c.type) {
    case kHotDogsSold:
        return _sold[kHotDogShop] - state.baseline;
    case kDrinksSold:
        return _sold[kDrinkShop] - state.baseline;
    case kBalloonsSold:
        return _sold[kBalloonShop] - state.baseline;
    case kRideUsers: {
        const auto uses = _usesById.find(c.targetObject);
        return (uses == _usesById.end() ? 0 : uses->second) - state.baseline;
    }
    case kDrinkProfit:
        return _shopProfit[kDrinkShop] + _shopProfit[kIceShop] - state.baseline;
    case kCostumedChildren: {
        int children = 0;
        int costumed = 0;
        for (const Visitor &v : _people)
            if (v.inPark && v.generation == 0) {
                ++children;
                costumed += v.appearance == kCostume;
            }
        return children ? costumed * 100 / children : 0;
    }
    case kStaffHappiness: {
        int lowest = 100;
        for (const StaffMember &m : _staff)
            lowest = std::min(lowest, int(m.happiness));
        return lowest;
    }
    case kProfitKept:
        return income(_lastMonth) - expenditure(_lastMonth);
    case kSideshowSatisfaction: // visitors' satisfaction isn't kept: their happiness stands in (guessed)
    case kVisitorHappiness: {
        const int count = inPark([](const Visitor &) { return 1; }, [](int a, int b) { return a + b; }, 0);
        const int sum =
            inPark([](const Visitor &v) { return int(v.happiness); }, [](int a, int b) { return a + b; }, 0);
        return count ? sum / count : 0;
    }
    case kVisitorsKept:
        return _visitors;
    case kNoPranks:
        return _kidPranks + _adultPranks - state.baseline;
    case kNoAdultPranks:
        return _adultPranks - state.baseline;
    case kNoChildPranks:
        return _kidPranks - state.baseline;
    case kNoStrikes:
        return int(std::count(_strikes.begin(), _strikes.end(), true));
    case kNoLitter:
        return int(_litter.size());
    case kRideRepair: {
        int lowest = 100;
        for (const PlacedObject &o : _objects)
            if (const Attractions::Attraction *a = _attractions.find(o.id); a && a->kind == Attractions::Kind::kRide)
                lowest = std::min(lowest, int(o.repair));
        return lowest;
    }
    case kHotDogScare:
        return amongTopThoughts({Thought::kHungry, Thought::kHungryAndThirsty}) ? 1 : 0;
    case kRideLoved:
        return satisfaction();
    case kRideSafeAndUsed: {
        const auto uses = _usesById.find(c.targetObject);
        return (uses == _usesById.end() ? 0 : uses->second) - state.baseline;
    }
    case kRideFastAndSafe:
    case kRideExcitingNotMessy:
    case kRideExcitingAndLiked: {
        const Attractions::Attraction *a = _attractions.find(c.targetObject);
        return a ? std::max(0, a->excitement) : 0;
    }
    case kRideWidelyUsed: {
        // Its users since it began, of the visitors come in since.
        const auto uses = _usesById.find(c.targetObject);
        const int users = (uses == _usesById.end() ? 0 : uses->second) - state.baseline;
        const int visitors = _totalVisitors - state.baseline2;
        return visitors > 0 ? users * 100 / visitors : 0;
    }
    default:
        return 0;
    }
}

int Park::challengeValue2(size_t index) const {
    if (index >= _challenges.size())
        return 0;
    const Challenge &c = _challenges[index].challenge;
    if (c.type == kRideExcitingAndLiked)
        return satisfaction();
    int lowest = 100;
    for (const PlacedObject &o : _objects)
        if (o.id == c.targetObject)
            lowest = std::min(lowest, int(o.repair));
    return lowest;
}

// Visitors' satisfaction isn't kept: their average happiness stands in (guessed).
int Park::satisfaction() const {
    int count = 0;
    float sum = 0;
    for (const Visitor &v : _people)
        if (v.inPark) {
            ++count;
            sum += v.happiness;
        }
    return count ? int(sum / float(count)) : 0;
}

// Whether any of the thoughts is among the three the most visitors in
// the park have now.
bool Park::amongTopThoughts(std::initializer_list<Thought> wanted) const {
    std::array<int, 64> thoughts{};
    for (const Visitor &v : _people)
        if (v.inPark)
            if (const auto t = size_t(v.thought != Thought::kNone ? v.thought : howTheyAre(v)); t < thoughts.size())
                ++thoughts[t];
    std::array<size_t, 64> order{};
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin() + 1, order.end(), [&](size_t a, size_t b) { return thoughts[a] > thoughts[b]; });
    for (size_t place = 1; place <= 3; ++place)
        if (thoughts[order[place]] > 0 &&
            std::find(wanted.begin(), wanted.end(), Thought(order[place])) != wanted.end())
            return true;
    return false;
}

// How a challenge under way stands: won (1), failed (−1), or on (0). A
// count is won on reaching its target and failed with time up; a kept
// measure failed as it slips and won with time up; a special ride's as
// its letter asks.
int Park::challengeOutcome(size_t index, bool timeUp) const {
    const ChallengeState &state = _challenges[index];
    const Challenge &c = state.challenge;
    const int value = challengeValue(index);
    switch (c.type) {
    case kRideSafeAndUsed:
        if (challengeValue2(index) < c.target2)
            return -1;
        return value >= c.target ? 1 : timeUp ? -1 : 0;
    case kRideLoved:
        if (value < c.target || _kidPranks + _adultPranks > state.baseline2)
            return -1;
        return timeUp ? 1 : 0;
    case kHappyThoughts:
        if (!amongTopThoughts({Thought::kVeryHappy, Thought::kHappy}))
            return -1;
        return timeUp ? 1 : 0;
    case kRideFastAndSafe:
        if (value < c.target || challengeValue2(index) < c.target2)
            return -1;
        return timeUp ? 1 : 0;
    case kRideExcitingNotMessy:
        if (value < c.target || amongTopThoughts({Thought::kMessy}))
            return -1;
        return timeUp ? 1 : 0;
    case kRideExcitingAndLiked:
        if (value < c.target || challengeValue2(index) < c.target2)
            return -1;
        return timeUp ? 1 : 0;
    case kRideWidelyUsed:
        return timeUp ? (value >= c.target ? 1 : -1) : 0;
    default:
        if (counts(c.type))
            return value >= c.target ? 1 : timeUp ? -1 : 0;
        if (!challengeKept(c, value))
            return -1;
        return timeUp ? 1 : 0;
    }
}

// Whether a kept measure holds: at or above the target, or, for what
// mustn't happen, nothing of it.
bool Park::challengeKept(const Challenge &c, int value) const {
    switch (c.type) {
    case kNoPranks:
    case kNoAdultPranks:
    case kNoChildPranks:
    case kNoStrikes:
    case kNoLitter:
    case kHotDogScare:
        return value == 0;
    default:
        return value >= c.target;
    }
}

// Each day: challenges set off and offered, one each DaysBetweenOffers
// (20); the one under way prepared for, then run: a count won when it's
// reached, a kept measure failed when it slips; at its end, the count
// failed, the measure won. A warning as time runs short.
void Park::challengeDay() {
    const int today = int(_lastDay);
    for (ChallengeState &state : _challenges) {
        const Challenge &c = state.challenge;
        if (state.status != ChallengeStatus::kNotYet || _progress < c.missionNeeded)
            continue;
        if (state.triggered < 0 && challengeTriggered(c))
            state.triggered = today;
        if (state.triggered >= 0 && today >= state.triggered + c.daysDelay &&
            today - _lastOffer >= _daysBetweenOffers) {
            state.status = ChallengeStatus::kAvailable;
            _lastOffer = today;
            postChallenge(c, kOffered);
        }
    }
    for (size_t i = 0; i < _challenges.size(); ++i) {
        ChallengeState &state = _challenges[i];
        const Challenge &c = state.challenge;
        if (state.status == ChallengeStatus::kPreparing) {
            if (--state.daysLeft > 0)
                continue;
            state.status = ChallengeStatus::kActive;
            state.daysLeft = c.time;
            startCounting(i);
            postChallenge(c, kUnderWay);
            continue;
        }
        if (state.status != ChallengeStatus::kActive)
            continue;
        if (const int outcome = challengeOutcome(i, false)) {
            endChallenge(state, outcome > 0);
            continue;
        }
        if (--state.daysLeft <= 0) {
            endChallenge(state, challengeOutcome(i, true) > 0);
            continue;
        }
        if (!state.warned && state.daysLeft * 100 <= c.time * _warningPercent) {
            state.warned = true;
            postChallenge(c, kTimeShort);
        }
    }
}

// Won: its prizes (cash, golden tickets, shares, a rise in the share
// price, a promotion). Failed: the share price falls, it may be offered
// again, and too many failed and the player's fired.
void Park::endChallenge(ChallengeState &state, bool won) {
    const Challenge &c = state.challenge;
    if (c.type == kHotDogScare)
        for (PlacedObject &o : _objects)
            if (const Attractions::Attraction *a = _attractions.find(o.id); a && a->shopType == int(kHotDogShop))
                o.open = true;
    postChallenge(c, won ? kWon : kFailed);
    if (won) {
        state.status = ChallengeStatus::kComplete;
        _challengesWon.push_back(c.number);
        _cash += c.cash;
        _goldenTickets += c.tickets;
        _shares.owned += c.shares;
        _shares.price = std::max(1, int(std::lround(double(_shares.price) * (100 + c.priceWon) / 100)));
        if (c.promotion > _rank) {
            _rank = c.promotion;
            Message m;
            m.title = kPromotionTitle;
            m.text = kPromotionLetter;
            m.rank = _rank;
            _messages.push_back(std::move(m));
        }
        return;
    }
    state.status = ChallengeStatus::kNotYet;
    state.triggered = -1;
    ++state.failures;
    ++_challengesFailed;
    _shares.price = std::max(1, int(std::lround(double(_shares.price) * (100 - c.priceLost) / 100)));
    if (failuresLeft() == 0 && !_fired) {
        _fired = true;
        post(kGameOverTitle, kFiredLetter);
    }
}

} // namespace reluva::tpw
