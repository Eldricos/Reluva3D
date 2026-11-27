// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Measured on the recording's Buy window at 2:43, at 640 × 480:
// docs/engines/tpw/games/simcoaster/systems/building.md#the-buy-window.

#include "engines/tpw/ui/buy_window.h"

#include "engines/tpw/game_files.h"
#include "engines/tpw/textures.h"
#include "engines/tpw/ui/text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace reluva::tpw {

namespace {

// The window's strings in UITEXT.
constexpr std::array<uint32_t, Attractions::kKinds> kTitles{271, 273, 272, 275, 274};
constexpr uint32_t kItemText = 276;
constexpr uint32_t kPriceText = 277;
constexpr uint32_t kOwnedText = 279;
constexpr uint32_t kExcitementText = 280;
constexpr uint32_t kCapacityText = 281;
constexpr uint32_t kWorkingLifeText = 283;
constexpr uint32_t kNoneText = 284;

constexpr std::array<const char *, Attractions::kKinds> kTabModels{
    "ui/tab_arides.MD2", "ui/tab_asshows.MD2", "ui/tab_ashops.MD2", "ui/tab_afacility.MD2", "ui/tab_afeatures.MD2"};

constexpr ScreenRect kWindow{62, 52, 566, 366};
constexpr int kTitleX = 100;
constexpr int kTitleY = 59;
constexpr ScreenRect kHelp{552, 56, 26, 26};
constexpr ScreenRect kClose{580, 56, 26, 26};
constexpr int kTabX = 102;
constexpr int kTabY = 84;
constexpr int kTabWidth = 42;
constexpr int kTabHeight = 48;
constexpr ScreenRect kPanel{82, 132, 306, 262};
constexpr ScreenRect kItemHeader{103, 150, 131, 35};
constexpr ScreenRect kPriceHeader{237, 150, 65, 35};
constexpr ScreenRect kList{98, 186, 251, 197};
constexpr int kRowX = 103;
constexpr int kRowY = 188;
constexpr int kRowHeight = 13;
constexpr int kRowWidth = 248;
constexpr int kPriceRight = 300;
constexpr ScreenRect kRowTick{304, 0, 16, 13}; // its y the row's
constexpr ScreenRect kPreview{442, 92, 126, 126};
constexpr int kPreviewTitleY = 98;
constexpr int kPreviewInset = 5;
constexpr ScreenRect kStats{403, 232, 205, 123};
constexpr int kStatX = 409;
constexpr int kStatY = 240;
constexpr int kStatHeight = 17;
constexpr int kStatInset = 6;
constexpr int kDescriptionHeight = 13;
constexpr int kValueX = 528;
constexpr int kBarWidth = 75;
constexpr int kBarHeight = 8;
constexpr ScreenRect kBuy{542, 366, 32, 32};
constexpr ScreenRect kCancel{580, 369, 26, 26};

constexpr Color kWhite{255, 255, 255};
constexpr Color kChosenRow{106, 125, 150};
// A control's meshes, in the order the models name them.
constexpr size_t kPlain = 0;
constexpr size_t kLit = 2;
constexpr size_t kDown = 5;

constexpr float kTurnPerSecond = 0.6f;
constexpr float kPreviewPitch = 0.6f;
constexpr float kPreviewDistance = 2.2f;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
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

} // namespace

Error BuyWindow::load(const SearchSet &files) {
    _files = &files;
    for (auto [model, name] :
         {std::pair{&_window, "ui/!window_s.MD2"}, std::pair{&_panel, "ui/!f_plain.MD2"},
          std::pair{&_frame, "ui/!frame.MD2"}, std::pair{&_header, "ui/!cbut.MD2"}, std::pair{&_ok, "ui/b_okay.MD2"},
          std::pair{&_cancel, "ui/mb_cancel.MD2"}, std::pair{&_help, "ui/b_help.MD2"},
          std::pair{&_closeButton, "ui/b_close.MD2"}, std::pair{&_tick, "ui/gtick.MD2"}})
        if (Error err = model->load(files, name))
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
    return _names.load(files, folder, _characters);
}

void BuyWindow::open(const Attractions &attractions, Attractions::Kind kind, const Park &park) {
    _open = true;
    _kind = kind;
    _attractions = &attractions;
    _park = &park;
    _list = attractions.onSale(kind);
    std::stable_sort(_list.begin(), _list.end(),
                     [&](const Attractions::Attraction *a, const Attractions::Attraction *b) {
                         return lower(_names.shown(*a)) < lower(_names.shown(*b));
                     });
    _owned.clear();
    for (const Attractions::Attraction *a : _list) {
        int n = 0;
        for (const Park::PlacedObject &o : park.objects())
            n += o.id == a->id;
        _owned.push_back(n);
    }
    _row = 0;
}

