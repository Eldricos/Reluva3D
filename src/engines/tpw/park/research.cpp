// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Research, upgrades, and attractions deleted and renamed:
// docs/engines/tpw/games/simcoaster/systems/research.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace reluva::tpw {

namespace {

// An upgrade takes its DurationOfUpgrade (2 to 4) by this many days (guessed).
constexpr float kUpgradeDays = 10;
// A scientist's research ability (Researcher.Min..MaxResearchAbility, by
// experience) in points a day at the full work rate: one of ability 2
// does the recording's § 100 project in about 30 days at 85 percent
// ("Days to go: about 30"), with SpeedScaleDown's 0.005 (guessed).
constexpr float kPointsPerAbility = (100.0f / 30) / (2 * 0.005f * 85);
// The game's numbering of staff kinds in RStaff[0].SType, by the
// recording's list of projects and their consultants: 1 a gardener, 3 an
// engineer, 5 a guard; the others guessed in the order of its strings.
constexpr std::array<std::pair<int, Park::StaffKind>, 6> kConsultants{{{1, Park::StaffKind::kGardener},
                                                                       {2, Park::StaffKind::kJanitor},
                                                                       {3, Park::StaffKind::kEngineer},
                                                                       {4, Park::StaffKind::kEntertainer},
                                                                       {5, Park::StaffKind::kGuard},
                                                                       {6, Park::StaffKind::kScientist}}};

} // namespace

const Attractions::Attraction::Level *Park::levelOf(size_t object, int level) const {
    if (object >= _objects.size())
        return nullptr;
    const Attractions::Attraction *a = _attractions.find(_objects[object].id);
    const int l = level < 0 ? _objects[object].level : level;
    return a && l < int(a->levels.size()) ? &a->levels[size_t(l)] : nullptr;
}

Park::Upgrading Park::upgrade(size_t object) {
    if (object >= _objects.size())
        return Upgrading::kFullyUpgraded;
    PlacedObject &o = _objects[object];
    if (o.upgrading > 0)
        return Upgrading::kBusy;
    const Attractions::Attraction::Level *next = levelOf(object, o.level + 1);
    if (!next || next->cost <= 0)
        return Upgrading::kFullyUpgraded;
    if (next->research > 0 && !researched(o.id, o.level + 1))
        return Upgrading::kNotResearched;
    // "You need to have hired some engineers and trained them in Ride
    // Upgrading before you can purchase an upgrade" (UITEXT 11).
    if (std::none_of(_staff.begin(), _staff.end(), [](const StaffMember &m) {
            return m.kind == StaffKind::kEngineer && m.ability == kRideUpgrading;
        }))
        return Upgrading::kNeedsEngineer;
    if (_cash < next->cost)
        return Upgrading::kNoMoney;
    book(Ledger::kBuilding, next->cost);
    o.costs += next->cost;
    o.upgrading = std::max(1.0f, float(next->days) * kUpgradeDays);
    return Upgrading::kStarted;
}

void Park::cancelUpgrade(size_t object) {
    if (object >= _objects.size() || _objects[object].upgrading <= 0)
        return;
    PlacedObject &o = _objects[object];
    if (const Attractions::Attraction::Level *next = levelOf(object, o.level + 1)) {
        book(Ledger::kBuilding, -next->cost);
        o.costs -= next->cost;
    }
    o.upgrading = 0;
}

