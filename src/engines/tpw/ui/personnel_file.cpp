// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The places are measured on a recording of the US release at 640 × 480:
// docs/engines/tpw/games/simcoaster/systems/personnel-file.md.

#include "engines/tpw/ui/personnel_file.h"

#include "common/events.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/text.h"

#include <map>

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

// How long a screen waits for the next, in milliseconds.
constexpr uint32_t kFrameMillis = 40;

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

constexpr Color kLabelColour{80, 72, 64};
constexpr Color kHandColour{64, 96, 176};
constexpr Color kTitleColour{255, 255, 255};
constexpr Color kButtonColour{64, 60, 56};

Error readTable(const SearchSet &files, const std::string &folder, const char *name, const CharacterSet &characters,
                StringTable &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(folder + name + ".str", data))
        return err;
    return out.parse(data, characters);
}

Error readFont(const SearchSet &files, const std::string &folder, const char *name, Font &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(folder + name + ".bf4", data))
        return err;
    return out.parse(data);
}

// A string with its parameters' places filled, by their numbers.
std::string fill(const StringTable &table, size_t index, const std::map<int, std::string> &values) {
    std::string out;
    for (const StringTable::Part &p : table.parts(index)) {
        const auto it = values.find(p.number);
        out += p.parameter ? (it == values.end() ? std::string() : it->second) : p.text;
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
         {Piece{&_window, "ui/!window_s.MD2"}, Piece{&_page, "ui/filepage.MD2"}, Piece{&_tab, "ui/filetab.MD2"},
          Piece{&_button, "ui/filebut.MD2"}, Piece{&_bigButton, "ui/!cbut.MD2"}, Piece{&_frame, "ui/!frame.MD2"},
          Piece{&_portraits, "ui/mugshot.MD2"}})
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
    for (auto [font, name] : {std::pair{&_title, "TITLEMED"}, std::pair{&_small, "GAME7AA"},
                              std::pair{&_label, "GAME8AA"}, std::pair{&_hand, "PERSONNELFILESMALL"}})
        if (Error err = readFont(files, folder, name, *font))
            return err;
    for (auto [table, name] : {std::pair{&_ui, "UITEXT"}, std::pair{&_ranks, "PROMOTIONLEVELS"},
                               std::pair{&_operations, "OPERATION"}, std::pair{&_themes, "THEMENAMES"}})
        if (Error err = readTable(files, folder, name, _characters, *table))
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

void PersonnelFile::draw(Surface &screen, const std::array<PlayerFile, kPlayers> &players, int selected,
                         Control hovered) const {
    drawUiMesh(screen, _window, 0, kWindow);
    drawText(screen, _title, kTitleX, kTitleY, _ui.text(kTitleText), kTitleColour);

    // The players not shown behind the page, the one shown over its edge.
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1)
            drawUiMesh(screen, _page, 0);
        for (int i = 0; i < kPlayers; ++i) {
            if ((i == selected) != (pass == 1))
                continue;
            const ScreenRect tab{kTabX + kTabW * i, kTabY, kTabW, kTabH};
            drawUiMesh(screen, _tab, i == selected ? kTabSelectedMesh : kTabMesh, tab);
            const std::string &name = players[size_t(i)].name;
            const std::string label = !name.empty() ? name : _ui.text(i == selected ? kEnterNameText : kNewPlayerText);
            drawCentred(screen, _small, tab, kTabTextY, label, kButtonColour);
        }
    }

    const PlayerFile &p = players[size_t(selected)];
    drawText(screen, _hand, kLeftLabel, kRankLine + kRankHandDrop, _ranks.text(size_t(p.rank)), kHandColour);
    drawText(screen, _label, kGoldenTicketsLabel, kRankLine, _ui.text(kGoldenTicketsText), kLabelColour);
    drawText(screen, _hand, kRightValue, kRankLine + kRankHandDrop, std::to_string(p.goldenTickets), kHandColour);

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
        drawText(screen, _hand, f.x == kLeftLabel ? kLeftValue : kRightValue, f.line + kShareHandDrop, f.value,
                 kHandColour);
    }
    for (size_t i = 0; i < p.worlds.size(); ++i) {
        const int line = kWorldLine + kLineStep * int(i);
        drawText(screen, _label, kLeftLabel, line, _themes.text(size_t(p.worlds[i].theme)), kLabelColour);
        drawText(screen, _hand, kLeftValue, line + kWorldHandDrop, worldLine(p.worlds[i]), kHandColour);
    }
    drawText(screen, _label, kLeftLabel, kObjectiveLine, _ui.text(kObjectiveText), kLabelColour);
    drawText(screen, _hand, kLeftValue, kObjectiveLine + kWorldHandDrop,
             _operations.text(size_t(p.objective)) + ", " + _themes.text(size_t(p.objectiveTheme)), kHandColour);

    const std::array<std::pair<Control, size_t>, 3> buttons{{{Control::kEnterGame, kEnterGameText},
                                                             {Control::kLoadGame, kLoadText},
                                                             {Control::kDeletePlayer, kDeleteText}}};
    for (size_t i = 0; i < buttons.size(); ++i) {
        drawUiMesh(screen, _button, hovered == buttons[i].first ? kButtonLitMesh : kButtonMesh, kButtons[i]);
        drawCentred(screen, _label, kButtons[i], kButtons[i].y + 9, _ui.text(buttons[i].second), kButtonColour);
    }
    const std::array<size_t, 2> bigTexts{kQuitText, kCoasterKitText};
    for (size_t i = 0; i < kBigButtons.size(); ++i) {
        // On a plate, as the portrait is: its rim shows round the button.
        const ScreenRect &r = kBigButtons[i];
        drawUiMesh(screen, _frame, 0, r);
        drawUiMesh(screen, _bigButton, 0,
                   {r.x + kPlateRim, r.y + kPlateRim - 1, r.w - 2 * kPlateRim, r.h - 2 * kPlateRim + 2});
        drawCentred(screen, _label, kBigButtons[i], kBigButtons[i].y + 12, _ui.text(bigTexts[i]), kTitleColour);
    }
    drawUiMesh(screen, _frame, 0, kFrame);
    drawUiMesh(screen, _portraits, kPortraitMesh, kPortrait);
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
    default:
        return {kTabX + kTabW * (int(control) - int(Control::kTab0)), kTabY, kTabW, kTabH};
    }
}

