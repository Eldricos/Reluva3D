// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park's smaller windows. The attraction's is measured on the
// recording's at 11:18, its menu on the one at 11:16, at 640 × 480; the
// others, which the recording doesn't open, are laid out in their style
// (docs/engines/tpw/games/simcoaster/systems/staff.md#the-windows).

#include "engines/tpw/ui/park_windows.h"

#include "engines/tpw/game_files.h"
#include "engines/tpw/textures.h"
#include "engines/tpw/ui/sprites.h"
#include "engines/tpw/ui/text.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace reluva::tpw {

namespace {

constexpr Color kWhite{255, 255, 255};
constexpr Color kChosenRow{106, 125, 150};
constexpr Color kMenuTitle{236, 253, 255};
constexpr Color kMenuItem{166, 193, 218};
constexpr Color kMenuItemLit{255, 255, 255};

constexpr std::array<const char *, 10> kButtonModels{
    "ui/b_minus.MD2", "ui/b_plus.MD2",    "ui/b_door.MD2",  "ui/b_fire.MD2",   "ui/b_train.MD2",
    "ui/b_okay.MD2",  "ui/mb_cancel.MD2", "ui/b_close.MD2", "ui/b_arleft.MD2", "ui/b_arright.MD2"};
constexpr std::array<const char *, Park::kStaffKinds> kTabModels{"ui/tab_sengnr.MD2",    "ui/tab_scleaner.MD2",
                                                                 "ui/tab_sgardenr.MD2",  "ui/tab_sguard.MD2",
                                                                 "ui/tab_sentertnr.MD2", "ui/tab_sscientst.MD2"};

// UITEXT's strings.
constexpr uint32_t kUsersText = 117;
constexpr uint32_t kExcitementText = 118;
constexpr uint32_t kReliabilityText = 119;
constexpr uint32_t kRepairText = 120;
constexpr uint32_t kRunningCostText = 123;
constexpr uint32_t kScrapText = 124;
constexpr uint32_t kCustomersText = 128;
constexpr uint32_t kEntryPriceText = 428;
constexpr uint32_t kSalePriceText = 135;
constexpr uint32_t kGamePriceText = 143;
constexpr uint32_t kParkEntryText = 189;
constexpr uint32_t kCapacityText = 148;
constexpr uint32_t kEmployedText = 190;
constexpr uint32_t kWageText = 191;
constexpr uint32_t kStatusText = 192;
constexpr uint32_t kExperienceText = 193;
constexpr uint32_t kEnergyText = 194;
constexpr uint32_t kHappinessText = 195;
constexpr uint32_t kMotivationText = 196;
constexpr uint32_t kAgencyText = 197;
constexpr uint32_t kFirstAgencyText = 110;
constexpr uint32_t kTrainingRoomText = 226;
constexpr std::array<uint32_t, 4> kTrainingColumns{227, 228, 229, 230};
constexpr uint32_t kNoneTrainingText = 231;
constexpr std::array<uint32_t, 4> kListColumns{234, 235, 236, 237};
constexpr uint32_t kAverageHappinessText = 238;
// "All Engineers" and the rest, by Park::StaffKind.
constexpr std::array<uint32_t, Park::kStaffKinds> kAllOfKindText{250, 245, 248, 246, 247, 249};
// Each kind's wage's label, by Park::StaffKind.
constexpr std::array<uint32_t, Park::kStaffKinds> kWageLabels{265, 266, 267, 268, 269, 270};
constexpr uint32_t kDemandTitle = 251;
constexpr uint32_t kOfferText = 252;
constexpr uint32_t kDemandText = 253;
constexpr uint32_t kResultTitle = 254;
constexpr uint32_t kAcceptedText = 255;
constexpr uint32_t kStaysText = 256;
constexpr uint32_t kRefusedText = 257;
constexpr uint32_t kAcceptDemandText = 258;
constexpr uint32_t kLetGoText = 259;
constexpr uint32_t kFinanceTitle = 260;
constexpr uint32_t kAdjustmentText = 261;
constexpr uint32_t kSetToText = 262; // then $ change, % change
constexpr uint32_t kNotExperiencedText = 232;
constexpr uint32_t kNoTrainingRoomText = 233;
constexpr uint32_t kStaffListTitle = 829;

// The right-click menu: its title's line, then a line an item.
constexpr int kMenuWidth = 150;
constexpr int kMenuHead = 36;
constexpr int kMenuLine = 16;
constexpr int kMenuIndent = 6;
constexpr int kMenuTitleY = 6;

// The attraction's window, as the recording's.
constexpr ScreenRect kAttractionWindow{90, 62, 515, 296};
constexpr int kTitleX = 118;
constexpr int kTitleY = 69;
constexpr ScreenRect kPreview{108, 104, 128, 122};
constexpr ScreenRect kStats{242, 104, 210, 135};
constexpr int kStatX = 250;
constexpr int kStatY = 112;
constexpr int kStatLine = 16;
constexpr int kValueX = 372;
constexpr int kBarWidth = 75;
constexpr ScreenRect kPriceBox{286, 250, 124, 50};
// A ride's sliders: speed, capacity and duration (UITEXT 932, 936, 938
// "Speed : {32}", "Capacity : {34}", "Duration : {35} secs", 937 for one
// second), green to the level's red line and red past it, the value marked
// white (guessed: the recording's window shows them only small).
constexpr ScreenRect kSideBox{462, 104, 133, 148};
constexpr int kSliderX = 470;
constexpr int kSliderLabelY = 110;
constexpr int kSliderStep = 40;
constexpr int kSliderBarY = 126;
constexpr int kSliderWidth = 117;
constexpr int kSliderHeight = 10;
constexpr int kSliderReach = 5; // above and below the bar, where a click still sets it
constexpr int kUpgradeLineY = 232;
constexpr uint32_t kSpeedText = 932;
constexpr uint32_t kCapacityLineText = 936;
constexpr uint32_t kSecondText = 937;
constexpr uint32_t kSecondsText = 938;
constexpr uint32_t kGreenZoneTip = 274;
constexpr uint32_t kRedZoneTip = 275;
constexpr Color kSliderGreen{40, 170, 60};
constexpr Color kSliderRed{190, 40, 30};
constexpr Color kSliderMark{255, 255, 255};

ScreenRect sliderBar(size_t setting) {
    return {kSliderX, kSliderBarY + int(setting) * kSliderStep, kSliderWidth, kSliderHeight};
}
constexpr uint32_t kNotUpgradedText = 37;
constexpr uint32_t kUpgradedText = 38;
constexpr ScreenRect kAttractionMinus{290, 257, 18, 18};
constexpr ScreenRect kAttractionPlus{388, 257, 18, 18};
constexpr ScreenRect kAttractionDoor{128, 306, 36, 36};
constexpr ScreenRect kAttractionOk{526, 316, 28, 28};
constexpr ScreenRect kAttractionClose{560, 64, 26, 26};

// The gates' window, the staff member's and the rest: in the attraction
// window's style (guessed).
constexpr ScreenRect kGateWindow{170, 150, 300, 160};
constexpr ScreenRect kGatePriceBox{258, 195, 124, 50};
constexpr ScreenRect kGateMinus{262, 202, 18, 18};
constexpr ScreenRect kGatePlus{360, 202, 18, 18};
constexpr ScreenRect kGateDoor{196, 262, 36, 36};
constexpr ScreenRect kGateOk{420, 268, 28, 28};
constexpr ScreenRect kGateClose{440, 154, 26, 26};

constexpr ScreenRect kStaffWindow{90, 62, 460, 290};
constexpr ScreenRect kStaffPreview{108, 104, 110, 120};
constexpr ScreenRect kStaffStats{232, 104, 300, 176};
constexpr int kStaffValueX = 360;
constexpr ScreenRect kStaffMinus{470, 129, 16, 16};
constexpr ScreenRect kStaffPlus{490, 129, 16, 16};
constexpr ScreenRect kStaffTrain{112, 290, 36, 36};
constexpr ScreenRect kStaffFire{160, 290, 36, 36};
constexpr ScreenRect kStaffOk{506, 312, 28, 28};
constexpr ScreenRect kStaffClose{520, 66, 26, 26};

constexpr ScreenRect kListWindow{70, 62, 500, 330};
constexpr int kListTabX = 100;
constexpr int kListTabY = 92;
constexpr int kTabWidth = 42;
constexpr int kTabHeight = 48;
constexpr std::array<ScreenRect, 4> kListColumnRects{
    {{94, 146, 140, 30}, {238, 146, 90, 30}, {332, 146, 110, 30}, {446, 146, 110, 30}}};
constexpr ScreenRect kListFrame{90, 180, 470, 160};
constexpr int kListRowY = 183;
constexpr int kRowHeight = 13;
constexpr int kLineHeight = 12; // a paragraph's lines
constexpr ScreenRect kListOk{506, 352, 28, 28};
constexpr ScreenRect kListClose{540, 66, 26, 26};

constexpr ScreenRect kTrainingWindow{70, 100, 500, 250};
constexpr std::array<ScreenRect, 4> kTrainingColumnRects{
    {{94, 136, 140, 30}, {238, 136, 90, 30}, {332, 136, 110, 30}, {446, 136, 110, 30}}};
constexpr ScreenRect kTrainingFrame{90, 170, 470, 130};
constexpr ScreenRect kTrainingOk{506, 310, 28, 28};

constexpr ScreenRect kFinanceWindow{120, 90, 400, 280};
constexpr int kFinanceTabX = 150;
constexpr int kFinanceTabY = 120;
constexpr ScreenRect kFinanceLeft{160, 230, 20, 20};
constexpr ScreenRect kFinanceRight{290, 230, 20, 20};
constexpr ScreenRect kFinanceMinus{360, 230, 18, 18};
constexpr ScreenRect kFinancePlus{450, 230, 18, 18};
constexpr ScreenRect kFinanceOk{456, 324, 28, 28};
constexpr ScreenRect kFinanceWageFrame{140, 178, 360, 30};
constexpr ScreenRect kFinanceAdjustFrame{140, 206, 360, 56};
constexpr ScreenRect kFinanceClose{490, 94, 26, 26};

constexpr ScreenRect kDemandWindow{130, 120, 380, 230};
constexpr ScreenRect kDemandTextFrame{144, 152, 352, 96};
constexpr ScreenRect kDemandOfferFrame{144, 252, 352, 90};
constexpr ScreenRect kDemandMinus{300, 262, 18, 18};
constexpr ScreenRect kDemandPlus{398, 262, 18, 18};
constexpr ScreenRect kDemandOk{466, 306, 28, 28};
constexpr ScreenRect kDemandAccept{150, 304, 170, 18};
constexpr ScreenRect kDemandLetGo{150, 324, 170, 18};

constexpr float kTurnPerSecond = 0.6f;
constexpr int kInset = 5;
constexpr uint32_t kSpriteTurnMillis = 400;

bool isClick(const Event &ev) {
    return WindowKit::isClick(ev);
}

// The control under the mouse, of those given.
template<typename Control, size_t N>
Control under(const Event &ev, const std::array<std::pair<Control, ScreenRect>, N> &controls) {
    for (const auto &[c, r] : controls)
        if (r.contains(ev.mouseX, ev.mouseY))
            return c;
    return Control::kNone;
}

void fill(Surface &s, const ScreenRect &r, Color c) {
    WindowKit::fill(s, r, c);
}

std::string money(int amount) {
    return WindowKit::money(amount);
}

// A price box: its label under, the amount between − and +.
void priceBox(Surface &screen, const WindowKit &kit, const ScreenRect &box, const ScreenRect &minus,
              const ScreenRect &plus, int amount, const std::string &label, bool minusLit, bool plusLit) {
    kit.frame(screen, box);
    kit.button(screen, WindowKit::Button::kMinus, minus, minusLit ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kPlus, plus, plusLit ? WindowKit::kLit : WindowKit::kPlain);
    const std::string value = money(amount);
    kit.text(screen, box.x + (box.w - kit.textWidth(value)) / 2, minus.y + 4, value);
    kit.text(screen, box.x + (box.w - kit.textWidth(label)) / 2, box.y + box.h - 14, label);
}

// The price's label by what it is.
uint32_t priceLabel(const Attractions::Attraction &a) {
    switch (a.kind) {
    case Attractions::Kind::kShop:
        return kSalePriceText;
    case Attractions::Kind::kSideshow:
        return kGamePriceText;
    default:
        return kEntryPriceText;
    }
}

} // namespace

