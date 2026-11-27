// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The places are measured on the recording, at 640 × 480, the models
// matched against its frames at 2:38 and 5:00, the menus at 2:36 to 11:47:
// docs/engines/tpw/games/simcoaster/systems/park.md#the-hud.

#include "engines/tpw/ui/park_hud.h"

#include "engines/tpw/game_files.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace reluva::tpw {

namespace {

// A control's meshes, in the order the models name them
// (docs/engines/tpw/games/simcoaster/formats/ui-models.md#states).
constexpr size_t kPlain = 0;
constexpr size_t kDisabled = 1;
constexpr size_t kLit = 2;
constexpr size_t kLitDown = 3;
constexpr size_t kHeldDown = 4;
constexpr size_t kDown = 5;

constexpr ScreenRect kGauge{14, 5, 36, 96};
// The thermometer's tube: its model is full, its liquid light blue; the
// recording's is black above the level, all of it in a bare park.
constexpr ScreenRect kTube{26, 11, 13, 57};
constexpr ScreenRect kFace{17, 69, 30, 30};
constexpr ScreenRect kCompass{553, 7, 80, 80};
constexpr ScreenRect kDial{569, 23, 48, 48};
constexpr ScreenRect kDateBox{554, 89, 80, 32};
constexpr ScreenRect kDateField{566, 97, 60, 16};
constexpr ScreenRect kToolBar{7, 249, 48, 218};
constexpr ScreenRect kGizmoBar{163, 436, 360, 13};
constexpr ScreenRect kHelp{570, 414, 36, 36};

constexpr int kToolX = 12;
constexpr int kToolSize = 38;
constexpr std::array<int, ParkHud::kTools> kToolY{253, 296, 339, 381, 424};
constexpr int kGizmoY = 425;
constexpr int kGizmoSize = 48;
constexpr std::array<int, ParkHud::kGizmos> kGizmoX{107, 181, 237, 293, 349, 405, 461};
constexpr int kCompassButtonSize = 40;

// A menu: its title and items as UITEXT numbers them, in the order the
// recording shows them; Finance's and Maps' are never opened there, so
// theirs is the table's (guessed). The messages' button has none.
struct Menu {
    uint32_t title = 0;
    std::vector<uint32_t> items;
};
const std::array<Menu, ParkHud::kGizmos> &menus() {
    static const std::array<Menu, ParkHud::kGizmos> kMenus{{
        {},
        {832, {833, 835, 834, 836}},
        {826, {827, 829, 828, 830, 831}},
        {837, {840, 838, 839}},
        {841, {842, 843, 844}},
        {845, {847, 850, 846, ParkHud::kObjectivesItem, 849, 600}},
        {851, {852, 853, 854}},
    }};
    return kMenus;
}
// Research Lab: greyed in a park that can't research yet, as the
// recording's first mission's is.
// Greyed till the park has a research room ("You need to build a
// research lab", UITEXT 104).
constexpr uint32_t kResearchLabItem = 834;

// A menu's panel: its left 49 pixels left of its button, 147 across; its
// foot on the bar, 38 pixels for its title and 18 an item; its title and
// items from 10 in. Its tail joins it to the button.
constexpr int kMenuLeft = -49;
constexpr int kMenuWidth = 147;
constexpr int kMenuFoot = 416;
constexpr int kMenuHead = 38;
constexpr int kMenuLine = 18;
constexpr int kMenuIndent = 10;
constexpr int kMenuTitleY = 10;
constexpr int kTailWidth = 32;
constexpr int kTailHeight = 16;
constexpr int kTailY = 413;
constexpr Color kMenuTitle{236, 253, 255};
constexpr Color kMenuItem{166, 193, 218};
constexpr Color kMenuItemLit{255, 255, 255};
constexpr Color kMenuItemGreyed{40, 80, 115};

// The tooltips' strings in UIHELPTEXT, for the tools, the bottom buttons
// and the compass's buttons.
constexpr std::array<uint32_t, ParkHud::kTools> kToolTips{116, 117, 118, 119, 120};
constexpr std::array<uint32_t, ParkHud::kGizmos> kGizmoTips{167, 169, 168, 170, 171, 172, 173};
constexpr std::array<uint32_t, ParkHud::kCompassButtons> kCompassTips{112, 113, 102, 103};
// The thermometer's: "Overall visitor happiness: {2}%".
constexpr uint32_t kGaugeTip = 336;
// The liquid's colours at its top and foot, from the recording's.
constexpr Color kLiquidTop{232, 126, 16};
constexpr Color kLiquidFoot{42, 140, 186};
// The mood's percent a face takes.
constexpr size_t kMoodBand = 20;

constexpr std::array<const char *, ParkHud::kTools> kToolModels{
    "ui/tb_pointer.MD2", "ui/tb_path.MD2", "ui/tb_delete.MD2", "ui/tb_camera.MD2", "ui/tb_postcard.MD2"};
constexpr std::array<const char *, ParkHud::kGizmos> kGizmoModels{
    "ui/giz_message.MD2", "ui/giz_attract.MD2", "ui/giz_staff.MD2", "ui/giz_visitors.MD2",
    "ui/giz_finance.MD2", "ui/giz_park.MD2",    "ui/giz_maps.MD2"};
constexpr std::array<const char *, ParkHud::kCompassButtons> kCompassModels{
    "ui/comp_rotatel.MD2", "ui/comp_rotater.MD2", "ui/comp_zoomin.MD2", "ui/comp_zoomout.MD2"};

} // namespace