const Attractions::Attraction *BuyWindow::chosen() const {
    return _row < _list.size() ? _list[_row] : nullptr;
}

void BuyWindow::choose(size_t row) {
    if (row < _list.size())
        _row = row;
}

ScreenRect BuyWindow::placeOf(Control control, size_t index) {
    switch (control) {
    case Control::kTab:
        return {kTabX + int(index) * kTabWidth, kTabY, kTabWidth, kTabHeight};
    case Control::kRow:
        return {kList.x + 1, kRowY + int(index) * kRowHeight, kRowWidth, kRowHeight};
    case Control::kBuy:
        return kBuy;
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

BuyWindow::Result BuyWindow::handle(const Event &ev) {
    if (!_open || (ev.type != EventType::kMouseMove && ev.type != EventType::kMouseDown))
        return Result::kNone;
    _over = Control::kNone;
    for (size_t i = 0; i < Attractions::kKinds; ++i)
        if (placeOf(Control::kTab, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kTab;
            _overIndex = i;
        }
    for (size_t i = 0; i < _list.size(); ++i)
        if (placeOf(Control::kRow, i).contains(ev.mouseX, ev.mouseY)) {
            _over = Control::kRow;
            _overIndex = i;
        }
    for (Control c : {Control::kBuy, Control::kCancel, Control::kClose, Control::kHelp})
        if (placeOf(c).contains(ev.mouseX, ev.mouseY))
            _over = c;
    if (ev.type != EventType::kMouseDown || ev.button != MouseButton::kLeft)
        return Result::kNone;
    switch (_over) {
    case Control::kTab:
        if (_attractions && _park)
            open(*_attractions, Attractions::Kind(_overIndex), *_park);
        return Result::kNone;
    case Control::kRow:
        choose(_overIndex);
        return Result::kNone;
    case Control::kBuy:
        if (!chosen())
            return Result::kNone;
        _open = false;
        return Result::kBuy;
    case Control::kCancel:
    case Control::kClose:
        _open = false;
        return Result::kClosed;
    default:
        return Result::kNone;
    }
}

void BuyWindow::drawBar(Surface &screen, int x, int y, float level) const {
    const int filled = int(std::lround(std::clamp(level, 0.0f, 1.0f) * float(kBarWidth)));
    for (int dy = 0; dy < kBarHeight; ++dy)
        for (int dx = 0; dx < kBarWidth; ++dx) {
            // The picture's top half the fill, red to green; its bottom the empty.
            const int u = dx * _bar.w / kBarWidth;
            const int v = dx < filled ? _bar.h / 4 : 3 * _bar.h / 4;
            const uint8_t *src = _bar.pixel(u, v);
            if (x + dx < 0 || y + dy < 0 || x + dx >= screen.w || y + dy >= screen.h)
                continue;
            uint8_t *p = screen.row(y + dy) + (x + dx) * 4;
            p[0] = src[0];
            p[1] = src[1];
            p[2] = src[2];
        }
}

// The chosen one's model, turning, drawn into the frame's black.
void BuyWindow::drawPreview(Surface &screen) const {
    const Attractions::Attraction *a = chosen();
    if (!a || a->model.empty() || !_files)
        return;
    auto it = _models.find(a->id);
    if (it == _models.end()) {
        LoadedModel model;
        if (model.load(*_files, a->model))
            return;
        it = _models.emplace(a->id, std::move(model)).first;
    }
    const ScreenRect inner{kPreview.x + kPreviewInset, kPreview.y + kPreviewInset, kPreview.w - 2 * kPreviewInset,
                           kPreview.h - 2 * kPreviewInset};
    Surface view;
    view.create(inner.w, inner.h, PixelFormat::kRGBA32);
    ModelView mv;
    mv.yaw = float(_clock) / 1000 * kTurnPerSecond;
    mv.pitch = kPreviewPitch;
    mv.distance = kPreviewDistance;
    drawModel(view, _rasterizer, it->second, mv);
    for (int y = 0; y < inner.h; ++y)
        for (int x = 0; x < inner.w; ++x) {
            const uint8_t *src = view.row(y) + x * 4;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0)
                continue;
            uint8_t *p = screen.row(inner.y + y) + (inner.x + x) * 4;
            std::copy(src, src + 3, p);
        }
}

void BuyWindow::draw(Surface &screen) const {
    if (!_open)
        return;
    drawUiMesh(screen, _window, 0, kWindow);
    drawGameText(screen, _titleFont, _characters, kTitleX, kTitleY, _ui.text(kTitles[size_t(_kind)]), kWhite);
    drawUiMesh(screen, _help, _over == Control::kHelp ? kLit : kPlain, kHelp);
    drawUiMesh(screen, _closeButton, _over == Control::kClose ? kLit : kPlain, kClose);
    for (size_t i = 0; i < Attractions::kKinds; ++i) {
        const bool lit = _over == Control::kTab && _overIndex == i;
        drawUiMesh(screen, _tabs[i], size_t(_kind) == i ? kDown : lit ? kLit : kPlain, placeOf(Control::kTab, i));
    }
    drawUiMesh(screen, _panel, 0, kPanel);
    for (auto [rect, text] : {std::pair{kItemHeader, kItemText}, std::pair{kPriceHeader, kPriceText}}) {
        drawUiMesh(screen, _header, 0, rect);
        const std::string label = _ui.text(text);
        drawGameText(screen, _font, _characters, rect.x + (rect.w - gameTextWidth(_font, _characters, label)) / 2,
                     rect.y + rect.h / 2 - 4, label, kWhite);
    }
    drawUiMesh(screen, _frame, 0, kList);
    for (size_t i = 0; i < _list.size(); ++i) {
        const ScreenRect row = placeOf(Control::kRow, i);
        if (i == _row)
            fill(screen, row, kChosenRow);
        drawGameText(screen, _font, _characters, kRowX, row.y + 1, _names.shown(*_list[i]), kWhite);
        const std::string price = "$ " + std::to_string(_list[i]->price);
        drawGameText(screen, _font, _characters, kPriceRight - gameTextWidth(_font, _characters, price), row.y + 1,
                     price, kWhite);
        if (_owned[i] > 0)
            drawUiMesh(screen, _tick, 0, {kRowTick.x, row.y - 1, kRowTick.w, kRowTick.w});
    }

    drawUiMesh(screen, _frame, 0, kPreview);
    drawPreview(screen);
    drawUiMesh(screen, _frame, 0, kStats);
    if (const Attractions::Attraction *a = chosen()) {
        const std::string name = _names.shown(*a);
        drawGameText(screen, _font, _characters,
                     kPreview.x + (kPreview.w - gameTextWidth(_font, _characters, name)) / 2, kPreviewTitleY, name,
                     kWhite);
        int y = kStatY;
        drawGameText(screen, _font, _characters, kStatX, y, _ui.text(kOwnedText), kWhite);
        const int owned = _owned[_row];
        drawGameText(screen, _font, _characters, kValueX, y, owned ? std::to_string(owned) : _ui.text(kNoneText),
                     kWhite);
        y += kStatHeight;
        if (a->excitement >= 0) {
            drawGameText(screen, _font, _characters, kStatX, y, _ui.text(kExcitementText), kWhite);
            drawBar(screen, kValueX, y + 1, float(a->excitement) / 100);
            y += kStatHeight;
        }
        if (a->capacity >= 0) {
            drawGameText(screen, _font, _characters, kStatX, y, _ui.text(kCapacityText), kWhite);
            drawGameText(screen, _font, _characters, kValueX, y, std::to_string(a->capacity), kWhite);
            y += kStatHeight;
        }
        if (a->workingLife >= 0) {
            drawGameText(screen, _font, _characters, kStatX, y, _ui.text(kWorkingLifeText), kWhite);
            drawBar(screen, kValueX, y + 1, float(a->workingLife) / 100);
            y += kStatHeight;
        }
        // Its description under the ratings, as the recording has it; as
        // many lines as the frame holds.
        for (const std::string &line :
             wrapGameText(_font, _characters, _names.description(*a), kStats.x + kStats.w - kStatX - kStatInset)) {
            if (y + kDescriptionHeight > kStats.y + kStats.h - kStatInset)
                break;
            drawGameText(screen, _font, _characters, kStatX, y, line, kWhite);
            y += kDescriptionHeight;
        }
    }
    drawUiMesh(screen, _ok, chosen() ? (_over == Control::kBuy ? kLit : kPlain) : 1, kBuy);
    drawUiMesh(screen, _cancel, _over == Control::kCancel ? kLit : kPlain, kCancel);
}

} // namespace reluva::tpw
