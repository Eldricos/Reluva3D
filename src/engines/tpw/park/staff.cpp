// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park's staff: who the agencies offer, hiring them, their wages, and
// their work about the paths: docs/engines/tpw/games/simcoaster/systems/staff.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace reluva::tpw {

namespace {

// How much faster a ride past a red line wears (guessed).
constexpr float kRedLineWear = 2;

// Each kind's names in the settings: StaffPoolInfo's plurals, and the
// singular its own constants go by.
constexpr std::array<const char *, Park::kStaffKinds> kPlurals{"Mechanics", "Handymen",     "Gardeners",
                                                               "Guards",    "Entertainers", "Researchers"};
constexpr std::array<const char *, Park::kStaffKinds> kSingulars{"Mechanic", "Handyman",    "Gardener",
                                                                 "Guard",    "Entertainer", "Researcher"};
// The names tables hold 35 names a kind.
constexpr int kNamesPerKind = 35;
// The agencies offering at the start: those of promotion level 1 and below,
// as the recording's window lists Cut Price Recruitment and Staff U Like
// (guessed).
constexpr int kPromotionLevel = 1;
// A wage is what their experience expects (their kind's MaxWage at the
// most) by the agency's InitialWageModifier plus this, percent: it fits all
// the recording's, the gardeners' § 11 and § 24 and the guards' § 48 and
// § 72 by their experience bars (guessed).
constexpr int kWageBase = 50;
// Experience in grades of this much: 0 to 4 (guessed).
constexpr int kExperiencePerGrade = 20000;
// An entertainer's cheer a day, of HappinessEffectOnCell (guessed).
constexpr float kCheerPerDay = 0.25f;
// Energy a day about the paths, and a job's worth: sweeping, mending,
// watering (guessed).
constexpr float kTiringPerDay = 0.25f;
constexpr float kJobEnergy = 2;
constexpr float kMendingEnergy = 5;
// Recuperation rates are taken as tenths of a day's rest: so much energy
// a day at the rate (guessed).
constexpr float kRestPerRate = 10;
// Tired staff lose happiness, so much a day.
constexpr float kTiredUnhappiness = 1;
// How long training takes, in days (guessed).
constexpr float kTrainingDays = 30;
// The staff room is the one object without a mark of its own: its number.
constexpr int kStaffRoom = 1400;
// A plant dries a little each day: from full to dry over this many (guessed).
constexpr float kDryingDays = 60;
// A gardener sees a plant wanting water below this.
constexpr float kThirstyPlant = 50;
// What one rider wears a ride: its WearRate (out of 10) by this, percent (guessed).
constexpr float kWearPerRider = 0.2f;
// A guard chasing runs a third faster than they walk, and has caught a
// prankster this near, in units; they chase for their kind's
// MinWorkDuration to MaxWorkDuration, "how long the guard pursues the
// thug", taken as seconds (guessed).
constexpr float kChaseSpeed = 1.33f;
constexpr float kCaughtUnits = 1.5f;

constexpr std::array<Park::Cell, 4> kSteps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

uint32_t mix(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    return n ^ (n >> 15);
}

float range(float from, float to, float along) {
    return from + (to - from) * along;
}

// How far along from the least experienced to the most, 0 to 1.
float skill(const Park::StaffMember &m) {
    return std::clamp(float(m.experience) / float(Park::kMostExperience), 0.0f, 1.0f);
}

} // namespace