Error ParkHud::load(const SearchSet &files) {
    if (Error err = _tip.load(files))
        return err;
    std::array<std::pair<LoadedModel *, const char *>, 10> singles{{
        {&_menuPanel, "ui/!f_plain.MD2"},
        {&_menuTail, "ui/gizmojoin.MD2"},
        {&_gauge, "ui/happyguage.MD2"},
        {&_faces, "ui/face.MD2"},
        {&_compassPlate, "ui/comp.MD2"},
        {&_compassDial, "ui/compgizn.MD2"},
        {&_dateBox, "ui/datebox.MD2"},
        {&_toolBar, "ui/toolbox.MD2"},
        {&_gizmoBar, "ui/gizmo.MD2"},
        {&_help, "ui/b_help.MD2"},
    }};
    for (auto [model, name] : singles)
        if (Error err = model->load(files, name))
            return err;
    for (size_t i = 0; i < kTools; ++i)
        if (Error err = _tools[i].load(files, kToolModels[i]))
            return err;
    for (size_t i = 0; i < kGizmos; ++i)
        if (Error err = _gizmos[i].load(files, kGizmoModels[i]))
            return err;
    for (size_t i = 0; i < kCompassButtons; ++i)
        if (Error err = _compass[i].load(files, kCompassModels[i]))
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
    for (auto [font, name] : {std::pair{&_titleFont, "TITLEMED"}, std::pair{&_itemFont, "GAME7AA"}})
        if (Error err = readGameFont(files, folder, name, *font))
            return err;
    if (Error err = readStringTable(files, folder, "UITEXT", _characters, _uiText))
        return err;
    return Error::success();
}

ScreenRect ParkHud::menuRect(Gizmo gizmo) {
    const Menu &menu = menus()[size_t(gizmo)];
    if (menu.items.empty())
        return {};
    const int height = kMenuHead + kMenuLine * int(menu.items.size());
    return {gizmoRect(gizmo).x + kMenuLeft, kMenuFoot - height, kMenuWidth, height};
}

ScreenRect ParkHud::menuItemRect(Gizmo gizmo, size_t item) {
    const ScreenRect panel = menuRect(gizmo);
    if (panel.w == 0 || item >= menus()[size_t(gizmo)].items.size())
        return {};
    return {panel.x, panel.y + kMenuHead + kMenuLine * int(item) - kMenuLine / 4, panel.w, kMenuLine};
}

