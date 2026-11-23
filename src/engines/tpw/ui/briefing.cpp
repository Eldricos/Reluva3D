// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Laid out as the recording shows the window at 640 × 480, from its
// frames at 1:52 and 1:58: docs/engines/tpw/games/simcoaster/systems/briefing.md.

#include "engines/tpw/ui/briefing.h"

#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/opening.h"
#include "engines/tpw/ui/text.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace reluva::tpw {

namespace {

// UITEXT's strings.
constexpr size_t kTitleText = 461;
constexpr size_t kObjectivesText = 463;
constexpr size_t kChallengesText = 464;
constexpr size_t kChallengesTitleText = 462;
constexpr size_t kProceedText = 82;
constexpr size_t kRequirementsText = 83;
constexpr size_t kStatusText = 86;
constexpr size_t kToText = 855;
constexpr size_t kFromText = 856;
constexpr size_t kSubjectText = 857;
constexpr size_t kToDetailsText = 988;
constexpr size_t kToBriefingText = 989;
// TAG_SYSTEM's: the president, who signs the first letters.
constexpr size_t kPresidentText = 173;
constexpr size_t kPresidentTitleText = 174;

// An objective's statistic, by its number in the mission, to its pair of
// STAT_TITLES strings, the requirement then the status: the missions'
// targets give these (40 and 200 people, a profit of 50,000); the others
// aren't known.
int statisticTitle(int statistic) {
    switch (statistic) {
    case 2:
        return 7;
    case 8:
        return 11;
    default:
        return -1;
    }
}

// The window's places at 640 × 480.
constexpr ScreenRect kWindow{60, 8, 570, 394};
constexpr ScreenRect kPanel{80, 78, 538, 312};
constexpr int kTitleX = 92;
constexpr int kTitleY = 21;
constexpr std::array<ScreenRect, 2> kTabs{{{95, 51, 92, 28}, {190, 51, 92, 28}}};
constexpr ScreenRect kNameArea{82, 104, 226, 20};
constexpr ScreenRect kFrame{102, 147, 188, 146};
constexpr ScreenRect kPicture{132, 156, 128, 128};
constexpr ScreenRect kList{310, 87, 296, 262};
constexpr ScreenRect kPortrait{556, 92, 44, 46};
constexpr int kLetterX = 318;
constexpr int kValueX = 397;
constexpr int kLetterY = 96;
constexpr int kLineStep = 11;
constexpr int kLetterWidth = 258; // the recording breaks its first line after "buy you"
constexpr int kHeaderLines = 3;   // From, To, Subject
// The bar and its arrows are one model, b_updown, drawn where it lies.
constexpr ScreenRect kBar{318, 313, 284, 32};
constexpr ScreenRect kProceed{392, 360, 135, 23};
constexpr ScreenRect kCancel{577, 356, 31, 31};
constexpr ScreenRect kHelp{557, 14, 22, 22};
constexpr ScreenRect kClose{585, 14, 22, 22};

constexpr size_t kPlainMesh = 0;
constexpr size_t kLitMesh = 2;
constexpr size_t kSelectedTabMesh = 5;
constexpr size_t kPresidentMesh = 0; // mugshot1, as the front end's advisor
// b_updown's: its arrows down (lit yellow) to the details, up back.
constexpr size_t kDownMesh = 0;
constexpr size_t kDownLitMesh = 2;
constexpr size_t kUpLitMesh = 3;
constexpr size_t kUpMesh = 5;
// The mission's picture in objectim: Operation Expand!'s fence is
// science1, as the recording shows; the others aren't known.
int pictureOf(int mission) {
    return mission == 0 ? 9 : -1;
}
constexpr uint32_t kFrameMillis = 40;

constexpr Color kWhite{255, 255, 255};
constexpr Color kLabelYellow{232, 232, 64};
constexpr Color kNameColour{224, 236, 248};
constexpr Color kProceedColour{200, 200, 200};

// A rectangle's colours as greys of their brightness, a little darker.
void greyOut(Surface &screen, const ScreenRect &r) {
    for (int y = std::max(0, r.y); y < std::min(screen.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(screen.w, r.x + r.w); ++x) {
            uint8_t *p = screen.row(y) + x * 4;
            const uint8_t v = uint8_t((p[0] * 30 + p[1] * 59 + p[2] * 11) / 100 * 3 / 4);
            p[0] = p[1] = p[2] = v;
        }
}

} // namespace

Error Briefing::load(const SearchSet &files) {
    for (auto [model, name] : {std::pair{&_window, "ui/!window_s.MD2"}, std::pair{&_panel, "ui/!f_plain.MD2"},
                               std::pair{&_frame, "ui/!frame.MD2"}, std::pair{&_pictures, "ui/objectim.MD2"},
                               std::pair{&_portraits, "ui/mugshot.MD2"}, std::pair{&_tab, "ui/tab_text.MD2"},
                               std::pair{&_bar, "ui/b_updown.MD2"}, std::pair{&_button, "ui/!cbut.MD2"},
                               std::pair{&_cancel, "ui/b_cancel.MD2"}, std::pair{&_help, "ui/b_help.MD2"},
                               std::pair{&_close, "ui/b_close.MD2"}})
        if (Error err = model->load(files, name))
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
    for (auto [font, name] : {std::pair{&_title, "TITLEMED"}, std::pair{&_name, "TITLEMED"},
                              std::pair{&_small, "GAME7AA"}, std::pair{&_label, "GAME8AA"}})
        if (Error err = readGameFont(files, folder, name, *font))
            return err;
    for (auto [table, name] :
         {std::pair{&_ui, "UITEXT"}, std::pair{&_operations, "OPERATION"}, std::pair{&_missions, "MISSION_DESC"},
          std::pair{&_statistics, "STAT_TITLES"}, std::pair{&_tags, "TAG_SYSTEM"}})
        if (Error err = readStringTable(files, folder, name, _characters, *table))
            return err;
    return Error::success();
}

void Briefing::drawText(Surface &screen, const Font &font, int x, int y, const std::string &text, Color c) const {
    drawGameText(screen, font, _characters, x, y, text, c);
}

void Briefing::drawCentred(Surface &screen, const Font &font, const ScreenRect &in, int y, const std::string &text,
                           Color c) const {
    drawText(screen, font, in.x + (in.w - gameTextWidth(font, _characters, text)) / 2, y, text, c);
}

std::vector<std::string> Briefing::letter(const Park &park, const std::string &player) const {
    const int m = park.mission().number;
    const std::string target = std::to_string(park.mission().target);
    std::vector<std::string> out{_tags.text(kPresidentText), player, _operations.text(size_t(std::max(0, m)))};
    std::string body = fillGameString(_missions, size_t(std::max(0, m)), {{0, target}});
    body += "\n\n" + _tags.text(kPresidentText) + "\n" + _tags.text(kPresidentTitleText);
    for (const std::string &line : wrapGameText(_label, _characters, body, kLetterWidth))
        out.push_back(line);
    return out;
}

std::vector<std::string> Briefing::details(const Park &park) const {
    const int title = statisticTitle(park.mission().statistic);
    std::vector<std::string> out{_ui.text(kRequirementsText)};
    if (title >= 0)
        out.push_back(fillGameString(_statistics, size_t(title), {{0, std::to_string(park.mission().target)}}));
    out.emplace_back();
    out.push_back(_ui.text(kStatusText));
    if (title >= 0)
        out.push_back(fillGameString(_statistics, size_t(title + 1), {{0, std::to_string(park.visitors())}}));
    return out;
}

void Briefing::draw(Surface &screen, const Park &park, const std::string &player, const View &view) const {
    auto lit = [&](Control c) { return view.hovered == c ? kLitMesh : kPlainMesh; };
    drawUiMesh(screen, _window, 0, kWindow);
    drawText(screen, _title, kTitleX, kTitleY, _ui.text(kTitleText), kWhite);
    drawUiMesh(screen, _help, 0, kHelp);
    drawUiMesh(screen, _close, 0, kClose);
    drawUiMesh(screen, _panel, 0, kPanel);
    for (size_t i = 0; i < kTabs.size(); ++i) {
        drawUiMesh(screen, _tab, i == 0 ? kSelectedTabMesh : kPlainMesh, kTabs[i]);
        drawCentred(screen, _label, kTabs[i], kTabs[i].y + 9, _ui.text(i == 0 ? kObjectivesText : kChallengesText),
                    kWhite);
    }

    const int m = park.mission().number;
    drawCentred(screen, _name, kNameArea, kNameArea.y, _operations.text(size_t(std::max(0, m))), kNameColour);
    drawUiMesh(screen, _frame, 0, kFrame);
    const int picture = pictureOf(m);
    if (picture >= 0 && size_t(picture) < _pictures.model.meshes().size())
        drawUiMesh(screen, _pictures, size_t(picture), kPicture);

    drawUiMesh(screen, _frame, 0, kList);
    int y = kLetterY;
    if (view.details) {
        for (const std::string &line : details(park)) {
            drawText(screen, _label, kLetterX, y, line, kWhite);
            y += kLineStep;
        }
    } else {
        drawUiMesh(screen, _portraits, kPresidentMesh, kPortrait);
        const std::vector<std::string> text = letter(park, player);
        const std::array<size_t, kHeaderLines> labels{kFromText, kToText, kSubjectText};
        for (size_t i = 0; i < text.size(); ++i) {
            if (i < kHeaderLines) {
                drawText(screen, _label, kLetterX, y, _ui.text(labels[i]), kLabelYellow);
                drawText(screen, _label, kValueX, y, text[i], kWhite);
                y += i + 1 == kHeaderLines ? 2 * kLineStep : kLineStep;
            } else {
                drawText(screen, _label, kLetterX, y, text[i], kWhite);
                y += kLineStep;
            }
        }
    }
    const bool barLit = view.hovered == Control::kTurn;
    drawUiMesh(screen, _bar, view.details ? (barLit ? kUpLitMesh : kUpMesh) : (barLit ? kDownLitMesh : kDownMesh),
               kBar);
    drawCentred(screen, _label, kBar, kBar.y + 11, _ui.text(view.details ? kToBriefingText : kToDetailsText), kWhite);
    // A blue button in greys, as the recording has it.
    drawUiMesh(screen, _button, 0, kProceed);
    greyOut(screen, kProceed);
    drawCentred(screen, _label, kProceed, kProceed.y + 7, _ui.text(kProceedText), kProceedColour);
    drawUiMesh(screen, _cancel, lit(Control::kCancel), kCancel);
}

ScreenRect Briefing::placeOf(Control control) {
    switch (control) {
    case Control::kObjectivesTab:
        return kTabs[0];
    case Control::kChallengesTab:
        return kTabs[1];
    case Control::kTurn:
        return kBar;
    case Control::kProceed:
        return kProceed;
    case Control::kCancel:
        return kCancel;
    case Control::kClose:
        return kClose;
    default:
        return {};
    }
}

Briefing::Control Briefing::controlAt(int x, int y) {
    for (Control c : {Control::kObjectivesTab, Control::kChallengesTab, Control::kTurn, Control::kProceed,
                      Control::kCancel, Control::kClose})
        if (placeOf(c).contains(x, y))
            return c;
    return Control::kNone;
}

bool showBriefing(System &sys, const Briefing &briefing, Park &park, const std::string &player,
                  const std::function<void(Surface &)> &backdrop) {
    Surface screen;
    screen.create(kOpeningWidth, kOpeningHeight, PixelFormat::kRGBA32);
    Briefing::View view;
    park.setPaused(true);
    struct Resume { // the clock runs again however the window goes
        Park &park;
        ~Resume() { park.setPaused(false); }
    } resume{park};
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            switch (ev.type) {
            case EventType::kQuit:
            case EventType::kReturnToLauncher:
                return false;
            case EventType::kKeyDown:
                if (ev.key == key::kEscape || ev.key == key::kReturn)
                    return true;
                break;
            case EventType::kMouseMove:
                view.hovered = Briefing::controlAt(ev.mouseX, ev.mouseY);
                break;
            case EventType::kMouseDown:
                if (ev.button != MouseButton::kLeft)
                    break;
                switch (Briefing::controlAt(ev.mouseX, ev.mouseY)) {
                case Briefing::Control::kTurn:
                    view.details = !view.details;
                    break;
                case Briefing::Control::kProceed:
                case Briefing::Control::kCancel:
                case Briefing::Control::kClose:
                    return true;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }
        backdrop(screen);
        briefing.draw(screen, park, player, view);
        sys.updateScreen(screen);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace reluva::tpw