void Park::loadStaffRules(const SamFile &general) {
    const std::string pool = "StaffPoolInfo.";
    for (size_t k = 0; k < kStaffKinds; ++k) {
        StaffRules &r = _staffRules[k];
        r.averageExperience = int(general.number(pool + "AvgExperienceOf" + kPlurals[k], r.averageExperience));
        r.beginning = int(general.number(pool + "BeginningNumberOf" + kPlurals[k], r.beginning));
        r.most = std::max(r.beginning, int(general.number(pool + "Max" + kPlurals[k], r.most)));
        const std::string own = std::string(kSingulars[k]) + ".";
        const bool entertainer = StaffKind(k) == StaffKind::kEntertainer;
        r.minRange = float(general.number(own + (entertainer ? "MinActiveRange" : "MinDetectionRange"), r.minRange));
        r.maxRange = float(general.number(own + (entertainer ? "MaxActiveRange" : "MaxDetectionRange"), r.maxRange));
        r.minWork = float(general.number(own + "MinWorkDuration", r.minWork));
        r.maxWork = float(general.number(own + "MaxWorkDuration", r.maxWork));
        r.maxWage = int(general.number(own + "MaxWage", r.maxWage));
    }
    _agencies.clear();
    for (int i = 0;; ++i) {
        const std::string base = "StaffAgencies[" + std::to_string(i) + "].";
        if (!general.find(base + "InitialWageModifier"))
            break;
        _agencies.push_back({int(general.number(base + "RequiredPromotionLevel")),
                             int(general.number(base + "InitialWageModifier", 10)),
                             int(general.number(base + "InitialExperienceModifier", 10)),
                             int(general.number(base + "RecuperationModifier", 10)),
                             int(general.number(base + "HappinessRecuperationModifier", 10)),
                             int(general.number(base + "TrainingExperienceGainModifier", 10)),
                             int(general.number(base + "ExperienceGainModifier", 10))});
    }
    if (_agencies.empty())
        _agencies.push_back({});
    _minExperience = int(general.number(pool + "MinExperience", 1000));
    _experienceSpread = int(general.number(pool + "AvgWageDifference", 15000));
    _fastest = float(general.number("AllStaffConstants.MaxSpeedDifference", 150)) / 100;
    _poolDays = uint32_t(std::max(1.0, general.number(pool + "TimeBetweenStaffUpdates", 90)));
    const std::string all = "AllStaffConstants.";
    StaffConstants &c = _constants;
    auto number = [&](const std::string &name, double fallback) { return general.number(name, fallback); };
    c.minWage = int(number(all + "MinWage", c.minWage));
    c.maxWage = int(number(all + "MaxWage", c.maxWage));
    c.badWagePercent = int(number(all + "PercentageForBadWageDecision", c.badWagePercent));
    c.minExperienceForTraining = int(number(all + "MinExperienceForTraining", c.minExperienceForTraining));
    c.happyWage = int(number(all + "HappyWageVal", c.happyWage));
    c.unhappyWage = int(number(all + "UnHappyWageVal", c.unhappyWage));
    c.lowestWageHappiness = int(number(all + "LowestWageHappiness", c.lowestWageHappiness));
    c.slackingEnergy = int(number(all + "StaffSlackingEnergy", c.slackingEnergy));
    c.slackingCells = int(number(all + "MinDistanceForSlacking", c.slackingCells));
    c.trainingExperience = int(number(all + "TrainingExtraExperience", c.trainingExperience));
    c.minRecuperation = float(number(all + "MinRecuperationRate", c.minRecuperation));
    c.maxRecuperation = float(number(all + "MaxRecuperationRate", c.maxRecuperation));
    c.minHappinessRecuperation = float(number(all + "MinHappinessRecuperationRate", c.minHappinessRecuperation));
    c.maxHappinessRecuperation = float(number(all + "MaxHappinessRecuperationRate", c.maxHappinessRecuperation));
    c.strikeMonths = int(number("Strike.MinMonthsForStrike", c.strikeMonths));
    c.strikeStaff = int(number("Strike.MinStaffOfTypeForStrike", c.strikeStaff));
    c.strikeEnergy = int(number("Strike.EnergyPercentToStrike", c.strikeEnergy));
    c.strikeHappiness = int(number("Strike.HappyPercentToStrike", c.strikeHappiness));
    c.strikeMotivation = int(number("Strike.MotivationPercentToStrike", c.strikeMotivation));
    // Experience gained a month at work, by grade: each kind's own
    // constants, the gardeners' by the handymen's (they have none).
    static constexpr std::array<const char *, kStaffKinds> kGrades{
        "MechanicConstsPerGrade", "HandymanConstsPerGrade",    "HandymanConstsPerGrade",
        "GuardConstsPerGrade",    "EntertainerConstsPerGrade", "ResearcherConstsPerGrade"};
    for (size_t k = 0; k < kStaffKinds; ++k)
        for (size_t g = 0; g < _experiencePerMonth[k].size(); ++g)
            _experiencePerMonth[k][g] =
                int(number(std::string(kGrades[k]) + "[" + std::to_string(g) + "].ExperiencePerMonth", 0));
    _staffRoomCapacity = int(number(all + "StaffRoomCapacity", _staffRoomCapacity));
    _workRate = int(number("Research.StartingWorkLoad", _workRate));
    _researchSpeed = float(number("Research.SpeedScaleDown", _researchSpeed));
    _researchCost = float(number("Research.CostScaleUp", _researchCost));
    _minResearchAbility = float(number("Researcher.MinResearchAbility", _minResearchAbility));
    _maxResearchAbility = float(number("Researcher.MaxResearchAbility", _maxResearchAbility));
    for (size_t g = 0; g < _entertaining.size(); ++g)
        _entertaining[g] = int(general.number(
            "EntertainerConstsPerGrade[" + std::to_string(g) + "].HappinessEffectOnCell", _entertaining[g]));
}

