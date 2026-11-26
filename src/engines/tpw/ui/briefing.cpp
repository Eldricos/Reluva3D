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
constexpr size_t kNoChallengesText = 465;
// CHALLENGE_GENERIC's.
constexpr size_t kChallengeOneTicketText = 0;
constexpr size_t kChallengeTicketsText = 1;
constexpr size_t kChallengeSharesText = 2;
constexpr size_t kChallengeCashText = 3;
constexpr size_t kChallengePriceWonText = 4;
constexpr size_t kChallengePriceLostText = 5;
constexpr size_t kChallengePromotionText = 6;
constexpr size_t kChallengeDetailsText = 7;
constexpr size_t kChallengeStatusLabel = 8;
constexpr size_t kChallengeNotAttemptedText = 9;
constexpr size_t kChallengeFailedTimesText = 10;
constexpr size_t kChallengeFailedOnceText = 11;
constexpr size_t kChallengeCompletedText = 14;
constexpr size_t kChallengeUnderwayText = 15;
constexpr size_t kChallengePreparingNowText = 16;
constexpr size_t kChallengeFiringOneText = 18;
constexpr size_t kChallengeFiringText = 19;
constexpr size_t kChallengeTitleColumnText = 20;
constexpr size_t kChallengeStatusColumnText = 21;
constexpr size_t kChallengeNotAvailableText = 22;
constexpr size_t kChallengeAvailableText = 23;
constexpr size_t kChallengePreparingText = 24;
constexpr size_t kChallengeActiveText = 25;
constexpr size_t kChallengeCompleteText = 26;
constexpr size_t kChallengeNotYetText = 27;
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
// STAT_TITLES strings, the requirement then the status, as the missions'
// comments and targets match them: 2 a profit ("Make a profit of ${0}"),
// 6 park awards, 7 a share of the shares, 8 people in the park.
int statisticTitle(int statistic) {
    switch (statistic) {
    case 2:
        return 7;
    case 6:
        return 5;
    case 7:
        return 9;
    case 8:
        return 11;
    default:
        return -1;
    }
}

// UITEXT's, for the objectives other than statistics.
constexpr size_t kElsewhereText = 75;
constexpr size_t kDoneGoText = 76;
constexpr size_t kResearchNeededText = 77;
constexpr size_t kPromotionText = 79;
constexpr size_t kChallengesNeededText = 80;
constexpr size_t kSolutionFoundText = 81;
constexpr size_t kTrainedStaffText = 84;
constexpr size_t kTicketsNeededText = 85;
constexpr size_t kResearchExpansionText = 90;
constexpr size_t kResearchingText = 91;
constexpr size_t kStaffMissingText = 92;
constexpr size_t kStaffReadyText = 93;
constexpr size_t kPositionText = 98;
constexpr size_t kAllDoneText = 493;
// STAT_TITLES': golden tickets, one, several, and those the player has.
constexpr size_t kOneTicketText = 13;
constexpr size_t kTicketsText = 14;
constexpr size_t kTicketsHeldText = 15;
// Operation Plughole!'s letter as it goes on: being researched; its
// solution found; its staff trained (MISSION_DESC 19 to 21).
constexpr int kPlughole = 1;
constexpr size_t kPlugholeResearching = 19;
constexpr size_t kPlugholeSolved = 20;
constexpr size_t kPlugholeReady = 21;
// STAFF_TYPES.str's singular, by Park::StaffKind; the plural follows it.
constexpr std::array<size_t, Park::kStaffKinds> kStaffTypeNames{2, 0, 10, 6, 4, 8};