Error WindowKit::load(const SearchSet &files) {
    for (auto [model, name] : {std::pair{&_window, "ui/!window_s.MD2"}, std::pair{&_frame, "ui/!frame.MD2"},
                               std::pair{&_header, "ui/!cbut.MD2"}, std::pair{&_panel, "ui/!f_plain.MD2"}})
        if (Error err = model->load(files, name))
            return err;
    for (size_t i = 0; i < kButtonModels.size(); ++i)
        if (Error err = _buttons[i].load(files, kButtonModels[i]))
            return err;
    for (size_t i = 0; i < kTabModels.size(); ++i)
        if (Error err = _tabs[i].load(files, kTabModels[i]))
            return err;
    if (Error err = loadPicture(files, "ui/textures/ridestatbar.fsh", _bar))
        return err;
    const std::string language = languageOf(files);
    if (language.empty())
        return Error::fail("no language's folder (Language/<language>/MBToUni.dat)");
    const std::string folder = "Language/" + language + "/";
    std::vector<uint8_t> data;
    if (Error err = files.read(folder + "MBToUni.dat", data))
        return err;
    if (Error err = _characters.parse(data))
        return err;
    for (auto [font, name] : {std::pair{&_titleFont, "TITLEMED"}, std::pair{&_font, "GAME7AA"}})
        if (Error err = readGameFont(files, folder, name, *font))
            return err;
    if (Error err = readStringTable(files, folder, "UITEXT", _characters, _ui))
        return err;
    if (Error err = readStringTable(files, folder, "MONTHS", _characters, _months))
        return err;
    if (Error err = readStringTable(files, folder, "STAFF_TYPES", _characters, _staffTypes))
        return err;
    if (Error err = readStringTable(files, folder, "STAFFSTATES", _characters, _staffStates))
        return err;
    if (Error err = readStringTable(files, folder, "TAG_SYSTEM", _characters, _tags))
        return err;
    if (Error err = readStringTable(files, folder, "LOANNAMES", _characters, _lenders))
        return err;
    if (Error err = readStringTable(files, folder, "PROMOTIONLEVELS", _characters, _ranks))
        return err;
    if (Error err = readStringTable(files, folder, "OBJECTIVE_COMPLETE", _characters, _objectiveLetters))
        return err;
    if (Error err = readStringTable(files, folder, "KIDSTATES", _characters, _kidStates))
        return err;
    if (Error err = readStringTable(files, folder, "THEMENAMES", _characters, _worlds))
        return err;
    if (Error err = readStringTable(files, folder, "CHALLENGE_TITLES", _characters, _challengeTitles))
        return err;
    if (Error err = readStringTable(files, folder, "ABILITIES", _characters, _abilities))
        return err;
    if (Error err = readGameFont(files, folder, "MENUBIG", _menuFont))
        return err;
    // The other windows' tabs and buttons.
    for (const char *name : {"tab_vall", "tab_vchild", "tab_vadult", "tab_vsenior", "tab_arides", "tab_asshows",
                             "tab_ashops", "tab_afeatures", "tab_afacility", "b_goto", "b_delete", "i_boxtick"})
        if (Error err = _pieces[name].load(files, std::string("ui/") + name + ".MD2"))
            return err;
    return readStringTable(files, folder, "THOUGHTS", _characters, _thoughts);
}

bool WindowKit::isClick(const Event &ev) {
    return ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft;
}

void WindowKit::fill(Surface &screen, const ScreenRect &r, Color c) {
    for (int y = std::max(0, r.y); y < std::min(screen.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(screen.w, r.x + r.w); ++x) {
            uint8_t *p = screen.row(y) + x * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
        }
}

// Below nothing, the minus before the sign, as "Cash  -$ {39}" (967) has it.
std::string WindowKit::money(int amount) {
    return (amount < 0 ? "-$ " : "$ ") + std::to_string(std::abs(amount));
}

std::string WindowKit::kidState(uint32_t index, const std::map<int, std::string> &values) const {
    return fillGameString(_kidStates, index, values);
}

void WindowKit::piece(Surface &screen, const std::string &name, size_t mesh, const ScreenRect &r) const {
    if (const auto p = _pieces.find(name); p != _pieces.end())
        drawUiMesh(screen, p->second, mesh, r);
}

void WindowKit::menuText(Surface &screen, int x, int y, const std::string &s, Color c) const {
    drawGameText(screen, _menuFont, _characters, x, y, s, c);
}

int WindowKit::menuTextWidth(const std::string &s) const {
    return gameTextWidth(_menuFont, _characters, s);
}

// A promotion's rank fills its {4} ("Promotion to {4}").
std::string WindowKit::messageTitle(const Park::Message &m) const {
    std::map<int, std::string> values = m.values;
    if (m.rank > 0)
        values[4] = rank(m.rank);
    return tag(m.title, values);
}

// A challenge's letters end with its title: "...select the challenge called:".
std::vector<std::string> WindowKit::messageText(const Park::Message &m) const {
    std::map<int, std::string> values = m.values;
    if (m.rank > 0)
        values[4] = rank(m.rank);
    std::string text = m.objectiveLetter ? objectiveLetter(int(m.text)) : tag(m.text, values);
    while (!text.empty() && text.back() == '\n')
        text.pop_back();
    std::vector<std::string> out{text};
    for (const Park::Message::Line &line : m.lines)
        out.push_back(tag(line.text, line.values) + (line.suffix ? tag(line.suffix, line.values) : ""));
    if (m.challenge > 0)
        out.push_back(challengeTitle(m.challenge));
    return out;
}

std::string WindowKit::tag(uint32_t index, const std::map<int, std::string> &values) const {
    return fillGameString(_tags, index, values);
}

void WindowKit::window(Surface &screen, const ScreenRect &r, const std::string &title) const {
    drawUiMesh(screen, _window, 0, r);
    drawGameText(screen, _titleFont, _characters, r.x + kTitleX - kAttractionWindow.x,
                 r.y + kTitleY - kAttractionWindow.y, title, kWhite);
}

void WindowKit::frame(Surface &screen, const ScreenRect &r) const {
    drawUiMesh(screen, _frame, 0, r);
}

void WindowKit::panel(Surface &screen, const ScreenRect &r) const {
    drawUiMesh(screen, _panel, 0, r);
}

void WindowKit::title(Surface &screen, int x, int y, const std::string &s, Color c) const {
    drawGameText(screen, _titleFont, _characters, x, y, s, c);
}

std::string WindowKit::staffType(Park::StaffKind kind) const {
    // STAFF_TYPES.str: each kind's name, then its plural; janitors first.
    static constexpr std::array<size_t, Park::kStaffKinds> kTypes{2, 0, 10, 6, 4, 8};
    return _staffTypes.text(kTypes[size_t(kind)]);
}

std::string WindowKit::staffState(const Park::StaffMember &m) const {
    // STAFFSTATES.str: idle, looking for work, working, resting, on strike,
    // picked up, -, researching, training.
    size_t state = 1;
    switch (m.doing) {
    case Park::StaffDoing::kWorking:
        state = m.working || m.target >= 0 || m.chasing ? 2 : 1;
        break;
    // At the obstacle, working; STAFFSTATES has no line of its own for it.
    case Park::StaffDoing::kGoingToClear:
    case Park::StaffDoing::kClearing:
        state = 2;
        break;
    case Park::StaffDoing::kGoingToRest:
    case Park::StaffDoing::kResting:
        state = 3;
        break;
    case Park::StaffDoing::kStriking:
        state = 4;
        break;
    case Park::StaffDoing::kGoingToResearch:
    case Park::StaffDoing::kResearching:
        state = 7;
        break;
    case Park::StaffDoing::kGoingToTrain:
    case Park::StaffDoing::kTraining:
        state = 8;
        break;
    }
    return _staffStates.text(state);
}

std::string WindowKit::date(uint32_t day) const {
    const Park::Date d = Park::dateOf(day);
    const std::string month =
        _months.size() > size_t(d.month) ? _months.text(size_t(d.month)) : std::to_string(d.month + 1);
    return std::to_string(d.day) + "." + month + "." + (d.year % 100 < 10 ? "0" : "") + std::to_string(d.year % 100);
}

void WindowKit::text(Surface &screen, int x, int y, const std::string &s, Color c) const {
    drawGameText(screen, _font, _characters, x, y, s, c);
}

int WindowKit::textWidth(const std::string &s) const {
    return gameTextWidth(_font, _characters, s);
}

int WindowKit::paragraph(Surface &screen, int x, int y, int width, const std::string &s) const {
    size_t start = 0;
    while (start <= s.size()) {
        const size_t end = std::min(s.find('\n', start), s.size());
        std::string line;
        size_t word = start;
        while (word < end) {
            size_t next = s.find(' ', word);
            next = next == std::string::npos || next > end ? end : next;
            const std::string with =
                line.empty() ? s.substr(word, next - word) : line + " " + s.substr(word, next - word);
            if (!line.empty() && textWidth(with) > width) {
                text(screen, x, y, line);
                y += kLineHeight;
                line = s.substr(word, next - word);
            } else {
                line = with;
            }
            word = next + 1;
        }
        text(screen, x, y, line);
        y += kLineHeight;
        start = end + 1;
    }
    return y;
}

void WindowKit::bar(Surface &screen, int x, int y, int width, float level) const {
    constexpr int kHeight = 8;
    const int filled = int(std::lround(std::clamp(level, 0.0f, 1.0f) * float(width)));
    for (int dy = 0; dy < kHeight; ++dy)
        for (int dx = 0; dx < width; ++dx) {
            // The picture's top half the fill, red to green; its bottom the empty.
            const uint8_t *src = _bar.pixel(dx * _bar.w / width, dx < filled ? _bar.h / 4 : 3 * _bar.h / 4);
            if (x + dx < 0 || y + dy < 0 || x + dx >= screen.w || y + dy >= screen.h)
                continue;
            std::copy(src, src + 3, screen.row(y + dy) + (x + dx) * 4);
        }
}

// UIHELPTEXT's for each Button: OK "Ok", cancel "Cancel", close
// "Close", the arrows "Previous" and "Next", fire "Fire employee"; the
// others' depend on their window.
constexpr uint32_t kNoTip = UINT32_MAX;
constexpr std::array<uint32_t, 10> kButtonTips{kNoTip, kNoTip, kNoTip, 14, kNoTip, 0, 3, 1, 5, 4};
// The staff tabs': "Show engineers" to "Show scientists", by kind.
constexpr uint32_t kStaffTabTips = 57;

void WindowKit::button(Surface &screen, Button b, const ScreenRect &r, size_t state) const {
    drawUiMesh(screen, _buttons[size_t(b)], state, r);
    if (kButtonTips[size_t(b)] != kNoTip)
        _tips.emplace_back(r, kButtonTips[size_t(b)]);
}

std::optional<uint32_t> WindowKit::tipAt(int x, int y) const {
    for (auto it = _tips.rbegin(); it != _tips.rend(); ++it)
        if (it->first.contains(x, y))
            return it->second;
    return std::nullopt;
}

// Its label centred, a line at a time where it breaks ("Users\nLast Month").
void WindowKit::header(Surface &screen, const ScreenRect &r, const std::string &label) const {
    drawUiMesh(screen, _header, 0, r);
    std::vector<std::string> lines;
    for (size_t at = 0;;) {
        const size_t end = label.find('\n', at);
        lines.push_back(label.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos)
            break;
        at = end + 1;
    }
    constexpr int kLine = 10;
    int y = r.y + r.h / 2 - 4 - (int(lines.size()) - 1) * kLine / 2;
    for (const std::string &line : lines) {
        text(screen, r.x + (r.w - textWidth(line)) / 2, y, line);
        y += kLine;
    }
}

void WindowKit::staffTab(Surface &screen, Park::StaffKind kind, const ScreenRect &r, size_t state) const {
    drawUiMesh(screen, _tabs[size_t(kind)], state, r);
    _tips.emplace_back(r, kStaffTabTips + uint32_t(kind));
}

std::string WindowKit::ui(uint32_t index, const std::map<int, std::string> &values) const {
    return fillGameString(_ui, index, values);
}

// The menu.

void PopupMenu::open(std::string title, std::vector<uint32_t> items, int x, int y, std::map<int, std::string> values) {
    _open = true;
    _values = std::move(values);
    _title = std::move(title);
    _items = std::move(items);
    _x = x;
    _y = y;
    _lit = -1;
    _chosen = 0;
}

// At the point clicked, kept on the screen.
ScreenRect PopupMenu::panel() const {
    const int h = kMenuHead + kMenuLine * int(_items.size()) + kMenuLine / 2;
    return {std::clamp(_x, 0, 640 - kMenuWidth), std::clamp(_y, 0, 480 - h), kMenuWidth, h};
}

ScreenRect PopupMenu::itemRect(size_t item) const {
    const ScreenRect p = panel();
    return {p.x, p.y + kMenuHead + kMenuLine * int(item) - 2, p.w, kMenuLine};
}

WindowResult PopupMenu::handle(const Event &ev) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _lit = -1;
    for (size_t i = 0; i < _items.size(); ++i)
        if (itemRect(i).contains(ev.mouseX, ev.mouseY))
            _lit = int(i);
    if (ev.type != EventType::kMouseDown)
        return WindowResult::kNone;
    _open = false;
    if (_lit < 0 || ev.button != MouseButton::kLeft)
        return WindowResult::kClosed;
    _chosen = _items[size_t(_lit)];
    return WindowResult::kChose;
}

void PopupMenu::draw(Surface &screen, const WindowKit &kit) const {
    if (!_open)
        return;
    const ScreenRect p = panel();
    kit.panel(screen, p);
    kit.title(screen, p.x + kMenuIndent, p.y + kMenuTitleY, _title, kMenuTitle);
    for (size_t i = 0; i < _items.size(); ++i)
        kit.text(screen, p.x + kMenuIndent, itemRect(i).y + 2, kit.ui(_items[i], _values),
                 int(i) == _lit ? kMenuItemLit : kMenuItem);
}

// The attraction's window.

void AttractionWindow::open(const Park &park, size_t object, std::string name, const LoadedModel *model) {
    (void)park;
    _open = true;
    _object = object;
    _name = std::move(name);
    _model = model;
}

