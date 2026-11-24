// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park's staff: who the agencies offer, hiring them, their wages, and
// their work about the paths: docs/engines/tpw/games/simcoaster/systems/staff.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::tpw {

namespace {

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
// A wage is the agency's InitialWageModifier, raised by experience: half
// again at the most, which fits the recording's § 11 and § 24 (guessed).
constexpr float kWageForExperience = 0.5f;
// Experience in grades of this much: 0 to 4 (guessed).
constexpr int kExperiencePerGrade = 20000;
// An entertainer's cheer a day, of HappinessEffectOnCell (guessed).
constexpr float kCheerPerDay = 0.25f;

constexpr std::array<Park::Cell, 4> kSteps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

uint32_t mix(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    return n ^ (n >> 15);
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
    }
    _agencies.clear();
    for (int i = 0;; ++i) {
        const std::string base = "StaffAgencies[" + std::to_string(i) + "].";
        if (!general.find(base + "InitialWageModifier"))
            break;
        _agencies.push_back({int(general.number(base + "RequiredPromotionLevel")),
                             int(general.number(base + "InitialWageModifier", 10)),
                             int(general.number(base + "InitialExperienceModifier", 10))});
    }
    if (_agencies.empty())
        _agencies.push_back({});
    _minExperience = int(general.number(pool + "MinExperience", 1000));
    _experienceSpread = int(general.number(pool + "AvgWageDifference", 15000));
    _fastest = float(general.number("AllStaffConstants.MaxSpeedDifference", 150)) / 100;
    _poolDays = uint32_t(std::max(1.0, general.number(pool + "TimeBetweenStaffUpdates", 90)));
    for (size_t g = 0; g < _entertaining.size(); ++g)
        _entertaining[g] = int(general.number(
            "EntertainerConstsPerGrade[" + std::to_string(g) + "].HappinessEffectOnCell", _entertaining[g]));
}

// The agencies' candidates: at the start BeginningNumberOf each kind, then
// topped up to it now and then, never past the kind's Max. Each from an
// agency open to the park, their experience about the kind's average
// (give or take AvgWageDifference, not below MinExperience) raised by the
// agency's InitialExperienceModifier percent, their wage the agency's
// InitialWageModifier raised by their experience (guessed: it fits the
// recording's window at 11:06).
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
            c.wage = int(std::lround(float(agency.wage) *
                                     (1 + kWageForExperience * float(c.experience) / float(kMostExperience))));
            pool.push_back(c);
        }
    }
}

Park::Hiring Park::hire(StaffKind kind, size_t candidate) {
    std::vector<Candidate> &pool = _pool[size_t(kind)];
    if (candidate >= pool.size())
        return Hiring::kNoSuchCandidate;
    if (kind == StaffKind::kScientist && std::none_of(_objects.begin(), _objects.end(), [&](const PlacedObject &o) {
            const Attractions::Attraction *a = _attractions.find(o.id);
            return a && a->lab;
        }))
        return Hiring::kNeedsLab;
    StaffMember m;
    static_cast<Candidate &>(m) = pool[candidate];
    pool.erase(pool.begin() + long(candidate));
    m.id = _nextStaffId++;
    m.kind = kind;
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

// Whether a guard is near enough to see: within their kind's
// MinDetectionRange to MaxDetectionRange, by experience.
bool Park::guarded(float x, float z) const {
    const StaffRules &r = _staffRules[size_t(StaffKind::kGuard)];
    for (const StaffMember &m : _staff)
        if (m.kind == StaffKind::kGuard &&
            std::hypot(m.x - x, m.z - z) <= (r.minRange + (r.maxRange - r.minRange) * skill(m)) * kCellUnits)
            return true;
    return false;
}

// At a path cell's middle. A janitor with litter within their range (their
// kind's MinDetectionRange to MaxDetectionRange by experience) makes for
// the nearest they can reach, and sweeps it up there, for their kind's
// MinWorkDuration to MaxWorkDuration; the rest walk on by chance, as
// visitors do.
void Park::staffWay(StaffMember &m) {
    const StaffRules &r = _staffRules[size_t(m.kind)];
    if (m.kind == StaffKind::kJanitor) {
        const float range = (r.minRange + (r.maxRange - r.minRange) * skill(m)) * kCellUnits;
        const auto here = [&](const Litter &l) {
            return int(std::floor(l.x / kCellUnits)) == m.cell.x && int(std::floor(l.z / kCellUnits)) == m.cell.y;
        };
        if (std::any_of(_litter.begin(), _litter.end(), here)) {
            m.working = true;
            m.headingX = m.headingZ = 0;
            const float tenths = r.minWork + (r.maxWork - r.minWork) * skill(m);
            m.workUntil = _millis + uint64_t(std::max(1.0f, tenths) * 100);
            m.route.clear();
            return;
        }
        if (m.route.empty()) {
            float best = range + 1;
            for (const Litter &l : _litter) {
                const float d = std::hypot(l.x - m.x, l.z - m.z);
                const Cell at{int(std::floor(l.x / kCellUnits)), int(std::floor(l.z / kCellUnits))};
                if (d < best && isPath(at)) {
                    std::vector<Cell> route = routeTo(m.cell, at);
                    if (!route.empty()) {
                        best = d;
                        m.route = std::move(route);
                    }
                }
            }
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
        if (isPath(next) && !(next == m.from))
            ways[count++] = next;
    }
    if (count == 0 && isPath(m.from) && !(m.from == m.cell))
        ways[count++] = m.from;
    if (count == 0)
        return;
    m.from = m.cell;
    m.cell = ways[mix(uint32_t(m.id) * 7919u + uint32_t(m.walked)) % count];
}

// Faster with experience, up to MaxSpeedDifference percent of a child's pace.
void Park::moveStaff(StaffMember &m, float left) {
    left *= 1 + (_fastest - 1) * skill(m);
    for (int pass = 0; pass < 16 && left > 0; ++pass) {
        if (m.working) {
            if (_millis < m.workUntil)
                return;
            m.working = false;
            _litter.erase(std::remove_if(_litter.begin(), _litter.end(),
                                         [&](const Litter &l) {
                                             return int(std::floor(l.x / kCellUnits)) == m.cell.x &&
                                                    int(std::floor(l.z / kCellUnits)) == m.cell.y;
                                         }),
                          _litter.end());
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

// Each day: an entertainer cheers those near (within their kind's range
// by experience) by their grade's HappinessEffectOnCell, a quarter of it a
// day (guessed); each month's first day the wages are paid; now and then
// the agencies offer more.
void Park::staffDay() {
    for (const StaffMember &m : _staff) {
        if (m.kind != StaffKind::kEntertainer)
            continue;
        const StaffRules &r = _staffRules[size_t(m.kind)];
        const float range = (r.minRange + (r.maxRange - r.minRange) * skill(m)) * kCellUnits;
        const size_t grade = size_t(std::clamp(m.experience / kExperiencePerGrade, 0, 4));
        for (Visitor &v : _people)
            if (v.inPark && v.doing != Doing::kRiding && std::hypot(v.x - m.x, v.z - m.z) <= range)
                v.happiness = std::min(100.0f, v.happiness + float(_entertaining[grade]) * kCheerPerDay);
    }
    if (dateOf(_lastDay).day == 1)
        _cash -= wages();
    if (_lastDay % _poolDays == 0)
        fillPool(false);
}

} // namespace reluva::tpw