// The project researching the mission's obstacle, if one is.
const Park::Project *expansion(const Park &park) {
    for (const std::optional<Park::Project> &p : park.projects())
        if (p && p->kind == Park::ProjectKind::kExpansion && !p->finished)
            return &*p;
    return nullptr;
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
// The Challenges tab at 2:30.
constexpr ScreenRect kChallengePanel{88, 88, 214, 214};
constexpr ScreenRect kChallengeTitle{96, 96, 126, 30};
constexpr ScreenRect kChallengeStatus{226, 96, 68, 30};
constexpr ScreenRect kChallengeList{92, 132, 206, 166};
constexpr int kChallengeRowStep = 13;
constexpr ScreenRect kFiringLine{88, 314, 214, 72};
constexpr ScreenRect kAccept{543, 356, 31, 31};
constexpr Color kChosenRow{106, 125, 150};
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
    if (Error err = _accept.load(files, "ui/b_okay.MD2"))
        return err;
    for (auto [table, name] :
         {std::pair{&_challengeTitles, "CHALLENGE_TITLES"}, std::pair{&_challengeGeneric, "CHALLENGE_GENERIC"},
          std::pair{&_challengeSpecific, "CHALLENGE_SPECIFIC"}, std::pair{&_ui, "UITEXT"},
          std::pair{&_operations, "OPERATION"}, std::pair{&_missions, "MISSION_DESC"},
          std::pair{&_statistics, "STAT_TITLES"}, std::pair{&_tags, "TAG_SYSTEM"}, std::pair{&_themes, "THEMENAMES"},
          std::pair{&_abilities, "ABILITIES"}, std::pair{&_staffTypes, "STAFF_TYPES"},
          std::pair{&_ranks, "PROMOTIONLEVELS"}})
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
    const Park::Mission &mission = park.mission();
    const int m = mission.number;
    std::vector<std::string> out{_tags.text(kPresidentText), player,
                                 _operations.text(size_t(std::max(0, m >= 0 ? m : park.progress())))};
    std::string body;
    if (m < 0) {
        body = noObjective(park);
    } else {
        // Plughole's letter tells how its obstacle's going; the others say
        // a solution's found (UITEXT 81).
        size_t text = size_t(m);
        if (m == kPlughole)
            text = park.obstacleResearched() && park.obstacleStaffReady() ? kPlugholeReady
                   : park.obstacleResearched()                            ? kPlugholeSolved
                   : expansion(park)                                      ? kPlugholeResearching
                                                                          : text;
        body = fillGameString(_missions, text, {{0, std::to_string(mission.target)}});
        if (m != kPlughole && mission.obstacle && park.obstacleResearched())
            body += _ui.text(kSolutionFoundText);
    }
    body += "\n\n" + _tags.text(kPresidentText) + "\n" + _tags.text(kPresidentTitleText);
    for (const std::string &line : wrapGameText(_label, _characters, body, kLetterWidth))
        out.push_back(line);
    return out;
}

// With no objective here: that it's done, and where the next is, or that
// all are.
std::string Briefing::noObjective(const Park &park) const {
    const std::vector<Park::Mission> &missions = park.missions();
    const int next = park.progress();
    if (next >= int(missions.size()))
        return _ui.text(kAllDoneText);
    const std::string where = _themes.text(size_t(Park::themeName(missions[size_t(next)].theme)));
    const bool doneHere = next > 0 && missions[size_t(next - 1)].theme == park.theme();
    return fillGameString(_ui, doneHere ? kDoneGoText : kElsewhereText, {{5, where}});
}

