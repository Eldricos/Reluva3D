// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park's windows of information. Visitor Statistics, a visitor's
// window, the Message Center and the game's menu are laid out as the
// recording shows them at 640 × 480; the others in their style, by
// guess: docs/engines/tpw/games/simcoaster/systems/park-windows.md.

#include "engines/tpw/ui/info_windows.h"

#include "engines/tpw/textures.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr Color kWhite{255, 255, 255};
constexpr Color kLabelYellow{232, 232, 64};
using Button = WindowKit::Button;

// UITEXT's.
constexpr uint32_t kAllVisitorsText = 40; // then children, adults, senior citizens
constexpr uint32_t kHappyText = 1;        // then undecided, unhappy
constexpr uint32_t kVisitorStatisticsText = 376;
constexpr uint32_t kAverageHappinessText = 377;
constexpr uint32_t kAveragePurchasesText = 378;
constexpr uint32_t kAverageRidesText = 380;
constexpr uint32_t kAverageSideshowsText = 381;
constexpr uint32_t kDominantThoughtsText = 382;
constexpr uint32_t kInParkText = 383;
constexpr uint32_t kToDateText = 384;
constexpr uint32_t kVisitorNumberText = 658;
constexpr uint32_t kRidesRiddenText = 201;
constexpr uint32_t kSideshowsPlayedText = 202;
constexpr uint32_t kSideshowsWonText = 203;
constexpr uint32_t kPurchasesText = 204;
constexpr uint32_t kHappinessText = 205;
constexpr uint32_t kStatusText = 206;
constexpr uint32_t kExcitementText = 207;
constexpr uint32_t kListAllVisitorsText = 838;
constexpr std::array<uint32_t, 6> kVisitorColumns{366, 367, 368, 369, 370, 371};
constexpr uint32_t kListAllAttractionsText = 835;
constexpr uint32_t kParkStatisticsText = 422;
constexpr uint32_t kArrivalRateText = 424;
constexpr uint32_t kParkRatingText = 425;
constexpr uint32_t kTotalVisitorsText = 426;
constexpr uint32_t kParkAwardsText = 452;
constexpr uint32_t kFirstAwardText = 453;
constexpr size_t kAwards = Park::kAwardCount;
constexpr uint32_t kNoneText = 472;
constexpr uint32_t kMessageCenterText = 861;
constexpr uint32_t kSubjectText = 209;
constexpr uint32_t kReceivedText = 210;
constexpr uint32_t kInboxText = 860;
constexpr uint32_t kDetailsText = 859;
constexpr uint32_t kFromText = 856;
constexpr uint32_t kToText = 855;
constexpr uint32_t kSubjectLabelText = 857;
// TAG_SYSTEM's: the president, who writes the letters.
constexpr uint32_t kPresidentText = 173;

// The visitors' tabs' models, and the attractions'.
constexpr std::array<const char *, 4> kVisitorTabs{"tab_vall", "tab_vchild", "tab_vadult", "tab_vsenior"};
constexpr std::array<const char *, 5> kAttractionTabs{"tab_arides", "tab_ashops", "tab_asshows", "tab_afeatures",
                                                      "tab_afacility"};
constexpr size_t kChosenTab = 5; // a tab's mesh when it's the one shown

// A visitor is happy from this, unhappy below the other (guessed).
constexpr float kHappyFrom = 66;
constexpr float kUnhappyBelow = 33;
constexpr Color kHappyBar{240, 232, 32};
constexpr Color kUndecidedBar{240, 136, 24};
constexpr Color kUnhappyBar{48, 136, 232};
constexpr Color kBarBack{48, 48, 48};

// Visitor Statistics at 7:20.
constexpr ScreenRect kStatsWindow{175, 8, 345, 392};
constexpr ScreenRect kStatsClose{486, 12, 26, 26};
constexpr int kStatsTabX = 228;
constexpr int kStatsTabY = 46;
constexpr int kStatsTabStep = 44;
constexpr ScreenRect kStatsTab{0, 0, 42, 40};
constexpr ScreenRect kStatsPanel{207, 88, 280, 196};
constexpr ScreenRect kStatsUpper{228, 128, 236, 148};
constexpr ScreenRect kStatsThoughts{276, 206, 140, 36};
constexpr ScreenRect kStatsThoughtsLabel{286, 246, 120, 18};
constexpr ScreenRect kStatsLower{228, 302, 236, 84};
constexpr ScreenRect kStatsCancel{480, 362, 28, 28};
constexpr int kStatsValueX = 357;
constexpr int kStatsBarWidth = 100;

// A visitor's window at 9:30.
constexpr ScreenRect kVisitorWindow{145, 95, 405, 232};
constexpr ScreenRect kVisitorClose{512, 99, 26, 26};
constexpr ScreenRect kVisitorPicture{165, 138, 84, 78};
constexpr ScreenRect kVisitorStats{268, 138, 233, 118};
constexpr ScreenRect kVisitorGoTo{158, 281, 28, 28};
constexpr ScreenRect kVisitorPrevious{196, 281, 28, 28};
constexpr ScreenRect kVisitorNext{229, 281, 28, 28};
constexpr ScreenRect kVisitorThought{270, 276, 232, 26};
constexpr ScreenRect kVisitorCancel{508, 281, 28, 28};
constexpr int kVisitorValueX = 395;
constexpr int kVisitorLine = 16;

// The lists, in the staff list's style.
constexpr ScreenRect kListWindow{70, 50, 500, 360};
constexpr ScreenRect kListClose{536, 54, 26, 26};
constexpr int kListTabX = 98;
constexpr int kListTabY = 88;
constexpr ScreenRect kListFrame{88, 172, 464, 196};
constexpr int kListHeaderY = 138;
constexpr ScreenRect kListOk{528, 376, 28, 28};

// Park Statistics and Awards.
constexpr ScreenRect kSmallWindow{170, 120, 300, 220};
constexpr ScreenRect kSmallFrame{185, 158, 270, 130};
constexpr ScreenRect kSmallOk{432, 300, 28, 28};
constexpr ScreenRect kAwardsWindow{150, 90, 340, 270};
constexpr ScreenRect kAwardsFrame{165, 128, 310, 172};
constexpr ScreenRect kAwardsOk{452, 316, 28, 28};

// The Message Center at 20:10.
constexpr ScreenRect kCenterWindow{65, 12, 560, 388};
constexpr ScreenRect kCenterClose{590, 16, 26, 26};
constexpr ScreenRect kCenterInboxPanel{80, 80, 230, 276};
constexpr ScreenRect kCenterSubject{92, 92, 124, 30};
constexpr ScreenRect kCenterReceived{220, 92, 52, 30};
constexpr ScreenRect kCenterList{88, 128, 188, 220};
constexpr ScreenRect kCenterDetails{325, 82, 286, 270};
constexpr ScreenRect kCenterDelete{76, 366, 28, 28};
constexpr ScreenRect kCenterOk{588, 366, 28, 28};
constexpr int kCenterValueX = 406;

// The game's menu at 4:20: its lines, centred, 46 pixels apart, in
// MENUBIG, cyan, white under the mouse; those that do nothing yet grey.
constexpr std::array<uint32_t, 8> kGameMenuItems{768, 769, 774, 766, 770, 50, 771, 772};
constexpr int kGameMenuTop = 66;
constexpr int kGameMenuStep = 46;
constexpr int kGameMenuHeight = 36;
constexpr size_t kLoadItem = 0;
constexpr size_t kSaveItem = 1;
constexpr size_t kNewPlayerItem = 2;
constexpr size_t kOptionsItem = 4;
constexpr size_t kResumeItem = 6;
constexpr size_t kQuitItem = 7;

bool works(size_t item) {
    return item == kLoadItem || item == kSaveItem || item == kNewPlayerItem || item == kOptionsItem ||
           item == kResumeItem || item == kQuitItem;
}
// "QUIT GAME ... Are you sure ...", "SELECT A NEW PLAYER ... (Your game
// WILL be saved)": in a box over the menu, laid out by guess.
constexpr uint32_t kQuitQuestion = 773;
constexpr uint32_t kNewPlayerQuestion = 775;
constexpr ScreenRect kAskBox{150, 150, 340, 170};
constexpr ScreenRect kAskYes{410, 278, 28, 28};
constexpr ScreenRect kAskNo{444, 280, 26, 26};
constexpr Color kMenuCyan{96, 200, 216};
constexpr Color kMenuGrey{96, 120, 128};

bool inGroup(const Park::Visitor &v, VisitorGroup group) {
    return group == VisitorGroup::kAll || v.generation == int(group) - 1;
}

const Park::Visitor *visitorOf(const Park &park, int id) {
    for (const Park::Visitor &v : park.visitorsAbout())
        if (v.id == id)
            return &v;
    return nullptr;
}

// A bar of a share, 0 to 1, in a colour.
void shareBar(Surface &screen, int x, int y, int width, float share, Color c) {
    WindowKit::fill(screen, {x, y, width, 8}, kBarBack);
    WindowKit::fill(screen, {x, y, int(float(width) * std::clamp(share, 0.0f, 1.0f)), 8}, c);
}

