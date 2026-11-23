// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/galaxy_screen.h"

#include "engines/botf/ui/galaxy_parts.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <set>
#include <string>

// The galaxy screen's empire panel: the empire's figures, the other
// empires' emblems, and its task forces
// (docs/engines/botf/systems/galaxy-screen.md, "The empire's panel").

namespace reluva::botf {

using namespace galaxy;

void GalaxyScreen::drawIcon(Surface &screen, int x, int y, const Widget &w, const std::string &file) const {
    if (const image::Image *img = _assets.picture(file))
        image::blit(screen, *img, x + (w.w - img->w) / 2, y);
}

void GalaxyScreen::drawTaskForcesBar(Surface &screen) const {
    const Placed *button = find(kTaskForcesButton);
    const Placed *panel = find(kPanel);
    if (!button || !panel)
        return;
    const int x = panel->x;
    const int y = panel->y + kTaskForcesY;
    const std::string &back = _taskForcesBack;
    if (const image::Image *img = back.empty() ? nullptr : _assets.picture(back))
        for (int py = 0; py < img->h; ++py)
            for (int px = 0; px < img->w; ++px) {
                const uint8_t *p = img->pixel(px, py);
                if ((p[0] | p[1] | p[2]) && p[3])
                    put(screen, x + px, y + py, Color{p[0], p[1], p[2]});
            }
    for (const auto &[what, file] : button->widget->pictures)
        if (what == "normal")
            drawPicture(screen, kTaskForcesButton, file);
    drawPicture(screen, kTaskForcesTab, kTaskForcesTabPicture);
}

void GalaxyScreen::drawPanel(Surface &screen) const {
    const SavedEmpire &e = game().empires[size_t(_empire)];
    // The empire's information: a label at the left of each line, its
    // figure at the right; dilithium and events share the last line.
    if (const Placed *box = find(kInfoText))
        if (const Font *font = _assets.font(box->widget->fonts.empty() ? "" : box->widget->fonts[0].file)) {
            const int right = box->x + box->widget->w - kFigureMargin;
            auto line = [&](int i, const std::string &left, Color leftColor, const std::string &figure,
                            Color figureColor) {
                const int y = box->y + 1 + i * kLinePitch;
                font->drawInColor(screen, box->x, y, left, leftColor);
                font->drawInColor(screen, right - font->measure(figure), y, figure, figureColor);
            };
            auto withSign = [](int v) { return (v >= 0 ? "+" : "") + std::to_string(v); };
            line(0, word(lex::kTotalCredits, "TOTAL CREDITS"), kLabelColor, std::to_string(e.credits), kFigureColor);
            line(1, word(lex::kCreditsPerTurn, "CREDITS PER TURN"), kLabelColor, withSign(e.creditsPerTurn),
                 kFigureColor);
            // The dilithium's figure first, in the figures' colour (but 0 in
            // the words', unsigned), and its word 6 pixels after it (so
            // after "+1" and "0" on screenshots); then how many of the last
            // turn's results the empire has, in the figures' colour, or "No
            // Events" in the words'.
            const int events = eventCount();
            if (events == 0)
                line(2, "", kLabelColor, word(lex::kNoEvents, "No Events"), kLabelColor);
            else
                line(2, "", kLabelColor,
                     std::to_string(events) + " " +
                         (events == 1 ? word(lex::kEvent, "Event") : word(lex::kEvents, "Events")),
                     kFigureColor);
            const std::string dilithium = e.dilithiumPerTurn == 0 ? "0" : withSign(e.dilithiumPerTurn);
            font->drawInColor(screen, box->x, box->y + 1 + 2 * kLinePitch, dilithium,
                              e.dilithiumPerTurn == 0 ? kLabelColor : kFigureColor);
            font->drawInColor(screen, box->x + font->measure(dilithium) + kDilithiumGap, box->y + 1 + 2 * kLinePitch,
                              upperAscii(word(lex::kDilithium, "Dilithium")), kLabelColor);
        }
    drawTaskForcesBar(screen);
    // The other empires' emblems, each in its place.
    for (int other = 0; other < 5; ++other)
        if (const Placed *p = other == _empire ? nullptr : find(kFirstEmblem + other))
            drawIcon(screen, p->x, p->y, *p->widget, std::string("i_") + kLetters[other] + "26.tga");
    // A row for each of the empire's outposts in the selected sector, then
    // each task force there (the thirty-first turn's lists the one of four
    // at Deneb, the seventeenth's a Klingon scout with the Federation's):
    // its emblem across its place, and two lines.
    const Placed *icon = find(kRowIcon);
    const Placed *text = find(kRowText);
    const Placed *panel = find(kPanel);
    if (!icon || !text || !panel || _selected.first < 0)
        return;
    const Font *font = _assets.font(text->widget->fonts.empty() ? "" : text->widget->fonts[0].file);
    int row = 0;
    auto drawRow = [&](const std::string &emblem, const std::string &first, const std::string &second) {
        const int dy = row++ * kRowHeight;
        if (icon->y + dy >= panel->y + panel->widget->h)
            return;
        drawIcon(screen, icon->x, icon->y + dy, *icon->widget, emblem);
        if (!font)
            return;
        font->drawInColor(screen, text->x, text->y + dy + 1, first, kTaskForceColor);
        font->drawInColor(screen, text->x, text->y + dy + 1 + kLinePitch, second, kTaskForceColor);
    };
    // An outpost: its emblem (i_ho30.tga), "Outpost" and "OPERATIONAL" (one
    // seen; one being built shows how far it's got, not read yet).
    for (const SavedOutpost *o : _state.outpostsAt(_selected.first, _selected.second))
        if (o->owner == _empire)
            drawRow(std::string("i_") + _letter + "o30.tga", word(lex::kOutpost, "Outpost"),
                    word(lex::kOperational, "OPERATIONAL"));
    // A task force: the empire's own, its mission and, where it's headed
    // elsewhere, how long it'll take ("ENGAGE eta 2 TURNS"), then its first
    // ship's class; another empire's, its first ship's class alone, in the
    // same colour (guessed: the one seen is selected, its line orange).
    for (const SavedTaskForce *tf : _state.taskForcesAt(_selected.first, _selected.second)) {
        const SavedTaskForce &t = *tf;
        if (t.owner < 0 || t.owner >= int(std::size(kLetters)))
            continue;
        std::string ship;
        if (const SavedShip *first = t.shipIds.empty() ? nullptr : _state.ship(t.shipIds.front());
            first && size_t(first->shipClass) < _shipClasses.count())
            ship = _shipClasses.name(size_t(first->shipClass));
        const std::string emblem = std::string("i_") + kLetters[t.owner] + "26.tga";
        if (t.owner != _empire) {
            drawRow(emblem, ship, "");
            continue;
        }
        std::string orders = missionWord(t.mission);
        if (const int turns = etaTurns(t); turns > 0) {
            orders += (orders.empty() ? "" : " ") + word(lex::kEta, "eta") + " " + std::to_string(turns) + " " +
                      (turns == 1 ? std::string("TURN") : word(lex::kTurns, "TURNS"));
        }
        drawRow(emblem, orders, ship);
    }
}

int GalaxyScreen::etaTurns(const SavedTaskForce &t) const {
    // The sectors to its destination, the larger of the two distances, over
    // its slowest ship's speed (shiplist.sst's), rounded up: 2 for a science
    // vessel (speed 1) two rows away, the one seen.
    if (t.destinationX < 0 || (t.destinationX == t.x && t.destinationY == t.y))
        return 0;
    double speed = 0;
    for (const int id : t.shipIds)
        if (const SavedShip *s = _state.ship(id); s && size_t(s->shipClass) < _shipClasses.count()) {
            double v = 0;
            std::memcpy(&v, _shipClasses.record(size_t(s->shipClass)) + kShipSpeedOffset, sizeof v);
            speed = speed == 0 ? v : std::min(speed, v);
        }
    if (speed <= 0)
        return 0;
    const int sectors = std::max(std::abs(t.destinationX - t.x), std::abs(t.destinationY - t.y));
    return int(std::ceil(sectors / speed));
}

std::string GalaxyScreen::missionWord(int mission) const {
    // GWTForce's missions (published) and lexicon.dic's words for them:
    // AVOID and ENGAGE seen; none for a plain move.
    struct Mission {
        int mission;
        int entry;
        const char *english;
    };
    static constexpr Mission kMissions[] = {
        {1, 663, "TERRAFORM PLANET"}, {2, 656, "COLONIZE SYSTEM"}, {3, 661, "RAID"},
        {5, lex::kAvoid, "AVOID"},    {6, lex::kEngage, "ENGAGE"}, {7, 653, "BUILD OUTPOST"},
        {8, 654, "BUILD STARBASE"},   {9, 664, "TRAIN CREW"},      {12, 662, "SCRAP"},
        {13, 657, "ENTER WORMHOLE"},  {14, 651, "ATTACK SYSTEM"}};
    for (const Mission &m : kMissions)
        if (m.mission == mission)
            return word(m.entry, m.english);
    return {};
}

} // namespace reluva::botf