// The agencies' candidates: at the start BeginningNumberOf each kind, then
// topped up to it now and then, never past the kind's Max. Each from an
// agency open to the park, their experience about the kind's average
// (give or take AvgWageDifference, not below MinExperience) raised by the
// agency's InitialExperienceModifier percent, their wage short of what
// their experience expects, by the agency (kWageBase).
void Park::fillPool(bool start) {
    std::vector<int> open;
    for (size_t a = 0; a < _agencies.size(); ++a)
        if (_agencies[a].level <= kPromotionLevel)
            open.push_back(int(a));
    if (open.empty())
        open.push_back(0);
    for (size_t k = 0; k < kStaffKinds; ++k) {
        std::vector<Candidate> &pool = _pool[k];
        if (start)
            pool.clear();
        const StaffRules &r = _staffRules[k];
        while (int(pool.size()) < std::min(r.beginning, r.most)) {
            const uint32_t h = mix(++_candidatesMade * 2654435761u);
            Candidate c;
            // A name not taken by another of the kind.
            for (int tries = 0; tries < kNamesPerKind; ++tries) {
                c.name = int((h + uint32_t(tries)) % kNamesPerKind);
                const auto taken = [&](const Candidate &o) { return o.name == c.name; };
                const bool hired = std::any_of(_staff.begin(), _staff.end(), [&](const StaffMember &m) {
                    return m.kind == StaffKind(k) && m.name == c.name;
                });
                if (!hired && std::none_of(pool.begin(), pool.end(), taken))
                    break;
            }
            c.agency = open[(h >> 8) % open.size()];
            const Agency &agency = _agencies[size_t(c.agency)];
            const int spread = std::max(0, _experienceSpread);
            const int base =
                std::max(_minExperience, r.averageExperience - spread + int((h >> 12) % uint32_t(2 * spread + 1)));
            c.experience = std::min(kMostExperience, base * (100 + agency.experience) / 100);
            const float expected = float(r.maxWage) * float(c.experience) / float(kMostExperience);
            c.wage = std::max(_constants.minWage, int(std::lround(expected * float(agency.wage + kWageBase) / 100)));
            pool.push_back(c);
        }
    }
}

Park::Hiring Park::hire(StaffKind kind, size_t candidate) {
    std::vector<Candidate> &pool = _pool[size_t(kind)];
    if (candidate >= pool.size())
        return Hiring::kNoSuchCandidate;
    if (striking(kind))
        return Hiring::kStrike;
    if (kind == StaffKind::kScientist && findObject([](const Attractions::Attraction &a) { return a.lab; }) < 0)
        return Hiring::kNeedsLab;
    StaffMember m;
    static_cast<Candidate &>(m) = pool[candidate];
    pool.erase(pool.begin() + long(candidate));
    m.id = _nextStaffId++;
    m.kind = kind;
    m.hiredDay = day();
    // In at the entrance's path, where the visitors' way in ends.
    const gfx::Vec3 in = _wayIn.empty() ? cellCentre({0, 0}) : _wayIn.back();
    m.x = in[0];
    m.z = in[2];
    m.cell = m.from = {int(std::floor(m.x / kCellUnits)), int(std::floor(m.z / kCellUnits))};
    _staff.push_back(std::move(m));
    return Hiring::kHired;
}

int Park::wages() const {
    int total = 0;
    for (const StaffMember &m : _staff)
        total += m.wage;
    return total;
}

const Park::StaffMember *Park::staffMember(int id) const {
    for (const StaffMember &m : _staff)
        if (m.id == id)
            return &m;
    return nullptr;
}

void Park::fire(int id) {
    for (PlacedObject &o : _objects) {
        if (o.engineer == id)
            o.engineer = 0;
        if (o.gardener == id)
            o.gardener = 0;
    }
    _demands.erase(std::remove_if(_demands.begin(), _demands.end(), [&](const WageDemand &d) { return d.staff == id; }),
                   _demands.end());
    _staff.erase(std::remove_if(_staff.begin(), _staff.end(), [&](const StaffMember &m) { return m.id == id; }),
                 _staff.end());
}

// A raise cheers them: their motivation and happiness up by its share of
// what they had (guessed).
void Park::setWage(int id, int wage) {
    for (StaffMember &m : _staff) {
        if (m.id != id)
            continue;
        const int was = m.wage;
        m.wage = std::clamp(wage, _constants.minWage, _constants.maxWage);
        if (m.wage > was) {
            const float lift = 100 * float(m.wage - was) / float(std::max(1, was));
            m.motivation = std::min(100.0f, m.motivation + lift);
            m.happiness = std::min(100.0f, m.happiness + lift / 2);
        }
    }
}

void Park::adjustWages(StaffKind kind, Adjustment how, int amount) {
    for (const StaffMember &m : std::vector<StaffMember>(_staff)) {
        if (m.kind != kind)
            continue;
        const int wage = how == Adjustment::kSetTo      ? amount
                         : how == Adjustment::kChangeBy ? m.wage + amount
                                                        : int(std::lround(float(m.wage) * float(100 + amount) / 100));
        setWage(m.id, wage);
    }
}

std::vector<int> Park::learnable(StaffKind kind) const {
    std::vector<int> out;
    for (int ability = 1; ability < kAbilities; ++ability)
        if (abilityKind(ability) == kind && abilityDiscovered(ability))
            out.push_back(ability);
    return out;
}

Park::Training Park::trainingCheck(int id) const {
    const StaffMember *m = staffMember(id);
    if (!m)
        return Training::kNotNow;
    if (m->experience < _constants.minExperienceForTraining)
        return Training::kTooInexperienced;
    if (findObject([](const Attractions::Attraction &a) { return a.trainingRoom; }) < 0)
        return Training::kNoRoom;
    if (m->doing != StaffDoing::kWorking)
        return Training::kNotNow;
    return Training::kSent;
}