// What a visitor's doing, as KIDSTATES.str says it: 2 "Going to ticket
// booth", 7 "Walking around", 14 "Lining-up", 20 "Using: {0}", 19
// "Exiting attraction", 22 "Leaving the park".
std::string doingOf(const WindowKit &kit, const Park &park, const Park::Visitor &v, const ObjectNames *names) {
    switch (v.doing) {
    case Park::Doing::kComing:
        return kit.kidState(v.paid ? 5 : 2);
    case Park::Doing::kWalking:
        return kit.kidState(v.target >= 0 ? 13 : 7);
    case Park::Doing::kQueueing:
        return kit.kidState(14);
    case Park::Doing::kRiding: {
        std::string name;
        const int object = v.queue >= 0 ? park.queues()[size_t(v.queue)].object : -1;
        if (const Attractions::Attraction *a =
                object >= 0 ? park.attractions().find(park.objects()[size_t(object)].id) : nullptr)
            name = names ? names->shown(*a) : a->name;
        return kit.kidState(20, {{0, name}});
    }
    case Park::Doing::kLeaving:
        return kit.kidState(19);
    case Park::Doing::kGoingHome:
        return kit.kidState(22);
    }
    return {};
}

// Their thought now, or how they are.
uint32_t thoughtOf(const Park &park, const Park::Visitor &v) {
    return uint32_t(v.thought != Park::Thought::kNone && park.millis() < v.thoughtUntil ? v.thought : park.mood(v));
}

// A time in the park as days.
std::string daysOf(uint64_t millis) {
    return std::to_string(millis / Park::kDayMillis);
}

} // namespace

// Visitor Statistics.

ScreenRect VisitorStatsWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kTab:
        return {kStatsTabX + int(index) * kStatsTabStep, kStatsTabY, kStatsTab.w, kStatsTab.h};
    case Control::kClose:
        return kStatsCancel;
    default:
        return {};
    }
}

WindowResult VisitorStatsWindow::handle(const Event &ev) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = Control::kNone;
    size_t tab = 0;
    for (size_t i = 0; i < kVisitorTabs.size(); ++i)
        if (placeOf(Control::kTab, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kTab;
            tab = i;
        }
    if (placeOf(Control::kClose).contains(ev.mouseX, ev.mouseY) || kStatsClose.contains(ev.mouseX, ev.mouseY))
        _over = Control::kClose;
    if (!WindowKit::isClick(ev))
        return WindowResult::kNone;
    if (_over == Control::kTab)
        _group = VisitorGroup(tab);
    if (_over != Control::kClose)
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kClosed;
}

void VisitorStatsWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kStatsWindow, kit.ui(kVisitorStatisticsText));
    kit.button(screen, Button::kClose, kStatsClose, WindowKit::kPlain);
    for (size_t i = 0; i < kVisitorTabs.size(); ++i)
        kit.piece(screen, kVisitorTabs[i], size_t(_group) == i ? kChosenTab : WindowKit::kPlain,
                  placeOf(Control::kTab, i));
    kit.panel(screen, kStatsPanel);
    const std::string heading = kit.ui(kAllVisitorsText + uint32_t(_group));
    kit.text(screen, kStatsPanel.x + (kStatsPanel.w - kit.textWidth(heading)) / 2, kStatsPanel.y + 12, heading);

    // Those in the park of the tab's group, and their averages.
    float happiness = 0;
    int rides = 0;
    int sideshows = 0;
    int purchases = 0;
    int counted = 0;
    std::array<int, 3> moods{};
    std::array<int, 64> thoughts{};
    for (const Park::Visitor &v : park.visitorsAbout()) {
        if (!v.inPark || !inGroup(v, _group))
            continue;
        ++counted;
        happiness += v.happiness;
        rides += v.ridden;
        sideshows += v.sideshowsPlayed;
        purchases += v.purchases;
        ++moods[v.happiness >= kHappyFrom ? 0 : v.happiness >= kUnhappyBelow ? 1 : 2];
        if (const uint32_t t = thoughtOf(park, v); t < thoughts.size())
            ++thoughts[t];
    }
    const auto average = [&](int total) { return counted ? std::to_string(total / counted) : std::string("0"); };
    kit.frame(screen, kStatsUpper);
    const int x = kStatsUpper.x + 8;
    kit.text(screen, x, kStatsUpper.y + 8, kit.ui(kAverageHappinessText));
    kit.bar(screen, kStatsValueX + 3, kStatsUpper.y + 9, kStatsBarWidth,
            counted ? happiness / float(counted) / 100 : 0);
    int y = kStatsUpper.y + 32;
    for (auto [label, total] : {std::pair{kAverageRidesText, rides}, std::pair{kAverageSideshowsText, sideshows},
                                std::pair{kAveragePurchasesText, purchases}}) {
        kit.text(screen, x, y, kit.ui(label));
        kit.text(screen, kStatsValueX + 33, y, average(total));
        y += kVisitorLine;
    }
    // The three thoughts most had, as words: the game shows their bubbles.
    kit.frame(screen, kStatsThoughts);
    std::array<size_t, 3> top{};
    for (size_t &t : top) {
        t = size_t(std::max_element(thoughts.begin() + 1, thoughts.end()) - thoughts.begin());
        if (thoughts[t] == 0)
            t = 0;
        thoughts[t] = 0;
    }
    std::string words;
    for (size_t t : top)
        if (t)
            words += (words.empty() ? "" : ", ") + kit.thought(uint32_t(t));
    kit.paragraph(screen, kStatsThoughts.x + 6, kStatsThoughts.y + 6, kStatsThoughts.w - 12, words);
    kit.header(screen, kStatsThoughtsLabel, kit.ui(kDominantThoughtsText));

    kit.frame(screen, kStatsLower);
    int total = park.totalVisitors();
    if (_group != VisitorGroup::kAll)
        total = park.totalVisitors(int(_group) - 1);
    y = kStatsLower.y + 8;
    kit.text(screen, x, y, kit.ui(kInParkText));
    kit.text(screen, kStatsValueX, y, std::to_string(counted));
    y += 14;
    kit.text(screen, x, y, kit.ui(kToDateText));
    kit.text(screen, kStatsValueX, y, std::to_string(total));
    y += 16;
    constexpr std::array<Color, 3> kMoodBars{kHappyBar, kUndecidedBar, kUnhappyBar};
    for (size_t m = 0; m < moods.size(); ++m) {
        kit.text(screen, x, y, kit.ui(kHappyText + uint32_t(m)));
        shareBar(screen, kStatsValueX, y + 1, kStatsBarWidth, counted ? float(moods[m]) / float(counted) : 0,
                 kMoodBars[m]);
        y += 16;
    }
    kit.button(screen, Button::kCancel, kStatsCancel, _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
}

// A visitor's window.

ScreenRect VisitorWindow::placeOf(Control control) {
    switch (control) {
    case Control::kGoTo:
        return kVisitorGoTo;
    case Control::kPrevious:
        return kVisitorPrevious;
    case Control::kNext:
        return kVisitorNext;
    case Control::kClose:
        return kVisitorCancel;
    default:
        return {};
    }
}

