// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The places are measured on a recording of the US release at 640 × 480:
// docs/engines/tpw/games/simcoaster/systems/personnel-file.md.

#include "engines/tpw/ui/personnel_file.h"

#include "common/events.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/text.h"

#include <algorithm>
#include <map>
#include <vector>

namespace reluva::tpw {

namespace {

// UITEXT.str's strings.
constexpr size_t kTitleText = 19;
constexpr size_t kNewPlayerText = 20;
constexpr size_t kGoldenTicketsText = 22;
constexpr size_t kShareholdingText = 23;
constexpr size_t kSharesOwnedText = 24;
constexpr size_t kSharePriceText = 25;
constexpr size_t kPortfolioText = 26;
constexpr size_t kChallengesText = 27; // "{1} of {2} challenges completed"
constexpr size_t kNotOpenText = 28;
constexpr size_t kObjectiveText = 29;
constexpr size_t kQuitText = 30;
constexpr size_t kCoasterKitText = 31;
constexpr size_t kEnterNameText = 32;
constexpr size_t kDeleteText = 33;
constexpr size_t kEnterGameText = 34;
constexpr size_t kLoadText = 35;
constexpr size_t kNoneText = 36;
constexpr size_t kDeleteQuestionText = 915; // "{0}", a blank line, then the question

// The worlds a player's file lists, by THEMENAMES.str's numbers, with
// their folders: Land of Invention, Polar Zone, Arabian Nights.
constexpr std::array<std::pair<int, const char *>, 3> kWorlds{
    {{0, "levels/science/"}, {1, "levels/water/"}, {3, "levels/arabian/"}}};
// Challenges.sam lists them a world after another, ten a world.
constexpr int kChallengesPerWorld = 10;

// The models' meshes: their first is each's plain state.
constexpr size_t kTabMesh = 0;         // filetab.tga: a player not shown
constexpr size_t kTabSelectedMesh = 3; // filetabd.tga
constexpr size_t kButtonMesh = 0;      // filebut.tga
constexpr size_t kButtonLitMesh = 2;   // filebuth.tga, under the mouse
constexpr size_t kPortraitMesh = 4;    // mugshot5.tga
// A control's states, as b_okay.MD2 names its meshes.
constexpr size_t kPlainMesh = 0;
constexpr size_t kDisabledMesh = 1;
constexpr size_t kLitMesh = 2;

// How long a screen waits for the next, in milliseconds.
constexpr uint32_t kFrameMillis = 40;
constexpr int kScreenWidth = 640;
constexpr int kScreenHeight = 480;

// The places, in pixels.
constexpr ScreenRect kWindow{88, 23, 464, 430}; // its shadow too, 8 pixels left and below
constexpr int kTitleX = 124;
constexpr int kTitleY = 30;
constexpr int kTabX = 137;
constexpr int kTabY = 71;
constexpr int kTabW = 94;
constexpr int kTabH = 32;
constexpr int kTabTextY = 83;
constexpr int kTabMargin = 4; // a name's room on its tab, each side
constexpr std::array<ScreenRect, 3> kButtons{{{141, 315, 112, 26}, {269, 315, 112, 26}, {397, 315, 112, 26}}};
constexpr std::array<ScreenRect, 2> kBigButtons{{{123, 391, 145, 32}, {382, 391, 145, 32}}};
constexpr ScreenRect kFrame{568, 389, 59, 56};
constexpr int kPlateRim = 5;
constexpr ScreenRect kPortrait{573, 394, 49, 46};
// The page's columns and lines.
constexpr int kLeftLabel = 137;
constexpr int kLeftValue = 251;
constexpr int kRightLabel = 337;
constexpr int kRightValue = 451;
constexpr int kGoldenTicketsLabel = 367;
constexpr int kRankLine = 134;
constexpr int kShareLine = 167;
constexpr int kOwnedLine = 184;
constexpr int kWorldLine = 213;
constexpr int kLineStep = 16;
constexpr int kObjectiveLine = 278;
// How far down from its label's top the handwriting starts: the
// recording's rows differ.
constexpr int kRankHandDrop = 0;
constexpr int kShareHandDrop = -2;
constexpr int kWorldHandDrop = 1;

// An empty slot's name box and its tick, measured on the recording.
constexpr ScreenRect kNameBox{228, 112, 197, 31};
constexpr int kNameX = 239;
constexpr int kNameY = 125;
constexpr ScreenRect kNameDone{390, 111, 32, 32};
// The delete dialog: a window, its title, the question's lines, its
// buttons; the Personnel File behind it at 48 % of its brightness.
constexpr ScreenRect kDialog{156, 72, 316, 186};
constexpr int kDialogTitleX = 187;
constexpr int kDialogTitleY = 80;
constexpr int kQuestionY = 149;
constexpr int kQuestionStep = 13;
constexpr ScreenRect kConfirmYes{394, 214, 28, 28};
constexpr ScreenRect kConfirmNo{430, 214, 28, 28};
constexpr ScreenRect kConfirmClose{430, 74, 25, 25};
constexpr int kDimPercent = 48;

constexpr Color kLabelColour{80, 72, 64};
constexpr Color kHandColour{64, 96, 176};
constexpr Color kTitleColour{255, 255, 255};
constexpr Color kButtonColour{64, 60, 56};
constexpr Color kDisabledColour{200, 196, 182}; // a disabled button's words, faint
constexpr Color kGreyedColour{112, 112, 112};   // a greyed blue button's
constexpr Color kQuestionColour{40, 72, 104};
constexpr Color kSelectionColour{91, 144, 174};

// A string with its parameters' places filled, by their numbers.
std::string fill(const StringTable &table, size_t index, const std::map<int, std::string> &values) {
    std::string out;
    for (const StringTable::Part &p : table.parts(index)) {
        const auto it = values.find(p.number);
        out += p.parameter ? (it == values.end() ? std::string() : it->second) : p.text;
    }
    return out;
}

// Multiplies a rectangle's colours by `percent` / 100.
void dim(Surface &screen, const ScreenRect &r, int percent) {
    for (int y = std::max(0, r.y); y < std::min(screen.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(screen.w, r.x + r.w); ++x) {
            uint8_t *p = screen.row(y) + x * 4;
            for (int i = 0; i < 3; ++i)
                p[i] = uint8_t(p[i] * percent / 100);
        }
}

// A rectangle's colours as greys of their brightness: a disabled control.
void grey(Surface &screen, const ScreenRect &r) {
    for (int y = std::max(0, r.y); y < std::min(screen.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(screen.w, r.x + r.w); ++x) {
            uint8_t *p = screen.row(y) + x * 4;
            const uint8_t v = uint8_t((p[0] * 30 + p[1] * 59 + p[2] * 11) / 100);
            p[0] = p[1] = p[2] = v;
        }
}

void fill(Surface &screen, const ScreenRect &r, Color c) {
    for (int y = std::max(0, r.y); y < std::min(screen.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(screen.w, r.x + r.w); ++x) {
            uint8_t *p = screen.row(y) + x * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
        }
}

// A string's lines.
std::vector<std::string> lines(const std::string &text) {
    std::vector<std::string> out(1);
    for (char c : text) {
        if (c == '\n')
            out.emplace_back();
        else if (c != '\r')
            out.back() += c;
    }
    return out;
}

} // namespace

Error PersonnelFile::load(const SearchSet &files) {
    struct Piece {
        LoadedModel *model;
        const char *name;
    };
    for (const Piece &p :
         {Piece{&_window, "ui/!window_s.MD2"}, Piece{&_dialog, "ui/!window.MD2"}, Piece{&_page, "ui/filepage.MD2"},
          Piece{&_tab, "ui/filetab.MD2"}, Piece{&_button, "ui/filebut.MD2"}, Piece{&_bigButton, "ui/!cbut.MD2"},
          Piece{&_frame, "ui/!frame.MD2"}, Piece{&_portraits, "ui/mugshot.MD2"}, Piece{&_nameBox, "ui/filebox.MD2"},
          Piece{&_nameDone, "ui/filebutok.MD2"}, Piece{&_yes, "ui/b_okay.MD2"}, Piece{&_no, "ui/mb_cancel.MD2"},
          Piece{&_close, "ui/b_close.MD2"}})
        if (Error err = p.model->load(files, p.name))
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
    for (auto [font, name] :
         {std::pair{&_title, "TITLEMED"}, std::pair{&_small, "GAME7AA"}, std::pair{&_label, "GAME8AA"},
          std::pair{&_message, "GAME9AA"}, std::pair{&_hand, "PERSONNELFILESMALL"}})
        if (Error err = readGameFont(files, folder, name, *font))
            return err;
    for (auto [table, name] : {std::pair{&_ui, "UITEXT"}, std::pair{&_ranks, "PROMOTIONLEVELS"},
                               std::pair{&_operations, "OPERATION"}, std::pair{&_themes, "THEMENAMES"}})
        if (Error err = readStringTable(files, folder, name, _characters, *table))
            return err;

    SamFile standard;
    if (Error err = files.read("levels/Standard.sam", data))
        return err;
    if (Error err = standard.parse(data))
        return err;
    _newPlayer = {};
    _newPlayer.sharesIssued = int(standard.number("ShareInfo.CompanySharesIssued"));
    _newPlayer.sharesOwned = int(standard.number("ShareInfo.InitialSharesOwned"));
    _newPlayer.sharePrice = int(standard.number("ShareInfo.InitialSharePrice"));
    for (size_t i = 0; i < kWorlds.size(); ++i) {
        PlayerFile::World &w = _newPlayer.worlds[i];
        w.theme = kWorlds[i].first;
        w.challenges = kChallengesPerWorld;
        SamFile global;
        if (!files.read(std::string(kWorlds[i].second) + "global.sam", data) && !global.parse(data))
            w.open = global.number("ParkInfo.AvailableFromStart") != 0;
    }
    _newPlayer.objectiveTheme = kWorlds[0].first;
    return Error::success();
}

void PersonnelFile::drawText(Surface &screen, const Font &font, int x, int y, const std::string &text, Color c) const {
    drawGameText(screen, font, _characters, x, y, text, c);
}

void PersonnelFile::drawCentred(Surface &screen, const Font &font, const ScreenRect &in, int y, const std::string &text,
                                Color c) const {
    drawText(screen, font, in.x + (in.w - gameTextWidth(font, _characters, text)) / 2, y, text, c);
}

std::string PersonnelFile::worldLine(const PlayerFile::World &world) const {
    if (!world.open)
        return _ui.text(kNotOpenText);
    return fill(_ui, kChallengesText, {{1, std::to_string(world.won)}, {2, std::to_string(world.challenges)}});
}

void PersonnelFile::drawRecord(Surface &screen, const PlayerFile &p) const {
    // A slot with no player shows the labels alone, its name box over the rank's line.
    const bool shown = p.exists();
    if (shown) {
        drawText(screen, _hand, kLeftLabel, kRankLine + kRankHandDrop, _ranks.text(size_t(p.rank)), kHandColour);
        drawText(screen, _label, kGoldenTicketsLabel, kRankLine, _ui.text(kGoldenTicketsText), kLabelColour);
        drawText(screen, _hand, kRightValue, kRankLine + kRankHandDrop, std::to_string(p.goldenTickets), kHandColour);
    }
    const int percent = p.sharesIssued > 0 ? p.sharesOwned * 100 / p.sharesIssued : 0;
    const struct {
        size_t label;
        int x;
        int line;
        std::string value;
    } figures[] = {
        {kShareholdingText, kLeftLabel, kShareLine, std::to_string(percent) + "%"},
        {kSharePriceText, kRightLabel, kShareLine, "$ " + std::to_string(p.sharePrice)},
        {kSharesOwnedText, kLeftLabel, kOwnedLine, std::to_string(p.sharesOwned)},
        {kPortfolioText, kRightLabel, kOwnedLine, "$ " + std::to_string(p.sharesOwned * p.sharePrice)},
    };
    for (const auto &f : figures) {
        drawText(screen, _label, f.x, f.line, _ui.text(f.label), kLabelColour);
        if (shown)
            drawText(screen, _hand, f.x == kLeftLabel ? kLeftValue : kRightValue, f.line + kShareHandDrop, f.value,
                     kHandColour);
    }
    for (size_t i = 0; i < p.worlds.size(); ++i) {
        const int line = kWorldLine + kLineStep * int(i);
        drawText(screen, _label, kLeftLabel, line, _themes.text(size_t(p.worlds[i].theme)), kLabelColour);
        if (shown)
            drawText(screen, _hand, kLeftValue, line + kWorldHandDrop, worldLine(p.worlds[i]), kHandColour);
    }
    drawText(screen, _label, kLeftLabel, kObjectiveLine, _ui.text(kObjectiveText), kLabelColour);
    if (shown)
        drawText(screen, _hand, kLeftValue, kObjectiveLine + kWorldHandDrop,
                 p.objective < 0
                     ? _ui.text(kNoneText)
                     : _operations.text(size_t(p.objective)) + ", " + _themes.text(size_t(p.objectiveTheme)),
                 kHandColour);
}

void PersonnelFile::drawNameBox(Surface &screen, const View &view) const {
    drawUiMesh(screen, _nameBox, 0, kNameBox);
    // "Enter name here" until typed over, selected; then the name and a cursor after it.
    const std::string text = view.placeholder ? _ui.text(kEnterNameText) : view.typed;
    const int width = gameTextWidth(_small, _characters, text);
    if (view.placeholder)
        fill(screen, {kNameX - 1, kNameY - 1, width + 2, 10}, kSelectionColour);
    drawText(screen, _small, kNameX, kNameY, text, kTitleColour);
    if (!view.placeholder)
        fill(screen, {kNameX + width + 1, kNameY - 1, 2, 10}, kSelectionColour);
    const bool ready = !view.placeholder && !view.typed.empty();
    drawUiMesh(screen, _nameDone,
               !ready                               ? kDisabledMesh
               : view.hovered == Control::kNameDone ? kLitMesh
                                                    : kPlainMesh,
               kNameDone);
}

void PersonnelFile::drawDeleteDialog(Surface &screen, const PlayerFile &p, const View &view) const {
    dim(screen, kWindow, kDimPercent);
    drawUiMesh(screen, _dialog, 0, kDialog);
    // The string's first line is the title, the player's name; a blank
    // line, then the question's.
    const std::vector<std::string> text = lines(fill(_ui, kDeleteQuestionText, {{0, p.name}}));
    drawText(screen, _title, kDialogTitleX, kDialogTitleY, text.front(), kTitleColour);
    int y = kQuestionY;
    for (size_t i = 2; i < text.size(); ++i, y += kQuestionStep)
        drawCentred(screen, _message, kDialog, y, text[i], kQuestionColour);
    auto state = [&](Control c) { return view.hovered == c ? kLitMesh : kPlainMesh; };
    drawUiMesh(screen, _close, state(Control::kConfirmClose), kConfirmClose);
    drawUiMesh(screen, _yes, state(Control::kConfirmYes), kConfirmYes);
    drawUiMesh(screen, _no, state(Control::kConfirmNo), kConfirmNo);
}

void PersonnelFile::draw(Surface &screen, const PlayerSlots &players, const View &view) const {
    drawUiMesh(screen, _window, 0, kWindow);
    drawText(screen, _title, kTitleX, kTitleY, _ui.text(kTitleText), kTitleColour);

    // The slots not shown behind the page, the one shown over its edge.
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1)
            drawUiMesh(screen, _page, 0);
        for (int i = 0; i < kPlayerSlots; ++i) {
            if ((i == view.selected) != (pass == 1))
                continue;
            const ScreenRect tab{kTabX + kTabW * i, kTabY, kTabW, kTabH};
            drawUiMesh(screen, _tab, i == view.selected ? kTabSelectedMesh : kTabMesh, tab);
            const PlayerFile &p = players[size_t(i)];
            drawCentred(screen, _small, tab, kTabTextY, p.exists() ? p.name : _ui.text(kNewPlayerText), kButtonColour);
        }
    }

    const PlayerFile &p = players[size_t(view.selected)];
    drawRecord(screen, p);
    if (p.exists()) {
        // No game saved yet: "Load A Game" disabled.
        const std::array<std::pair<Control, size_t>, 3> buttons{{{Control::kEnterGame, kEnterGameText},
                                                                 {Control::kLoadGame, kLoadText},
                                                                 {Control::kDeletePlayer, kDeleteText}}};
        for (size_t i = 0; i < buttons.size(); ++i) {
            const bool enabled = buttons[i].first != Control::kLoadGame;
            drawUiMesh(screen, _button,
                       !enabled                           ? kDisabledMesh
                       : view.hovered == buttons[i].first ? kButtonLitMesh
                                                          : kButtonMesh,
                       kButtons[i]);
            drawCentred(screen, _label, kButtons[i], kButtons[i].y + 9, _ui.text(buttons[i].second),
                        enabled ? kButtonColour : kDisabledColour);
        }
    } else {
        drawNameBox(screen, view);
    }
    const std::array<size_t, 2> bigTexts{kQuitText, kCoasterKitText};
    for (size_t i = 0; i < kBigButtons.size(); ++i) {
        // On a plate, as the portrait is: its rim shows round the button.
        // The Coaster Design Kit is grey without a player.
        const ScreenRect &r = kBigButtons[i];
        const ScreenRect button{r.x + kPlateRim, r.y + kPlateRim - 1, r.w - 2 * kPlateRim, r.h - 2 * kPlateRim + 2};
        const bool enabled = i == 0 || p.exists();
        drawUiMesh(screen, _frame, 0, r);
        drawUiMesh(screen, _bigButton, 0, button);
        if (!enabled)
            grey(screen, button);
        drawCentred(screen, _label, r, r.y + 12, _ui.text(bigTexts[i]), enabled ? kTitleColour : kGreyedColour);
    }
    drawUiMesh(screen, _frame, 0, kFrame);
    drawUiMesh(screen, _portraits, kPortraitMesh, kPortrait);
    if (view.confirmingDelete && p.exists())
        drawDeleteDialog(screen, p, view);
}

ScreenRect PersonnelFile::placeOf(Control control) {
    switch (control) {
    case Control::kNone:
        return {};
    case Control::kEnterGame:
        return kButtons[0];
    case Control::kLoadGame:
        return kButtons[1];
    case Control::kDeletePlayer:
        return kButtons[2];
    case Control::kQuitGame:
        return kBigButtons[0];
    case Control::kCoasterKit:
        return kBigButtons[1];
    case Control::kNameDone:
        return kNameDone;
    case Control::kConfirmYes:
        return kConfirmYes;
    case Control::kConfirmNo:
        return kConfirmNo;
    case Control::kConfirmClose:
        return kConfirmClose;
    default:
        return {kTabX + kTabW * (int(control) - int(Control::kTab0)), kTabY, kTabW, kTabH};
    }
}

PersonnelFile::Control PersonnelFile::controlAt(int x, int y, const PlayerSlots &players, const View &view) {
    const bool player = players[size_t(view.selected)].exists();
    std::vector<Control> live;
    if (view.confirmingDelete && player) {
        live = {Control::kConfirmYes, Control::kConfirmNo, Control::kConfirmClose};
    } else {
        live = {Control::kTab0, Control::kTab1, Control::kTab2, Control::kTab3, Control::kQuitGame};
        if (player)
            live.insert(live.end(), {Control::kEnterGame, Control::kDeletePlayer, Control::kCoasterKit});
        else if (!view.placeholder && !view.typed.empty())
            live.push_back(Control::kNameDone);
    }
    for (Control c : live)
        if (placeOf(c).contains(x, y))
            return c;
    return Control::kNone;
}

void PersonnelFile::type(std::string &name, const std::string &utf8) const {
    for (size_t i = 0; i < utf8.size();) {
        // A character's bytes: its first, then those of the form 10xxxxxx.
        size_t end = i + 1;
        while (end < utf8.size() && (uint8_t(utf8[end]) & 0xc0) == 0x80)
            ++end;
        const std::string ch = utf8.substr(i, end - i);
        i = end;
        if (ch == "\b") {
            while (!name.empty() && (uint8_t(name.back()) & 0xc0) == 0x80)
                name.pop_back();
            if (!name.empty())
                name.pop_back();
            continue;
        }
        // Only what the tab's font draws, and only while it fits on a tab.
        const std::string longer = name + ch;
        const int width = gameTextWidth(_small, _characters, longer);
        if (width > gameTextWidth(_small, _characters, name) && width <= kTabW - 2 * kTabMargin)
            name = longer;
    }
}

bool choosePlayer(System &sys, const SearchSet &files, const std::filesystem::path &saveFolder,
                  const Backdrop &backdrop, PlayerFile &chosen) {
    using Control = PersonnelFile::Control;
    PersonnelFile file;
    if (file.load(files)) {
        chosen = {};
        return true;
    }
    PlayerSlots players = readPlayers(saveFolder, file.newPlayer());
    // Kept as they change; without a save folder, for this run only.
    auto keep = [&] {
        if (!saveFolder.empty())
            (void)writePlayers(saveFolder, players);
    };
    PersonnelFile::View view;
    auto show = [&](int slot) {
        view.selected = slot;
        view.typed.clear();
        view.placeholder = true;
        view.confirmingDelete = false;
    };
    auto makePlayer = [&] {
        if (view.placeholder || view.typed.empty())
            return;
        PlayerFile &p = players[size_t(view.selected)];
        p = file.newPlayer();
        p.name = view.typed;
        keep();
    };
    struct TextInput { // typed text arrives while the window is up
        System &sys;
        explicit TextInput(System &s) : sys(s) { sys.setTextInput(true); }
        ~TextInput() { sys.setTextInput(false); }
    } typing(sys);
    Surface screen;
    screen.create(kScreenWidth, kScreenHeight, PixelFormat::kRGBA32);
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            PlayerFile &shown = players[size_t(view.selected)];
            switch (ev.type) {
            case EventType::kQuit:
            case EventType::kReturnToLauncher:
                return false;
            case EventType::kMouseMove:
                view.hovered = PersonnelFile::controlAt(ev.mouseX, ev.mouseY, players, view);
                break;
            case EventType::kMouseDown: {
                if (ev.button != MouseButton::kLeft)
                    break;
                const Control c = PersonnelFile::controlAt(ev.mouseX, ev.mouseY, players, view);
                if (c >= Control::kTab0 && c <= Control::kTab3) {
                    show(int(c) - int(Control::kTab0));
                } else if (c == Control::kEnterGame) {
                    // A player's first game starts the first mission.
                    if (shown.objective < 0) {
                        shown.objective = 0;
                        shown.objectiveTheme = file.newPlayer().objectiveTheme;
                        keep();
                    }
                    chosen = shown;
                    return true;
                } else if (c == Control::kQuitGame) {
                    return false;
                } else if (c == Control::kNameDone) {
                    makePlayer();
                } else if (c == Control::kDeletePlayer) {
                    view.confirmingDelete = true;
                } else if (c == Control::kConfirmYes) {
                    shown = file.newPlayer();
                    keep();
                    show(view.selected);
                } else if (c == Control::kConfirmNo || c == Control::kConfirmClose) {
                    view.confirmingDelete = false;
                }
                view.hovered = PersonnelFile::controlAt(ev.mouseX, ev.mouseY, players, view);
                break;
            }
            case EventType::kTextInput:
                if (!shown.exists() && !view.confirmingDelete) {
                    // Typing replaces "Enter name here", selected.
                    if (view.placeholder)
                        view.typed.clear();
                    view.placeholder = false;
                    file.type(view.typed, ev.text);
                }
                break;
            case EventType::kKeyDown:
                if (!shown.exists() && ev.key == key::kBackspace) {
                    if (view.placeholder)
                        view.typed.clear();
                    view.placeholder = false;
                    file.type(view.typed, "\b");
                } else if (!shown.exists() && ev.key == key::kReturn) {
                    makePlayer();
                } else if (view.confirmingDelete && ev.key == key::kEscape) {
                    view.confirmingDelete = false;
                }
                break;
            default:
                break;
            }
        }
        backdrop(screen);
        file.draw(screen, players, view);
        sys.updateScreen(screen);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace reluva::tpw