Park::Training Park::sendForTraining(int id, int ability) {
    if (const Training t = trainingCheck(id); t != Training::kSent)
        return t;
    const int room = findObject([](const Attractions::Attraction &a) { return a.trainingRoom; });
    for (StaffMember &m : _staff)
        if (m.id == id) {
            m.trained = 0;
            m.learning = ability;
            m.working = false;
            sendTo(m, room, StaffDoing::kGoingToTrain);
        }
    return Training::kSent;
}

float Park::trainingProgress(const StaffMember &m) const {
    return m.doing == StaffDoing::kTraining ? std::clamp(m.trained / kTrainingDays, 0.0f, 1.0f) : 0;
}

void Park::setPatrol(int id, const Cell &from, const Cell &to) {
    for (StaffMember &m : _staff)
        if (m.id == id) {
            m.patrolling = true;
            m.patrolFrom = {std::min(from.x, to.x), std::min(from.y, to.y)};
            m.patrolTo = {std::max(from.x, to.x), std::max(from.y, to.y)};
        }
}

void Park::clearPatrol(int id) {
    for (StaffMember &m : _staff)
        if (m.id == id)
            m.patrolling = false;
}

Park::Offer Park::offer(int raise) {
    if (_demands.empty())
        return Offer::kRefused;
    const WageDemand d = _demands.front();
    // Within PercentageForBadWageDecision of it, "they'll just about live with it".
    const bool whole = raise >= d.raise;
    if (!whole && raise * 100 < d.raise * (100 - _constants.badWagePercent))
        return Offer::kRefused;
    _demands.erase(_demands.begin());
    for (StaffMember &m : _staff)
        if (m.id == d.staff) {
            m.wage = std::clamp(m.wage + raise, _constants.minWage, _constants.maxWage);
            m.motivation = whole ? 100 : 50;
        }
    return whole ? Offer::kAccepted : Offer::kStays;
}

void Park::refuseDemand() {
    if (!_demands.empty())
        fire(_demands.front().staff);
}

// Whether a guard at work is near enough to see: within their kind's
// MinDetectionRange to MaxDetectionRange, by experience.
bool Park::guarded(float x, float z) const {
    const StaffRules &r = _staffRules[size_t(StaffKind::kGuard)];
    for (const StaffMember &m : _staff)
        if (m.kind == StaffKind::kGuard && m.doing == StaffDoing::kWorking &&
            std::hypot(m.x - x, m.z - z) <= range(r.minRange, r.maxRange, skill(m)) * kCellUnits)
            return true;
    return false;
}

// A prank seen by a guard at work within twice their range: the nearest
// gives chase.
void Park::chase(Visitor &v) {
    const StaffRules &r = _staffRules[size_t(StaffKind::kGuard)];
    StaffMember *best = nullptr;
    float nearest = 0;
    for (StaffMember &m : _staff) {
        if (m.kind != StaffKind::kGuard || m.doing != StaffDoing::kWorking || m.chasing)
            continue;
        const float d = std::hypot(m.x - v.x, m.z - v.z);
        if (d <= 2 * range(r.minRange, r.maxRange, skill(m)) * kCellUnits && (!best || d < nearest)) {
            best = &m;
            nearest = d;
        }
    }
    if (best) {
        best->chasing = v.id;
        best->chaseUntil = _millis + uint64_t(range(r.minWork, r.maxWork, skill(*best)) * 1000);
    }
}

int Park::findObject(bool (*what)(const Attractions::Attraction &)) const {
    for (size_t i = 0; i < _objects.size(); ++i)
        if (const Attractions::Attraction *a = _attractions.find(_objects[i].id); a && what(*a))
            return int(i);
    return -1;
}

// The path cell to work on an object from: its queue's or kiosk's end, or
// the path cell nearest any of its cells.
Park::Cell Park::pathBeside(size_t object) const {
    for (const Queue &q : _queues)
        if (q.object == int(object) && q.atPath)
            return q.pathEnd;
    const PlacedObject &o = _objects[object];
    std::vector<Cell> cells{o.at};
    if (const Attractions::Attraction *a = _attractions.find(o.id))
        cells = footprint(a->shape, o.at, o.angle).cells;
    Cell best = o.at;
    int nearest = INT_MAX;
    for (int y = 0; y < _mapHeight; ++y)
        for (int x = 0; x < _mapWidth; ++x) {
            if (!isPath({x, y}))
                continue;
            for (const Cell &c : cells) {
                const int d = std::abs(x - c.x) + std::abs(y - c.y);
                if (d < nearest) {
                    nearest = d;
                    best = {x, y};
                }
            }
        }
    return best;
}

int Park::nearestFree(StaffKind kind, const Cell &to) const {
    int best = 0;
    int nearest = INT_MAX;
    for (const StaffMember &m : _staff) {
        if (m.kind != kind || m.doing != StaffDoing::kWorking || m.target >= 0 || m.working)
            continue;
        const int d = std::abs(m.cell.x - to.x) + std::abs(m.cell.y - to.y);
        if (d < nearest) {
            nearest = d;
            best = m.id;
        }
    }
    return best;
}

void Park::sendTo(StaffMember &m, int object, StaffDoing doing) {
    m.target = object;
    m.doing = doing;
    m.route = routeTo(m.cell, pathBeside(size_t(object)));
}

