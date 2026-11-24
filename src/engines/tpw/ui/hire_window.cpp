// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Measured on the recording's Hire Gardeners window at 11:06, at 640 × 480:
// docs/engines/tpw/games/simcoaster/systems/staff.md#the-hire-staff-window.

#include "engines/tpw/ui/hire_window.h"

#include "engines/tpw/game_files.h"
#include "engines/tpw/textures.h"
#include "engines/tpw/ui/text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace reluva::tpw {

namespace {

// The window's strings in UITEXT, by Park::StaffKind: its titles, "Hire
// Janitors" and the rest; its columns; the agencies; the messages.
constexpr std::array<uint32_t, Park::kStaffKinds> kTitles{214, 212, 213, 216, 215, 217};
constexpr std::array<uint32_t, 4> kColumnTexts{218, 219, 220, 221};
constexpr uint32_t kFirstAgencyText = 110;
constexpr uint32_t kNeedsLabText = 222;
constexpr uint32_t kNoneLeftText = 225;

constexpr std::array<const char *, Park::kStaffKinds> kTabModels{"ui/tab_sengnr.MD2",    "ui/tab_scleaner.MD2",
                                                                 "ui/tab_sgardenr.MD2",  "ui/tab_sguard.MD2",
                                                                 "ui/tab_sentertnr.MD2", "ui/tab_sscientst.MD2"};
constexpr std::array<const char *, Park::kStaffKinds> kNameTables{
    "MECHANIC_NAMES", "HANDYMAN_NAMES", "GARDENER_NAMES", "GUARD_NAMES", "ENTERTAINER_NAMES", "RESEARCHER_NAMES"};
// The staff's sprites; the entertainers' are each world's own.
constexpr std::array<const char *, Park::kStaffKinds> kSprites{"esprites/Generic/Mechanics/Spr_om",
                                                               "esprites/Generic/Handymen/Spr_ha",
                                                               "esprites/Generic/Gardeners/Spr_gd",
                                                               "esprites/Generic/Guards/Spr_gu",
                                                               nullptr,
                                                               "esprites/Generic/Researchers/Spr_re"};

constexpr ScreenRect kWindow{70, 62, 555, 350};
constexpr int kTitleX = 100;
constexpr int kTitleY = 69;
constexpr ScreenRect kHelp{552, 63, 26, 26};
constexpr ScreenRect kClose{581, 63, 26, 26};
constexpr int kTabX = 108;
constexpr int kTabY = 143;
constexpr int kTabWidth = 42;
constexpr int kTabHeight = 48;
constexpr ScreenRect kPanel{83, 188, 528, 172};
constexpr std::array<ScreenRect, 4> kColumns{
    {{105, 205, 110, 32}, {219, 205, 112, 32}, {334, 205, 112, 32}, {450, 205, 112, 32}}};
constexpr ScreenRect kList{99, 241, 468, 105};
constexpr int kNameX = 105;
constexpr int kRowY = 243;
constexpr int kRowHeight = 13;
constexpr int kBarX = 220;
constexpr int kBarWidth = 107;
constexpr int kBarHeight = 7;
constexpr int kWageRight = 399;
constexpr int kAgencyX = 450;
constexpr ScreenRect kPreview{508, 98, 90, 80};
constexpr int kPreviewInset = 4;
constexpr ScreenRect kHire{544, 368, 32, 32};
constexpr ScreenRect kCancel{582, 371, 26, 26};

constexpr Color kWhite{255, 255, 255};
constexpr Color kChosenRow{106, 125, 150};
// A control's meshes, in the order the models name them.
constexpr size_t kPlain = 0;
constexpr size_t kLit = 2;
constexpr size_t kDown = 5;
// The sprite in the frame turns a direction this often, standing.
constexpr uint32_t kTurnMillis = 400;
constexpr size_t kStandAnimation = 0;
constexpr float kPreviewScale = 1;

void fill(Surface &s, const ScreenRect &r, Color c) {
    for (int y = std::max(0, r.y); y < std::min(s.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(s.w, r.x + r.w); ++x) {
            uint8_t *p = s.row(y) + x * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
        }
}

} // namespace

Error HireWindow::load(const SearchSet &files, const std::string &world) {
    for (auto [model, name] : {std::pair{&_window, "ui/!window_s.MD2"}, std::pair{&_panel, "ui/!f_plain.MD2"},
                               std::pair{&_frame, "ui/!frame.MD2"}, std::pair{&_header, "ui/!cbut.MD2"},
                               std::pair{&_ok, "ui/b_okay.MD2"}, std::pair{&_cancel, "ui/mb_cancel.MD2"},
                               std::pair{&_help, "ui/b_help.MD2"}, std::pair{&_closeButton, "ui/b_close.MD2"}})
        if (Error err = model->load(files, name))
            return err;
    for (size_t i = 0; i < kTabModels.size(); ++i)
        if (Error err = _tabs[i].load(files, kTabModels[i]))
            return err;
    if (Error err = loadPicture(files, "ui/textures/ridestatbar.fsh", _bar))
        return err;
    // The world's first entertainer: esprites/Science/Entertainers/SPR_BU.
    const std::string worldName = world.substr(world.rfind('/') + 1);
    const std::string prefix = "esprites/" + worldName + "/entertainers/";
    for (size_t k = 0; k < kSprites.size(); ++k) {
        _sprites[k].reset();
        std::string name = kSprites[k] ? kSprites[k] : "";
        if (name.empty())
            for (const std::string &file : files.list()) {
                std::string low = file;
                std::transform(low.begin(), low.end(), low.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                if (low.compare(0, prefix.size(), prefix) == 0 && low.size() > 4 &&
                    low.compare(low.size() - 4, 4, ".esp") == 0) {
                    name = file.substr(0, file.size() - 4);
                    break;
                }
            }
        LoadedSprite sprite;
        if (!name.empty() && !sprite.load(files, name))
            _sprites[k] = std::move(sprite);
    }
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
    for (size_t k = 0; k < kNameTables.size(); ++k)
        if (Error err = readStringTable(files, folder, kNameTables[k], _characters, _names[k]))
            return err;
    return readStringTable(files, folder, "UITEXT", _characters, _ui);
}

void HireWindow::open(const Park &park, Park::StaffKind kind) {
    _open = true;
    _park = &park;
    _kind = kind;
    _row = 0;
}

std::string HireWindow::nameOf(Park::StaffKind kind, int name) const {
    return _names[size_t(kind)].text(size_t(std::max(0, name)));
}

const LoadedSprite *HireWindow::spriteOf(Park::StaffKind kind) const {
    const auto &sprite = _sprites[size_t(kind)];
    return sprite ? &*sprite : nullptr;
}

const std::vector<Park::Candidate> &HireWindow::list() const {
    static const std::vector<Park::Candidate> kNone;
    return _park ? _park->candidates(_kind) : kNone;
}

// Scientists want a research room before any can be hired (UITEXT 222).
bool HireWindow::needsLab() const {
    if (_kind != Park::StaffKind::kScientist || !_park)
        return false;
    for (const Park::PlacedObject &o : _park->objects())
        if (const Attractions::Attraction *a = _park->attractions().find(o.id); a && a->lab)
            return false;
    return true;
}

ScreenRect HireWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kTab:
        return {kTabX + int(index) * kTabWidth, kTabY, kTabWidth, kTabHeight};
    case Control::kRow:
        return {kList.x + 1, kRowY + int(index) * kRowHeight, kList.w - 2, kRowHeight};
    case Control::kHire:
        return kHire;
    case Control::kCancel:
        return kCancel;
    case Control::kClose:
        return kClose;
    case Control::kHelp:
        return kHelp;
    default:
        return {};
    }
}