std::vector<std::string> Briefing::details(const Park &park) const {
    const Park::Mission &mission = park.mission();
    std::vector<std::string> lines{_ui.text(kRequirementsText)};
    std::vector<std::string> status{_ui.text(kStatusText)};
    if (mission.number < 0) {
        lines.push_back(noObjective(park));
        status.clear();
    }
    for (const Park::Mission::Objective &o : mission.objectives) {
        const int title = statisticTitle(o.statistic);
        if (title < 0)
            continue;
        lines.push_back(fillGameString(_statistics, size_t(title), {{0, std::to_string(o.target)}}));
        status.push_back(
            fillGameString(_statistics, size_t(title + 1), {{0, std::to_string(park.statisticValue(o.statistic))}}));
    }
    if (mission.promotion > 0) {
        lines.push_back(fillGameString(
            _ui, kPromotionText,
            {{7, _ranks.text(size_t(mission.promotion))}, {8, _ranks.text(size_t(std::max(1, park.rank())))}}));
        status.push_back(fillGameString(_ui, kPositionText, {{12, _ranks.text(size_t(std::max(1, park.rank())))}}));
    }
    if (mission.challenges > 0)
        lines.push_back(fillGameString(_ui, kChallengesNeededText,
                                       {{9, std::to_string(mission.challenges)},
                                        {5, _themes.text(size_t(Park::themeName(mission.challengesTheme)))},
                                        {10, std::to_string(park.challengesWonIn(mission.challengesTheme))}}));
    if (mission.obstacle && mission.obstacleEffort > 0) {
        lines.push_back(_ui.text(kResearchExpansionText));
        if (park.obstacleResearched()) {
            lines.push_back(_ui.text(kTrainedStaffText));
            // Each need: so many of a kind, able so, and those trained.
            const std::vector<std::vector<int>> staff = park.obstacleStaff();
            for (size_t i = 0; i < mission.staffNeeded.size(); ++i) {
                const Park::Mission::StaffNeed &need = mission.staffNeeded[i];
                const std::optional<Park::StaffKind> kind = Park::missionStaffKind(need.staffType);
                const std::string who =
                    kind ? _staffTypes.text(kStaffTypeNames[size_t(*kind)] + (need.number > 1 ? 1 : 0)) : "";
                lines.push_back(std::to_string(need.number) + " " + who + ": " + _abilities.text(size_t(need.ability)) +
                                " (" + std::to_string(staff[i].size()) + "/" + std::to_string(need.number) + ")");
            }
            status.push_back(_ui.text(park.obstacleStaffReady() ? kStaffReadyText : kStaffMissingText));
        } else if (const Park::Project *p = expansion(park)) {
            status.push_back(fillGameString(_ui, kResearchingText,
                                            {{11, std::to_string(int(100 * p->done / float(std::max(1, p->cost))))}}));
        } else {
            status.push_back(_ui.text(kResearchNeededText));
        }
    }
    if (mission.goldenTickets > 0) {
        lines.push_back(_ui.text(kTicketsNeededText));
        lines.push_back(fillGameString(_statistics, mission.goldenTickets == 1 ? kOneTicketText : kTicketsText,
                                       {{0, std::to_string(mission.goldenTickets)}}));
        status.push_back(fillGameString(_statistics, kTicketsHeldText, {{0, std::to_string(park.goldenTickets())}}));
    }
    // Broken to the letter's width, the status after a blank line.
    std::vector<std::string> out;
    auto add = [&](const std::string &text) {
        for (const std::string &line : wrapGameText(_label, _characters, text, kLetterWidth))
            out.push_back(line);
    };
    for (const std::string &line : lines)
        add(line);
    if (!status.empty()) {
        out.emplace_back();
        for (const std::string &line : status)
            add(line);
    }
    return out;
}