void Park::callEngineer(size_t object) {
    if (object >= _objects.size() || _objects[object].engineer)
        return;
    const int id = nearestFree(StaffKind::kEngineer, pathBeside(object));
    for (StaffMember &m : _staff)
        if (m.id == id) {
            sendTo(m, int(object), StaffDoing::kWorking);
            _objects[object].engineer = id;
        }
}

void Park::cancelEngineer(size_t object) {
    if (object >= _objects.size())
        return;
    for (StaffMember &m : _staff)
        if (m.id == _objects[object].engineer && m.target == int(object)) {
            m.target = -1;
            m.route.clear();
            m.working = false;
        }
    _objects[object].engineer = 0;
}

void Park::callGardener(size_t object) {
    if (object >= _objects.size() || _objects[object].gardener)
        return;
    const int id = nearestFree(StaffKind::kGardener, pathBeside(object));
    for (StaffMember &m : _staff)
        if (m.id == id) {
            sendTo(m, int(object), StaffDoing::kWorking);
            _objects[object].gardener = id;
        }
}

// A special ride opens only once it's activated.
void Park::setOpen(size_t object, bool open) {
    if (object < _objects.size())
        _objects[object].open = open && _objects[object].active;
}

int Park::scrapValue(size_t object) const {
    if (object >= _objects.size())
        return 0;
    const Attractions::Attraction *a = _attractions.find(_objects[object].id);
    if (!a)
        return 0;
    const uint32_t years = (day() - _objects[object].placedDay) / 365;
    return a->price * a->scrapPercent[std::min<size_t>(years, a->scrapPercent.size() - 1)] / 100;
}

// One rider's wear on a ride: its WearRate by kWearPerRider, twice that
// past a red line, broken at 0.
void Park::wear(size_t object) {
    PlacedObject &o = _objects[object];
    const Attractions::Attraction::Level *level = levelOf(object);
    if (!level || level->wearRate <= 0)
        return;
    const float times = pastRedLine(object) ? kRedLineWear : 1;
    o.repair = std::max(0.0f, o.repair - float(level->wearRate) * kWearPerRider * times);
    if (o.repair <= 0)
        o.broken = true;
}

// Each day: plants dry; a ride worn below RideBreakDown.Imminent ("call
// engineer automatically when repairstate falls below this", its comment
// says), or broken, has an engineer sent, if one's free.
void Park::objectsDay() {
    for (size_t i = 0; i < _objects.size(); ++i) {
        PlacedObject &o = _objects[i];
        const Attractions::Attraction *a = _attractions.find(o.id);
        if (!a)
            continue;
        if (a->requiresWatering)
            o.water = std::max(0.0f, o.water - 100 / kDryingDays);
        if ((o.broken || (a->wearRate > 0 && o.repair < _constants.breakdownImminent)) && !o.engineer)
            callEngineer(i);
    }
}

