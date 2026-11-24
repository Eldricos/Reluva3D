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
#include <cmath>
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
// The staff's states in THOUGHTS.str (its staff's thoughts, 22 on).
constexpr uint32_t kHappyThought = 22;
constexpr uint32_t kTiredThought = 24;
constexpr uint32_t kStrikeThought = 25;
constexpr uint32_t kTrainingThought = 26;
constexpr uint32_t kStaffRoomThought = 28;
constexpr uint32_t kJobThought = 29;

// The right-click menu: its title's line, then a line an item.
constexpr int kMenuWidth = 130;
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
constexpr ScreenRect kSideBox{462, 104, 133, 55};
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
    return ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft;
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
    for (int y = std::max(0, r.y); y < std::min(s.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(s.w, r.x + r.w); ++x) {
            uint8_t *p = s.row(y) + x * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
        }
}

std::string money(int amount) {
    return "$ " + std::to_string(amount);
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
    return readStringTable(files, folder, "THOUGHTS", _characters, _thoughts);
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

void WindowKit::button(Surface &screen, Button b, const ScreenRect &r, size_t state) const {
    drawUiMesh(screen, _buttons[size_t(b)], state, r);
}

void WindowKit::header(Surface &screen, const ScreenRect &r, const std::string &label) const {
    drawUiMesh(screen, _header, 0, r);
    text(screen, r.x + (r.w - textWidth(label)) / 2, r.y + r.h / 2 - 4, label);
}

void WindowKit::staffTab(Surface &screen, Park::StaffKind kind, const ScreenRect &r, size_t state) const {
    drawUiMesh(screen, _tabs[size_t(kind)], state, r);
}

std::string WindowKit::ui(uint32_t index, const std::map<int, std::string> &values) const {
    return fillGameString(_ui, index, values);
}

// The menu.

void PopupMenu::open(std::string title, std::vector<uint32_t> items, int x, int y) {
    _open = true;
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
        kit.text(screen, p.x + kMenuIndent, itemRect(i).y + 2, kit.ui(_items[i]),
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
    if (a && a->excitement >= 0)
        rating(kExcitementText, float(a->excitement) / 100);
    if (a && a->wearRate > 0) {
        rating(kReliabilityText, 1 - float(a->wearRate) / 10);
        rating(kRepairText, o.repair / 100);
    }
    line(kRunningCostText, money(a ? a->runningCost : 0));
    line(kScrapText, money(park.scrapValue(_object)));
    if (a && a->capacity >= 0) {
        kit.frame(screen, kSideBox);
        const std::string capacity = kit.ui(kCapacityText) + " : " + std::to_string(a->capacity);
        kit.text(screen, kSideBox.x + (kSideBox.w - kit.textWidth(capacity)) / 2, kSideBox.y + kSideBox.h / 2 - 4,
                 capacity);
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
        switch (park.sendForTraining(_staff)) {
        case Park::Training::kTooInexperienced:
            _message = kNotExperiencedText;
            break;
        case Park::Training::kNoRoom:
            _message = kNoTrainingRoomText;
            break;
        default:
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
    uint32_t status = kJobThought;
    switch (m->doing) {
    case Park::StaffDoing::kGoingToRest:
    case Park::StaffDoing::kResting:
        status = kStaffRoomThought;
        break;
    case Park::StaffDoing::kGoingToTrain:
    case Park::StaffDoing::kTraining:
        status = kTrainingThought;
        break;
    case Park::StaffDoing::kStriking:
        status = kStrikeThought;
        break;
    case Park::StaffDoing::kWorking:
        status = m->energy < 50 ? kTiredThought : m->working ? kJobThought : kHappyThought;
        break;
    }
    line(kStatusText, kit.thought(status));
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

} // namespace reluva::tpw