void Briefing::draw(Surface &screen, const Park &park, const std::string &player, const View &view) const {
    auto lit = [&](Control c) { return view.hovered == c ? kLitMesh : kPlainMesh; };
    drawUiMesh(screen, _window, 0, kWindow);
    drawText(screen, _title, kTitleX, kTitleY, _ui.text(view.challenges ? kChallengesTitleText : kTitleText), kWhite);
    drawUiMesh(screen, _help, 0, kHelp);
    drawUiMesh(screen, _close, 0, kClose);
    drawUiMesh(screen, _panel, 0, kPanel);
    for (size_t i = 0; i < kTabs.size(); ++i) {
        drawUiMesh(screen, _tab, (i == 1) == view.challenges ? kSelectedTabMesh : kPlainMesh, kTabs[i]);
        drawCentred(screen, _label, kTabs[i], kTabs[i].y + 9, _ui.text(i == 0 ? kObjectivesText : kChallengesText),
                    kWhite);
    }
    if (view.challenges) {
        drawChallenges(screen, park, view);
        drawUiMesh(screen, _cancel, lit(Control::kCancel), kCancel);
        return;
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

// The Challenges tab, as the recording shows it at 2:30: this world's
// challenges and their status; the one chosen in full beside them, or
// that none is available; the firing line below.
void Briefing::drawChallenges(Surface &screen, const Park &park, const View &view) const {
    drawUiMesh(screen, _panel, 0, kChallengePanel);
    for (auto [rect, text] : {std::pair{kChallengeTitle, kChallengeTitleColumnText},
                              std::pair{kChallengeStatus, kChallengeStatusColumnText}}) {
        drawUiMesh(screen, _button, 0, rect);
        drawCentred(screen, _label, rect, rect.y + 11, _challengeGeneric.text(text), kWhite);
    }
    drawUiMesh(screen, _frame, 0, kChallengeList);
    const std::vector<Park::ChallengeState> &challenges = park.challenges();
    for (size_t i = 0; i < challenges.size(); ++i) {
        const ScreenRect row = challengeRow(i);
        if (row.y + row.h > kChallengeList.y + kChallengeList.h)
            break;
        if (int(i) == view.chosen)
            for (int y = row.y; y < row.y + row.h; ++y)
                for (int x = row.x; x < row.x + row.w; ++x) {
                    uint8_t *p = screen.row(y) + x * 4;
                    p[0] = kChosenRow.r;
                    p[1] = kChosenRow.g;
                    p[2] = kChosenRow.b;
                }
        drawText(screen, _label, row.x + 4, row.y + 1, _challengeTitles.text(size_t(challenges[i].challenge.number)),
                 kWhite);
        drawText(screen, _label, kChallengeStatus.x + 22, row.y + 1, challengeStatus(challenges[i]), kWhite);
    }
    // "FIRING LINE STATUS: You can only fail {6} more challenges before you get fired."
    drawUiMesh(screen, _frame, 0, kFiringLine);
    const int left = park.failuresLeft();
    std::string firing = fillGameString(_challengeGeneric, left == 1 ? kChallengeFiringOneText : kChallengeFiringText,
                                        {{6, std::to_string(left)}});
    if (const size_t start = firing.find_first_not_of('\n'); start != std::string::npos)
        firing = firing.substr(start);
    int y = kFiringLine.y + 8;
    for (const std::string &line : wrapGameText(_label, _characters, firing, kFiringLine.w - 16)) {
        drawText(screen, _label, kFiringLine.x + 8, y, line, kWhite);
        y += kLineStep;
    }
    drawUiMesh(screen, _frame, 0, kList);
    if (view.chosen < 0 || size_t(view.chosen) >= challenges.size()) {
        const bool any = std::any_of(challenges.begin(), challenges.end(), [](const Park::ChallengeState &s) {
            return s.status == Park::ChallengeStatus::kAvailable;
        });
        if (!any)
            drawCentred(screen, _label, kList, kList.y + kList.h / 2 - 6, _ui.text(kNoChallengesText), kWhite);
        return;
    }
    y = kLetterY;
    for (const std::string &line : challengeDetails(park, size_t(view.chosen))) {
        if (y + kLineStep > kList.y + kList.h)
            break;
        drawText(screen, _label, kLetterX, y, line, kWhite);
        y += kLineStep;
    }
    if (challenges[size_t(view.chosen)].status == Park::ChallengeStatus::kAvailable)
        drawUiMesh(screen, _accept, view.hovered == Control::kAccept ? kLitMesh : kPlainMesh, kAccept);
}

std::string Briefing::challengeStatus(const Park::ChallengeState &state) const {
    switch (state.status) {
    case Park::ChallengeStatus::kAvailable:
        return _challengeGeneric.text(kChallengeAvailableText);
    case Park::ChallengeStatus::kPreparing:
        return _challengeGeneric.text(kChallengePreparingText);
    case Park::ChallengeStatus::kActive:
        return _challengeGeneric.text(kChallengeActiveText);
    case Park::ChallengeStatus::kComplete:
        return _challengeGeneric.text(kChallengeCompleteText);
    default:
        return _challengeGeneric.text(kChallengeNotAvailableText);
    }
}

// Its letter (CHALLENGE_SPECIFIC's first of its three), its prizes, then
// "CHALLENGE STATUS:" and how it stands, with its count or measure as
// it's being prepared for or run (the second and third).
std::vector<std::string> Briefing::challengeDetails(const Park &park, size_t index) const {
    const Park::ChallengeState &state = park.challenges()[index];
    const Park::Challenge &c = state.challenge;
    const size_t first = size_t(c.number - 1) * 3;
    const std::map<int, std::string> values{
        {0, std::to_string(state.status == Park::ChallengeStatus::kPreparing ? state.daysLeft : c.preparation)},
        {1, std::to_string(state.status == Park::ChallengeStatus::kActive ? state.daysLeft : c.time)},
        {2, std::to_string(c.target)},
        {3, std::to_string(c.target)},
        {5, std::to_string(c.target2)},
        {6, std::to_string(c.target2)}};
    std::string text =
        _challengeGeneric.text(kChallengeDetailsText) + fillGameString(_challengeSpecific, first, values);
    // The prizes: golden tickets, shares, cash, the share price's rise and fall, a promotion.
    text += fillGameString(_challengeGeneric, c.tickets == 1 ? kChallengeOneTicketText : kChallengeTicketsText,
                           {{0, std::to_string(c.tickets)}});
    if (c.shares > 0)
        text += fillGameString(_challengeGeneric, kChallengeSharesText, {{1, std::to_string(c.shares)}});
    if (c.cash > 0)
        text += fillGameString(_challengeGeneric, kChallengeCashText, {{2, std::to_string(c.cash)}});
    text += fillGameString(_challengeGeneric, kChallengePriceWonText, {{3, std::to_string(c.priceWon)}}) +
            fillGameString(_challengeGeneric, kChallengePriceLostText, {{4, std::to_string(c.priceLost)}});
    if (c.promotion > 0)
        text += _challengeGeneric.text(kChallengePromotionText) + _ranks.text(size_t(c.promotion)) + "\n";
    text += _challengeGeneric.text(kChallengeStatusLabel);
    std::map<int, std::string> now = values;
    now[2] = std::to_string(park.challengeValue(index));
    now[5] = std::to_string(park.challengeValue2(index));
    now[4] = "";
    switch (state.status) {
    case Park::ChallengeStatus::kPreparing:
        text += _challengeGeneric.text(kChallengePreparingNowText) + fillGameString(_challengeSpecific, first + 1, now);
        break;
    case Park::ChallengeStatus::kActive:
        text += _challengeGeneric.text(kChallengeUnderwayText) + fillGameString(_challengeSpecific, first + 2, now);
        break;
    case Park::ChallengeStatus::kComplete:
        text += _challengeGeneric.text(kChallengeCompletedText);
        break;
    case Park::ChallengeStatus::kAvailable:
        text += state.failures == 0   ? _challengeGeneric.text(kChallengeNotAttemptedText)
                : state.failures == 1 ? _challengeGeneric.text(kChallengeFailedOnceText)
                                      : fillGameString(_challengeGeneric, kChallengeFailedTimesText,
                                                       {{5, std::to_string(state.failures)}});
        break;
    default:
        text += _challengeGeneric.text(kChallengeNotYetText);
        break;
    }
    return wrapGameText(_label, _characters, text, kLetterWidth + 20);
}

int Briefing::challengeRowAt(int x, int y) {
    for (size_t i = 0; i < 16; ++i)
        if (challengeRow(i).contains(x, y))
            return int(i);
    return -1;
}

ScreenRect Briefing::challengeRow(size_t row) {
    return {kChallengeList.x + 3, kChallengeList.y + 4 + int(row) * kChallengeRowStep, kChallengeList.w - 6,
            kChallengeRowStep};
}

ScreenRect Briefing::placeOf(Control control) {
    switch (control) {
    case Control::kAccept:
        return kAccept;
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
                      Control::kCancel, Control::kClose, Control::kAccept})
        if (placeOf(c).contains(x, y))
            return c;
    return Control::kNone;
}

bool showBriefing(System &sys, const Briefing &briefing, Park &park, const std::string &player,
                  const std::function<void(Surface &)> &backdrop, bool challenges) {
    Surface screen;
    screen.create(kOpeningWidth, kOpeningHeight, PixelFormat::kRGBA32);
    Briefing::View view;
    view.challenges = challenges;
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
                if (const int row = Briefing::challengeRowAt(ev.mouseX, ev.mouseY);
                    view.challenges && row >= 0 && size_t(row) < park.challenges().size()) {
                    view.chosen = row;
                    break;
                }
                switch (Briefing::controlAt(ev.mouseX, ev.mouseY)) {
                case Briefing::Control::kObjectivesTab:
                case Briefing::Control::kChallengesTab:
                    view.challenges = Briefing::controlAt(ev.mouseX, ev.mouseY) == Briefing::Control::kChallengesTab;
                    break;
                case Briefing::Control::kAccept:
                    if (view.challenges && view.chosen >= 0)
                        park.acceptChallenge(size_t(view.chosen));
                    break;
                case Briefing::Control::kTurn:
                    if (!view.challenges)
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