// At a path cell's middle. A janitor with litter within their range (their
// kind's MinDetectionRange to MaxDetectionRange by experience) makes for
// the nearest they can reach, and sweeps it up there; a gardener likewise
// a plant wanting water; an engineer goes where they're sent, to mend.
// Each works for their kind's MinWorkDuration to MaxWorkDuration. Staff
// with a patrol area keep to it; the rest walk on by chance, as visitors do.
void Park::staffWay(StaffMember &m) {
    const StaffRules &r = _staffRules[size_t(m.kind)];
    const auto inPatrol = [&](const Cell &c) {
        return !m.patrolling ||
               (c.x >= m.patrolFrom.x && c.x <= m.patrolTo.x && c.y >= m.patrolFrom.y && c.y <= m.patrolTo.y);
    };
    const auto startWork = [&]() {
        m.working = true;
        m.headingX = m.headingZ = 0;
        const float tenths = range(r.minWork, r.maxWork, skill(m));
        m.workUntil = _millis + uint64_t(std::max(1.0f, tenths) * 100);
        m.route.clear();
    };
    if (!m.route.empty()) {
        m.from = m.cell;
        m.cell = m.route.back();
        m.route.pop_back();
        return;
    }
    switch (m.doing) {
    case StaffDoing::kGoingToRest:
        m.doing = StaffDoing::kResting;
        return;
    case StaffDoing::kGoingToTrain:
        m.doing = StaffDoing::kTraining;
        return;
    case StaffDoing::kGoingToResearch:
        m.doing = StaffDoing::kResearching;
        return;
    case StaffDoing::kGoingToClear:
        m.doing = StaffDoing::kClearing;
        m.headingX = m.headingZ = 0;
        return;
    case StaffDoing::kClearing:
        return;
    case StaffDoing::kResting:
    case StaffDoing::kTraining:
    case StaffDoing::kResearching:
    case StaffDoing::kStriking:
        return;
    case StaffDoing::kWorking:
        break;
    }
    // Arrived where they were sent.
    if (m.target >= 0) {
        startWork();
        return;
    }
    const float reach = range(r.minRange, r.maxRange, skill(m)) * kCellUnits;
    if (m.kind == StaffKind::kJanitor) {
        const auto here = [&](const Litter &l) {
            return int(std::floor(l.x / kCellUnits)) == m.cell.x && int(std::floor(l.z / kCellUnits)) == m.cell.y;
        };
        if (std::any_of(_litter.begin(), _litter.end(), here)) {
            startWork();
            return;
        }
        float best = reach + 1;
        for (const Litter &l : _litter) {
            const float d = std::hypot(l.x - m.x, l.z - m.z);
            const Cell at{int(std::floor(l.x / kCellUnits)), int(std::floor(l.z / kCellUnits))};
            if (d < best && isPath(at) && inPatrol(at)) {
                std::vector<Cell> route = routeTo(m.cell, at);
                if (!route.empty()) {
                    best = d;
                    m.route = std::move(route);
                }
            }
        }
    }
    if (m.kind == StaffKind::kGardener) {
        float best = reach + 1;
        int plant = -1;
        for (size_t i = 0; i < _objects.size(); ++i) {
            const PlacedObject &o = _objects[i];
            const Attractions::Attraction *a = _attractions.find(o.id);
            const gfx::Vec3 at = cellCentre(o.at);
            const float d = std::hypot(at[0] - m.x, at[2] - m.z);
            if (a && a->requiresWatering && o.water < kThirstyPlant && !o.gardener && d < best && inPatrol(o.at)) {
                best = d;
                plant = int(i);
            }
        }
        if (plant >= 0) {
            _objects[size_t(plant)].gardener = m.id;
            sendTo(m, plant, StaffDoing::kWorking);
            if (m.route.empty())
                startWork();
            return;
        }
    }
    if (!m.route.empty()) {
        m.from = m.cell;
        m.cell = m.route.back();
        m.route.pop_back();
        return;
    }
    std::array<Cell, 4> ways{};
    size_t count = 0;
    for (const Cell &step : kSteps) {
        const Cell next{m.cell.x + step.x, m.cell.y + step.y};
        if (isPath(next) && !(next == m.from) && (inPatrol(next) || !inPatrol(m.cell)))
            ways[count++] = next;
    }
    if (count == 0 && isPath(m.from) && !(m.from == m.cell))
        ways[count++] = m.from;
    if (count == 0)
        return;
    m.from = m.cell;
    m.cell = ways[mix(uint32_t(m.id) * 7919u + uint32_t(m.walked)) % count];
}

// A job done: litter swept, a ride mended, a plant watered.
void Park::finishWork(StaffMember &m) {
    m.working = false;
    if (m.kind == StaffKind::kJanitor) {
        _litter.erase(std::remove_if(_litter.begin(), _litter.end(),
                                     [&](const Litter &l) {
                                         return int(std::floor(l.x / kCellUnits)) == m.cell.x &&
                                                int(std::floor(l.z / kCellUnits)) == m.cell.y;
                                     }),
                      _litter.end());
        m.energy = std::max(0.0f, m.energy - kJobEnergy);
    }
    if (m.target >= 0 && size_t(m.target) < _objects.size()) {
        PlacedObject &o = _objects[size_t(m.target)];
        if (m.kind == StaffKind::kEngineer && o.engineer == m.id) {
            o.repair = 100;
            o.broken = false;
            o.engineer = 0;
            m.energy = std::max(0.0f, m.energy - kMendingEnergy);
        }
        if (m.kind == StaffKind::kGardener && o.gardener == m.id) {
            o.water = 100;
            o.gardener = 0;
            m.energy = std::max(0.0f, m.energy - kJobEnergy);
        }
    }
    m.target = -1;
}

// Faster with experience, up to MaxSpeedDifference percent of a child's pace.
void Park::moveStaff(StaffMember &m, float left) {
    if (m.doing == StaffDoing::kResting || m.doing == StaffDoing::kTraining || m.doing == StaffDoing::kResearching ||
        m.doing == StaffDoing::kClearing)
        return;
    // A guard after a prankster, straight at them, a third faster, till
    // caught or their WorkDuration's up: caught, they're seen out.
    if (m.chasing) {
        Visitor *v = nullptr;
        for (Visitor &p : _people)
            if (p.id == m.chasing && p.inPark && p.doing != Doing::kGoingHome)
                v = &p;
        if (!v || _millis >= m.chaseUntil) {
            m.chasing = 0;
        } else {
            const float dx = v->x - m.x;
            const float dz = v->z - m.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            const float step = left * kChaseSpeed;
            if (d <= kCaughtUnits + step) {
                v->happiness = 0;
                think(*v, Thought::kAngry);
                if (v->doing == Doing::kQueueing) {
                    std::vector<int> &line = _queues[size_t(v->queue)].line;
                    line.erase(std::remove(line.begin(), line.end(), v->id), line.end());
                    v->queue = -1;
                }
                v->doing = Doing::kWalking;
                v->cell = {int(std::floor(v->x / kCellUnits)), int(std::floor(v->z / kCellUnits))};
                goHome(*v);
                m.chasing = 0;
                m.cell = m.from = {int(std::floor(m.x / kCellUnits)), int(std::floor(m.z / kCellUnits))};
                m.route.clear();
            } else {
                m.headingX = dx / d;
                m.headingZ = dz / d;
                m.x += m.headingX * step;
                m.z += m.headingZ * step;
                m.walked += step;
            }
            return;
        }
    }
    left *= 1 + (_fastest - 1) * skill(m);
    for (int pass = 0; pass < 16 && left > 0; ++pass) {
        if (m.working) {
            if (_millis < m.workUntil)
                return;
            finishWork(m);
        }
        const gfx::Vec3 to = cellCentre(m.cell);
        const float dx = to[0] - m.x;
        const float dz = to[2] - m.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > 0) {
            m.headingX = dx / d;
            m.headingZ = dz / d;
        }
        if (d > left) {
            m.x += dx / d * left;
            m.z += dz / d * left;
            m.walked += left;
            return;
        }
        m.x = to[0];
        m.z = to[2];
        m.walked += d;
        left -= d;
        const Cell was = m.cell;
        staffWay(m);
        if (!m.working && m.cell == was) {
            m.headingX = m.headingZ = 0;
            return;
        }
    }
}

