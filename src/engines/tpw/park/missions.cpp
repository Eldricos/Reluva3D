// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The campaign's missions: their objectives, obstacles and what doing them
// opens. docs/engines/tpw/games/simcoaster/systems/missions.md.

#include "engines/tpw/park/park.h"

#include <cctype>
#include <cstdio>

namespace reluva::tpw {

namespace {

// The game has 18 missions; more are read if there are.
constexpr int kMostMissions = 64;
constexpr int kMostObjectives = 8;
constexpr int kMostStaffNeeds = 8;
constexpr int kMostBoundaryClicks = 128;
constexpr int kBoundaryEnd = 512;

// The worlds' folders, by the missions' theme numbers.
constexpr std::array<const char *, 3> kThemeFolders{"arabian", "science", "water"};

// The objectives' statistics (ExitStatistic[n].Type), as the missions'
// comments name them.
constexpr int kProfit = 2;
constexpr int kAwards = 6;
constexpr int kShares = 7;
constexpr int kVisitors = 8;

// TAG_SYSTEM's messages.
constexpr uint32_t kTrainingRoomTitle = 254;
constexpr uint32_t kLakeSolved = 255;
constexpr uint32_t kObjectiveCompleteTitle = 168;
constexpr uint32_t kWonTitle = 163;
constexpr uint32_t kWonLetter = 164;
// The one obstacle with a message of its own when its solution's found:
// the lake ("Your scientists have found a way to drain the lake").
constexpr int kLake = 1703;

// The game's staff numbering in ObstacleStaff[n].StaffType, by the
// missions' comments: 0 a handyman ("Cleaner"), 1 a mechanic, 5 a
// gardener; the others guessed in Park::StaffKind's order.
constexpr std::array<std::pair<int, Park::StaffKind>, 6> kMissionStaff{{{0, Park::StaffKind::kJanitor},
                                                                        {1, Park::StaffKind::kEngineer},
                                                                        {2, Park::StaffKind::kGuard},
                                                                        {3, Park::StaffKind::kEntertainer},
                                                                        {4, Park::StaffKind::kScientist},
                                                                        {5, Park::StaffKind::kGardener}}};

Park::Cell cellOf(const SamFile &sam, const std::string &x, const std::string &y) {
    return {int(sam.number(x)), int(sam.number(y))};
}

} // namespace

int Park::themeName(int theme) {
    constexpr std::array<int, 3> kNames{3, 0, 1};
    return theme >= 0 && theme < int(kNames.size()) ? kNames[size_t(theme)] : 0;
}

std::optional<Park::StaffKind> Park::missionStaffKind(int staffType) {
    for (const auto &[n, kind] : kMissionStaff)
        if (n == staffType)
            return kind;
    return std::nullopt;
}

// Every mission, from missions/miss00.sam on; then the park as the
// player's progress leaves it: the obstacles of those done here gone, the
// first not done played if it's here.
Error Park::loadMissions(const SearchSet &files, int mission) {
    const std::string world = _folder.substr(_folder.rfind('/') + 1);
    for (size_t t = 0; t < kThemeFolders.size(); ++t) {
        std::string low = world;
        for (char &c : low)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        if (low == kThemeFolders[t])
            _theme = int(t);
    }
    for (int n = 0; n < kMostMissions; ++n) {
        char name[32];
        std::snprintf(name, sizeof name, "missions/miss%02d.sam", n);
        std::vector<uint8_t> data;
        SamFile m;
        if (files.read(name, data) || m.parse(data)) {
            // The mission asked for must be there.
            if (n <= mission)
                return Error::fail(std::string("can't read ") + name);
            break;
        }
        Mission out;
        out.number = int(m.number("Info.Number", n));
        out.theme = int(m.number("Completion.EndingTheme", -1));
        out.noResearch = m.number("Info.NoResearch", 0) != 0;
        out.techGroup = int(m.number("TechLevel.NewMaxGroup", -1));
        out.techTheme = int(m.number("TechLevel.Theme", -1));
        for (int i = 0; i < kMostObjectives; ++i) {
            const std::string at = "ExitStatistic[" + std::to_string(i) + "].";
            const int statistic = int(m.number(at + "Type", 0));
            if (statistic != 0)
                out.objectives.push_back({statistic, int(m.number(at + "TargetValue", 0))});
        }
        if (!out.objectives.empty()) {
            out.statistic = out.objectives[0].statistic;
            out.target = out.objectives[0].target;
        }
        out.obstacle = int(m.number("Obstacle.Type", 0));
        out.obstacleEffort = int(m.number("Obstacle.ResearchEffort", 0));
        out.goldenTickets = int(m.number("Obstacle.GoldenTickets", 0));
        for (int i = 0; i < kMostStaffNeeds; ++i) {
            const std::string at = "ObstacleStaff[" + std::to_string(i) + "].";
            if (!m.find(at + "Ability"))
                break;
            out.staffNeeded.push_back({int(m.number(at + "Ability")), int(m.number(at + "StaffType")),
                                       std::max(1, int(m.number(at + "Number", 1)))});
        }
        out.opens = int(m.number("Completion.ObstacleOpened", 0));
        if (m.number("Completion.ThemeOpened", 0) != 0)
            out.themeOpened = int(m.number("Completion.ThemeNumber", -1));
        out.endOfGame = m.number("Completion.EndOfGame", 0) != 0;
        out.promotion = int(m.number("Promotion.Number", 0));
        out.challenges = int(m.number("RequiredChallenges.Number", 0));
        out.challengesTheme = int(m.number("RequiredChallenges.Theme", -1));
        for (int i = 0; i < kMostBoundaryClicks; ++i) {
            const std::string base = "BoundaryClicks[" + std::to_string(i) + "].";
            if (!m.find(base + "X"))
                break;
            // (512, 512) ends the list.
            const Cell click = cellOf(m, base + "X", base + "Y");
            if (click.x == kBoundaryEnd && click.y == kBoundaryEnd)
                break;
            out.boundary.push_back(click);
        }
        out.newLand = m.number("Completion.LoadNewHoardings", 0) != 0;
        _missions.push_back(std::move(out));
    }
    _progress = std::clamp(mission, 0, int(_missions.size()));
    for (int n = 0; n < _progress; ++n) {
        const Mission &done = _missions[size_t(n)];
        if (done.theme >= 0 && done.theme != _theme)
            continue;
        if (done.obstacle)
            clearObstacle(done.obstacle);
        if (done.opens)
            clearObstacle(done.opens);
        if (done.newLand)
            growLand(done.boundary);
    }
    startMission(_progress);
    return Error::success();
}

// The mission numbered so, if it's played here; else none.
void Park::startMission(int number) {
    _mission = {};
    _obstacleResearched = false;
    if (number >= 0 && number < int(_missions.size())) {
        const Mission &m = _missions[size_t(number)];
        if (m.theme < 0 || m.theme == _theme)
            _mission = m;
    }
    setResearchables();
}

bool Park::researchAllowed() const {
    return !_mission.noResearch;
}

// The lab's on sale while the park may research; the training room once a
// mission here has needed trained staff (the game makes it "available"
// with the lake's solution, TAG_SYSTEM 255).
void Park::setResearchables() {
    bool training = _obstacleResearched && !_mission.staffNeeded.empty();
    for (int n = 0; n < _progress && n < int(_missions.size()); ++n) {
        const Mission &m = _missions[size_t(n)];
        training = training || ((m.theme < 0 || m.theme == _theme) && !m.staffNeeded.empty());
    }
    std::vector<std::pair<int, bool>> changes;
    for (const Attractions::Attraction &a : _attractions.all()) {
        if (a.lab)
            changes.emplace_back(a.id, researchAllowed());
        else if (a.trainingRoom)
            changes.emplace_back(a.id, training);
    }
    for (const auto &[id, available] : changes)
        _attractions.setAvailable(id, available);
}

int Park::statisticValue(int statistic) const {
    switch (statistic) {
    case kProfit:
        // "Profit = Bank Balance less Outstanding Loans" (MISSION_DESC 7).
        return _cash - owed();
    case kAwards:
        return awardsHeld();
    case kShares:
        return _shares.issued > 0 ? _shares.owned * 100 / _shares.issued : 0;
    case kVisitors:
        return _visitors;
    default:
        return 0;
    }
}

std::vector<std::vector<int>> Park::obstacleStaff() const {
    std::vector<std::vector<int>> out;
    std::vector<int> taken;
    for (const Mission::StaffNeed &need : _mission.staffNeeded) {
        std::vector<int> &ids = out.emplace_back();
        const std::optional<StaffKind> kind = missionStaffKind(need.staffType);
        for (const StaffMember &m : _staff)
            if (kind && m.kind == *kind && m.ability == need.ability && int(ids.size()) < need.number &&
                std::find(taken.begin(), taken.end(), m.id) == taken.end()) {
                ids.push_back(m.id);
                taken.push_back(m.id);
            }
    }
    return out;
}

bool Park::obstacleStaffReady() const {
    const std::vector<std::vector<int>> staff = obstacleStaff();
    for (size_t i = 0; i < staff.size(); ++i)
        if (int(staff[i].size()) < _mission.staffNeeded[i].number)
            return false;
    return true;
}

// What the obstacle still wants of a kind, once its solution's found.
int Park::abilityToLearn(StaffKind kind) const {
    if (_obstacleResearched) {
        const std::vector<std::vector<int>> staff = obstacleStaff();
        for (size_t i = 0; i < staff.size(); ++i) {
            const Mission::StaffNeed &need = _mission.staffNeeded[i];
            if (missionStaffKind(need.staffType) == kind && int(staff[i].size()) < need.number)
                return need.ability;
        }
    }
    for (size_t object = 0; object < _objects.size(); ++object) {
        if (!awaitsActivation(object))
            continue;
        const Attractions::Attraction *a = _attractions.find(_objects[object].id);
        const std::vector<std::vector<int>> staff = activationStaff(object);
        for (size_t i = 0; i < a->skills.size(); ++i) {
            const auto [ability, count] = a->skills[i];
            if (abilityKind(ability) == kind && abilityDiscovered(ability) && int(staff[i].size()) < count)
                return ability;
        }
    }
    return 0;
}

// Researched, its staff there, its tickets and its cost (the obstacle's
// Upgrades[0].CostOfUpgrade, guessed) paid: it's cleared, and the
// mission done. The staff's work at it, which the game shows, isn't.
Park::Solution Park::proceedWithSolution() {
    if (!_mission.needsSolution())
        return Solution::kNoObstacle;
    if (_mission.obstacleEffort > 0 && !_obstacleResearched)
        return Solution::kNotResearched;
    if (!obstacleStaffReady())
        return Solution::kNeedsStaff;
    if (_goldenTickets < _mission.goldenTickets)
        return Solution::kNeedsTickets;
    const Attractions::Attraction *a = _attractions.find(_mission.obstacle);
    const int cost = a ? a->price : 0;
    if (_cash < cost)
        return Solution::kNoMoney;
    book(Ledger::kBuilding, cost);
    _goldenTickets -= _mission.goldenTickets;
    clearObstacle(_mission.obstacle);
    completeMission();
    return Solution::kCleared;
}

// An obstacle's cleared: what it leaves in its place, or nothing.
void Park::clearObstacle(int id) {
    const Attractions::Attraction *a = _attractions.find(id);
    for (size_t i = _objects.size(); i-- > 0;) {
        if (_objects[i].id != id)
            continue;
        if (a && a->replaceWith)
            _objects[i].id = a->replaceWith;
        else
            remove(i, false);
    }
}

// Each day: a mission of objectives done once they're all reached, and
// what else it asks: a rank, challenges won, golden tickets. One whose
// obstacle wants a solution is done as the obstacle's cleared.
void Park::missionDay() {
    const Mission &m = _mission;
    if (m.number < 0 || m.needsSolution())
        return;
    // One asking nothing Reluva can count waits.
    if (m.objectives.empty() && m.promotion == 0 && m.challenges == 0)
        return;
    for (const Mission::Objective &o : m.objectives)
        if (statisticValue(o.statistic) < o.target)
            return;
    if (_rank < m.promotion || challengesWonIn(m.challengesTheme) < m.challenges || _goldenTickets < m.goldenTickets)
        return;
    completeMission();
}

// The president's letter (OBJECTIVE_COMPLETE.str, by the mission); what
// the mission opens; the next mission, played here if it's here.
void Park::completeMission() {
    const Mission done = _mission;
    _messages.push_back({kObjectiveCompleteTitle, uint32_t(done.number), {}, {}, true});
    if (done.opens)
        clearObstacle(done.opens);
    if (done.newLand)
        growLand(done.boundary);
    if (done.themeOpened >= 0)
        _themesOpened.push_back(done.themeOpened);
    if (done.endOfGame) {
        _won = true;
        post(kWonTitle, kWonLetter);
    }
    _progress = done.number + 1;
    startMission(_progress);
}

void Park::researchedObstacle() {
    _obstacleResearched = true;
    setResearchables();
    if (_mission.obstacle == kLake)
        post(kTrainingRoomTitle, kLakeSolved);
}

} // namespace reluva::tpw