int ParkHud::menuItemAt(int x, int y) const {
    if (!menuRisen())
        return -1;
    const Menu &menu = menus()[size_t(_menu)];
    for (size_t i = 0; i < menu.items.size(); ++i)
        if (menuItemRect(Gizmo(_menu), i).contains(x, y) && (menu.items[i] != kResearchLabItem || _researchLab))
            return int(i);
    return -1;
}

bool ParkHud::covers(int x, int y) const {
    return spotAt(x, y).kind != Hit::kNone || (_menu >= 0 && menuRect(Gizmo(_menu)).contains(x, y)) ||
           kGauge.contains(x, y) || kToolBar.contains(x, y) || kCompass.contains(x, y) || kDateBox.contains(x, y);
}

uint32_t ParkHud::takeChoice() {
    return std::exchange(_choice, 0);
}

ScreenRect ParkHud::toolRect(Tool tool) {
    return {kToolX, kToolY[size_t(tool)], kToolSize, kToolSize};
}

ScreenRect ParkHud::gizmoRect(Gizmo gizmo) {
    return {kGizmoX[size_t(gizmo)], kGizmoY, kGizmoSize, kGizmoSize};
}

ScreenRect ParkHud::compassRect(CompassButton button) {
    // Turning above, zooming below; left, then right.
    const int i = int(button);
    return {kCompass.x + (i % 2) * kCompassButtonSize, kCompass.y + (i / 2) * kCompassButtonSize, kCompassButtonSize,
            kCompassButtonSize};
}

ScreenRect ParkHud::dateField() {
    return kDateField;
}

ParkHud::Spot ParkHud::spotAt(int x, int y) {
    for (size_t i = 0; i < kTools; ++i)
        if (toolRect(Tool(i)).contains(x, y))
            return {Hit::kTool, i};
    for (size_t i = 0; i < kGizmos; ++i)
        if (gizmoRect(Gizmo(i)).contains(x, y))
            return {Hit::kGizmo, i};
    for (size_t i = 0; i < kCompassButtons; ++i)
        if (compassRect(CompassButton(i)).contains(x, y))
            return {Hit::kCompass, i};
    if (kHelp.contains(x, y))
        return {Hit::kHelp, 0};
    if (kGauge.contains(x, y))
        return {Hit::kGauge, 0};
    return {};
}

bool ParkHud::handle(const Event &ev) {
    if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp) {
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
        const Spot over = spotAt(ev.mouseX, ev.mouseY);
        if (!(over == _over))
            _overSince = _clock;
        _over = over;
    }
    const bool overMenu = _menu >= 0 && menuRect(Gizmo(_menu)).contains(ev.mouseX, ev.mouseY);
    switch (ev.type) {
    case EventType::kMouseMove:
        _menuLit = menuItemAt(ev.mouseX, ev.mouseY);
        return _over.kind != Hit::kNone || overMenu;
    case EventType::kMouseDown: {
        if (_menu >= 0 && !(_over.kind == Hit::kGizmo && int(_over.index) == _menu)) {
            // A click on an item picks it; any other closes the menu, and
            // goes no further unless it's on another button.
            if (ev.button == MouseButton::kLeft && overMenu) {
                const int item = menuItemAt(ev.mouseX, ev.mouseY);
                if (item < 0)
                    return true;
                _choice = menus()[size_t(_menu)].items[size_t(item)];
            }
            _menu = -1;
            _menuLit = -1;
            if (_over.kind == Hit::kNone)
                return true;
        }
        if (ev.button != MouseButton::kLeft) {
            // "Right-click to Buy Attractions" (UIHELPTEXT 169).
            if (ev.button == MouseButton::kRight && _over.kind == Hit::kGizmo &&
                Gizmo(_over.index) == Gizmo::kAttractions)
                _choice = kBuildAttractionItem;
            return _over.kind != Hit::kNone;
        }
        _pressed = _over;
        return _pressed.kind != Hit::kNone || kGauge.contains(ev.mouseX, ev.mouseY) ||
               kToolBar.contains(ev.mouseX, ev.mouseY);
    }
    case EventType::kMouseUp: {
        if (ev.button != MouseButton::kLeft || _pressed.kind == Hit::kNone)
            return false;
        // A tool is chosen, or a menu opened or closed, as the button is
        // let go over it.
        if (_over == _pressed) {
            if (_pressed.kind == Hit::kTool) {
                _tool = Tool(_pressed.index);
            } else if (_pressed.kind == Hit::kGizmo && Gizmo(_pressed.index) == Gizmo::kMessages) {
                _choice = kMessageCenterItem;
            } else if (_pressed.kind == Hit::kGizmo && !menus()[_pressed.index].items.empty()) {
                if (_menu == int(_pressed.index)) {
                    _menu = -1;
                } else {
                    _menu = int(_pressed.index);
                    _menuOpened = _clock;
                }
                _menuLit = -1;
            }
        }
        _pressed = {};
        return true;
    }
    default:
        return false;
    }
}