// Its scrap value back; its cells bare; its queue's cells bare, those in
// it back to the path, its riders off at its entrance; everything that
// counts the objects or queues by their places moved down past it.
void Park::remove(size_t object) {
    if (object >= _objects.size())
        return;
    book(Ledger::kBuilding, -scrapValue(object));
    const PlacedObject gone = _objects[object];
    if (const Attractions::Attraction *a = _attractions.find(gone.id))
        for (const Cell &c : footprint(a->shape, gone.at, gone.angle).cells)
            if (ground(c) == Ground::kTaken)
                _ground[size_t(c.y) * size_t(_mapWidth) + size_t(c.x)] = Ground::kOpen;
    const int queue = queueOf(object);
    if (queue >= 0) {
        const Queue &q = _queues[size_t(queue)];
        for (const Cell &c : q.cells)
            if (ground(c) == Ground::kQueue)
                _ground[size_t(c.y) * size_t(_mapWidth) + size_t(c.x)] = Ground::kOpen;
        for (Visitor &v : _people) {
            if (v.queue == queue && (v.doing == Doing::kQueueing || v.doing == Doing::kRiding)) {
                const gfx::Vec3 at = cellCentre(v.doing == Doing::kRiding ? q.entrance : v.cell);
                v.x = at[0];
                v.z = at[2];
                v.doing = Doing::kLeaving;
                v.cell = v.from = q.atPath ? q.pathEnd : v.cell;
                v.queue = -1;
            }
            for (int *index : {&v.queue, &v.target, &v.lastQueue})
                if (*index == queue)
                    *index = -1;
                else if (*index > queue)
                    --*index;
        }
        _queues.erase(_queues.begin() + queue);
    }
    _objects.erase(_objects.begin() + long(object));
    for (Queue &q : _queues)
        if (q.object == int(object))
            q.object = -1;
        else if (q.object > int(object))
            --q.object;
    for (StaffMember &m : _staff)
        if (m.target == int(object)) {
            m.target = -1;
            m.route.clear();
            m.working = false;
            if (m.doing != StaffDoing::kStriking)
                m.doing = StaffDoing::kWorking;
        } else if (m.target > int(object)) {
            --m.target;
        }
}

void Park::rename(size_t object, std::string name) {
    if (object < _objects.size())
        _objects[object].name = std::move(name);
}

std::optional<Park::StaffKind> Park::consultantKind(int stype) {
    for (const auto &[n, kind] : kConsultants)
        if (n == stype)
            return kind;
    return std::nullopt;
}

bool Park::hasResearchLab() const {
    return findObject([](const Attractions::Attraction &a) { return a.lab; }) >= 0;
}

bool Park::researched(int id, int level) const {
    return std::find(_researched.begin(), _researched.end(), std::pair{id, level}) != _researched.end();
}

// What's not on sale but would be by research (Research.Group past 0,
// a price), and the upgrades of what's on sale that cost research; not
// those being researched.
std::vector<Park::Project> Park::projectChoices() const {
    std::vector<Project> out;
    const auto underway = [&](int id, int level) {
        return std::any_of(_projects.begin(), _projects.end(), [&](const std::optional<Project> &p) {
            return p && !p->finished && p->id == id && p->level == level;
        });
    };
    for (const Attractions::Attraction &a : _attractions.all()) {
        if (!a.available && a.researchGroup > 0 && a.price > 0 && !researched(a.id) && !underway(a.id, 0))
            out.push_back({ProjectKind::kAttraction,
                           a.id,
                           0,
                           int(std::lround(float(a.levels[0].research) * _researchCost)),
                           0,
                           {},
                           false});
        if (!a.available)
            continue;
        for (int l = 1; l < int(a.levels.size()); ++l) {
            const Attractions::Attraction::Level &level = a.levels[size_t(l)];
            if (level.cost > 0 && level.research > 0 && !researched(a.id, l) && !underway(a.id, l))
                out.push_back({ProjectKind::kUpgrade,
                               a.id,
                               l,
                               int(std::lround(float(level.research) * _researchCost)),
                               0,
                               {},
                               false});
        }
    }
    return out;
}

void Park::startProject(size_t slot, const Project &project) {
    if (slot >= _projects.size())
        return;
    cancelProject(slot);
    _projects[slot] = project;
    _projects[slot]->done = 0;
    _projects[slot]->finished = false;
    _projects[slot]->scientists.clear();
}

void Park::cancelProject(size_t slot) {
    if (slot >= _projects.size() || !_projects[slot])
        return;
    for (int id : std::vector<int>(_projects[slot]->scientists))
        unassign(slot, id);
    _projects[slot].reset();
}