PersonnelFile::Control PersonnelFile::controlAt(int x, int y) {
    for (int c = int(Control::kTab0); c <= int(Control::kCoasterKit); ++c)
        if (placeOf(Control(c)).contains(x, y))
            return Control(c);
    return Control::kNone;
}

void PersonnelFile::type(PlayerFile &player, const std::string &utf8) const {
    for (size_t i = 0; i < utf8.size();) {
        // A character's bytes: its first, then those of the form 10xxxxxx.
        size_t end = i + 1;
        while (end < utf8.size() && (uint8_t(utf8[end]) & 0xc0) == 0x80)
            ++end;
        const std::string ch = utf8.substr(i, end - i);
        i = end;
        if (ch == "\b") {
            while (!player.name.empty() && (uint8_t(player.name.back()) & 0xc0) == 0x80)
                player.name.pop_back();
            if (!player.name.empty())
                player.name.pop_back();
            continue;
        }
        // Only what the tab's font draws, and only while it fits on the tab.
        const std::string longer = player.name + ch;
        const int width = gameTextWidth(_small, _characters, longer);
        if (width > gameTextWidth(_small, _characters, player.name) && width <= kTabW - 2 * kTabMargin)
            player.name = longer;
    }
}

bool choosePlayer(System &sys, const SearchSet &files, const Surface &background) {
    PersonnelFile file;
    if (file.load(files))
        return true;
    std::array<PlayerFile, PersonnelFile::kPlayers> players;
    players.fill(file.newPlayer());
    int selected = 0;
    PersonnelFile::Control hovered = PersonnelFile::Control::kNone;
    struct TextInput { // typed text arrives while the window is up
        System &sys;
        explicit TextInput(System &s) : sys(s) { sys.setTextInput(true); }
        ~TextInput() { sys.setTextInput(false); }
    } typing(sys);
    Surface screen;
    screen.create(background.w, background.h, PixelFormat::kRGBA32);
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            switch (ev.type) {
            case EventType::kQuit:
            case EventType::kReturnToLauncher:
                return false;
            case EventType::kMouseMove:
                hovered = PersonnelFile::controlAt(ev.mouseX, ev.mouseY);
                break;
            case EventType::kMouseDown: {
                if (ev.button != MouseButton::kLeft)
                    break;
                const PersonnelFile::Control c = PersonnelFile::controlAt(ev.mouseX, ev.mouseY);
                if (c == PersonnelFile::Control::kEnterGame)
                    return true;
                if (c == PersonnelFile::Control::kQuitGame)
                    return false;
                if (c >= PersonnelFile::Control::kTab0 && c <= PersonnelFile::Control::kTab3)
                    selected = int(c) - int(PersonnelFile::Control::kTab0);
                break;
            }
            case EventType::kTextInput:
                file.type(players[size_t(selected)], ev.text);
                break;
            case EventType::kKeyDown:
                if (ev.key == key::kBackspace)
                    file.type(players[size_t(selected)], "\b");
                break;
            default:
                break;
            }
        }
        screen.pixels = background.pixels;
        file.draw(screen, players, selected, hovered);
        sys.updateScreen(screen);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace reluva::tpw