// Each day. An entertainer cheers those near (within their kind's range
// by experience) by their grade's HappinessEffectOnCell, a quarter of it a
// day (guessed). Staff tire about their work; below StaffSlackingEnergy,
// with a staff room within MinDistanceForSlacking cells, they go to rest
// ("then member of staff will go and rest for a bit", the settings say),
// else they grow unhappy. Resting, they recover energy and happiness by
// their Min..Max RecuperationRate and HappinessRecuperationRate, by
// experience and their agency's modifiers (tenths); training, they gain
// TrainingExtraExperience by their agency's TrainingExperienceGainModifier.
// A strike ends when its kind's averages are past Strike's again. Each
// month's first day comes the month's reckoning; now and then the
// agencies offer more.
void Park::staffDay() {
    const int room = findObject([](const Attractions::Attraction &a) { return a.id == kStaffRoom; });
    for (StaffMember &m : _staff) {
        const Agency &agency = _agencies[size_t(std::clamp(m.agency, 0, int(_agencies.size()) - 1))];
        switch (m.doing) {
        case StaffDoing::kResting:
            m.energy += range(_constants.minRecuperation, _constants.maxRecuperation, skill(m)) * kRestPerRate *
                        float(agency.recuperation) / 10;
            m.happiness = std::min(100.0f, m.happiness + range(_constants.minHappinessRecuperation,
                                                               _constants.maxHappinessRecuperation, skill(m)) *
                                                             float(agency.happinessRecuperation) / 10);
            if (m.energy >= 100) {
                m.energy = 100;
                m.doing = StaffDoing::kWorking;
                m.target = -1;
            }
            break;
        case StaffDoing::kTraining:
            m.trained += 1;
            if (m.trained >= kTrainingDays) {
                m.experience =
                    std::min(kMostExperience, m.experience + _constants.trainingExperience * agency.training / 10);
                // They learn the ability chosen for them ("Select Ability To
                // Learn"); without a choice, what the mission's obstacle wants
                // of their kind, else an engineer to upgrade rides (guessed).
                if (m.learning)
                    m.ability = m.learning;
                else if (const int ability = abilityToLearn(m.kind))
                    m.ability = ability;
                else if (m.kind == StaffKind::kEngineer)
                    m.ability = kRideUpgrading;
                m.learning = 0;
                m.doing = StaffDoing::kWorking;
                m.target = -1;
            }
            break;
        case StaffDoing::kWorking:
            m.energy = std::max(0.0f, m.energy - kTiringPerDay);
            if (m.energy < float(_constants.slackingEnergy) && m.target < 0 && !m.working) {
                // Not to a staff room already full (StaffRoomCapacity).
                const int resting = int(std::count_if(_staff.begin(), _staff.end(), [](const StaffMember &s) {
                    return s.doing == StaffDoing::kResting || s.doing == StaffDoing::kGoingToRest;
                }));
                if (room >= 0 && resting < _staffRoomCapacity) {
                    const Cell at = _objects[size_t(room)].at;
                    if (std::abs(at.x - m.cell.x) + std::abs(at.y - m.cell.y) <= _constants.slackingCells)
                        sendTo(m, room, StaffDoing::kGoingToRest);
                }
                if (m.doing == StaffDoing::kWorking)
                    m.happiness = std::max(0.0f, m.happiness - kTiredUnhappiness);
            }
            if (m.kind == StaffKind::kEntertainer) {
                const StaffRules &r = _staffRules[size_t(m.kind)];
                const float reach = range(r.minRange, r.maxRange, skill(m)) * kCellUnits;
                const size_t grade = size_t(std::clamp(m.experience / kExperiencePerGrade, 0, 4));
                for (Visitor &v : _people)
                    if (v.inPark && v.doing != Doing::kRiding && std::hypot(v.x - m.x, v.z - m.z) <= reach)
                        v.happiness = std::min(100.0f, v.happiness + float(_entertaining[grade]) * kCheerPerDay);
            }
            break;
        default:
            break;
        }
    }
    for (size_t k = 0; k < kStaffKinds; ++k)
        if (_strikes[k] && !wantsToStrike(StaffKind(k))) {
            _strikes[k] = false;
            for (StaffMember &m : _staff)
                if (m.kind == StaffKind(k) && m.doing == StaffDoing::kStriking) {
                    m.doing = StaffDoing::kWorking;
                    const gfx::Vec3 in = _wayIn.empty() ? cellCentre({0, 0}) : _wayIn.back();
                    m.cell = m.from = {int(std::floor(in[0] / kCellUnits)), int(std::floor(in[2] / kCellUnits))};
                }
        }
    if (dateOf(_lastDay).day == 1)
        staffMonth();
    if (_lastDay % _poolDays == 0)
        fillPool(false);
}