bool ParkHud::held(CompassButton button) const {
    return _pressed.kind == Hit::kCompass && _pressed.index == size_t(button) && _over == _pressed;
}

size_t ParkHud::stateOf(const Spot &spot, bool chosen, bool disabled) const {
    if (disabled)
        return kDisabled;
    const bool over = _over == spot;
    if (over && _pressed == spot)
        return kHeldDown;
    if (chosen)
        return over ? kLitDown : kDown;
    return over ? kLit : kPlain;
}

// The tube filled to the mood, black above, the liquid orange at its top
// to light blue at its foot, as the recording has it at 2:50 to 24:00;
// the face by the mood, a fifth of it each, unhappiest first (guessed:
// the recording's smiles from about 60).
void ParkHud::draw(Surface &screen, const View &view) const {
    _mood = view.mood;
    drawUiMesh(screen, _gauge, 0, kGauge);
    const int filled = view.mood < 0 ? 0 : kTube.h * std::clamp(view.mood, 0, 100) / 100;
    const int surface = kTube.y + kTube.h - filled;
    for (int y = kTube.y; y < kTube.y + kTube.h; ++y) {
        uint8_t *row = screen.pixels.data() + size_t(y) * screen.pitch;
        // Its top rounded off.
        const int inset = y < kTube.y + 2 ? 2 : 0;
        Color c{0, 0, 0};
        if (y >= surface) {
            const float up = filled > 1 ? float(kTube.y + kTube.h - 1 - y) / float(filled - 1) : 1;
            c = {uint8_t(kLiquidFoot.r + (kLiquidTop.r - kLiquidFoot.r) * up),
                 uint8_t(kLiquidFoot.g + (kLiquidTop.g - kLiquidFoot.g) * up),
                 uint8_t(kLiquidFoot.b + (kLiquidTop.b - kLiquidFoot.b) * up)};
        }
        for (int x = kTube.x + inset; x < kTube.x + kTube.w - inset; ++x) {
            row[x * 4] = c.r;
            row[x * 4 + 1] = c.g;
            row[x * 4 + 2] = c.b;
            row[x * 4 + 3] = 255;
        }
    }
    const size_t face = view.mood < 0 ? kMoods - 1 : std::min<size_t>(kMoods - 2, size_t(view.mood) / kMoodBand);
    drawUiMesh(screen, _faces, face, kFace);

    drawUiMesh(screen, _compassPlate, 0, kCompass);
    for (size_t i = 0; i < kCompassButtons; ++i) {
        const bool disabled = (CompassButton(i) == CompassButton::kZoomIn && !view.canZoomIn) ||
                              (CompassButton(i) == CompassButton::kZoomOut && !view.canZoomOut);
        drawUiMesh(screen, _compass[i], stateOf({Hit::kCompass, i}, false, disabled), compassRect(CompassButton(i)));
    }
    drawUiMesh(screen, _compassDial, 0, kDial);
    drawUiMesh(screen, _dateBox, 0, kDateBox);

    drawUiMesh(screen, _toolBar, 0, kToolBar);
    for (size_t i = 0; i < kTools; ++i)
        drawUiMesh(screen, _tools[i], stateOf({Hit::kTool, i}, size_t(_tool) == i, false), toolRect(Tool(i)));

    drawUiMesh(screen, _gizmoBar, 0, kGizmoBar);
    for (size_t i = 0; i < kGizmos; ++i)
        drawUiMesh(screen, _gizmos[i], stateOf({Hit::kGizmo, i}, _menu == int(i), false), gizmoRect(Gizmo(i)));
    // The unread messages' count by the envelope, as the recording's "1"
    // at its foot, on a dark spot.
    if (view.unread > 0) {
        const ScreenRect envelope = gizmoRect(Gizmo::kMessages);
        const std::string count = std::to_string(view.unread);
        const int w = gameTextWidth(_itemFont, _characters, count);
        const ScreenRect spot{envelope.x + 26, envelope.y + 28, std::max(14, w + 6), 14};
        for (int y = spot.y; y < spot.y + spot.h && y < screen.h; ++y)
            for (int x = spot.x; x < spot.x + spot.w && x < screen.w; ++x) {
                uint8_t *p = screen.row(y) + x * 4;
                p[0] = p[1] = p[2] = 16;
            }
        drawGameText(screen, _itemFont, _characters, spot.x + (spot.w - w) / 2, spot.y + 3, count,
                     Color{255, 255, 255});
    }
    drawUiMesh(screen, _help, _over.kind == Hit::kHelp ? kLit : kPlain, kHelp);
    drawMenu(screen);
    drawTooltip(screen);
}