HireWindow::Result HireWindow::handle(const Event &ev) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return Result::kNone;
    _over = Control::kNone;
    for (size_t i = 0; i < Park::kStaffKinds; ++i)
        if (placeOf(Control::kTab, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kTab;
            _overIndex = i;
        }
    for (size_t i = 0; i < list().size() && !needsLab(); ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kRow;
            _overIndex = i;
        }
    for (Control c : {Control::kHire, Control::kCancel, Control::kClose, Control::kHelp})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (ev.type != EventType::kMouseDown || ev.button != MouseButton::kLeft)
        return Result::kNone;
    switch (_over) {
    case Control::kTab:
        if (_park)
            open(*_park, Park::StaffKind(_overIndex));
        return Result::kNone;
    case Control::kRow:
        _row = _overIndex;
        return Result::kNone;
    case Control::kHire:
        // Hired, the window stays, for more (the recording hires several).
        return _row < list().size() && !needsLab() ? Result::kHire : Result::kNone;
    case Control::kCancel:
    case Control::kClose:
        _open = false;
        return Result::kClosed;
    default:
        return Result::kNone;
    }
}

void HireWindow::draw(Surface &screen) const {
    if (!_open)
        return;
    drawUiMesh(screen, _window, 0, kWindow);
    drawGameText(screen, _titleFont, _characters, kTitleX, kTitleY, _ui.text(kTitles[size_t(_kind)]), kWhite);
    drawUiMesh(screen, _help, _over == Control::kHelp ? kLit : kPlain, kHelp);
    drawUiMesh(screen, _closeButton, _over == Control::kClose ? kLit : kPlain, kClose);
    for (size_t i = 0; i < Park::kStaffKinds; ++i) {
        const bool lit = _over == Control::kTab && _overIndex == i;
        drawUiMesh(screen, _tabs[i], size_t(_kind) == i ? kDown : lit ? kLit : kPlain, placeOf(Control::kTab, i));
    }
    drawUiMesh(screen, _panel, 0, kPanel);
    for (size_t c = 0; c < kColumns.size(); ++c) {
        drawUiMesh(screen, _header, 0, kColumns[c]);
        const std::string label = _ui.text(kColumnTexts[c]);
        drawGameText(screen, _font, _characters,
                     kColumns[c].x + (kColumns[c].w - gameTextWidth(_font, _characters, label)) / 2,
                     kColumns[c].y + kColumns[c].h / 2 - 4, label, kWhite);
    }
    drawUiMesh(screen, _frame, 0, kList);
    const std::vector<Park::Candidate> &candidates = list();
    if (needsLab() || candidates.empty()) {
        const std::string message = _ui.text(needsLab() ? kNeedsLabText : kNoneLeftText);
        drawGameText(screen, _font, _characters, kNameX, kRowY + 1, message, kWhite);
    } else {
        for (size_t i = 0; i < candidates.size(); ++i) {
            const Park::Candidate &c = candidates[i];
            const ScreenRect row = placeOf(Control::kRow, i);
            if (i == _row)
                fill(screen, row, kChosenRow);
            drawGameText(screen, _font, _characters, kNameX, row.y + 1, nameOf(_kind, c.name), kWhite);
            // Experience as a bar: the picture's top half, red to yellow
            // to green, filled to it; its bottom half the rest.
            const float level = std::clamp(float(c.experience) / float(Park::kMostExperience), 0.0f, 1.0f);
            const int filled = int(std::lround(level * float(kBarWidth)));
            for (int dy = 0; dy < kBarHeight; ++dy)
                for (int dx = 0; dx < kBarWidth; ++dx) {
                    const uint8_t *src = _bar.pixel(dx * _bar.w / kBarWidth, dx < filled ? _bar.h / 4 : 3 * _bar.h / 4);
                    const int x = kBarX + dx;
                    const int y = row.y + 3 + dy;
                    if (x < screen.w && y < screen.h)
                        std::copy(src, src + 3, screen.row(y) + x * 4);
                }
            const std::string wage = "$ " + std::to_string(c.wage);
            drawGameText(screen, _font, _characters, kWageRight - gameTextWidth(_font, _characters, wage), row.y + 1,
                         wage, kWhite);
            drawGameText(screen, _font, _characters, kAgencyX, row.y + 1,
                         _ui.text(kFirstAgencyText + uint32_t(std::max(0, c.agency))), kWhite);
        }
    }
    // The kind, standing, turning round in its frame.
    drawUiMesh(screen, _frame, 0, kPreview);
    if (const LoadedSprite *sprite = spriteOf(_kind)) {
        const auto &stand = sprite->set.animations()[kStandAnimation];
        const float heading = float(_clock / kTurnMillis % 8) * 3.14159265f / 4;
        const SpritePose pose = spritePose(stand, heading, 0);
        drawSpriteFrame(screen, sprite->sheet, pose.frame, float(kPreview.x + kPreview.w / 2),
                        float(kPreview.y + kPreview.h - kPreviewInset), kPreviewScale, pose.mirror);
    }
    const bool canHire = !needsLab() && _row < candidates.size();
    drawUiMesh(screen, _ok, canHire ? (_over == Control::kHire ? kLit : kPlain) : 1, kHire);
    drawUiMesh(screen, _cancel, _over == Control::kCancel ? kLit : kPlain, kCancel);
}

} // namespace reluva::tpw