ScreenRect AttractionWindow::placeOf(Control control) {
    switch (control) {
    case Control::kMinus:
        return kAttractionMinus;
    case Control::kPlus:
        return kAttractionPlus;
    case Control::kDoor:
        return kAttractionDoor;
    case Control::kOk:
        return kAttractionOk;
    case Control::kClose:
        return kAttractionClose;
    case Control::kSpeed:
    case Control::kCapacity:
    case Control::kDuration: {
        const ScreenRect bar = sliderBar(size_t(control) - size_t(Control::kSpeed));
        return {bar.x, bar.y - kSliderReach, bar.w, bar.h + 2 * kSliderReach};
    }
    default:
        return {};
    }
}

WindowResult AttractionWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = under(ev, std::array{std::pair{Control::kMinus, kAttractionMinus},
                                 std::pair{Control::kPlus, kAttractionPlus}, std::pair{Control::kDoor, kAttractionDoor},
                                 std::pair{Control::kOk, kAttractionOk}, std::pair{Control::kClose, kAttractionClose}});
    if (!isClick(ev) || _object >= park.objects().size())
        return WindowResult::kNone;
    // A slider set where it's clicked, its least at its left.
    for (const Control c : {Control::kSpeed, Control::kCapacity, Control::kDuration}) {
        const auto setting = Park::RideSetting(size_t(c) - size_t(Control::kSpeed));
        const std::optional<Park::SettingRange> r = park.rideSetting(_object, setting);
        const ScreenRect at = placeOf(c);
        if (!r || !at.contains(ev.mouseX, ev.mouseY))
            continue;
        const float along = float(ev.mouseX - at.x) / float(std::max(1, at.w - 1));
        park.setRideSetting(_object, setting, r->least + int(std::lround(along * float(r->most - r->least))));
        return WindowResult::kNone;
    }
    const int queue = park.queueOf(_object);
    switch (_over) {
    case Control::kMinus:
    case Control::kPlus:
        if (queue >= 0)
            park.setFare(queue, park.queues()[size_t(queue)].fare + (_over == Control::kPlus ? 1 : -1));
        return WindowResult::kNone;
    case Control::kDoor:
        park.setOpen(_object, !park.objects()[_object].open);
        return WindowResult::kNone;
    case Control::kOk:
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void AttractionWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open || _object >= park.objects().size())
        return;
    const Park::PlacedObject &o = park.objects()[_object];
    const Attractions::Attraction *a = park.attractions().find(o.id);
    kit.window(screen, kAttractionWindow, _name);
    kit.button(screen, WindowKit::Button::kClose, kAttractionClose,
               _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    // Its model turning.
    kit.frame(screen, kPreview);
    if (_model) {
        const ScreenRect inner{kPreview.x + kInset, kPreview.y + kInset, kPreview.w - 2 * kInset,
                               kPreview.h - 2 * kInset};
        Surface view;
        view.create(inner.w, inner.h, PixelFormat::kRGBA32);
        ModelView mv;
        mv.yaw = float(_clock) / 1000 * kTurnPerSecond;
        drawModel(view, _rasterizer, *_model, mv);
        for (int y = 0; y < inner.h; ++y)
            for (int x = 0; x < inner.w; ++x) {
                const uint8_t *src = view.row(y) + x * 4;
                if (src[0] || src[1] || src[2])
                    std::copy(src, src + 3, screen.row(inner.y + y) + (inner.x + x) * 4);
            }
    }
    // Its ratings and accounts.
    kit.frame(screen, kStats);
    int y = kStatY;
    auto line = [&](uint32_t label, const std::string &value) {
        kit.text(screen, kStatX, y, kit.ui(label));
        kit.text(screen, kValueX, y, value);
        y += kStatLine;
    };
    auto rating = [&](uint32_t label, float level) {
        kit.text(screen, kStatX, y, kit.ui(label));
        kit.bar(screen, kValueX, y + 1, kBarWidth, level);
        y += kStatLine;
    };
    const bool ride = a && a->kind == Attractions::Kind::kRide;
    line(ride ? kUsersText : kCustomersText, std::to_string(o.lastUsers));
    if (const int excitement = park.excitementOf(_object); excitement >= 0)
        rating(kExcitementText, float(excitement) / 100);
    if (a && a->wearRate > 0) {
        rating(kReliabilityText, 1 - float(a->wearRate) / 10);
        rating(kRepairText, o.repair / 100);
    }
    const Attractions::Attraction::Level *level = park.levelOf(_object);
    line(kRunningCostText, money(level ? level->runningCost : 0));
    line(kScrapText, money(park.scrapValue(_object)));
    if (a && a->capacity >= 0) {
        kit.frame(screen, kSideBox);
        for (size_t s = 0; s < Park::kRideSettings; ++s) {
            const std::optional<Park::SettingRange> r = park.rideSetting(_object, Park::RideSetting(s));
            if (!r)
                continue;
            const std::string value = std::to_string(r->value);
            const std::string label = s == 0   ? kit.ui(kSpeedText, {{32, value + "%"}})
                                      : s == 1 ? kit.ui(kCapacityLineText, {{34, value}})
                                               : kit.ui(r->value == 1 ? kSecondText : kSecondsText, {{35, value}});
            kit.text(screen, kSliderX, kSliderLabelY + int(s) * kSliderStep, label);
            const ScreenRect bar = sliderBar(s);
            const auto at = [&](int v) {
                return bar.x + (r->most > r->least ? (v - r->least) * (bar.w - 1) / (r->most - r->least) : 0);
            };
            const int red = r->redLine > 0 ? std::clamp(at(r->redLine), bar.x, bar.x + bar.w) : bar.x + bar.w;
            fillRect(screen, {bar.x, bar.y, red - bar.x, bar.h}, kSliderGreen);
            fillRect(screen, {red, bar.y, bar.x + bar.w - red, bar.h}, kSliderRed);
            fillRect(screen, {at(r->value) - 1, bar.y - 2, 3, bar.h + 4}, kSliderMark);
            kit.addTip({bar.x, bar.y, red - bar.x, bar.h}, kGreenZoneTip);
            kit.addTip({red, bar.y, bar.x + bar.w - red, bar.h}, kRedZoneTip);
        }
        const std::string upgraded =
            o.level > 0 ? kit.ui(kUpgradedText, {{3, std::to_string(o.level)}}) : kit.ui(kNotUpgradedText);
        kit.text(screen, kSideBox.x + (kSideBox.w - kit.textWidth(upgraded)) / 2, kUpgradeLineY, upgraded);
    }
    // Its price, and the door: lit while it's open.
    const int queue = park.queueOf(_object);
    if (queue >= 0 && a)
        priceBox(screen, kit, kPriceBox, kAttractionMinus, kAttractionPlus, park.queues()[size_t(queue)].fare,
                 kit.ui(priceLabel(*a)), _over == Control::kMinus, _over == Control::kPlus);
    kit.button(screen, WindowKit::Button::kDoor, kAttractionDoor, o.open ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kOk, kAttractionOk,
               _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// The gates' window.

ScreenRect GateWindow::placeOf(Control control) {
    switch (control) {
    case Control::kMinus:
        return kGateMinus;
    case Control::kPlus:
        return kGatePlus;
    case Control::kDoor:
        return kGateDoor;
    case Control::kOk:
        return kGateOk;
    case Control::kClose:
        return kGateClose;
    default:
        return {};
    }
}

WindowResult GateWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = under(ev, std::array{std::pair{Control::kMinus, kGateMinus}, std::pair{Control::kPlus, kGatePlus},
                                 std::pair{Control::kDoor, kGateDoor}, std::pair{Control::kOk, kGateOk},
                                 std::pair{Control::kClose, kGateClose}});
    if (!isClick(ev))
        return WindowResult::kNone;
    switch (_over) {
    case Control::kMinus:
    case Control::kPlus:
        park.setAdmissionFee(park.admissionFee() + (_over == Control::kPlus ? 1 : -1));
        return WindowResult::kNone;
    case Control::kDoor:
        park.setParkOpen(!park.parkOpen());
        return WindowResult::kNone;
    case Control::kOk:
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void GateWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kGateWindow, kit.ui(kParkEntryText));
    kit.button(screen, WindowKit::Button::kClose, kGateClose,
               _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    priceBox(screen, kit, kGatePriceBox, kGateMinus, kGatePlus, park.admissionFee(), kit.ui(kParkEntryText),
             _over == Control::kMinus, _over == Control::kPlus);
    kit.button(screen, WindowKit::Button::kDoor, kGateDoor, park.parkOpen() ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kOk, kGateOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// A member of staff's window.

ScreenRect StaffWindow::placeOf(Control control) {
    switch (control) {
    case Control::kMinus:
        return kStaffMinus;
    case Control::kPlus:
        return kStaffPlus;
    case Control::kTrain:
        return kStaffTrain;
    case Control::kFire:
        return kStaffFire;
    case Control::kOk:
        return kStaffOk;
    case Control::kClose:
        return kStaffClose;
    default:
        return {};
    }
}

WindowResult StaffWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = under(ev, std::array{std::pair{Control::kMinus, kStaffMinus}, std::pair{Control::kPlus, kStaffPlus},
                                 std::pair{Control::kTrain, kStaffTrain}, std::pair{Control::kFire, kStaffFire},
                                 std::pair{Control::kOk, kStaffOk}, std::pair{Control::kClose, kStaffClose}});
    const Park::StaffMember *m = park.staffMember(_staff);
    if (!isClick(ev) || !m)
        return WindowResult::kNone;
    switch (_over) {
    case Control::kMinus:
    case Control::kPlus:
        park.setWage(_staff, m->wage + (_over == Control::kPlus ? 1 : -1));
        return WindowResult::kNone;
    case Control::kTrain:
        switch (park.trainingCheck(_staff)) {
        case Park::Training::kTooInexperienced:
            _message = kNotExperiencedText;
            break;
        case Park::Training::kNoRoom:
            _message = kNoTrainingRoomText;
            break;
        case Park::Training::kSent:
            _message = 0;
            _training = _staff;
            break;
        case Park::Training::kNotNow:
            _message = 0;
            break;
        }
        return WindowResult::kNone;
    case Control::kFire:
        park.fire(_staff);
        _open = false;
        return WindowResult::kClosed;
    case Control::kOk:
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void StaffWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const {
    const Park::StaffMember *m = park.staffMember(_staff);
    if (!_open || !m)
        return;
    kit.window(screen, kStaffWindow, names.nameOf(m->kind, m->name));
    kit.button(screen, WindowKit::Button::kClose, kStaffClose,
               _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    // Their sprite, turning.
    kit.frame(screen, kStaffPreview);
    if (const LoadedSprite *sprite = names.spriteOf(m->kind)) {
        const SpritePose pose =
            spritePose(sprite->set.animations()[0], float(_clock / kSpriteTurnMillis % 8) * 3.14159265f / 4, 0);
        drawSpriteFrame(screen, sprite->sheet, pose.frame, float(kStaffPreview.x + kStaffPreview.w / 2),
                        float(kStaffPreview.y + kStaffPreview.h - kInset), 1, pose.mirror);
    }
    kit.frame(screen, kStaffStats);
    int y = kStatY;
    const int x = kStaffStats.x + 8;
    auto line = [&](uint32_t label, const std::string &value) {
        kit.text(screen, x, y, kit.ui(label));
        kit.text(screen, kStaffValueX, y, value);
        y += kStatLine;
    };
    auto rating = [&](uint32_t label, float level) {
        kit.text(screen, x, y, kit.ui(label));
        kit.bar(screen, kStaffValueX, y + 1, kBarWidth, level);
        y += kStatLine;
    };
    // Employed since the day they were hired.
    line(kEmployedText, kit.date(m->hiredDay));
    line(kWageText, money(m->wage));
    line(kStatusText, kit.staffState(*m));
    rating(kExperienceText, float(m->experience) / float(Park::kMostExperience));
    rating(kEnergyText, m->energy / 100);
    rating(kHappinessText, m->happiness / 100);
    rating(kMotivationText, m->motivation / 100);
    line(kAgencyText, kit.ui(kFirstAgencyText + uint32_t(std::max(0, m->agency))));
    kit.button(screen, WindowKit::Button::kMinus, kStaffMinus,
               _over == Control::kMinus ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kPlus, kStaffPlus,
               _over == Control::kPlus ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kTrain, kStaffTrain,
               _over == Control::kTrain ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kFire, kStaffFire,
               _over == Control::kFire ? WindowKit::kLit : WindowKit::kPlain);
    if (_message)
        kit.text(screen, kStaffFire.x + kStaffFire.w + 10, kStaffFire.y + 12, kit.ui(_message));
    kit.button(screen, WindowKit::Button::kOk, kStaffOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// The staff list.

ScreenRect StaffListWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kTab:
        return {kListTabX + int(index) * kTabWidth, kListTabY, kTabWidth, kTabHeight};
    case Control::kRow:
        return {kListFrame.x + 1, kListRowY + int(index) * kRowHeight, kListFrame.w - 2, kRowHeight};
    case Control::kOk:
        return kListOk;
    case Control::kClose:
        return kListClose;
    default:
        return {};
    }
}

std::vector<int> StaffListWindow::listed(const Park &park) const {
    std::vector<int> ids;
    for (const Park::StaffMember &m : park.staff())
        if (m.kind == _kind)
            ids.push_back(m.id);
    return ids;
}

WindowResult StaffListWindow::handle(const Event &ev, const Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = Control::kNone;
    for (size_t i = 0; i < Park::kStaffKinds; ++i)
        if (placeOf(Control::kTab, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kTab;
            _overIndex = i;
        }
    const std::vector<int> ids = listed(park);
    for (size_t i = 0; i < ids.size(); ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kRow;
            _overIndex = i;
        }
    for (Control c : {Control::kOk, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!isClick(ev))
        return WindowResult::kNone;
    switch (_over) {
    case Control::kTab:
        open(Park::StaffKind(_overIndex));
        return WindowResult::kNone;
    case Control::kRow:
        _chosen = ids[_overIndex];
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

void StaffListWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const {
    if (!_open)
        return;
    kit.window(screen, kListWindow, kit.ui(kStaffListTitle) + ": " + kit.ui(kAllOfKindText[size_t(_kind)]));
    kit.button(screen, WindowKit::Button::kClose, kListClose,
               _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    for (size_t i = 0; i < Park::kStaffKinds; ++i) {
        const bool lit = _over == Control::kTab && _overIndex == i;
        kit.staffTab(screen, Park::StaffKind(i), placeOf(Control::kTab, i),
                     size_t(_kind) == i ? 5
                     : lit              ? WindowKit::kLit
                                        : WindowKit::kPlain);
    }
    for (size_t c = 0; c < kListColumns.size(); ++c)
        kit.header(screen, kListColumnRects[c], kit.ui(kListColumns[c]));
    kit.frame(screen, kListFrame);
    float happiness = 0;
    const std::vector<int> ids = listed(park);
    for (size_t i = 0; i < ids.size(); ++i) {
        const Park::StaffMember *m = park.staffMember(ids[i]);
        if (!m)
            continue;
        const ScreenRect row = placeOf(Control::kRow, i);
        if (m->id == _chosen)
            fill(screen, row, kChosenRow);
        kit.text(screen, kListColumnRects[0].x, row.y + 1, names.nameOf(m->kind, m->name));
        kit.text(screen, kListColumnRects[1].x + 10, row.y + 1, money(m->wage));
        kit.bar(screen, kListColumnRects[2].x, row.y + 3, kListColumnRects[2].w - 10,
                float(m->experience) / float(Park::kMostExperience));
        kit.bar(screen, kListColumnRects[3].x, row.y + 3, kListColumnRects[3].w - 10, m->happiness / 100);
        happiness += m->happiness;
    }
    if (!ids.empty()) {
        kit.text(screen, kListFrame.x, kListOk.y + 8, kit.ui(kAverageHappinessText));
        kit.bar(screen, kListColumnRects[2].x, kListOk.y + 9, kListColumnRects[2].w - 10,
                happiness / float(ids.size()) / 100);
    }
    kit.button(screen, WindowKit::Button::kOk, kListOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// The training room.

ScreenRect TrainingWindow::okRect() {
    return kTrainingOk;
}

WindowResult TrainingWindow::handle(const Event &ev) {
    if (!_open || !isClick(ev) || !kTrainingOk.contains(ev.mouseX, ev.mouseY))
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kClosed;
}

void TrainingWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const {
    if (!_open)
        return;
    kit.window(screen, kTrainingWindow, kit.ui(kTrainingRoomText));
    for (size_t c = 0; c < kTrainingColumns.size(); ++c)
        kit.header(screen, kTrainingColumnRects[c], kit.ui(kTrainingColumns[c]));
    kit.frame(screen, kTrainingFrame);
    int row = 0;
    for (const Park::StaffMember &m : park.staff()) {
        if (m.doing != Park::StaffDoing::kTraining && m.doing != Park::StaffDoing::kGoingToTrain)
            continue;
        const int y = kTrainingFrame.y + 3 + row * kRowHeight;
        kit.text(screen, kTrainingColumnRects[0].x, y, names.nameOf(m.kind, m.name));
        kit.text(screen, kTrainingColumnRects[1].x, y, kit.ui(kAllOfKindText[size_t(m.kind)]));
        // How able to learn: their experience (guessed).
        kit.bar(screen, kTrainingColumnRects[2].x, y + 2, kTrainingColumnRects[2].w - 10,
                float(m.experience) / float(Park::kMostExperience));
        kit.bar(screen, kTrainingColumnRects[3].x, y + 2, kTrainingColumnRects[3].w - 10, park.trainingProgress(m));
        ++row;
    }
    if (row == 0)
        kit.text(screen, kTrainingFrame.x + 6, kTrainingFrame.y + 4, kit.ui(kNoneTrainingText));
    kit.button(screen, WindowKit::Button::kOk, kTrainingOk, WindowKit::kPlain);
}

// The staff's finance controls.

ScreenRect StaffFinanceWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kTab:
        return {kFinanceTabX + int(index) * kTabWidth, kFinanceTabY, kTabWidth, kTabHeight};
    case Control::kLeft:
        return kFinanceLeft;
    case Control::kRight:
        return kFinanceRight;
    case Control::kMinus:
        return kFinanceMinus;
    case Control::kPlus:
        return kFinancePlus;
    case Control::kOk:
        return kFinanceOk;
    case Control::kClose:
        return kFinanceClose;
    default:
        return {};
    }
}

WindowResult StaffFinanceWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = Control::kNone;
    for (size_t i = 0; i < Park::kStaffKinds; ++i)
        if (placeOf(Control::kTab, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kTab;
            _overIndex = i;
        }
    for (Control c : {Control::kLeft, Control::kRight, Control::kMinus, Control::kPlus, Control::kOk, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!isClick(ev))
        return WindowResult::kNone;
    constexpr int kHows = 3;
    switch (_over) {
    case Control::kTab:
        _kind = Park::StaffKind(_overIndex);
        return WindowResult::kNone;
    case Control::kLeft:
    case Control::kRight:
        _how = Park::Adjustment((int(_how) + (_over == Control::kRight ? 1 : kHows - 1)) % kHows);
        return WindowResult::kNone;
    case Control::kMinus:
    case Control::kPlus:
        _amount += _over == Control::kPlus ? 1 : -1;
        return WindowResult::kNone;
    case Control::kOk:
        // Applied to the kind shown; the window stays for others.
        park.adjustWages(_kind, _how, _amount);
        _amount = 0;
        return WindowResult::kChose;
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void StaffFinanceWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kFinanceWindow, kit.ui(kFinanceTitle));
    kit.button(screen, WindowKit::Button::kClose, kFinanceClose,
               _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    for (size_t i = 0; i < Park::kStaffKinds; ++i) {
        const bool lit = _over == Control::kTab && _overIndex == i;
        kit.staffTab(screen, Park::StaffKind(i), placeOf(Control::kTab, i),
                     size_t(_kind) == i ? 5
                     : lit              ? WindowKit::kLit
                                        : WindowKit::kPlain);
    }
    // The kind's wages, as they stand: their average.
    int total = 0;
    int count = 0;
    for (const Park::StaffMember &m : park.staff())
        if (m.kind == _kind) {
            total += m.wage;
            ++count;
        }
    const int x = kFinanceWindow.x + 30;
    kit.frame(screen, kFinanceWageFrame);
    kit.frame(screen, kFinanceAdjustFrame);
    kit.text(screen, x, kFinanceTabY + kTabHeight + 20, kit.ui(kWageLabels[size_t(_kind)]));
    kit.text(screen, x + 200, kFinanceTabY + kTabHeight + 20, count ? money(total / count) : "-");
    kit.text(screen, x, kFinanceLeft.y - 18, kit.ui(kAdjustmentText));
    kit.button(screen, WindowKit::Button::kLeft, kFinanceLeft,
               _over == Control::kLeft ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kRight, kFinanceRight,
               _over == Control::kRight ? WindowKit::kLit : WindowKit::kPlain);
    const std::string how = kit.ui(kSetToText + uint32_t(_how));
    kit.text(screen, (kFinanceLeft.x + kFinanceLeft.w + kFinanceRight.x - kit.textWidth(how)) / 2, kFinanceLeft.y + 5,
             how);
    kit.button(screen, WindowKit::Button::kMinus, kFinanceMinus,
               _over == Control::kMinus ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kPlus, kFinancePlus,
               _over == Control::kPlus ? WindowKit::kLit : WindowKit::kPlain);
    const std::string amount = _how == Park::Adjustment::kPercent ? std::to_string(_amount) + " %" : money(_amount);
    kit.text(screen, (kFinanceMinus.x + kFinanceMinus.w + kFinancePlus.x - kit.textWidth(amount)) / 2,
             kFinanceMinus.y + 4, amount);
    kit.button(screen, WindowKit::Button::kOk, kFinanceOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// A wage demand.

ScreenRect WageDemandWindow::placeOf(Control control) {
    switch (control) {
    case Control::kMinus:
        return kDemandMinus;
    case Control::kPlus:
        return kDemandPlus;
    case Control::kOk:
        return kDemandOk;
    case Control::kAccept:
        return kDemandAccept;
    case Control::kLetGo:
        return kDemandLetGo;
    default:
        return {};
    }
}

WindowResult WageDemandWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = under(ev, std::array{std::pair{Control::kMinus, kDemandMinus}, std::pair{Control::kPlus, kDemandPlus},
                                 std::pair{Control::kOk, kDemandOk}, std::pair{Control::kAccept, kDemandAccept},
                                 std::pair{Control::kLetGo, kDemandLetGo}});
    const Park::WageDemand *demand = park.wageDemand();
    if (!isClick(ev) || !demand)
        return WindowResult::kNone;
    // Once answered, the next click closes it.
    if (_answer == kAcceptedText || _answer == kStaysText) {
        _open = false;
        return WindowResult::kClosed;
    }
    switch (_over) {
    case Control::kMinus:
    case Control::kPlus:
        _offer = std::max(0, _offer + (_over == Control::kPlus ? 1 : -1));
        return WindowResult::kNone;
    case Control::kOk:
        switch (park.offer(_offer)) {
        case Park::Offer::kAccepted:
            _answer = kAcceptedText;
            break;
        case Park::Offer::kStays:
            _answer = kStaysText;
            break;
        case Park::Offer::kRefused:
            _answer = kRefusedText;
            break;
        }
        return WindowResult::kNone;
    case Control::kAccept:
        if (_answer == kRefusedText) {
            park.offer(demand->raise);
            _open = false;
            return WindowResult::kClosed;
        }
        return WindowResult::kNone;
    case Control::kLetGo:
        if (_answer == kRefusedText) {
            park.refuseDemand();
            _open = false;
            return WindowResult::kClosed;
        }
        return WindowResult::kNone;
    default:
        return WindowResult::kNone;
    }
}

void WageDemandWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const {
    const Park::WageDemand *demand = park.wageDemand();
    if (!_open)
        return;
    kit.window(screen, kDemandWindow, kit.ui(_answer ? kResultTitle : kDemandTitle));
    kit.frame(screen, kDemandTextFrame);
    kit.frame(screen, kDemandOfferFrame);
    const int x = kDemandWindow.x + 20;
    int y = kDemandWindow.y + 40;
    if (demand)
        if (const Park::StaffMember *m = park.staffMember(demand->staff)) {
            kit.text(screen, x, y, names.nameOf(m->kind, m->name) + ", " + money(m->wage));
            y += 18;
        }
    const std::string said = _answer             ? kit.ui(_answer, {{13, std::to_string(demand ? demand->raise : 0)}})
                             : demand != nullptr ? kit.ui(kDemandText, {{13, std::to_string(demand->raise)}})
                                                 : std::string();
    // The demand, broken into lines that fit.
    std::string lineText;
    for (size_t i = 0; i <= said.size(); ++i) {
        const bool end = i == said.size();
        if (!end && said[i] != ' ' && said[i] != '\n') {
            lineText.push_back(said[i]);
            continue;
        }
        const size_t space = lineText.rfind(' ');
        if (kit.textWidth(lineText) > kDemandWindow.w - 40 && space != std::string::npos) {
            kit.text(screen, x, y, lineText.substr(0, space));
            y += 14;
            lineText = lineText.substr(space + 1);
        }
        if (end || said[i] == '\n') {
            kit.text(screen, x, y, lineText);
            y += 14;
            lineText.clear();
        } else {
            lineText.push_back(' ');
        }
    }
    if (_answer != kAcceptedText && _answer != kStaysText) {
        kit.text(screen, kDemandMinus.x - 90, kDemandMinus.y + 4, kit.ui(kOfferText));
        kit.button(screen, WindowKit::Button::kMinus, kDemandMinus,
                   _over == Control::kMinus ? WindowKit::kLit : WindowKit::kPlain);
        kit.button(screen, WindowKit::Button::kPlus, kDemandPlus,
                   _over == Control::kPlus ? WindowKit::kLit : WindowKit::kPlain);
        const std::string offered = money(_offer);
        kit.text(screen, (kDemandMinus.x + kDemandMinus.w + kDemandPlus.x - kit.textWidth(offered)) / 2,
                 kDemandMinus.y + 4, offered);
    }
    if (_answer == kRefusedText) {
        kit.text(screen, kDemandAccept.x, kDemandAccept.y + 3, kit.ui(kAcceptDemandText),
                 _over == Control::kAccept ? kMenuItemLit : kMenuItem);
        kit.text(screen, kDemandLetGo.x, kDemandLetGo.y + 3, kit.ui(kLetGoText),
                 _over == Control::kLetGo ? kMenuItemLit : kMenuItem);
    }
    kit.button(screen, WindowKit::Button::kOk, kDemandOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// The Research Lab.

namespace {

constexpr uint32_t kResearchLabText = 285;
constexpr uint32_t kEditProjectText = 286;
constexpr uint32_t kCancelProjectText = 288;
constexpr uint32_t kStartProjectText = 290;
constexpr uint32_t kNoProjectText = 291;
constexpr uint32_t kWorkRateText = 294;
constexpr uint32_t kProgressText = 295;
constexpr uint32_t kDaysToGoText = 296;
constexpr uint32_t kExpenditureText = 297;
constexpr uint32_t kAssignedText = 298;
constexpr uint32_t kFinishedText = 305;
constexpr uint32_t kNewProjectText = 306;
constexpr uint32_t kChooseText = 307;
constexpr std::array<uint32_t, 5> kChooserColumns{308, 309, 312, 311, 310};
constexpr uint32_t kRideTypeText = 313; // then sideshow, shop, feature, facility
constexpr uint32_t kUpgradeTypeText = 319;
constexpr uint32_t kExpansionTypeText = 318;
constexpr uint32_t kAssignTitle = 321;
constexpr uint32_t kAvailableText = 322;
constexpr uint32_t kAssignedListText = 323;
constexpr uint32_t kNoConsultantNeededText = 293;
constexpr uint32_t kConsultantText = 299;
constexpr uint32_t kConsultingText = 324;
constexpr uint32_t kNoneConsultingText = 325;
constexpr uint32_t kOthersAssignedText = 326;
constexpr uint32_t kNoOthersAssignedText = 327;
constexpr uint32_t kNoScientistsText = 329;
constexpr uint32_t kNoConsultantText = 331;
constexpr uint32_t kNoneText = 36;
constexpr uint32_t kFinanceSummaryTitle = 45;
constexpr std::array<uint32_t, 6> kFinanceRows{173, 170, 174, 175, 177, 178};
constexpr uint32_t kEditNameText = 534;

// As the recording's at 14:52.
constexpr ScreenRect kLabWindow{65, 20, 563, 378};
constexpr int kLabSlotStep = 145;
constexpr ScreenRect kLabSlot{83, 66, 434, 134};
constexpr ScreenRect kLabInfo{90, 103, 275, 88};
constexpr ScreenRect kLabEdit{374, 118, 135, 22};
constexpr ScreenRect kLabCancel{374, 152, 135, 22};
constexpr ScreenRect kLabStart{228, 355, 145, 30};
constexpr ScreenRect kLabRate{560, 86, 20, 150};
constexpr ScreenRect kLabRateUp{561, 64, 18, 18};
constexpr ScreenRect kLabRateDown{561, 240, 18, 18};
constexpr ScreenRect kLabRateLabel{528, 320, 85, 22};
constexpr ScreenRect kLabOk{586, 357, 26, 26};
constexpr ScreenRect kLabClose{597, 24, 26, 26};
constexpr int kRateStep = 5;
constexpr Color kRateFill{90, 200, 90};

// As the recording's at 14:42.
constexpr ScreenRect kChooserWindow{65, 62, 563, 320};
constexpr std::array<ScreenRect, 5> kChooserHeaders{
    {{95, 118, 130, 30}, {231, 118, 78, 30}, {315, 118, 105, 30}, {426, 118, 73, 30}, {505, 118, 67, 30}}};
constexpr ScreenRect kChooserList{88, 153, 488, 145};
constexpr int kChooserRowY = 157;
constexpr ScreenRect kChooserMessage{85, 312, 452, 42};
constexpr ScreenRect kChooserOk{546, 322, 28, 28};
constexpr ScreenRect kChooserCancel{580, 324, 26, 26};
// The estimated time's bar is full at this cost (guessed).
constexpr float kLongestProject = 1200;

constexpr ScreenRect kAssignWindow{110, 90, 420, 300};
constexpr ScreenRect kAvailableList{125, 150, 170, 120};
constexpr ScreenRect kAssignedList{345, 150, 170, 120};
constexpr ScreenRect kAssignArrow{306, 180, 28, 28};
constexpr ScreenRect kUnassignArrow{306, 220, 28, 28};
constexpr ScreenRect kAssignNote{125, 280, 390, 56};
constexpr ScreenRect kAssignOk{484, 346, 28, 28};

constexpr ScreenRect kSummaryWindow{150, 110, 340, 230};
constexpr ScreenRect kSummaryFrame{165, 150, 310, 120};
constexpr ScreenRect kSummaryOk{440, 292, 28, 28};

constexpr ScreenRect kRenameWindow{170, 170, 300, 130};
constexpr ScreenRect kRenameField{185, 215, 270, 26};
constexpr ScreenRect kRenameOk{426, 256, 28, 28};
// The longest name kept.
constexpr size_t kLongestName = 30;

// A project's name: its attraction's, and an upgrade's level.
std::string projectName(const Park &park, const Park::Project &p, const ObjectNames &names) {
    const Attractions::Attraction *a = park.attractions().find(p.id);
    std::string name = a ? names.shown(*a) : std::to_string(p.id);
    if (p.kind == Park::ProjectKind::kUpgrade)
        name += " " + std::to_string(p.level);
    return name;
}

uint32_t typeText(const Park &park, const Park::Project &p) {
    if (p.kind == Park::ProjectKind::kUpgrade)
        return kUpgradeTypeText;
    if (p.kind == Park::ProjectKind::kExpansion)
        return kExpansionTypeText;
    const Attractions::Attraction *a = park.attractions().find(p.id);
    if (!a)
        return kRideTypeText;
    switch (a->kind) {
    case Attractions::Kind::kRide:
        return kRideTypeText;
    case Attractions::Kind::kSideshow:
        return kRideTypeText + 1;
    case Attractions::Kind::kShop:
        return kRideTypeText + 2;
    case Attractions::Kind::kFeature:
        return kRideTypeText + 3;
    case Attractions::Kind::kFacility:
        return kRideTypeText + 4;
    }
    return kRideTypeText;
}

std::optional<Park::StaffKind> consultantKind(const Park &park, const Park::Project &p) {
    const Attractions::Attraction *a = park.attractions().find(p.id);
    return a ? Park::consultantKind(a->consultant) : std::nullopt;
}

std::string consultantOf(const WindowKit &kit, const Park &park, const Park::Project &p) {
    const std::optional<Park::StaffKind> kind = consultantKind(park, p);
    return kind ? kit.staffType(*kind) : kit.ui(kNoneText);
}

// Who consults: the first of the kind employed (guessed).
const Park::StaffMember *consultant(const Park &park, const Park::Project &p) {
    const std::optional<Park::StaffKind> kind = consultantKind(park, p);
    for (const Park::StaffMember &m : park.staff())
        if (kind && m.kind == *kind)
            return &m;
    return nullptr;
}

// A consultant's parameters: {0} their name, {15} their kind (guessed by
// "Consultant: {0}, {15}"); {17} unknown.
std::map<int, std::string> consultantValues(const WindowKit &kit, const HireWindow &staffNames,
                                            const Park::StaffMember &m) {
    return {{0, staffNames.nameOf(m.kind, m.name)}, {15, kit.staffType(m.kind)}, {17, ""}};
}

} // namespace

ScreenRect ResearchLabWindow::placeOf(Control control, size_t slot) {
    const int dy = int(slot) * kLabSlotStep;
    switch (control) {
    case Control::kEdit:
        return {kLabEdit.x, kLabEdit.y + dy, kLabEdit.w, kLabEdit.h};
    case Control::kCancel:
        return {kLabCancel.x, kLabCancel.y + dy, kLabCancel.w, kLabCancel.h};
    case Control::kStart:
        return kLabStart;
    case Control::kRateUp:
        return kLabRateUp;
    case Control::kRateDown:
        return kLabRateDown;
    case Control::kOk:
        return kLabOk;
    case Control::kClose:
        return kLabClose;
    default:
        return {};
    }
}

ResearchLabWindow::Wants ResearchLabWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return Wants::kNothing;
    _over = Control::kNone;
    for (size_t slot = 0; slot < Park::kProjectSlots; ++slot)
        for (Control c : {Control::kEdit, Control::kCancel})
            if (placeOf(c, slot).contains(ev.mouseX, ev.mouseY)) {
                _over = c;
                _overSlot = slot;
            }
    for (Control c : {Control::kStart, Control::kRateUp, Control::kRateDown, Control::kOk, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!isClick(ev))
        return Wants::kNothing;
    switch (_over) {
    case Control::kEdit:
        if (!park.projects()[_overSlot] || park.projects()[_overSlot]->finished)
            return Wants::kNothing;
        _slot = _overSlot;
        return Wants::kAssign;
    case Control::kCancel:
        park.cancelProject(_overSlot);
        return Wants::kNothing;
    case Control::kStart:
        // Into the first slot free, or one finished.
        for (size_t slot = 0; slot < Park::kProjectSlots; ++slot)
            if (!park.projects()[slot] || park.projects()[slot]->finished) {
                _slot = slot;
                return Wants::kChoose;
            }
        return Wants::kNothing;
    case Control::kRateUp:
    case Control::kRateDown:
        park.setWorkRate(park.workRate() + (_over == Control::kRateUp ? kRateStep : -kRateStep));
        return Wants::kNothing;
    case Control::kOk:
    case Control::kClose:
        _open = false;
        return Wants::kNothing;
    default:
        return Wants::kNothing;
    }
}

void ResearchLabWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &staffNames,
                             const ObjectNames &names) const {
    if (!_open)
        return;
    kit.window(screen, kLabWindow, kit.ui(kResearchLabText));
    kit.button(screen, WindowKit::Button::kClose, kLabClose,
               _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    for (size_t slot = 0; slot < Park::kProjectSlots; ++slot) {
        const int dy = int(slot) * kLabSlotStep;
        const ScreenRect info{kLabInfo.x, kLabInfo.y + dy, kLabInfo.w, kLabInfo.h};
        kit.panel(screen, {kLabSlot.x, kLabSlot.y + dy, kLabSlot.w, kLabSlot.h});
        kit.frame(screen, info);
        const std::optional<Park::Project> &p = park.projects()[slot];
        kit.title(screen, kLabSlot.x + 8, kLabSlot.y + dy + 8,
                  p ? "'" + projectName(park, *p, names) + "'" : kit.ui(kNoProjectText), kWhite);
        int y = info.y + 22;
        auto line = [&](const std::string &text) { y = kit.paragraph(screen, info.x + 6, y, info.w - 12, text); };
        if (!p) {
            line(kit.ui(kNewProjectText));
        } else if (p->finished) {
            line(kit.ui(kFinishedText));
        } else {
            const float perDay = park.researchPerDay(slot);
            const float left = float(p->cost) - p->done;
            const auto value = [](float v) { return std::map<int, std::string>{{14, std::to_string(int(v))}}; };
            line(kit.ui(kProgressText, value(100 * p->done / float(std::max(1, p->cost)))));
            line(kit.ui(kDaysToGoText, {{14, perDay > 0 ? std::to_string(int(std::ceil(left / perDay))) : "-"}}));
            line(kit.ui(kExpenditureText, value(left)));
            line(kit.ui(kAssignedText, value(float(p->scientists.size()))));
            if (const Park::StaffMember *adviser = consultant(park, *p))
                line(kit.ui(kConsultantText, consultantValues(kit, staffNames, *adviser)));
            else if (!consultantKind(park, *p))
                line(kit.ui(kNoConsultantNeededText));
            if (const uint32_t held = park.projectHeldUp(slot))
                line(kit.ui(held));
        }
        kit.header(screen, placeOf(Control::kEdit, slot), kit.ui(kEditProjectText));
        kit.header(screen, placeOf(Control::kCancel, slot), kit.ui(kCancelProjectText));
    }
    kit.header(screen, kLabStart, kit.ui(kStartProjectText));
    // The work rate, a bar from the foot up.
    kit.frame(screen, kLabRate);
    const int filled = (kLabRate.h - 6) * park.workRate() / 100;
    fill(screen, {kLabRate.x + 3, kLabRate.y + kLabRate.h - 3 - filled, kLabRate.w - 6, filled}, kRateFill);
    kit.button(screen, WindowKit::Button::kPlus, kLabRateUp,
               _over == Control::kRateUp ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kMinus, kLabRateDown,
               _over == Control::kRateDown ? WindowKit::kLit : WindowKit::kPlain);
    kit.frame(screen, kLabRateLabel);
    const std::string rate = kit.ui(kWorkRateText);
    kit.text(screen, kLabRateLabel.x + (kLabRateLabel.w - kit.textWidth(rate)) / 2, kLabRateLabel.y + 7, rate);
    kit.button(screen, WindowKit::Button::kOk, kLabOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// Choosing a project.

ScreenRect ProjectWindow::placeOf(Control control, size_t row) {
    switch (control) {
    case Control::kRow:
        return {kChooserList.x + 2, kChooserRowY + int(row) * kRowHeight, kChooserList.w - 4, kRowHeight};
    case Control::kOk:
        return kChooserOk;
    case Control::kCancel:
        return kChooserCancel;
    default:
        return {};
    }
}

WindowResult ProjectWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    const std::vector<Park::Project> choices = park.projectChoices();
    _over = Control::kNone;
    size_t row = 0;
    for (size_t i = 0; i < choices.size(); ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kRow;
            row = i;
        }
    for (Control c : {Control::kOk, Control::kCancel})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!isClick(ev))
        return WindowResult::kNone;
    switch (_over) {
    case Control::kRow:
        _row = row;
        _message = 0;
        return WindowResult::kNone;
    case Control::kOk:
        if (_row >= choices.size())
            return WindowResult::kNone;
        // "You need to hire some scientists before you can carry out any
        // research!"; "You cannot start this project as you don't have anyone
        // with the required ability to act as consultant".
        if (std::none_of(park.staff().begin(), park.staff().end(),
                         [](const Park::StaffMember &m) { return m.kind == Park::StaffKind::kScientist; })) {
            _message = kNoScientistsText;
            return WindowResult::kNone;
        }
        if (consultantKind(park, choices[_row]) && !consultant(park, choices[_row])) {
            _message = kNoConsultantText;
            return WindowResult::kNone;
        }
        park.startProject(_slot, choices[_row]);
        _open = false;
        return WindowResult::kChose;
    case Control::kCancel:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void ProjectWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const ObjectNames &names) const {
    if (!_open)
        return;
    kit.window(screen, kChooserWindow, kit.ui(kChooseText));
    for (size_t c = 0; c < kChooserHeaders.size(); ++c)
        kit.header(screen, kChooserHeaders[c], kit.ui(kChooserColumns[c]));
    kit.frame(screen, kChooserList);
    const std::vector<Park::Project> choices = park.projectChoices();
    for (size_t i = 0; i < choices.size(); ++i) {
        const ScreenRect row = placeOf(Control::kRow, i);
        if (row.y + row.h > kChooserList.y + kChooserList.h)
            break;
        if (i == _row)
            fill(screen, row, kChosenRow);
        const Park::Project &p = choices[i];
        auto centred = [&](size_t column, const std::string &text) {
            const ScreenRect &h = kChooserHeaders[column];
            kit.text(screen, h.x + (h.w - kit.textWidth(text)) / 2, row.y + 1, text);
        };
        kit.text(screen, kChooserHeaders[0].x, row.y + 1, projectName(park, p, names));
        centred(1, kit.ui(typeText(park, p)));
        centred(2, consultantOf(kit, park, p));
        kit.bar(screen, kChooserHeaders[3].x, row.y + 3, kChooserHeaders[3].w, float(p.cost) / kLongestProject);
        const std::string cost = money(p.cost);
        kit.text(screen, kChooserHeaders[4].x + kChooserHeaders[4].w - 6 - kit.textWidth(cost), row.y + 1, cost);
    }
    kit.frame(screen, kChooserMessage);
    if (_message)
        kit.paragraph(screen, kChooserMessage.x + 8, kChooserMessage.y + 8, kChooserMessage.w - 16, kit.ui(_message));
    kit.button(screen, WindowKit::Button::kOk, kChooserOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kCancel, kChooserCancel,
               _over == Control::kCancel ? WindowKit::kLit : WindowKit::kPlain);
}

// Assigning scientists.

ScreenRect AssignWindow::placeOf(Control control, size_t row) {
    switch (control) {
    case Control::kAvailableRow:
        return {kAvailableList.x + 2, kAvailableList.y + 3 + int(row) * kRowHeight, kAvailableList.w - 4, kRowHeight};
    case Control::kAssignedRow:
        return {kAssignedList.x + 2, kAssignedList.y + 3 + int(row) * kRowHeight, kAssignedList.w - 4, kRowHeight};
    case Control::kAssign:
        return kAssignArrow;
    case Control::kUnassign:
        return kUnassignArrow;
    case Control::kOk:
        return kAssignOk;
    default:
        return {};
    }
}

std::vector<int> AssignWindow::available(const Park &park) const {
    const std::vector<int> on = assigned(park);
    std::vector<int> ids;
    for (const Park::StaffMember &m : park.staff())
        if (m.kind == Park::StaffKind::kScientist && std::find(on.begin(), on.end(), m.id) == on.end())
            ids.push_back(m.id);
    return ids;
}

std::vector<int> AssignWindow::assigned(const Park &park) const {
    const std::optional<Park::Project> &p = park.projects()[_slot];
    return p ? p->scientists : std::vector<int>{};
}

WindowResult AssignWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    const std::vector<int> left = available(park);
    const std::vector<int> right = assigned(park);
    _over = Control::kNone;
    int under = 0;
    for (size_t i = 0; i < left.size(); ++i)
        if (placeOf(Control::kAvailableRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kAvailableRow;
            under = left[i];
        }
    for (size_t i = 0; i < right.size(); ++i)
        if (placeOf(Control::kAssignedRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kAssignedRow;
            under = right[i];
        }
    for (Control c : {Control::kAssign, Control::kUnassign, Control::kOk})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!isClick(ev))
        return WindowResult::kNone;
    switch (_over) {
    case Control::kAvailableRow:
    case Control::kAssignedRow:
        _chosen = under;
        return WindowResult::kNone;
    case Control::kAssign:
        if (std::find(left.begin(), left.end(), _chosen) != left.end())
            park.assign(_slot, _chosen);
        return WindowResult::kNone;
    case Control::kUnassign:
        if (std::find(right.begin(), right.end(), _chosen) != right.end())
            park.unassign(_slot, _chosen);
        return WindowResult::kNone;
    case Control::kOk:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void AssignWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &staffNames,
                        const ObjectNames &names) const {
    const std::optional<Park::Project> &p = park.projects()[_slot];
    if (!_open || !p)
        return;
    kit.window(screen, kAssignWindow, kit.ui(kAssignTitle, {{16, "'" + projectName(park, *p, names) + "'"}}));
    for (auto [list, label, ids, control] :
         {std::tuple{kAvailableList, kAvailableText, available(park), Control::kAvailableRow},
          std::tuple{kAssignedList, kAssignedListText, assigned(park), Control::kAssignedRow}}) {
        kit.text(screen, list.x, list.y - 14, kit.ui(label));
        kit.frame(screen, list);
        for (size_t i = 0; i < ids.size(); ++i) {
            const ScreenRect row = placeOf(control, i);
            if (ids[i] == _chosen)
                fill(screen, row, kChosenRow);
            if (const Park::StaffMember *m = park.staffMember(ids[i]))
                kit.text(screen, row.x + 2, row.y + 1, staffNames.nameOf(m->kind, m->name));
        }
    }
    kit.button(screen, WindowKit::Button::kRight, kAssignArrow,
               _over == Control::kAssign ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kLeft, kUnassignArrow,
               _over == Control::kUnassign ? WindowKit::kLit : WindowKit::kPlain);
    kit.frame(screen, kAssignNote);
    int y = kAssignNote.y + 6;
    const Park::StaffMember *adviser = consultant(park, *p);
    if (adviser)
        y = kit.paragraph(screen, kAssignNote.x + 6, y, kAssignNote.w - 12,
                          kit.ui(kConsultingText, consultantValues(kit, staffNames, *adviser)));
    else if (!consultantKind(park, *p))
        y = kit.paragraph(screen, kAssignNote.x + 6, y, kAssignNote.w - 12, kit.ui(kNoneConsultingText));
    size_t others = 0;
    for (size_t slot = 0; slot < Park::kProjectSlots; ++slot)
        if (slot != _slot && park.projects()[slot])
            others += park.projects()[slot]->scientists.size();
    kit.paragraph(screen, kAssignNote.x + 6, y, kAssignNote.w - 12,
                  others ? kit.ui(kOthersAssignedText, {{14, std::to_string(others)}}) : kit.ui(kNoOthersAssignedText));
    kit.button(screen, WindowKit::Button::kOk, kAssignOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// An attraction's financial summary.

ScreenRect FinanceSummaryWindow::okRect() {
    return kSummaryOk;
}

WindowResult FinanceSummaryWindow::handle(const Event &ev) {
    if (!_open || !isClick(ev) || !kSummaryOk.contains(ev.mouseX, ev.mouseY))
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kClosed;
}

void FinanceSummaryWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open || _object >= park.objects().size())
        return;
    const Park::PlacedObject &o = park.objects()[_object];
    kit.window(screen, kSummaryWindow, kit.ui(kFinanceSummaryTitle, {{4, _name}}));
    kit.frame(screen, kSummaryFrame);
    // Users, takings, less its costs, profit last month; profit to date.
    const std::array<std::string, 6> values{std::to_string(o.lastUsers),
                                            money(o.lastTakings),
                                            money(-o.lastCosts),
                                            money(-o.lastGoods),
                                            money(o.lastTakings - o.lastCosts - o.lastGoods),
                                            money(o.profit)};
    int y = kSummaryFrame.y + 8;
    for (size_t i = 0; i < kFinanceRows.size(); ++i) {
        kit.text(screen, kSummaryFrame.x + 8, y, kit.ui(kFinanceRows[i]));
        kit.text(screen, kSummaryFrame.x + kSummaryFrame.w - 10 - kit.textWidth(values[i]), y, values[i]);
        y += 17;
    }
    kit.button(screen, WindowKit::Button::kOk, kSummaryOk, WindowKit::kPlain);
}

// Naming an attraction.

ScreenRect RenameWindow::okRect() {
    return kRenameOk;
}

WindowResult RenameWindow::handle(const Event &ev) {
    if (!_open)
        return WindowResult::kNone;
    if (ev.type == EventType::kTextInput) {
        for (char c : std::string_view(ev.text))
            if (_typed.size() < kLongestName && static_cast<unsigned char>(c) >= ' ')
                _typed.push_back(c);
        return WindowResult::kNone;
    }
    if (ev.type == EventType::kKeyDown && ev.key == key::kBackspace) {
        // A whole character off, its UTF-8 continuation bytes and all.
        while (!_typed.empty() && (static_cast<unsigned char>(_typed.back()) & 0xC0) == 0x80)
            _typed.pop_back();
        if (!_typed.empty())
            _typed.pop_back();
        return WindowResult::kNone;
    }
    if ((ev.type == EventType::kKeyDown && ev.key == key::kReturn) ||
        (isClick(ev) && kRenameOk.contains(ev.mouseX, ev.mouseY))) {
        _open = false;
        return WindowResult::kChose;
    }
    return WindowResult::kNone;
}

void RenameWindow::draw(Surface &screen, const WindowKit &kit) const {
    if (!_open)
        return;
    kit.window(screen, kRenameWindow, kit.ui(kEditNameText));
    kit.frame(screen, kRenameField);
    kit.text(screen, kRenameField.x + 6, kRenameField.y + 9, _typed + "_");
    kit.button(screen, WindowKit::Button::kOk, kRenameOk, WindowKit::kPlain);
}

// The money's windows, laid out by guess in the others' style: the
// recording opens none of them.

namespace {

constexpr uint32_t kFinancialStatisticsTitle = 386;
constexpr uint32_t kBankBalanceText = 387;
constexpr uint32_t kParkValueText = 388;
constexpr uint32_t kTotalIncomeText = 389;
constexpr uint32_t kGateTakingsText = 390; // then rides', shops', sideshows', facilities'
constexpr uint32_t kTotalExpenditureText = 395;
constexpr uint32_t kNewAttractionsText = 396; // then running costs, wages
constexpr uint32_t kLastMonthText = 399;
constexpr uint32_t kTakingsText = 400; // then less expenditure, balance
constexpr uint32_t kAvailableLoansText = 403;
constexpr uint32_t kOutstandingLoansText = 404;
constexpr std::array<uint32_t, 6> kAvailableColumns{407, 408, 409, 410, 411, 412};
constexpr std::array<uint32_t, 5> kOutstandingColumns{407, 413, 411, 414, 415};
constexpr uint32_t kRepayTitle = 416;
constexpr uint32_t kRepayText = 417;
constexpr uint32_t kCantRepayText = 419;
constexpr uint32_t kTakeTitle = 420;
constexpr uint32_t kTakeText = 421;
constexpr uint32_t kSharePortfolioTitle = 844;
constexpr uint32_t kPortfolioFor = 469;
constexpr uint32_t kSharePriceText = 466;
constexpr uint32_t kSharesOwnedText = 467;
constexpr uint32_t kPortfolioValueText = 468;
constexpr uint32_t kStakeText = 470;
constexpr uint32_t kAttractionFinanceTitle = 179;
constexpr uint32_t kPriceKindText = 184; // ride, shop, sideshow, bathroom, photo, park entry
constexpr uint32_t kAdjustmentTypeText = 180;
constexpr uint32_t kSetToAmountText = 181; // then $ change, % change

constexpr ScreenRect kStatsWindow{120, 30, 400, 420};
constexpr ScreenRect kStatsFrame{135, 68, 370, 316};
constexpr ScreenRect kStatsOk{478, 404, 28, 28};
constexpr int kStatsLine = 17;

constexpr ScreenRect kLoansWindow{55, 50, 530, 380};
constexpr ScreenRect kLoansClose{548, 54, 26, 26};
constexpr ScreenRect kAvailableTab{78, 88, 180, 24};
constexpr ScreenRect kOutstandingTab{266, 88, 180, 24};
constexpr std::array<ScreenRect, 6> kAvailableHeaders{{{78, 120, 120, 30},
                                                       {200, 120, 70, 30},
                                                       {272, 120, 70, 30},
                                                       {344, 120, 66, 30},
                                                       {412, 120, 80, 30},
                                                       {494, 120, 76, 30}}};
constexpr std::array<ScreenRect, 5> kOutstandingHeaders{
    {{78, 120, 120, 30}, {200, 120, 90, 30}, {292, 120, 90, 30}, {384, 120, 90, 30}, {476, 120, 94, 30}}};
constexpr ScreenRect kLoanList{76, 154, 496, 196};
constexpr ScreenRect kLoanAct{78, 362, 160, 26};
constexpr ScreenRect kLoansOk{544, 394, 28, 28};
constexpr ScreenRect kAskWindow{180, 160, 280, 160};
constexpr ScreenRect kAskYes{368, 282, 28, 28};
constexpr ScreenRect kAskNo{402, 284, 26, 26};

constexpr ScreenRect kPortfolioWindow{150, 130, 340, 200};
constexpr ScreenRect kPortfolioFrame{165, 168, 310, 108};
constexpr ScreenRect kPortfolioOk{452, 290, 28, 28};

constexpr ScreenRect kPricesWindow{150, 110, 340, 250};
constexpr ScreenRect kPricesClose{456, 114, 26, 26};
constexpr ScreenRect kPricesKindLeft{170, 156, 26, 26};
constexpr ScreenRect kPricesKindRight{444, 156, 26, 26};
constexpr ScreenRect kPricesCurrent{170, 190, 300, 26};
constexpr ScreenRect kPricesLeft{170, 250, 26, 26};
constexpr ScreenRect kPricesRight{444, 250, 26, 26};
constexpr ScreenRect kPricesMinus{250, 284, 26, 26};
constexpr ScreenRect kPricesPlus{364, 284, 26, 26};
constexpr ScreenRect kPricesOk{452, 322, 28, 28};
// The kinds of price the arrows go through; photos aren't sold yet.
constexpr std::array<Park::PriceKind, 5> kPriceKinds{Park::PriceKind::kRide, Park::PriceKind::kShop,
                                                     Park::PriceKind::kSideshow, Park::PriceKind::kBathroom,
                                                     Park::PriceKind::kEntry};

constexpr int kMessageWidth = 400;
constexpr int kMessageX = 120;
constexpr int kMessageY = 60;

// A label at the left of a frame, its value at the right.
void row(Surface &screen, const WindowKit &kit, const ScreenRect &frame, int y, const std::string &label,
         const std::string &value, int indent = 0) {
    kit.text(screen, frame.x + 8 + indent, y, label);
    kit.text(screen, frame.x + frame.w - 10 - kit.textWidth(value), y, value);
}

} // namespace

// Financial Statistics.

ScreenRect FinanceStatsWindow::okRect() {
    return kStatsOk;
}

WindowResult FinanceStatsWindow::handle(const Event &ev) {
    if (!_open || !isClick(ev) || !kStatsOk.contains(ev.mouseX, ev.mouseY))
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kClosed;
}

void FinanceStatsWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kStatsWindow, kit.ui(kFinancialStatisticsTitle));
    kit.frame(screen, kStatsFrame);
    const Park::Accounts &year = park.thisYear();
    const Park::Accounts &month = park.lastMonth();
    int y = kStatsFrame.y + 8;
    auto line = [&](uint32_t label, int amount, int indent = 0) {
        row(screen, kit, kStatsFrame, y, kit.ui(label), money(amount), indent);
        y += kStatsLine;
    };
    constexpr int kIndent = 14;
    line(kBankBalanceText, park.cash());
    line(kParkValueText, park.parkValue());
    y += kStatsLine / 2;
    line(kTotalIncomeText, Park::income(year));
    for (size_t i = 0; i < Park::kIncomeLines; ++i)
        line(kGateTakingsText + uint32_t(i), year[i], kIndent);
    y += kStatsLine / 2;
    line(kTotalExpenditureText, Park::expenditure(year));
    for (Park::Ledger l : {Park::Ledger::kBuilding, Park::Ledger::kRunning, Park::Ledger::kWages})
        line(kNewAttractionsText + uint32_t(size_t(l) - size_t(Park::Ledger::kBuilding)), year[size_t(l)], kIndent);
    y += kStatsLine / 2;
    kit.text(screen, kStatsFrame.x + 8, y, kit.ui(kLastMonthText));
    y += kStatsLine;
    line(kTakingsText, Park::income(month), kIndent);
    line(kTakingsText + 1, -Park::expenditure(month), kIndent);
    line(kTakingsText + 2, Park::income(month) - Park::expenditure(month), kIndent);
    kit.button(screen, WindowKit::Button::kOk, kStatsOk, WindowKit::kPlain);
}

// Loans.

ScreenRect LoansWindow::placeOf(Control control, size_t row) {
    switch (control) {
    case Control::kAvailable:
        return kAvailableTab;
    case Control::kOutstanding:
        return kOutstandingTab;
    case Control::kRow:
        return {kLoanList.x + 2, kLoanList.y + 3 + int(row) * kRowHeight, kLoanList.w - 4, kRowHeight};
    case Control::kAct:
        return kLoanAct;
    case Control::kYes:
        return kAskYes;
    case Control::kNo:
        return kAskNo;
    case Control::kOk:
        return kLoansOk;
    case Control::kClose:
        return kLoansClose;
    default:
        return {};
    }
}

WindowResult LoansWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = Control::kNone;
    // Asking, only its buttons count.
    if (_asking != Asking::kNothing) {
        for (Control c : {Control::kYes, Control::kNo})
            if (placeOf(c).contains(ev.mouseX, ev.mouseY))
                _over = c;
        if (!isClick(ev) || _over == Control::kNone)
            return WindowResult::kNone;
        if (_over == Control::kYes && _asking == Asking::kTake) {
            const std::vector<Park::Loan> offers = park.availableLoans();
            if (_row < offers.size())
                park.takeLoan(offers[_row].offer);
        } else if (_over == Control::kYes && _asking == Asking::kRepay) {
            park.repayLoan(_row);
        }
        _asking = Asking::kNothing;
        _row = 0;
        return WindowResult::kNone;
    }
    const size_t rows = _outstanding ? park.loans().size() : park.availableLoans().size();
    size_t under = 0;
    for (size_t i = 0; i < rows; ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kRow;
            under = i;
        }
    for (Control c : {Control::kAvailable, Control::kOutstanding, Control::kAct, Control::kOk, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!isClick(ev))
        return WindowResult::kNone;
    switch (_over) {
    case Control::kAvailable:
    case Control::kOutstanding:
        _outstanding = _over == Control::kOutstanding;
        _row = 0;
        return WindowResult::kNone;
    case Control::kRow:
        _row = under;
        return WindowResult::kNone;
    case Control::kAct:
        if (_row >= rows)
            return WindowResult::kNone;
        _asking = !_outstanding                               ? Asking::kTake
                  : park.cash() >= park.loans()[_row].balance ? Asking::kRepay
                                                              : Asking::kCantRepay;
        return WindowResult::kNone;
    case Control::kOk:
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void LoansWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kLoansWindow, kit.ui(843));
    kit.button(screen, WindowKit::Button::kClose, kLoansClose,
               _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    kit.header(screen, kAvailableTab, kit.ui(kAvailableLoansText));
    kit.header(screen, kOutstandingTab, kit.ui(kOutstandingLoansText));
    // The list shown marked under its tab.
    const ScreenRect &tab = _outstanding ? kOutstandingTab : kAvailableTab;
    fill(screen, {tab.x + 4, tab.y + tab.h, tab.w - 8, 3}, kChosenRow);
    kit.frame(screen, kLoanList);
    std::vector<std::vector<std::string>> cells;
    if (_outstanding) {
        for (size_t c = 0; c < kOutstandingColumns.size(); ++c)
            kit.header(screen, kOutstandingHeaders[c], kit.ui(kOutstandingColumns[c]));
        for (const Park::Loan &l : park.loans())
            cells.push_back({kit.lender(l.lender), money(l.amount), money(l.repayment), std::to_string(l.monthsLeft),
                             money(l.balance)});
    } else {
        for (size_t c = 0; c < kAvailableColumns.size(); ++c)
            kit.header(screen, kAvailableHeaders[c], kit.ui(kAvailableColumns[c]));
        for (const Park::Loan &l : park.availableLoans()) {
            char rate[16];
            std::snprintf(rate, sizeof rate, "%g %%", double(l.rate));
            cells.push_back({kit.lender(l.lender), std::to_string(l.months), money(l.amount), rate, money(l.repayment),
                             money(l.totalPayable())});
        }
    }
    for (size_t r = 0; r < cells.size(); ++r) {
        const ScreenRect place = placeOf(Control::kRow, r);
        if (r == _row)
            fill(screen, place, kChosenRow);
        for (size_t c = 0; c < cells[r].size(); ++c) {
            const ScreenRect &h = _outstanding ? kOutstandingHeaders[c] : kAvailableHeaders[c];
            // Names from the left; numbers to the right.
            const int x = c == 0 ? h.x + 2 : h.x + h.w - 8 - kit.textWidth(cells[r][c]);
            kit.text(screen, x, place.y + 1, cells[r][c]);
        }
    }
    if (!cells.empty())
        kit.header(screen, kLoanAct, kit.ui(_outstanding ? kRepayTitle : kTakeTitle));
    kit.button(screen, WindowKit::Button::kOk, kLoansOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
    if (_asking == Asking::kNothing)
        return;
    // The question, over the list.
    const bool taking = _asking == Asking::kTake;
    kit.window(screen, kAskWindow, kit.ui(taking ? kTakeTitle : kRepayTitle));
    int amount = 0;
    if (taking && _row < park.availableLoans().size())
        amount = park.availableLoans()[_row].amount;
    else if (!taking && _row < park.loans().size())
        amount = park.loans()[_row].balance;
    // The strings' first line is their title; the question follows.
    std::string text = kit.ui(taking                      ? kTakeText
                              : _asking == Asking::kRepay ? kRepayText
                                                          : kCantRepayText,
                              {{18, std::to_string(amount)}});
    if (const size_t start = text.find_first_not_of('\n'); start != std::string::npos)
        text = text.substr(start);
    kit.paragraph(screen, kAskWindow.x + 18, kAskWindow.y + 40, kAskWindow.w - 36, text);
    kit.button(screen, WindowKit::Button::kOk, kAskYes, _over == Control::kYes ? WindowKit::kLit : WindowKit::kPlain);
    if (_asking != Asking::kCantRepay)
        kit.button(screen, WindowKit::Button::kCancel, kAskNo,
                   _over == Control::kNo ? WindowKit::kLit : WindowKit::kPlain);
}

// The Share Portfolio.

ScreenRect PortfolioWindow::okRect() {
    return kPortfolioOk;
}

WindowResult PortfolioWindow::handle(const Event &ev) {
    if (!_open || !isClick(ev) || !kPortfolioOk.contains(ev.mouseX, ev.mouseY))
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kClosed;
}

void PortfolioWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    // "Portfolio details for {0}, {12}": the player's name and rank (guessed).
    kit.window(screen, kPortfolioWindow,
               _owner.empty() ? kit.ui(kSharePortfolioTitle)
                              : kit.ui(kPortfolioFor, {{0, _owner}, {12, kit.rank(_rank)}}));
    kit.frame(screen, kPortfolioFrame);
    const Park::Shares &shares = park.shares();
    int y = kPortfolioFrame.y + 8;
    for (auto [label, value] :
         {std::pair{kSharePriceText, money(shares.price)}, std::pair{kSharesOwnedText, std::to_string(shares.owned)},
          std::pair{kPortfolioValueText, money(shares.owned * shares.price)}}) {
        row(screen, kit, kPortfolioFrame, y, kit.ui(label), value);
        y += kStatsLine;
    }
    // A whole percent, as the Personnel File shows it ("3%" for 1,000 of 30,000).
    const int stake = shares.issued > 0 ? shares.owned * 100 / shares.issued : 0;
    kit.paragraph(screen, kPortfolioFrame.x + 8, y + 6, kPortfolioFrame.w - 16,
                  kit.ui(kStakeText, {{21, std::to_string(stake)}}));
    kit.button(screen, WindowKit::Button::kOk, kPortfolioOk, WindowKit::kPlain);
}

// Attraction Finance Controls.

ScreenRect AttractionFinanceWindow::placeOf(Control control) {
    switch (control) {
    case Control::kKindLeft:
        return kPricesKindLeft;
    case Control::kKindRight:
        return kPricesKindRight;
    case Control::kLeft:
        return kPricesLeft;
    case Control::kRight:
        return kPricesRight;
    case Control::kMinus:
        return kPricesMinus;
    case Control::kPlus:
        return kPricesPlus;
    case Control::kOk:
        return kPricesOk;
    case Control::kClose:
        return kPricesClose;
    default:
        return {};
    }
}

WindowResult AttractionFinanceWindow::handle(const Event &ev, Park &park) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return WindowResult::kNone;
    _over = Control::kNone;
    for (Control c : {Control::kKindLeft, Control::kKindRight, Control::kLeft, Control::kRight, Control::kMinus,
                      Control::kPlus, Control::kOk, Control::kClose})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (!isClick(ev))
        return WindowResult::kNone;
    constexpr int kHows = 3;
    const size_t at = size_t(std::find(kPriceKinds.begin(), kPriceKinds.end(), _kind) - kPriceKinds.begin());
    switch (_over) {
    case Control::kKindLeft:
    case Control::kKindRight:
        _kind = kPriceKinds[(at + (_over == Control::kKindRight ? 1 : kPriceKinds.size() - 1)) % kPriceKinds.size()];
        return WindowResult::kNone;
    case Control::kLeft:
    case Control::kRight:
        _how = Park::Adjustment((int(_how) + (_over == Control::kRight ? 1 : kHows - 1)) % kHows);
        return WindowResult::kNone;
    case Control::kMinus:
    case Control::kPlus:
        _amount += _over == Control::kPlus ? 1 : -1;
        return WindowResult::kNone;
    case Control::kOk:
        park.adjustPrices(_kind, _how, _amount);
        _amount = 0;
        return WindowResult::kChose;
    case Control::kClose:
        _open = false;
        return WindowResult::kClosed;
    default:
        return WindowResult::kNone;
    }
}

void AttractionFinanceWindow::draw(Surface &screen, const WindowKit &kit, const Park &park) const {
    if (!_open)
        return;
    kit.window(screen, kPricesWindow, kit.ui(kAttractionFinanceTitle));
    kit.button(screen, WindowKit::Button::kClose, kPricesClose,
               _over == Control::kClose ? WindowKit::kLit : WindowKit::kPlain);
    auto picker = [&](const ScreenRect &left, const ScreenRect &right, Control l, Control r, const std::string &label) {
        kit.button(screen, WindowKit::Button::kLeft, left, _over == l ? WindowKit::kLit : WindowKit::kPlain);
        kit.button(screen, WindowKit::Button::kRight, right, _over == r ? WindowKit::kLit : WindowKit::kPlain);
        kit.text(screen, (left.x + left.w + right.x - kit.textWidth(label)) / 2, left.y + 8, label);
    };
    picker(kPricesKindLeft, kPricesKindRight, Control::kKindLeft, Control::kKindRight,
           kit.ui(kPriceKindText + uint32_t(_kind)));
    // The kind's prices as they stand: their average.
    int total = 0;
    int count = 0;
    if (_kind == Park::PriceKind::kEntry) {
        total = park.admissionFee();
        count = 1;
    }
    for (const Park::Queue &q : park.queues()) {
        const Attractions::Attraction *a =
            q.object >= 0 ? park.attractions().find(park.objects()[size_t(q.object)].id) : nullptr;
        const bool match = a && ((_kind == Park::PriceKind::kRide && a->kind == Attractions::Kind::kRide) ||
                                 (_kind == Park::PriceKind::kShop && a->kind == Attractions::Kind::kShop) ||
                                 (_kind == Park::PriceKind::kSideshow && a->kind == Attractions::Kind::kSideshow) ||
                                 (_kind == Park::PriceKind::kBathroom && a->kind == Attractions::Kind::kFacility));
        if (match) {
            total += q.fare;
            ++count;
        }
    }
    kit.frame(screen, kPricesCurrent);
    const std::string current = count ? money(total / count) : "-";
    kit.text(screen, kPricesCurrent.x + (kPricesCurrent.w - kit.textWidth(current)) / 2, kPricesCurrent.y + 9, current);
    kit.text(screen, kPricesLeft.x, kPricesLeft.y - 18, kit.ui(kAdjustmentTypeText));
    picker(kPricesLeft, kPricesRight, Control::kLeft, Control::kRight, kit.ui(kSetToAmountText + uint32_t(_how)));
    kit.button(screen, WindowKit::Button::kMinus, kPricesMinus,
               _over == Control::kMinus ? WindowKit::kLit : WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kPlus, kPricesPlus,
               _over == Control::kPlus ? WindowKit::kLit : WindowKit::kPlain);
    const std::string amount = _how == Park::Adjustment::kPercent ? std::to_string(_amount) + " %" : money(_amount);
    kit.text(screen, (kPricesMinus.x + kPricesMinus.w + kPricesPlus.x - kit.textWidth(amount)) / 2, kPricesMinus.y + 8,
             amount);
    kit.button(screen, WindowKit::Button::kOk, kPricesOk, _over == Control::kOk ? WindowKit::kLit : WindowKit::kPlain);
}

// A message.

namespace {

// Its text and lines, as they're drawn.
std::vector<std::string> messageLines(const WindowKit &kit, const Park::Message &m) {
    return kit.messageText(m);
}

// How tall it is: measured by drawing it where it can't be seen.
int messageHeight(const WindowKit &kit, const Park::Message &m) {
    Surface scratch;
    scratch.create(kMessageWidth, 1, PixelFormat::kRGBA32);
    int y = 44;
    for (const std::string &text : messageLines(kit, m))
        y = kit.paragraph(scratch, 18, y, kMessageWidth - 36, text) + 4;
    return y + 56;
}

ScreenRect messageOk(int height) {
    return {kMessageX + kMessageWidth - 44, kMessageY + height - 40, 28, 28};
}

} // namespace

ScreenRect MessageWindow::okRect() const {
    return messageOk(_height);
}

WindowResult MessageWindow::handle(const Event &ev) {
    if (!_open || !isClick(ev) || !okRect().contains(ev.mouseX, ev.mouseY))
        return WindowResult::kNone;
    _open = false;
    return WindowResult::kClosed;
}

void MessageWindow::draw(Surface &screen, const WindowKit &kit) const {
    if (!_open)
        return;
    const int height = messageHeight(kit, _message);
    _height = height;
    kit.window(screen, {kMessageX, kMessageY, kMessageWidth, height}, kit.messageTitle(_message));
    kit.frame(screen, {kMessageX + 12, kMessageY + 36, kMessageWidth - 24, height - 84});
    int y = kMessageY + 44;
    for (const std::string &text : messageLines(kit, _message))
        y = kit.paragraph(screen, kMessageX + 18, y, kMessageWidth - 36, text) + 4;
    kit.button(screen, WindowKit::Button::kOk, messageOk(height), WindowKit::kPlain);
}

// Choosing an ability to learn, and staff to clear the obstacle: laid
// out by guess, in the staff list's style.

namespace {

constexpr uint32_t kSelectAbilityText = 705;
constexpr uint32_t kAbilityColumnText = 706;
// "There are currently no special abilities available for janitors", for
// engineers, gardeners, guards; entertainers and scientists can't learn.
constexpr std::array<uint32_t, Park::kStaffKinds> kNoAbilitiesText{708, 707, 709, 710, 711, 712};
constexpr uint32_t kAlreadyTrainedText = 714;
constexpr uint32_t kLoseAbilityText = 715;
constexpr uint32_t kSelectEmployeeText = 695;
constexpr std::array<uint32_t, 5> kEmployeeColumns{696, 697, 698, 699, 700};
constexpr ScreenRect kChooseWindow{80, 70, 480, 340};
constexpr ScreenRect kChooseList{95, 132, 450, 200};
constexpr int kChooseRow = 16;
constexpr ScreenRect kChooseNote{95, 338, 340, 54};
constexpr ScreenRect kChooseOk{482, 362, 28, 28};
constexpr ScreenRect kChooseCancel{516, 364, 26, 26};
constexpr std::array<ScreenRect, 5> kEmployeeHeaders{
    {{95, 106, 110, 22}, {207, 106, 110, 22}, {319, 106, 74, 22}, {395, 106, 74, 22}, {471, 106, 74, 22}}};
constexpr size_t kChooseRows = size_t(kChooseList.h / kChooseRow);

ScreenRect chooseRow(size_t index) {
    return {kChooseList.x + 4, kChooseList.y + 4 + int(index) * kChooseRow, kChooseList.w - 8, kChooseRow};
}

} // namespace

ScreenRect AbilityWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kRow:
        return chooseRow(index);
    case Control::kOk:
        return kChooseOk;
    case Control::kCancel:
        return kChooseCancel;
    case Control::kNone:
        break;
    }
    return {};
}

WindowResult AbilityWindow::handle(const Event &ev) {
    if (!_open || !isClick(ev))
        return WindowResult::kNone;
    if (kChooseCancel.contains(ev.mouseX, ev.mouseY)) {
        _open = false;
        return WindowResult::kClosed;
    }
    if (kChooseOk.contains(ev.mouseX, ev.mouseY)) {
        // With none to learn, the tick sends them for the experience alone.
        if (_chosen < 0 && !_abilities.empty())
            return WindowResult::kNone;
        if (_chosen >= 0 && chosen() == _held) {
            _note = kAlreadyTrainedText;
            return WindowResult::kNone;
        }
        // Another ability held: asked once, then gone.
        if (_chosen >= 0 && _held && !_asking) {
            _asking = true;
            return WindowResult::kNone;
        }
        _open = false;
        return WindowResult::kChose;
    }
    for (size_t i = 0; i < std::min(_abilities.size(), kChooseRows); ++i)
        if (chooseRow(i).contains(ev.mouseX, ev.mouseY)) {
            _chosen = int(i);
            _note = 0;
            _asking = false;
        }
    return WindowResult::kNone;
}

void AbilityWindow::draw(Surface &screen, const WindowKit &kit) const {
    if (!_open)
        return;
    kit.window(screen, kChooseWindow, kit.ui(kSelectAbilityText));
    kit.header(screen, {kChooseList.x, 106, kChooseList.w, 22}, kit.ui(kAbilityColumnText));
    kit.frame(screen, kChooseList);
    if (_abilities.empty())
        kit.paragraph(screen, kChooseList.x + 8, kChooseList.y + 8, kChooseList.w - 16,
                      kit.ui(kNoAbilitiesText[size_t(_kind)]));
    for (size_t i = 0; i < std::min(_abilities.size(), kChooseRows); ++i) {
        const ScreenRect r = chooseRow(i);
        if (int(i) == _chosen)
            fill(screen, r, kChosenRow);
        kit.text(screen, r.x + 4, r.y + 2, kit.ability(_abilities[i]));
    }
    if (_note || _asking)
        kit.paragraph(screen, kChooseNote.x, kChooseNote.y, kChooseNote.w, kit.ui(_asking ? kLoseAbilityText : _note));
    kit.button(screen, WindowKit::Button::kOk, kChooseOk, WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kCancel, kChooseCancel, WindowKit::kPlain);
}

ScreenRect EmployeeWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kRow:
        return chooseRow(index);
    case Control::kOk:
        return kChooseOk;
    case Control::kCancel:
        return kChooseCancel;
    case Control::kNone:
        break;
    }
    return {};
}

WindowResult EmployeeWindow::handle(const Event &ev) {
    if (!_open || !isClick(ev))
        return WindowResult::kNone;
    if (kChooseCancel.contains(ev.mouseX, ev.mouseY)) {
        _open = false;
        return WindowResult::kClosed;
    }
    if (kChooseOk.contains(ev.mouseX, ev.mouseY)) {
        _open = false;
        return WindowResult::kChose;
    }
    for (size_t i = 0; i < std::min(_staff.size(), kChooseRows); ++i)
        if (chooseRow(i).contains(ev.mouseX, ev.mouseY)) {
            const auto at = std::find(_chosen.begin(), _chosen.end(), _staff[i]);
            if (at == _chosen.end())
                _chosen.push_back(_staff[i]);
            else
                _chosen.erase(at);
        }
    return WindowResult::kNone;
}

void EmployeeWindow::draw(Surface &screen, const WindowKit &kit, const Park &park, const HireWindow &names) const {
    if (!_open)
        return;
    kit.window(screen, kChooseWindow, kit.ui(kSelectEmployeeText));
    for (size_t c = 0; c < kEmployeeHeaders.size(); ++c)
        kit.header(screen, kEmployeeHeaders[c], kit.ui(kEmployeeColumns[c]));
    kit.frame(screen, kChooseList);
    for (size_t i = 0; i < std::min(_staff.size(), kChooseRows); ++i) {
        const Park::StaffMember *m = park.staffMember(_staff[i]);
        if (!m)
            continue;
        const ScreenRect r = chooseRow(i);
        if (std::find(_chosen.begin(), _chosen.end(), m->id) != _chosen.end())
            fill(screen, r, kChosenRow);
        kit.text(screen, kEmployeeHeaders[0].x + 2, r.y + 2, names.nameOf(m->kind, m->name));
        kit.text(screen, kEmployeeHeaders[1].x + 2, r.y + 2, kit.ability(m->ability));
        kit.bar(screen, kEmployeeHeaders[2].x + 2, r.y + 4, kEmployeeHeaders[2].w - 8,
                float(m->experience) / float(Park::kMostExperience));
        kit.bar(screen, kEmployeeHeaders[3].x + 2, r.y + 4, kEmployeeHeaders[3].w - 8, m->happiness / 100);
        kit.text(screen, kEmployeeHeaders[4].x + 2, r.y + 2, kit.staffState(*m));
    }
    kit.button(screen, WindowKit::Button::kOk, kChooseOk, WindowKit::kPlain);
    kit.button(screen, WindowKit::Button::kCancel, kChooseCancel, WindowKit::kPlain);
}

} // namespace reluva::tpw