// It rises from the bar, slowing as it comes, its items shown once it's
// up.
void ParkHud::drawMenu(Surface &screen) const {
    if (_menu < 0)
        return;
    const Menu &menu = menus()[size_t(_menu)];
    const ScreenRect full = menuRect(Gizmo(_menu));
    const float t = std::min(1.0f, float(_clock - _menuOpened) / float(kMenuOpenMillis));
    const float risen = 1 - (1 - t) * (1 - t);
    ScreenRect panel = full;
    panel.h = std::max(1, int(float(full.h) * risen));
    panel.y = kMenuFoot - panel.h;
    const ScreenRect button = gizmoRect(Gizmo(_menu));
    drawUiMesh(screen, _menuTail, 0, {button.x + (button.w - kTailWidth) / 2, kTailY, kTailWidth, kTailHeight});
    drawUiMesh(screen, _menuPanel, 0, panel);
    if (t < 1)
        return;
    drawGameText(screen, _titleFont, _characters, full.x + kMenuIndent, full.y + kMenuTitleY, _uiText.text(menu.title),
                 kMenuTitle);
    for (size_t i = 0; i < menu.items.size(); ++i) {
        const Color c = menu.items[i] == kResearchLabItem && !_researchLab ? kMenuItemGreyed
                        : int(i) == _menuLit                               ? kMenuItemLit
                                                                           : kMenuItem;
        drawGameText(screen, _itemFont, _characters, full.x + kMenuIndent, full.y + kMenuHead + kMenuLine * int(i),
                     _uiText.text(menu.items[i]), c);
    }
}

uint32_t ParkHud::tooltipText() const {
    switch (_over.kind) {
    case Hit::kTool:
        return kToolTips[_over.index];
    case Hit::kGizmo:
        return kGizmoTips[_over.index];
    case Hit::kCompass:
        return kCompassTips[_over.index];
    case Hit::kGauge:
        return _mood >= 0 ? kGaugeTip : 0;
    default:
        return 0;
    }
}

void ParkHud::drawTooltip(Surface &screen) const {
    const uint32_t text = tooltipText();
    if (text == 0 || _pressed.kind != Hit::kNone)
        return;
    _tip.draw(screen, _mouseX, _mouseY,
              _over.kind == Hit::kGauge ? _tip.help(text, {{2, std::to_string(_mood)}}) : _tip.help(text),
              _clock - _overSince);
}

} // namespace reluva::tpw