WindowResult VisitorWindow::handle(const Event &ev, const Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = Control::kNone;
    for (Control c : {Control::kGoTo, Control::kPrevious, Control::kNext, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (kVisitorClose.contains(ev.mouseX, ev.mouseY))
        _over = Control::kClose;
    if (!WindowKit::isClick(ev))
        return WindowResult::kNone;
    // The arrows go through those in the park, round.
    std::vector<int> in;
    for (const Park::Visitor &v : park.visitorsAbout())
        if (v.inPark)
            in.push_back(v.id);
    const auto at = std::find(in.begin(), in.end(), _visitor);
    switch (_over) {
    case Control::kGoTo:
        return WindowResult::kChose;
    case Control::kPrevious:
    case Control::kNext:
        if (!in.empty()) {
            const size_t i = at == in.end() ? 0 : size_t(at - in.begin());
            _visitor = in[(i + (_over == Control::kNext ? 1 : in.size() - 1)) % in.size()];
        }
        return WindowResult::kNone;
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void VisitorWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    const Park::Visitor *v = visitorOf(park, _visitor);
    if (!_open || !v)
        return;
    kit.window(screen, kVisitorWindow, kit.ui(kVisitorNumberText, {{28, std::to_string(v->id)}}));
    kit.button(screen, Button::kClose, kVisitorClose, WindowKit::kPlain);
    kit.frame(screen, kVisitorPicture);
    kit.frame(screen, kVisitorStats);
    const int x = kVisitorStats.x + 6;
    int y = kVisitorStats.y + 6;
    const Park::VisitorType *type =
        v->type >= 0 && size_t(v->type) < park.visitorTypes().size() ? &park.visitorTypes()[size_t(v->type)] : nullptr;
    kit.text(screen, x, y, kit.ui(kExcitementText));
    kit.bar(screen, kVisitorValueX, y + 1, kVisitorStats.x + kVisitorStats.w - 6 - kVisitorValueX,
            type ? float(type->excitement) / 100 : 0);
    y += kVisitorLine;
    kit.text(screen, x, y, kit.ui(kHappinessText));
    kit.bar(screen, kVisitorValueX, y + 1, kVisitorStats.x + kVisitorStats.w - 6 - kVisitorValueX, v->happiness / 100);
    y += kVisitorLine;
    for (auto [label, value] :
         {std::pair{kRidesRiddenText, v->ridden}, std::pair{kPurchasesText, v->purchases},
          std::pair{kSideshowsPlayedText, v->sideshowsPlayed}, std::pair{kSideshowsWonText, v->sideshowsWon}}) {
        kit.text(screen, x, y, kit.ui(label));
        kit.text(screen, kVisitorValueX, y, std::to_string(value));
        y += kVisitorLine;
    }
    kit.text(screen, x, y, kit.ui(kStatusText));
    kit.text(screen, kVisitorValueX, y, doingOf(kit, park, *v, nullptr));
    kit.piece(screen, "b_goto", _over == Control::kGoTo ? WindowKit::kLit : WindowKit::kPlain, kVisitorGoTo);
    kit.button(screen, Button::kLeft, kVisitorPrevious,
               _over == Control::kPrevious ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, Button::kRight, kVisitorNext, _over == Control::kNext ? WindowKit::kLit : WindowKit::kPlain);
    kit.frame(screen, kVisitorThought);
    const std::string thought = kit.thought(thoughtOf(park, *v));
    kit.text(screen, kVisitorThought.x + 40 + (kVisitorThought.w - 40 - kit.textWidth(thought)) / 2,
             kVisitorThought.y + 9, thought);
    kit.button(screen, Button::kCancel, kVisitorCancel, _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
}

// The visitors' list.

ScreenRect VisitorListWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kTab:
        return {kListTabX + int(index) * kStatsTabStep, kListTabY, kStatsTab.w, kStatsTab.h};
    case Control::kRow:
        return {kListFrame.x + 2, kListFrame.y + 3 + int(index) * WindowKit::kRowHeight, kListFrame.w - 4,
                WindowKit::kRowHeight};
    case Control::kOk:
        return kListOk;
    case Control::kClose:
        return kListClose;
    default:
        return {};
    }
}

std::vector<int> VisitorListWindow::listed(const Park &park) const {
    std::vector<int> ids;
    for (const Park::Visitor &v : park.visitorsAbout())
        if (v.inPark && inGroup(v, _group))
            ids.push_back(v.id);
    return ids;
}

WindowResult VisitorListWindow::handle(const Event &ev, const Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    const std::vector<int> ids = listed(park);
    _over = Control::kNone;
    size_t index = 0;
    for (size_t i = 0; i < kVisitorTabs.size(); ++i)
        if (placeOf(Control::kTab, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kTab;
            index = i;
        }
    for (size_t i = 0; i < ids.size() && placeOf(Control::kRow, i).y < kListFrame.y + kListFrame.h; ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kRow;
            index = i;
        }
    for (Control c : {Control::kOk, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!WindowKit::isClick(ev))
        return WindowResult::kNone;
    switch (_over) {
    case Control::kTab:
        _group = VisitorGroup(index);
        _chosen = 0;
        return WindowResult::kNone;
    case Control::kRow:
        _chosen = ids[index];
        return WindowResult::kNone;
    case Control::kOk:
        _open = false;
        return _chosen ? WindowResult::kChose : WindowResult::kClosed;
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void VisitorListWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kListWindow, kit.ui(kListAllVisitorsText));
    kit.button(screen, Button::kClose, kListClose, _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    for (size_t i = 0; i < kVisitorTabs.size(); ++i)
        kit.piece(screen, kVisitorTabs[i], size_t(_group) == i ? kChosenTab : WindowKit::kPlain,
                  placeOf(Control::kTab, i));
    // Six columns across the list.
    const int width = kListFrame.w / int(kVisitorColumns.size());
    for (size_t c = 0; c < kVisitorColumns.size(); ++c)
        kit.header(screen, {kListFrame.x + int(c) * width, kListHeaderY, width - 2, 30}, kit.ui(kVisitorColumns[c]));
    kit.frame(screen, kListFrame);
    size_t row = 0;
    for (int id : listed(park)) {
        const ScreenRect r = placeOf(Control::kRow, row++);
        if (r.y + r.h > kListFrame.y + kListFrame.h)
            break;
        const Park::Visitor *v = visitorOf(park, id);
        if (id == _chosen)
            WindowKit::fill(screen, r, WindowKit::kChosenRow);
        const std::array<std::string, 5> cells{std::to_string(v->id), WindowKit::money(v->cash),
                                               daysOf(park.millis() - std::min(park.millis(), v->arrived)),
                                               std::to_string(v->ridden), kit.thought(thoughtOf(park, *v))};
        for (size_t c = 0; c < cells.size(); ++c)
            kit.text(screen, kListFrame.x + int(c) * width + 6, r.y + 1, cells[c]);
        kit.bar(screen, kListFrame.x + 5 * width + 6, r.y + 3, width - 14, v->happiness / 100);
    }
    kit.button(screen, Button::kOk, kListOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// The attractions' list.

namespace {

// A tab's kind, its tab's models' order: rides, shops, sideshows,
// features, facilities.
constexpr std::array<Attractions::Kind, 5> kTabKinds{Attractions::Kind::kRide, Attractions::Kind::kShop,
                                                     Attractions::Kind::kSideshow, Attractions::Kind::kFeature,
                                                     Attractions::Kind::kFacility};
// Its title (342 to 346: All Rides, All Shops, All Sideshows, All
// Features, All Facilities), and its columns.
constexpr std::array<uint32_t, 5> kTabTitles{342, 343, 344, 345, 346};
constexpr std::array<std::array<uint32_t, 4>, 5> kTabColumns{
    {{347, 348, 349, 350}, {351, 352, 353, 354}, {356, 357, 359, 360}, {362, 363, 364, 0}, {351, 352, 353, 354}}};

} // namespace

ScreenRect AttractionListWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kTab:
        return {kListTabX + int(index) * kStatsTabStep, kListTabY, kStatsTab.w, kStatsTab.h};
    case Control::kRow:
    case Control::kOk:
    case Control::kClose:
        return VisitorListWindow::placeOf(control == Control::kRow  ? VisitorListWindow::Control::kRow
                                          : control == Control::kOk ? VisitorListWindow::Control::kOk
                                                                    : VisitorListWindow::Control::kClose,
                                          index);
    default:
        return {};
    }
}

// The tab's kind's objects: a feature's kind once, its number owned the
// count of it; obstacles not listed.
std::vector<size_t> AttractionListWindow::listed(const Park &park) const {
    std::vector<size_t> out;
    std::vector<int> seen;
    for (size_t i = 0; i < park.objects().size(); ++i) {
        const Attractions::Attraction *a = park.attractions().find(park.objects()[i].id);
        if (!a || a->obstacle || a->kind != kTabKinds[_tab])
            continue;
        if (a->kind == Attractions::Kind::kFeature) {
            if (std::find(seen.begin(), seen.end(), a->id) != seen.end())
                continue;
            seen.push_back(a->id);
        }
        out.push_back(i);
    }
    return out;
}

WindowResult AttractionListWindow::handle(const Event &ev, const Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    const std::vector<size_t> objects = listed(park);
    _over = Control::kNone;
    size_t index = 0;
    for (size_t i = 0; i < kAttractionTabs.size(); ++i)
        if (placeOf(Control::kTab, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kTab;
            index = i;
        }
    for (size_t i = 0; i < objects.size() && placeOf(Control::kRow, i).y < kListFrame.y + kListFrame.h; ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kRow;
            index = i;
        }
    for (Control c : {Control::kOk, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!WindowKit::isClick(ev))
        return WindowResult::kNone;
    switch (_over) {
    case Control::kTab:
        _tab = index;
        _chosen = -1;
        return WindowResult::kNone;
    case Control::kRow:
        _chosen = int(objects[index]);
        return WindowResult::kNone;
    case Control::kOk:
        _open = false;
        return _chosen >= 0 ? WindowResult::kChose : WindowResult::kClosed;
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void AttractionListWindow::draw(Surface &screen, const WindowKit &kit, const Park &park,
                                const ObjectNames &names) const {
    if (!_open)
        return;
    kit.window(screen, kListWindow, kit.ui(kListAllAttractionsText));
    kit.button(screen, Button::kClose, kListClose, _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    for (size_t i = 0; i < kAttractionTabs.size(); ++i)
        kit.piece(screen, kAttractionTabs[i], _tab == i ? kChosenTab : WindowKit::kPlain, placeOf(Control::kTab, i));
    const std::string title = kit.ui(kTabTitles[_tab]);
    kit.text(screen, kListWindow.x + kListWindow.w - 30 - kit.textWidth(title), kListTabY + 16, title);
    const std::array<uint32_t, 4> &columns = kTabColumns[_tab];
    const int nameWidth = 160;
    const int width = (kListFrame.w - nameWidth) / 3;
    const auto columnRect = [&](size_t c) {
        return c == 0 ? ScreenRect{kListFrame.x, kListHeaderY, nameWidth - 2, 30}
                      : ScreenRect{kListFrame.x + nameWidth + int(c - 1) * width, kListHeaderY, width - 2, 30};
    };
    for (size_t c = 0; c < columns.size(); ++c)
        if (columns[c])
            kit.header(screen, columnRect(c), kit.ui(columns[c]));
    kit.frame(screen, kListFrame);
    size_t row = 0;
    for (size_t object : listed(park)) {
        const ScreenRect r = placeOf(Control::kRow, row++);
        if (r.y + r.h > kListFrame.y + kListFrame.h)
            break;
        const Park::PlacedObject &o = park.objects()[object];
        const Attractions::Attraction *a = park.attractions().find(o.id);
        if (int(object) == _chosen)
            WindowKit::fill(screen, r, WindowKit::kChosenRow);
        kit.text(screen, r.x + 4, r.y + 1, o.name.empty() ? names.shown(*a) : o.name);
        // The kind's figures: users, then excitement and repair as bars;
        // or customers and profits; a feature's number owned and health.
        const auto value = [&](size_t c, const std::string &s) {
            const ScreenRect h = columnRect(c);
            kit.text(screen, h.x + (h.w - kit.textWidth(s)) / 2, r.y + 1, s);
        };
        const auto bar = [&](size_t c, float level) {
            const ScreenRect h = columnRect(c);
            kit.bar(screen, h.x + 8, r.y + 3, h.w - 16, level);
        };
        switch (a->kind) {
        case Attractions::Kind::kRide:
            value(1, std::to_string(o.lastUsers));
            bar(2, float(std::max(0, a->excitement)) / 100);
            bar(3, o.repair / 100);
            break;
        case Attractions::Kind::kFeature: {
            const auto owned = std::count_if(park.objects().begin(), park.objects().end(),
                                             [&](const Park::PlacedObject &p) { return p.id == o.id; });
            value(1, std::to_string(owned));
            bar(2, a->requiresWatering ? o.water / 100 : 1);
            break;
        }
        default:
            value(1, std::to_string(o.lastUsers));
            value(2, WindowKit::money(o.lastTakings - o.lastCosts - o.lastGoods));
            value(3, WindowKit::money(o.profit));
            break;
        }
    }
    kit.button(screen, Button::kOk, kListOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// Park Statistics.

ScreenRect ParkStatsWindow::okRect() {
    return kSmallOk;
}

WindowResult ParkStatsWindow::handle(const Event &ev) {
    if (!_open || !WindowKit::isClick(ev) || !kSmallOk.contains(ev.mouseX, ev.mouseY))
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kClosed;
}

// The arrival rate as the visitors the park draws now, and its rating as
// its points (both guessed).
void ParkStatsWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kSmallWindow, kit.ui(kParkStatisticsText));
    kit.frame(screen, kSmallFrame);
    int y = kSmallFrame.y + 10;
    for (auto [label, value] :
         {std::pair{kInParkText, park.visitors()}, std::pair{kArrivalRateText, park.visitorsWanted()},
          std::pair{kParkRatingText, park.points()}, std::pair{kTotalVisitorsText, park.totalVisitors()}}) {
        const std::string number = std::to_string(value);
        kit.text(screen, kSmallFrame.x + 8, y, kit.ui(label));
        kit.text(screen, kSmallFrame.x + kSmallFrame.w - 10 - kit.textWidth(number), y, number);
        y += 18;
    }
    kit.button(screen, Button::kOk, kSmallOk, WindowKit::kPlain);
}

// Park Awards.

ScreenRect AwardsWindow::okRect() {
    return kAwardsOk;
}

WindowResult AwardsWindow::handle(const Event &ev) {
    if (!_open || !WindowKit::isClick(ev) || !kAwardsOk.contains(ev.mouseX, ev.mouseY))
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kClosed;
}

// A held award's measure is shown as a percent, by guess: the recording
// never opens the window.
void AwardsWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kAwardsWindow, kit.ui(kParkAwardsText, {{20, _park}}));
    kit.frame(screen, kAwardsFrame);
    int y = kAwardsFrame.y + 10;
    const std::array<Park::AwardStanding, Park::kAwardCount> standing = park.awards();
    for (size_t i = 0; i < kAwards; ++i) {
        kit.text(screen, kAwardsFrame.x + 8, y, kit.ui(kFirstAwardText + uint32_t(i)));
        const std::string held = standing[i].held ? std::to_string(standing[i].value) + "%" : kit.ui(kNoneText);
        kit.text(screen, kAwardsFrame.x + kAwardsFrame.w - 10 - kit.textWidth(held), y, held);
        y += 20;
    }
    kit.button(screen, Button::kOk, kAwardsOk, WindowKit::kPlain);
}

// The Message Center.

ScreenRect MessageCenterWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kRow:
        return {kCenterList.x + 2, kCenterList.y + 3 + int(index) * WindowKit::kRowHeight, kCenterList.w - 4,
                WindowKit::kRowHeight};
    case Control::kDelete:
        return kCenterDelete;
    case Control::kOk:
        return kCenterOk;
    case Control::kClose:
        return kCenterClose;
    default:
        return {};
    }
}

WindowResult MessageCenterWindow::handle(const Event &ev, std::vector<Received> &inbox) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = Control::kNone;
    size_t row = 0;
    for (size_t i = 0; i < inbox.size(); ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kRow;
            row = i;
        }
    for (Control c : {Control::kDelete, Control::kOk, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!WindowKit::isClick(ev))
        return WindowResult::kNone;
    // Listed newest first: row i is the inbox's from its end.
    switch (_over) {
    case Control::kRow:
        _chosen = row;
        return WindowResult::kNone;
    case Control::kDelete:
        if (_chosen < inbox.size()) {
            inbox.erase(inbox.end() - 1 - long(_chosen));
            _chosen = std::min(_chosen, inbox.empty() ? 0 : inbox.size() - 1);
        }
        return WindowResult::kNone;
    case Control::kOk:
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void MessageCenterWindow::draw(Surface &screen, const WindowKit &kit, const std::vector<Received> &inbox,
                               const std::string &player, const std::function<std::string(uint32_t)> &date) const {
    if (!_open)
        return;
    kit.window(screen, kCenterWindow, kit.ui(kMessageCenterText));
    kit.button(screen, Button::kClose, kCenterClose, _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    kit.text(screen, kCenterInboxPanel.x + 20, kCenterInboxPanel.y - 12, kit.ui(kInboxText));
    kit.text(screen, kCenterDetails.x - 2, kCenterInboxPanel.y - 12, kit.ui(kDetailsText));
    kit.panel(screen, kCenterInboxPanel);
    kit.header(screen, kCenterSubject, kit.ui(kSubjectText));
    kit.header(screen, kCenterReceived, kit.ui(kReceivedText));
    kit.frame(screen, kCenterList);
    for (size_t i = 0; i < inbox.size(); ++i) {
        const ScreenRect r = placeOf(Control::kRow, i);
        if (r.y + r.h > kCenterList.y + kCenterList.h)
            break;
        const Received &m = inbox[inbox.size() - 1 - i];
        if (i == _chosen)
            WindowKit::fill(screen, r, WindowKit::kChosenRow);
        kit.text(screen, r.x + 4, r.y + 1, kit.messageTitle(m.message));
        const std::string when = date(m.day);
        kit.text(screen, r.x + r.w - 4 - kit.textWidth(when), r.y + 1, when);
    }
    kit.frame(screen, kCenterDetails);
    if (_chosen < inbox.size()) {
        const Park::Message &m = inbox[inbox.size() - 1 - _chosen].message;
        int y = kCenterDetails.y + 10;
        for (auto [label, value] : {std::pair{kFromText, kit.tag(m.sender ? m.sender : kPresidentText)},
                                    std::pair{kToText, player}, std::pair{kSubjectLabelText, kit.messageTitle(m)}}) {
            kit.text(screen, kCenterDetails.x + 6, y, kit.ui(label), kLabelYellow);
            kit.text(screen, kCenterValueX, y, value);
            y += 14;
        }
        y += 6;
        for (const std::string &text : kit.messageText(m))
            y = kit.paragraph(screen, kCenterDetails.x + 6, y, kCenterDetails.w - 12, text);
    }
    kit.piece(screen, "b_delete", _over == Control::kDelete ? WindowKit::kLit : WindowKit::kPlain, kCenterDelete);
    kit.button(screen, Button::kOk, kCenterOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// The Park Map.

namespace {

constexpr ScreenRect kMapWindow{150, 50, 340, 380};
constexpr ScreenRect kMapFrame{165, 86, 310, 310};
constexpr ScreenRect kMapOk{452, 398, 28, 28};
constexpr uint32_t kParkMapText = 852;
constexpr uint32_t kVisitorOpinionText = 854;
// The map picture's cells: 96 across its 512 pixels, the park's cell
// (0, 0) at its left and 5.6 cells down, as Land of Invention's entrance
// (47, 17), fence (y 48) and river (x 60) lie on it (inferred).
constexpr float kMapCells = 96;
constexpr float kMapTop = -5.6f;
constexpr int kMarkSize = 10;
// The marks' pictures, by Attractions::Kind: ride, sideshow, shop, facility, feature.
constexpr std::array<const char *, Attractions::kKinds> kMarks{"2dmap/map_ride.tga", "2dmap/map_sshow.tga",
                                                               "2dmap/map_shop.tga", "2dmap/map_facility.tga",
                                                               "2dmap/map_feature.tga"};
constexpr Color kVisitorDot{255, 255, 255};
constexpr Color kNaughtyDot{232, 32, 32};
// The map's other pictures, for its layers.
enum LayerMark : size_t { kWrench, kSkull, kNoEntry, kGuardGlow, kEntertainerGlow, kLitterGlow, kStaffDot, kDot };
constexpr std::array<const char *, 8> kLayerMarks{"2dmap/ksprite.tga", "2dmap/msprite.tga", "2dmap/nsprite.tga",
                                                  "2dmap/gsprite.tga", "2dmap/esprite.tga", "2dmap/lsprite.tga",
                                                  "2dmap/ssprite.tga", "2dmap/vsprite.tga"};
// The layers' buttons, below the map: each a square with its layer's mark.
constexpr int kLayerButton = 26;
constexpr int kLayerButtonsY = 400;
constexpr Color kLayerOff{24, 40, 72};
// How far an entertainer's glow reaches, in cells (guessed).
constexpr int kEntertainerReach = 3;
constexpr Color kCameraMark{232, 32, 32};

// A picture laid over what's there by its own alpha, nearest pixel: the
// glows.
void drawBlended(Surface &screen, const image::Image &picture, const ScreenRect &r, const ScreenRect &clip) {
    if (picture.w <= 0 || picture.h <= 0 || r.w <= 0 || r.h <= 0)
        return;
    for (int y = std::max(clip.y, r.y); y < std::min(clip.y + clip.h, r.y + r.h); ++y)
        for (int x = std::max(clip.x, r.x); x < std::min(clip.x + clip.w, r.x + r.w); ++x) {
            const uint8_t *p = picture.pixel((x - r.x) * picture.w / r.w, (y - r.y) * picture.h / r.h);
            uint8_t *q = screen.row(y) + x * 4;
            for (int i = 0; i < 3; ++i)
                q[i] = uint8_t((q[i] * (255 - p[3]) + p[i] * p[3]) / 255);
        }
}

// A picture drawn into a rectangle, nearest pixel, its clear pixels left.
void drawScaled(Surface &screen, const image::Image &picture, const ScreenRect &r) {
    if (picture.w <= 0 || picture.h <= 0)
        return;
    for (int y = std::max(0, r.y); y < std::min(screen.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(screen.w, r.x + r.w); ++x) {
            const uint8_t *p = picture.pixel((x - r.x) * picture.w / r.w, (y - r.y) * picture.h / r.h);
            if (p[3] < 128)
                continue;
            uint8_t *q = screen.row(y) + x * 4;
            std::copy(p, p + 3, q);
        }
}

} // namespace

void ParkMapWindow::load(const SearchSet &files, const std::string &folder) {
    if (loadPicture(files, folder + "/2dmap.tga", _map))
        _map = {};
    for (size_t k = 0; k < kMarks.size(); ++k)
        if (loadPicture(files, kMarks[k], _marks[k]))
            _marks[k] = {};
    for (size_t i = 0; i < _stars.size(); ++i)
        if (loadPicture(files, "2dmap/map_stars" + std::to_string(i + 1) + ".tga", _stars[i]))
            _stars[i] = {};
    for (size_t i = 0; i < _upgrades.size(); ++i)
        if (loadPicture(files, "2dmap/map_upgradel" + std::to_string(i + 1) + ".tga", _upgrades[i]))
            _upgrades[i] = {};
    for (size_t i = 0; i < kLayerMarks.size(); ++i)
        if (loadPicture(files, kLayerMarks[i], _layerMarks[i]))
            _layerMarks[i] = {};
}

ScreenRect ParkMapWindow::layerRect(Layer layer) {
    return {kMapFrame.x + int(layer) * (kLayerButton + 4), kLayerButtonsY, kLayerButton, kLayerButton};
}

ScreenRect ParkMapWindow::mapRect() {
    return {kMapFrame.x + 5, kMapFrame.y + 5, kMapFrame.w - 10, kMapFrame.h - 10};
}

ScreenRect ParkMapWindow::okRect() {
    return kMapOk;
}

ParkMapWindow::Spot ParkMapWindow::placeOf(const Park::Cell &cell) {
    const ScreenRect m = mapRect();
    return {m.x + int((float(cell.x) + 0.5f) * float(m.w) / kMapCells),
            m.y + int((float(cell.y) + 0.5f - kMapTop) * float(m.h) / kMapCells)};
}

Park::Cell ParkMapWindow::cellAt(int x, int y) {
    const ScreenRect m = mapRect();
    return {int(std::floor(float(x - m.x) * kMapCells / float(m.w))),
            int(std::floor(float(y - m.y) * kMapCells / float(m.h) + kMapTop))};
}

WindowResult ParkMapWindow::handle(const Event &ev) {
    if (!_open || !WindowKit::isClick(ev))
        return WindowResult::kNone;
    for (size_t l = 0; l < kLayers; ++l)
        if (layerRect(Layer(l)).contains(ev.mouseX, ev.mouseY)) {
            _layers[l] = !_layers[l];
            return WindowResult::kNone;
        }
    if (kMapOk.contains(ev.mouseX, ev.mouseY)) {
        _open = false;
        return WindowResult::kClosed;
    }
    if (!mapRect().contains(ev.mouseX, ev.mouseY))
        return WindowResult::kNone;
    _target = cellAt(ev.mouseX, ev.mouseY);
    return WindowResult::kChose;
}

void ParkMapWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const Park::Cell &camera) const {
    if (!_open)
        return;
    kit.window(screen, kMapWindow, kit.ui(_mode == Mode::kOpinion ? kVisitorOpinionText : kParkMapText));
    kit.frame(screen, kMapFrame);
    const ScreenRect m = mapRect();
    drawScaled(screen, _map, m);
    // Land beyond the park's fence darkened, by guess: the recording's map
    // is of a park that hasn't grown.
    for (int y = m.y; y < m.y + m.h; ++y)
        for (int x = m.x; x < m.x + m.w; ++x) {
            const Park::Cell c = cellAt(x, y);
            if (c.x < 0 || c.y < 0 || c.x >= park.groundWidth() || c.y >= park.groundHeight() || park.owns(c))
                continue;
            uint8_t *q = screen.row(y) + x * 4;
            for (int i = 0; i < 3; ++i)
                q[i] = uint8_t(q[i] / 2);
        }
    const auto inside = [&](const Spot &p) { return m.contains(p.x, p.y); };
    const auto cellOf = [](float x, float z) {
        return Park::Cell{int(std::floor(x / Park::kCellUnits)), int(std::floor(z / Park::kCellUnits))};
    };
    // A mark of `size` pixels on a cell.
    const auto mark = [&](const image::Image &picture, const Park::Cell &cell, int size, int dx = 0, int dy = 0) {
        const Spot p = placeOf(cell);
        if (inside(p))
            drawScaled(screen, picture, {p.x - size / 2 + dx, p.y - size / 2 + dy, size, size});
    };
    // A glow over the cells from one corner to the other.
    const auto glow = [&](const image::Image &picture, const Park::Cell &from, const Park::Cell &to) {
        const Spot a = placeOf(from);
        const Spot b = placeOf({to.x + 1, to.y + 1});
        drawBlended(screen, picture, {a.x, a.y, b.x - a.x, b.y - a.y}, m);
    };
    if (shows(Layer::kCoverage))
        for (const Park::StaffMember &s : park.staff()) {
            if (s.kind == Park::StaffKind::kGuard && s.patrolling)
                glow(_layerMarks[kGuardGlow],
                     {std::min(s.patrolFrom.x, s.patrolTo.x) - 1, std::min(s.patrolFrom.y, s.patrolTo.y) - 1},
                     {std::max(s.patrolFrom.x, s.patrolTo.x) + 1, std::max(s.patrolFrom.y, s.patrolTo.y) + 1});
            if (s.kind == Park::StaffKind::kEntertainer) {
                const Park::Cell c = cellOf(s.x, s.z);
                glow(_layerMarks[kEntertainerGlow], {c.x - kEntertainerReach, c.y - kEntertainerReach},
                     {c.x + kEntertainerReach, c.y + kEntertainerReach});
            }
        }
    if (shows(Layer::kProblems)) {
        for (const Park::Litter &l : park.litter()) {
            const Park::Cell c = cellOf(l.x, l.z);
            glow(_layerMarks[kLitterGlow], {c.x - 1, c.y - 1}, {c.x + 1, c.y + 1});
        }
        for (const Park::StinkBomb &b : park.stinkBombs())
            if (park.millis() < b.until)
                mark(_layerMarks[kSkull], cellOf(b.x, b.z), kMarkSize);
    }
    for (const Park::PlacedObject &o : park.objects()) {
        const Attractions::Attraction *a = park.attractions().find(o.id);
        if (!a || a->obstacle)
            continue;
        if (shows(Layer::kAttractions))
            mark(_marks[size_t(a->kind)], o.at, kMarkSize);
        // Its stars over it, by its excitement, a fifth each; its upgrades by it.
        if (shows(Layer::kDetails) && a->kind == Attractions::Kind::kRide && a->excitement >= 0)
            mark(_stars[size_t(std::clamp(a->excitement / 20, 0, 4))], o.at, kMarkSize + 2, 0, -kMarkSize);
        if (shows(Layer::kDetails) && o.level > 0)
            mark(_upgrades[size_t(std::min(o.level, 2) - 1)], o.at, kMarkSize - 2, kMarkSize, 0);
        if (shows(Layer::kProblems) && o.broken)
            mark(_layerMarks[kWrench], o.at, kMarkSize, kMarkSize / 2, kMarkSize / 2);
        else if (shows(Layer::kProblems) && !o.open)
            mark(_layerMarks[kNoEntry], o.at, kMarkSize, kMarkSize / 2, kMarkSize / 2);
    }
    // Visitor Opinion shows them always, by their moods.
    if (shows(Layer::kVisitors) || _mode == Mode::kOpinion)
        for (const Park::Visitor &v : park.visitorsAbout()) {
            const Spot p = placeOf(cellOf(v.x, v.z));
            if (!v.inPark || !inside(p))
                continue;
            const Color dot = _mode == Mode::kOpinion                ? (v.happiness >= kHappyFrom      ? kHappyBar
                                                                        : v.happiness >= kUnhappyBelow ? kUndecidedBar
                                                                                                       : kUnhappyBar)
                              : v.thought == Park::Thought::kNaughty ? kNaughtyDot
                                                                     : kVisitorDot;
            WindowKit::fill(screen, {p.x, p.y, _mode == Mode::kPark ? 2 : 3, _mode == Mode::kPark ? 2 : 3}, dot);
        }
    if (shows(Layer::kStaff))
        for (const Park::StaffMember &s : park.staff())
            mark(_layerMarks[kStaffDot], cellOf(s.x, s.z), 5);
    // The camera's place, a cross.
    const Spot c = placeOf(camera);
    if (inside(c)) {
        WindowKit::fill(screen, {c.x - 4, c.y, 9, 1}, kCameraMark);
        WindowKit::fill(screen, {c.x, c.y - 4, 1, 9}, kCameraMark);
    }
    // The layers' buttons, lit while on, each with its layer's mark.
    const std::array<const image::Image *, kLayers> icons{&_marks[0],
                                                          &_stars[2],
                                                          &_layerMarks[kDot],
                                                          &_layerMarks[kStaffDot],
                                                          &_layerMarks[kWrench],
                                                          &_layerMarks[kGuardGlow]};
    for (size_t l = 0; l < kLayers; ++l) {
        const ScreenRect r = layerRect(Layer(l));
        WindowKit::fill(screen, r, _layers[l] ? WindowKit::kChosenRow : kLayerOff);
        const ScreenRect in{r.x + 4, r.y + 4, r.w - 8, r.h - 8};
        if (l == size_t(Layer::kCoverage))
            drawBlended(screen, *icons[l], in, in);
        else
            drawScaled(screen, *icons[l], in);
    }
    kit.button(screen, Button::kOk, kMapOk, WindowKit::kPlain);
}

// The Overview Map.

namespace {

constexpr uint32_t kOverviewMapText = 853;
constexpr uint32_t kChallengesCompletedText = 491;
constexpr uint32_t kAreaOpenedText = 494;
constexpr uint32_t kAreaNotOpenedText = 495;
constexpr ScreenRect kOverviewWindow{150, 90, 340, 270};
constexpr ScreenRect kOverviewFrame{165, 128, 310, 172};
constexpr int kOverviewRow = 56;
constexpr ScreenRect kOverviewOk{452, 316, 28, 28};
constexpr Color kClosedWorld{150, 150, 150};

} // namespace

ScreenRect OverviewMapWindow::rowRect(size_t index) {
    return {kOverviewFrame.x + 4, kOverviewFrame.y + 4 + int(index) * kOverviewRow, kOverviewFrame.w - 8,
            kOverviewRow - 2};
}

ScreenRect OverviewMapWindow::okRect() {
    return kOverviewOk;
}

WindowResult OverviewMapWindow::handle(const Event &ev) {
    if (!_open || !WindowKit::isClick(ev))
        return WindowResult::kNone;
    if (kOverviewOk.contains(ev.mouseX, ev.mouseY)) {
        _open = false;
        return WindowResult::kClosed;
    }
    for (size_t i = 0; i < _worlds.size(); ++i)
        if (rowRect(i).contains(ev.mouseX, ev.mouseY) && _worlds[i].open && _worlds[i].theme != _here) {
            _chosen = _worlds[i].theme;
            _open = false;
            return WindowResult::kChose;
        }
    return WindowResult::kNone;
}

// The world the park's in marked as a chosen row is.
void OverviewMapWindow::draw(Surface &screen, const WindowKit &kit) const {
    if (!_open)
        return;
    kit.window(screen, kOverviewWindow, kit.ui(kOverviewMapText));
    kit.frame(screen, kOverviewFrame);
    for (size_t i = 0; i < _worlds.size(); ++i) {
        const World &w = _worlds[i];
        const ScreenRect r = rowRect(i);
        if (w.theme == _here)
            WindowKit::fill(screen, r, WindowKit::kChosenRow);
        const Color c = w.open ? Color{255, 255, 255} : kClosedWorld;
        kit.text(screen, r.x + 6, r.y + 10,
                 kit.ui(kChallengesCompletedText,
                        {{5, kit.worldName(w.theme)}, {22, std::to_string(w.won)}, {23, std::to_string(w.challenges)}}),
                 c);
        kit.text(screen, r.x + 6, r.y + 30, kit.ui(w.open ? kAreaOpenedText : kAreaNotOpenedText), c);
    }
    kit.button(screen, Button::kOk, kOverviewOk, WindowKit::kPlain);
}

// Activating a special ride, laid out by guess: the recording opens none.

namespace {

constexpr uint32_t kActivateRideText = 984;
constexpr uint32_t kActivateNoteText = 985;
constexpr uint32_t kOneTicketNeededText = 986;
constexpr uint32_t kTicketsNeededText = 987;
constexpr uint32_t kStaffTypeText = 982;
constexpr uint32_t kAbilityText = 697;
constexpr uint32_t kAssignedText = 983;
constexpr uint32_t kUndiscoveredText = 114;
constexpr uint32_t kUndiscoveredNoteText = 115;
constexpr uint32_t kSpecialRideText = 116;
constexpr ScreenRect kActivateWindow{90, 60, 460, 350};
constexpr ScreenRect kActivateNote{105, 98, 430, 62};
constexpr std::array<ScreenRect, 3> kActivateHeaders{{{105, 168, 110, 30}, {217, 168, 140, 30}, {359, 168, 176, 30}}};
constexpr ScreenRect kActivateList{105, 200, 430, 110};
constexpr ScreenRect kActivateFoot{105, 316, 430, 50};
constexpr ScreenRect kActivateOk{474, 372, 28, 28};
constexpr ScreenRect kActivateCancel{508, 374, 26, 26};

} // namespace

ScreenRect ActivateRideWindow::okRect() {
    return kActivateOk;
}
ScreenRect ActivateRideWindow::cancelRect() {
    return kActivateCancel;
}

WindowResult ActivateRideWindow::handle(const Event &ev, Park &park) {
    if (!_open || !WindowKit::isClick(ev))
        return WindowResult::kNone;
    if (kActivateCancel.contains(ev.mouseX, ev.mouseY)) {
        _open = false;
        return WindowResult::kClosed;
    }
    if (!kActivateOk.contains(ev.mouseX, ev.mouseY) || park.activate(_object) != Park::Activation::kActivated)
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kChose;
}

void ActivateRideWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const {
    if (!_open || _object >= park.objects().size())
        return;
    const Attractions::Attraction *a = park.attractions().find(park.objects()[_object].id);
    if (!a)
        return;
    kit.window(screen, kActivateWindow, kit.ui(kSpecialRideText, {{0, _name}}));
    kit.frame(screen, kActivateNote);
    kit.paragraph(screen, kActivateNote.x + 6, kActivateNote.y + 6, kActivateNote.w - 12,
                  kit.ui(kActivateRideText) + ": " + kit.ui(kActivateNoteText));
    for (auto [rect, text] :
         {std::pair{kActivateHeaders[0], kStaffTypeText}, std::pair{kActivateHeaders[1], kAbilityText},
          std::pair{kActivateHeaders[2], kAssignedText}})
        kit.header(screen, rect, kit.ui(text));
    kit.frame(screen, kActivateList);
    // A row for each member of staff needed: their kind, the ability, who's assigned.
    const std::vector<std::vector<int>> staff = park.activationStaff(_object);
    int y = kActivateList.y + 4;
    bool undiscovered = false;
    for (size_t i = 0; i < a->skills.size(); ++i) {
        const auto [ability, count] = a->skills[i];
        const std::optional<Park::StaffKind> kind = Park::abilityKind(ability);
        const bool known = park.abilityDiscovered(ability);
        undiscovered = undiscovered || !known;
        for (int n = 0; n < count; ++n) {
            if (y + WindowKit::kRowHeight > kActivateList.y + kActivateList.h)
                break;
            if (kind)
                kit.text(screen, kActivateHeaders[0].x + 4, y, kit.staffType(*kind));
            kit.text(screen, kActivateHeaders[1].x + 4, y, known ? kit.ability(ability) : kit.ui(kUndiscoveredText));
            if (size_t(n) < staff[i].size())
                if (const Park::StaffMember *m = park.staffMember(staff[i][size_t(n)]))
                    kit.text(screen, kActivateHeaders[2].x + 4, y, names.nameOf(m->kind, m->name));
            y += WindowKit::kRowHeight;
        }
    }
    kit.frame(screen, kActivateFoot);
    y = kActivateFoot.y + 6;
    if (a->ticketCost > 0)
        y = kit.paragraph(screen, kActivateFoot.x + 6, y, kActivateFoot.w - 12,
                          kit.ui(a->ticketCost == 1 ? kOneTicketNeededText : kTicketsNeededText,
                                 {{50, std::to_string(a->ticketCost)}, {51, std::to_string(park.goldenTickets())}}));
    if (undiscovered)
        kit.paragraph(screen, kActivateFoot.x + 6, y, kActivateFoot.w - 12, kit.ui(kUndiscoveredNoteText));
    kit.button(screen, Button::kOk, kActivateOk, WindowKit::kPlain);
    kit.button(screen, Button::kCancel, kActivateCancel, WindowKit::kPlain);
}

// The game's menu.

ScreenRect GameMenu::itemRect(size_t item) {
    return {120, kGameMenuTop + int(item) * kGameMenuStep, 400, kGameMenuHeight};
}

ScreenRect GameMenu::askRect(bool yes) {
    return yes ? kAskYes : kAskNo;
}

GameMenu::Choice GameMenu::handle(const Event &ev) {
    if (!_open)
        return Choice::kNone;
    if (_asking >= 0) {
        const bool no = (ev.type == EventType::kKeyDown && ev.key == key::kEscape) ||
                        (WindowKit::isClick(ev) && kAskNo.contains(ev.mouseX, ev.mouseY));
        if (no) {
            _asking = -1;
            return Choice::kNone;
        }
        if (!WindowKit::isClick(ev) || !kAskYes.contains(ev.mouseX, ev.mouseY))
            return Choice::kNone;
        const bool quit = _asking == int(kQuitItem);
        _asking = -1;
        _open = false;
        return quit ? Choice::kQuit : Choice::kNewPlayer;
    }
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
        _open = false;
        return Choice::kResume;
    }
    if (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown)
        return Choice::kNone;
    _over = -1;
    for (size_t i = 0; i < kGameMenuItems.size(); ++i)
        if (itemRect(i).contains(ev.mouseX, ev.mouseY))
            _over = int(i);
    if (!WindowKit::isClick(ev) || _over < 0 || !works(size_t(_over)))
        return Choice::kNone;
    if (_over == int(kQuitItem) || _over == int(kNewPlayerItem)) {
        _asking = _over;
        return Choice::kNone;
    }
    _open = false;
    return _over == int(kResumeItem) ? Choice::kResume
           : _over == int(kQuitItem) ? Choice::kQuit
           : _over == int(kLoadItem) ? Choice::kLoad
           : _over == int(kSaveItem) ? Choice::kSave
                                     : Choice::kOptions;
}

// The park darkened beneath it, as the recording's is.
void GameMenu::draw(Surface &screen, const WindowKit &kit) const {
    if (!_open)
        return;
    for (int y = 0; y < screen.h; ++y) {
        uint8_t *p = screen.row(y);
        for (int x = 0; x < screen.w * 4; ++x)
            if (x % 4 != 3)
                p[x] = uint8_t(p[x] / 2);
    }
    for (size_t i = 0; i < kGameMenuItems.size(); ++i) {
        const std::string label = kit.ui(kGameMenuItems[i]);
        const bool working = works(i);
        const ScreenRect r = itemRect(i);
        kit.menuText(screen, r.x + (r.w - kit.menuTextWidth(label)) / 2, r.y, label,
                     !working          ? kMenuGrey
                     : _over == int(i) ? kWhite
                                       : kMenuCyan);
    }
    if (_asking < 0)
        return;
    kit.frame(screen, kAskBox);
    kit.paragraph(screen, kAskBox.x + 16, kAskBox.y + 16, kAskBox.w - 32,
                  kit.ui(_asking == int(kQuitItem) ? kQuitQuestion : kNewPlayerQuestion));
    kit.button(screen, WindowKit::Button::kOk, kAskYes, WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kCancel, kAskNo, WindowKit::kPlain);
}

// Save Game and Load Game.

namespace {

constexpr uint32_t kSaveGameText = 777;
constexpr uint32_t kLoadGameText = 778;
constexpr uint32_t kSaveNameText = 779;
constexpr uint32_t kSaveDateText = 780;
constexpr uint32_t kFileExistsText = 781;
constexpr uint32_t kNewSaveText = 782;
constexpr uint32_t kDeleteSaveTitle = 4;
constexpr uint32_t kDeleteSaveText = 5;
constexpr ScreenRect kSaveWindow{120, 70, 400, 330};
constexpr std::array<ScreenRect, 2> kSaveHeaders{{{135, 106, 250, 22}, {387, 106, 118, 22}}};
constexpr ScreenRect kSaveList{135, 130, 370, 180};
constexpr int kSaveRow = 16;
constexpr ScreenRect kSaveField{135, 318, 300, 30};
constexpr ScreenRect kSaveOk{448, 360, 28, 28};
constexpr ScreenRect kSaveCancel{482, 362, 26, 26};
constexpr ScreenRect kSaveDelete{135, 360, 28, 28};
constexpr size_t kLongestSaveName = 30;
constexpr size_t kSaveRowsShown = size_t(kSaveList.h / kSaveRow);

} // namespace

ScreenRect SaveLoadWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kRow:
        return {kSaveList.x + 4, kSaveList.y + 4 + int(index) * kSaveRow, kSaveList.w - 8, kSaveRow};
    case Control::kOk:
        return kSaveOk;
    case Control::kCancel:
        return kSaveCancel;
    case Control::kDelete:
        return kSaveDelete;
    case Control::kNone:
        break;
    }
    return {};
}

WindowResult SaveLoadWindow::handle(const Event &ev) {
    if (!_open)
        return WindowResult::kNone;
    if (_naming && ev.type == EventType::kTextInput) {
        for (char c : std::string_view(ev.text))
            if (_typed.size() < kLongestSaveName && static_cast<unsigned char>(c) >= ' ')
                _typed.push_back(c);
        return WindowResult::kNone;
    }
    if (_naming && ev.type == EventType::kKeyDown && ev.key == key::kBackspace) {
        while (!_typed.empty() && (static_cast<unsigned char>(_typed.back()) & 0xC0) == 0x80)
            _typed.pop_back();
        if (!_typed.empty())
            _typed.pop_back();
        return WindowResult::kNone;
    }
    const bool ok = (ev.type == EventType::kKeyDown && ev.key == key::kReturn) ||
                    (WindowKit::isClick(ev) && kSaveOk.contains(ev.mouseX, ev.mouseY));
    if (WindowKit::isClick(ev) && kSaveCancel.contains(ev.mouseX, ev.mouseY)) {
        // Out of naming or asking, else out of the window.
        if (_naming || _confirming || _deleting) {
            _naming = _confirming = _deleting = false;
            return WindowResult::kNone;
        }
        _open = false;
        return WindowResult::kClosed;
    }
    // A save chosen, the bin asks whether to delete it; the tick does.
    if (WindowKit::isClick(ev) && kSaveDelete.contains(ev.mouseX, ev.mouseY) && !_naming && !_confirming) {
        _deleting = _picked && _chosen >= 0;
        return WindowResult::kNone;
    }
    if (ok && _deleting) {
        _deleting = false;
        _toDelete = _chosen;
        return WindowResult::kNone;
    }
    if (ok) {
        const bool chose = _mode == Mode::kLoad ? _picked && _chosen >= 0 : _naming ? !_typed.empty() : _confirming;
        if (chose) {
            _open = false;
            return WindowResult::kChose;
        }
        // Saving: New Save goes on to its name, an old one to "replace it?".
        if (_mode == Mode::kSave && _picked) {
            _naming = _chosen < 0;
            _confirming = _chosen >= 0;
        }
        return WindowResult::kNone;
    }
    if (!WindowKit::isClick(ev) || _naming || _confirming || _deleting)
        return WindowResult::kNone;
    const size_t first = _mode == Mode::kSave ? 1 : 0;
    for (size_t i = 0; i < std::min(_rows.size() + first, kSaveRowsShown); ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _chosen = int(i) - int(first);
            _picked = true;
        }
    return WindowResult::kNone;
}

void SaveLoadWindow::draw(Surface &screen, const WindowKit &kit) const {
    if (!_open)
        return;
    kit.window(screen, kSaveWindow, kit.ui(_mode == Mode::kSave ? kSaveGameText : kLoadGameText));
    kit.header(screen, kSaveHeaders[0], kit.ui(kSaveNameText));
    kit.header(screen, kSaveHeaders[1], kit.ui(kSaveDateText));
    kit.frame(screen, kSaveList);
    const size_t first = _mode == Mode::kSave ? 1 : 0;
    for (size_t i = 0; i < std::min(_rows.size() + first, kSaveRowsShown); ++i) {
        const ScreenRect r = placeOf(Control::kRow, i);
        if (_picked && int(i) - int(first) == _chosen)
            WindowKit::fill(screen, r, WindowKit::kChosenRow);
        if (i < first) {
            kit.text(screen, r.x + 4, r.y + 2, kit.ui(kNewSaveText));
            continue;
        }
        const Row &row = _rows[i - first];
        kit.text(screen, r.x + 4, r.y + 2, row.name);
        kit.text(screen, kSaveHeaders[1].x + 2, r.y + 2, row.date);
    }
    if (_naming) {
        kit.frame(screen, kSaveField);
        kit.text(screen, kSaveField.x + 6, kSaveField.y + 9, _typed + "_");
    } else if (_confirming && _chosen >= 0) {
        kit.paragraph(screen, kSaveField.x, kSaveField.y - 4, kSaveField.w,
                      kit.ui(kFileExistsText, {{31, _rows[size_t(_chosen)].name}}));
    } else if (_deleting) {
        kit.paragraph(screen, kSaveField.x, kSaveField.y - 4, kSaveField.w,
                      kit.ui(kDeleteSaveTitle) + ": " + kit.ui(kDeleteSaveText));
    }
    kit.piece(screen, "b_delete", WindowKit::kPlain, kSaveDelete);
    kit.button(screen, Button::kOk, kSaveOk, WindowKit::kPlain);
    kit.button(screen, Button::kCancel, kSaveCancel, WindowKit::kPlain);
}

// Options.

namespace {

constexpr uint32_t kOptionsText = 783;
constexpr uint32_t kGraphicsTabText = 784; // then Sound, Game
constexpr uint32_t kRenderingText = 787;
constexpr uint32_t kSoftwareText = 789;
constexpr uint32_t kResolutionText = 793;
constexpr uint32_t k640Text = 795;
constexpr uint32_t kQualityText = 800;
constexpr uint32_t kHighText = 803;
constexpr uint32_t kSoundEffectsText = 804; // then Speech, Music
constexpr uint32_t kAudioQualityText = 807;
constexpr uint32_t kVolumeText = 808;
constexpr uint32_t kGeneralText = 812;
constexpr uint32_t kAdvisorText = 813;
constexpr uint32_t kGameSpeedText = 818;
constexpr uint32_t kCameraRotationText = 820;
constexpr uint32_t kSmoothText = 821;
constexpr uint32_t kCameraScrollingText = 823;
constexpr uint32_t kPushScrollText = 825;
// Tooltips, Confirmations, Animate Menus, Auto-rotate and Right Mouse
// Button Cancels: not followed.
constexpr std::array<uint32_t, 5> kUnfollowed{814, 815, 816, 817, 819};
constexpr ScreenRect kOptionsWindow{110, 60, 420, 360};
constexpr ScreenRect kOptionsPage{125, 126, 390, 240};
constexpr ScreenRect kOptionsOk{482, 378, 28, 28};
constexpr int kOptionsTabX = 125;
constexpr int kOptionsTabY = 98;
constexpr int kOptionsTabW = 128;
constexpr int kOptionsRow = 30;
constexpr int kBarX = 290;
constexpr int kBarW = 150;
constexpr int kOptionStep = 10;
constexpr Color kOptionGrey{150, 150, 150};
// i_boxtick's meshes: an empty box, a ticked one (a starred one besides).
constexpr size_t kEmptyBox = 0;
constexpr size_t kTickedBox = 1;

// A bar's row's top: the volumes' from the page's top, the speed's below the game's ticks.
int barY(size_t row) {
    return row == OptionsWindow::kSpeedRow ? kOptionsPage.y + 10 + 7 * 22
                                           : kOptionsPage.y + 10 + int(row) * kOptionsRow;
}

} // namespace

ScreenRect OptionsWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kTab:
        return {kOptionsTabX + int(index) * (kOptionsTabW + 2), kOptionsTabY, kOptionsTabW, 24};
    case Control::kMinus:
        return {kBarX + kBarW + 8, barY(index) - 4, 22, 22};
    case Control::kPlus:
        return {kBarX + kBarW + 34, barY(index) - 4, 22, 22};
    case Control::kAdvisor:
        return {kOptionsPage.x + 12, kOptionsPage.y + 32, 16, 16};
    case Control::kOk:
        return kOptionsOk;
    case Control::kNone:
        break;
    }
    return {};
}

WindowResult OptionsWindow::handle(const Event &ev) {
    if (!_open || !WindowKit::isClick(ev))
        return WindowResult::kNone;
    const auto at = [&](Control c, size_t i = 0) { return placeOf(c, i).contains(ev.mouseX, ev.mouseY); };
    if (at(Control::kOk)) {
        _open = false;
        return WindowResult::kClosed;
    }
    for (size_t t = 0; t < 3; ++t)
        if (at(Control::kTab, t))
            _tab = Tab(t);
    auto step = [&](int &value, int least, int by) {
        const int was = value;
        value = std::clamp(value + by, least, 100);
        return value != was ? WindowResult::kChose : WindowResult::kNone;
    };
    if (_tab == Tab::kSound)
        for (size_t row = 0; row < _options.volume.size(); ++row) {
            if (at(Control::kMinus, row))
                return step(_options.volume[row], 0, -kOptionStep);
            if (at(Control::kPlus, row))
                return step(_options.volume[row], 0, kOptionStep);
        }
    if (_tab == Tab::kGame) {
        if (at(Control::kMinus, kSpeedRow))
            return step(_options.gameSpeed, kOptionStep, -kOptionStep);
        if (at(Control::kPlus, kSpeedRow))
            return step(_options.gameSpeed, kOptionStep, kOptionStep);
        if (at(Control::kAdvisor)) {
            _options.advisor = !_options.advisor;
            return WindowResult::kChose;
        }
    }
    return WindowResult::kNone;
}

void OptionsWindow::draw(Surface &screen, const WindowKit &kit) const {
    if (!_open)
        return;
    kit.window(screen, kOptionsWindow, kit.ui(kOptionsText));
    for (size_t t = 0; t < 3; ++t)
        kit.header(screen, placeOf(Control::kTab, t), kit.ui(kGraphicsTabText + uint32_t(t)));
    kit.frame(screen, kOptionsPage);
    const int x = kOptionsPage.x + 12;
    // A chosen setting ticked; one Reluva doesn't follow, grey.
    const auto tick = [&](int tx, int ty, bool on, const std::string &label, bool followed) {
        // The empty box is dark on the page's black: outlined, to be seen.
        for (const ScreenRect &edge : {ScreenRect{tx, ty, 16, 1}, ScreenRect{tx, ty + 15, 16, 1},
                                       ScreenRect{tx, ty, 1, 16}, ScreenRect{tx + 15, ty, 1, 16}})
            WindowKit::fill(screen, edge, kOptionGrey);
        kit.piece(screen, "i_boxtick", on ? kTickedBox : kEmptyBox, {tx, ty, 16, 16});
        kit.text(screen, tx + 24, ty + 2, label, followed ? Color{255, 255, 255} : kOptionGrey);
    };
    const auto bar = [&](size_t row, uint32_t label, int value) {
        const int y = barY(row);
        kit.text(screen, x, y, kit.ui(label));
        kit.text(screen, kBarX - 60, y, kit.ui(kVolumeText));
        kit.bar(screen, kBarX, y + 1, kBarW, float(value) / 100);
        kit.button(screen, Button::kMinus, placeOf(Control::kMinus, row), WindowKit::kPlain);
        kit.button(screen, Button::kPlus, placeOf(Control::kPlus, row), WindowKit::kPlain);
    };
    int y = kOptionsPage.y + 10;
    switch (_tab) {
    case Tab::kGraphics:
        for (const auto &[heading, choice] :
             {std::pair{kRenderingText, kSoftwareText}, {kResolutionText, k640Text}, {kQualityText, kHighText}}) {
            kit.text(screen, x, y, kit.ui(heading), kOptionGrey);
            tick(x + 160, y - 2, true, kit.ui(choice), false);
            y += kOptionsRow;
        }
        break;
    case Tab::kSound:
        for (size_t row = 0; row < _options.volume.size(); ++row)
            bar(row, kSoundEffectsText + uint32_t(row), _options.volume[row]);
        kit.text(screen, x, barY(3), kit.ui(kAudioQualityText), kOptionGrey);
        break;
    case Tab::kGame: {
        kit.text(screen, x, y, kit.ui(kGeneralText));
        y += 22;
        tick(x, y, _options.advisor, kit.ui(kAdvisorText), true);
        for (const uint32_t text : kUnfollowed) {
            y += 22;
            tick(x, y, false, kit.ui(text), false);
        }
        const int speedY = barY(kSpeedRow);
        kit.text(screen, x, speedY, kit.ui(kGameSpeedText));
        kit.bar(screen, kBarX, speedY + 1, kBarW, float(_options.gameSpeed) / 100);
        kit.button(screen, Button::kMinus, placeOf(Control::kMinus, kSpeedRow), WindowKit::kPlain);
        kit.button(screen, Button::kPlus, placeOf(Control::kPlus, kSpeedRow), WindowKit::kPlain);
        kit.text(screen, x, speedY + 26, kit.ui(kCameraRotationText) + ": " + kit.ui(kSmoothText), kOptionGrey);
        kit.text(screen, x + 190, speedY + 26, kit.ui(kCameraScrollingText) + ": " + kit.ui(kPushScrollText),
                 kOptionGrey);
        break;
    }
    }
    kit.button(screen, Button::kOk, kOptionsOk, WindowKit::kPlain);
}

} // namespace reluva::tpw