// A scientist on one project at a time, off to the lab while on it.
void Park::assign(size_t slot, int scientist) {
    if (slot >= _projects.size() || !_projects[slot])
        return;
    const int lab = findObject([](const Attractions::Attraction &a) { return a.lab; });
    for (size_t other = 0; other < _projects.size(); ++other)
        if (other != slot)
            unassign(other, scientist);
    for (StaffMember &m : _staff) {
        if (m.id != scientist || m.kind != StaffKind::kScientist)
            continue;
        std::vector<int> &on = _projects[slot]->scientists;
        if (std::find(on.begin(), on.end(), scientist) == on.end())
            on.push_back(scientist);
        if (lab >= 0 && m.doing == StaffDoing::kWorking) {
            m.working = false;
            sendTo(m, lab, StaffDoing::kGoingToResearch);
        }
    }
}

void Park::unassign(size_t slot, int scientist) {
    if (slot >= _projects.size() || !_projects[slot])
        return;
    std::vector<int> &on = _projects[slot]->scientists;
    on.erase(std::remove(on.begin(), on.end(), scientist), on.end());
    for (StaffMember &m : _staff)
        if (m.id == scientist && (m.doing == StaffDoing::kResearching || m.doing == StaffDoing::kGoingToResearch)) {
            m.doing = StaffDoing::kWorking;
            m.target = -1;
            m.route.clear();
        }
}

// Why a project can't go on (UITEXT 300 to 304): its scientists on
// strike, none assigned or none in the park, its consultant's kind on
// strike or none employed.
uint32_t Park::projectHeldUp(size_t slot) const {
    if (slot >= _projects.size() || !_projects[slot] || _projects[slot]->finished)
        return 0;
    const Project &p = *_projects[slot];
    if (striking(StaffKind::kScientist))
        return kHeldUpByStrike;
    if (std::none_of(_staff.begin(), _staff.end(),
                     [](const StaffMember &m) { return m.kind == StaffKind::kScientist; }))
        return kHeldUpByNoScientists;
    if (p.scientists.empty())
        return kHeldUpByNoneAssigned;
    if (const Attractions::Attraction *a = _attractions.find(p.id))
        if (const std::optional<StaffKind> consultant = consultantKind(a->consultant)) {
            if (striking(*consultant))
                return kHeldUpByConsultantStrike;
            if (std::none_of(_staff.begin(), _staff.end(), [&](const StaffMember &m) { return m.kind == *consultant; }))
                return kHeldUpByNoConsultant;
        }
    return 0;
}

// Each assigned scientist in the lab adds their research ability,
// Researcher.Min..MaxResearchAbility by experience, at the work rate.
float Park::researchPerDay(size_t slot) const {
    if (slot >= _projects.size() || !_projects[slot] || projectHeldUp(slot))
        return 0;
    float points = 0;
    for (int id : _projects[slot]->scientists)
        if (const StaffMember *m = staffMember(id); m && m->doing == StaffDoing::kResearching) {
            const float skill = std::clamp(float(m->experience) / float(kMostExperience), 0.0f, 1.0f);
            const float ability = _minResearchAbility + (_maxResearchAbility - _minResearchAbility) * skill;
            points += ability * kPointsPerAbility * _researchSpeed * float(_workRate);
        }
    return points;
}

// Each day: the projects go on, their points paid for a dollar each, and
// one done puts its attraction on sale, or its upgrade within reach; the
// upgrades being made go on.
void Park::researchDay() {
    for (size_t slot = 0; slot < _projects.size(); ++slot) {
        std::optional<Project> &p = _projects[slot];
        if (!p || p->finished)
            continue;
        const float points = std::min(researchPerDay(slot), float(p->cost) - p->done);
        p->done += points;
        book(Ledger::kResearch, int(std::lround(points)));
        if (p->done >= float(p->cost)) {
            p->finished = true;
            _researched.emplace_back(p->id, p->level);
            if (p->kind == ProjectKind::kAttraction)
                _attractions.markAvailable(p->id);
            for (int id : std::vector<int>(p->scientists))
                unassign(slot, id);
        }
    }
    for (PlacedObject &o : _objects) {
        if (o.upgrading <= 0)
            continue;
        o.upgrading -= 1;
        if (o.upgrading <= 0) {
            o.upgrading = 0;
            ++o.level;
            o.repair = 100;
        }
    }
}

} // namespace reluva::tpw