// Whether a kind would strike, by the Strike settings: at least
// MinStaffOfTypeForStrike of them, their average energy, happiness or
// motivation below its percent.
bool Park::wantsToStrike(StaffKind kind) const {
    int count = 0;
    float energy = 0;
    float happiness = 0;
    float motivation = 0;
    for (const StaffMember &m : _staff)
        if (m.kind == kind) {
            ++count;
            energy += m.energy;
            happiness += m.happiness;
            motivation += m.motivation;
        }
    if (count < _constants.strikeStaff)
        return false;
    const float n = float(count);
    return energy / n < float(_constants.strikeEnergy) || happiness / n < float(_constants.strikeHappiness) ||
           motivation / n < float(_constants.strikeMotivation);
}

// The month's first day: wages paid; each one's wage set against what
// their experience expects (their kind's MaxWage at the most), their
// motivation and happiness up by HappyWageVal or down by UnHappyWageVal;
// below LowestWageHappiness, a demand for the difference. After
// MinMonthsForStrike months, a kind that would strikes, at the world's
// strike area. The attractions' month closes, their running costs
// (Upgrades[0].RunningCost) paid.
void Park::staffMonth() {
    book(Ledger::kWages, wages());
    for (StaffMember &m : _staff) {
        const StaffRules &r = _staffRules[size_t(m.kind)];
        // Experience at work: their grade's ExperiencePerMonth, by their
        // agency's ExperienceGainModifier (tenths); none on strike.
        if (m.doing != StaffDoing::kStriking) {
            const Agency &agency = _agencies[size_t(std::clamp(m.agency, 0, int(_agencies.size()) - 1))];
            const size_t grade = size_t(std::clamp(m.experience / kExperiencePerGrade, 0, 4));
            m.experience = std::min(kMostExperience, m.experience + _experiencePerMonth[size_t(m.kind)][grade] *
                                                                        agency.experienceGain / 10);
        }
        const int expected = std::max(_constants.minWage, int(float(r.maxWage) * skill(m)));
        const float change = float(m.wage >= expected ? _constants.happyWage : -_constants.unhappyWage);
        m.motivation = std::clamp(m.motivation + change, 0.0f, 100.0f);
        m.happiness = std::clamp(m.happiness + change, 0.0f, 100.0f);
        const bool asked =
            std::any_of(_demands.begin(), _demands.end(), [&](const WageDemand &d) { return d.staff == m.id; });
        if (m.motivation < float(_constants.lowestWageHappiness) && !asked)
            _demands.push_back({m.id, std::max(1, expected - m.wage)});
    }
    const Date d = dateOf(_lastDay);
    const int months = (d.year - 2000) * 12 + d.month;
    if (months >= _constants.strikeMonths)
        for (size_t k = 0; k < kStaffKinds; ++k) {
            if (_strikes[k] || !wantsToStrike(StaffKind(k)))
                continue;
            _strikes[k] = true;
            int place = 0;
            for (StaffMember &m : _staff)
                if (m.kind == StaffKind(k)) {
                    m.doing = StaffDoing::kStriking;
                    m.working = false;
                    m.route.clear();
                    if (m.target >= 0 && size_t(m.target) < _objects.size()) {
                        if (_objects[size_t(m.target)].engineer == m.id)
                            _objects[size_t(m.target)].engineer = 0;
                        if (_objects[size_t(m.target)].gardener == m.id)
                            _objects[size_t(m.target)].gardener = 0;
                    }
                    m.target = -1;
                    const Cell &from = _constants.strikeFrom;
                    const Cell &size = _constants.strikeSize;
                    const int w = std::max(1, size.x);
                    m.cell = {from.x + place % w, from.y + (place / w) % std::max(1, size.y)};
                    ++place;
                }
        }
    for (size_t i = 0; i < _objects.size(); ++i) {
        PlacedObject &o = _objects[i];
        if (const Attractions::Attraction::Level *level = levelOf(i)) {
            book(Ledger::kRunning, level->runningCost);
            o.costs += level->runningCost;
        }
        o.profit += o.takings - o.costs - o.goods;
        o.lastUsers = std::exchange(o.users, 0);
        o.lastTakings = std::exchange(o.takings, 0);
        o.lastCosts = std::exchange(o.costs, 0);
        o.lastGoods = std::exchange(o.goods, 0);
    }
}

} // namespace reluva::tpw
