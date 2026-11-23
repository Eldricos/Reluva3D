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

// The galaxy screen's strip under the map: the selected system, its star
// and planets, or a stellar object
// (docs/engines/botf/systems/galaxy-screen.md, "The strip").

namespace reluva::botf {

using namespace galaxy;

const WindowLayout::Placed *GalaxyScreen::stripWidget(const std::vector<WindowLayout::Placed> &strip,
                                                      int32_t id) const {
    for (const WindowLayout::Placed &w : strip)
        if (w.depth > 0 && w.widget->id == id)
            return &w;
    return nullptr;
}

void GalaxyScreen::drawFrame(Surface &screen, const Animation &a, uint64_t frameMillis, int cx, int cy) const {
    if (a.frames.empty())
        return;
    const image::Image &f = a.frames[size_t(_now / frameMillis % a.frames.size())];
    image::blit(screen, f, cx - f.w / 2, cy - f.h / 2);
}

void GalaxyScreen::drawStrip(Surface &screen) const {
    if (_selected.first < 0)
        return;
    const SavedSector &sector = game().galaxy.at(_selected.first, _selected.second);
    if (sector.system >= 0)
        drawSystemStrip(screen, game().galaxy.systems[size_t(sector.system)]);
    else if (sector.stellarObject >= 0 && stellarStrip(game().galaxy.stellarObjects[size_t(sector.stellarObject)].type))
        drawStellarStrip(screen, game().galaxy.stellarObjects[size_t(sector.stellarObject)]);
}

std::vector<int> GalaxyScreen::planetCentres(int &sunCx, int &cy) const {
    // The star at the planets' place's right end, then the planets leftward
    // from it.
    const WindowLayout::Placed &area = *stripWidget(_systemStrip, kStripArea);
    cy = area.y + area.widget->h / 2;
    const int sunW = _sun.frameWidth;
    sunCx = area.x + area.widget->w - sunW + sunW / 2;
    auto centres = [&](int gap) {
        std::vector<int> out;
        int cx = sunCx;
        int w = sunW;
        for (const Animation &p : _planets) {
            cx -= (w + p.frameWidth) / 2 + gap;
            w = p.frameWidth;
            out.push_back(cx);
        }
        return out;
    };
    int gap = kPlanetGap;
    std::vector<int> xs = centres(gap);
    while (gap > 0 && !xs.empty() && xs.back() - _planets.back().frameWidth / 2 < area.x + kPlanetsLeftMargin)
        xs = centres(--gap);
    return xs;
}

int GalaxyScreen::planetAt(int x, int y) const {
    if (_selected.first < 0 || game().galaxy.at(_selected.first, _selected.second).system < 0)
        return -1;
    int sunCx = 0;
    int cy = 0;
    const std::vector<int> xs = planetCentres(sunCx, cy);
    for (size_t i = 0; i < xs.size(); ++i) {
        const Animation &a = _planets[i];
        if (x >= xs[i] - a.frameWidth / 2 && x < xs[i] - a.frameWidth / 2 + a.frameWidth &&
            y >= cy - a.frameHeight / 2 && y < cy - a.frameHeight / 2 + a.frameHeight)
            return int(i);
    }
    return -1;
}

void GalaxyScreen::drawSystemStrip(Surface &screen, const SavedSystem &system) const {
    const std::vector<WindowLayout::Placed> &strip = _systemStrip;
    if (const image::Image *back = _assets.picture(_systemLayout.root().background))
        image::blit(screen, *back, strip.front().x, strip.front().y);
    const WindowLayout::Placed &area = *stripWidget(strip, kStripArea);
    int cy = 0;
    int sunCx = 0;
    const std::vector<int> xs = planetCentres(sunCx, cy);
    const int sunW = _sun.frameWidth;
    if (system.controllingRace >= 0 && system.controllingRace < int(std::size(kLetters)))
        if (const image::Image *emblem =
                _assets.dimmed(std::string("i_") + kLetters[system.controllingRace] + "46.tga", kEmblemBrightness))
            image::blit(screen, *emblem, area.x, area.y);
    drawFrame(screen, _sun, kStarFrameMillis, sunCx, cy);
    if (const image::Image *icon = system.dilithium > 0 ? _assets.picture(kDilithiumIcon) : nullptr)
        drawKeyed(screen, *icon, sunCx - sunW / 2, area.y);
    const Font *small = _assets.font("human9.fnt");
    for (size_t i = 0; i < _planets.size(); ++i) {
        const Animation &a = _planets[i];
        drawFrame(screen, a, kPlanetFrameMillis, xs[i], cy);
        const SavedPlanet &p = system.planets[i];
        if (const char *file = bonusIcon(p))
            if (const image::Image *icon = _assets.picture(file))
                drawKeyed(screen, *icon, xs[i] - a.frameWidth / 2, cy - a.frameHeight / 2 - icon->h);
        if (!small || p.type < 0 || p.type >= int(std::size(kPlanetClasses)))
            continue;
        const std::string letter(1, kPlanetClasses[p.type]);
        const Color c = p.type == kGasGiant                    ? kClassColors[0]
                        : p.terraformDone >= p.terraformNeeded ? kClassColors[1]
                                                               : kClassColors[2];
        // Centred under the frame as the map's names are (14 letters of
        // Sol's, Deneb's and Babel's agree).
        const int left = xs[i] - a.frameWidth / 2;
        const int spare = a.frameWidth - 1 - small->measure(letter);
        small->drawInColor(screen, left + (spare - (spare < 0 && spare % 2 != 0 ? 1 : 0)) / 2, cy + a.frameHeight / 2,
                           letter, c);
    }
    // The title: "SYSTEM" and its name, and who lives there.
    const WindowLayout::Placed &title = *stripWidget(strip, kStripTitle);
    drawTextAt(screen, *title.widget, title.x, title.y, upperAscii(word(lex::kSystem, "system")), kLabelColor);
    drawTextAt(screen, *title.widget, title.x + kTitleNameX, title.y, system.name, kFigureColor);
    // Who lives there: "HUMANS" for the Federation's people, a minor
    // race's name in capitals ("BANDI" at Deneb); the other empires' are
    // taken to be their names' too (guessed).
    std::string people;
    if (system.residentRace < 0)
        people = upperAscii(word(lex::kUninhabited, "uninhabited"));
    else if (system.residentRace == 1)
        people = word(lex::kHumans, "HUMANS");
    else if (size_t(system.residentRace) < _races.count())
        people = upperAscii(_races.name(size_t(system.residentRace)));
    const WindowLayout::Placed &heading = *stripWidget(strip, kStripHeading);
    drawTextAt(screen, *heading.widget, heading.x, heading.y, people, kLabelColor);
    // The labels and the figures known; the growth rate and the orbitals
    // aren't worked out yet. With the pointer over a planet, its details
    // instead.
    const WindowLayout::Placed &labels = *stripWidget(strip, kStripLabels);
    const WindowLayout::Placed &values = *stripWidget(strip, kStripValues);
    const Font *font = _assets.font(labels.widget->fonts.empty() ? "" : labels.widget->fonts[0].file);
    if (!font)
        return;
    auto line = [&](int i, const std::string &label, const std::string &figure) {
        font->drawInColor(screen, labels.x, labels.y + 1 + i * kStripLinePitch, label, kLabelColor);
        font->drawInColor(screen, values.x + values.widget->w - font->measure(figure),
                          values.y + 1 + i * kStripLinePitch, figure, kFigureColor);
    };
    if (_pointedPlanet >= 0 && size_t(_pointedPlanet) < system.planets.size()) {
        drawPlanetDetails(system, system.planets[size_t(_pointedPlanet)], line);
        return;
    }
    const std::string label[] = {word(lex::kPopulation, "Population"), word(lex::kMaxPop, "Max Pop"),
                                 word(lex::kGrowthRate, "Growth Rate"), word(lex::kOrbitals, "Orbitals")};
    const std::string figure[] = {std::to_string(system.population), std::to_string(system.maxPopulation), "", ""};
    for (int i = 0; i < 4; ++i)
        line(i, upperAscii(label[i]), figure[i]);
}

void GalaxyScreen::drawPlanetDetails(
    const SavedSystem &system, const SavedPlanet &p,
    const std::function<void(int, const std::string &, const std::string &)> &line) const {
    // Its name and its type's word ("Volcanic"), its maximum population
    // (planspac.bin's, by its type, size and bonuses: every planet of Sol
    // and Xerxes on the screenshots), its growth rate (not worked out yet),
    // then its terraforming done and needed, or once terraformed who lives
    // there: "Humans" for the Federation's people, a minor race's name.
    const std::string type = p.type >= 0 && p.type < int(std::size(kPlanetTypeWords))
                                 ? word(kPlanetTypeWords[p.type].entry, kPlanetTypeWords[p.type].english)
                                 : "";
    line(0, p.name, type);
    const int maxPop =
        _planetPopulations.maxPopulation(p.type, p.size, bonusLevel(p.growthBonus), bonusLevel(p.energyBonus));
    line(1, word(lex::kMaxPop, "Max Pop"), std::to_string(maxPop));
    line(2, word(lex::kGrowthRate, "Growth Rate"), "");
    if (p.terraformDone < p.terraformNeeded) {
        line(3, word(lex::kTerraform, "Terraform"),
             std::to_string(p.terraformDone) + " / " + std::to_string(p.terraformNeeded));
        return;
    }
    std::string people;
    if (system.residentRace == 1)
        people = titleCase(word(lex::kHumans, "HUMANS"));
    else if (system.residentRace >= 0 && size_t(system.residentRace) < _races.count())
        people = _races.name(size_t(system.residentRace));
    line(3, "", people);
}

void GalaxyScreen::drawStellarStrip(Surface &screen, const SavedStellarObject &object) const {
    const std::vector<WindowLayout::Placed> &strip = _stellarStrip;
    if (const image::Image *back = _assets.picture(_stellarLayout.root().background))
        image::blit(screen, *back, strip.front().x, strip.front().y);
    const WindowLayout::Placed &area = *stripWidget(strip, kStripArea);
    drawFrame(screen, _sun, kStellarFrameMillis, area.x + area.widget->w / 2, area.y + area.widget->h / 2);
    const WindowLayout::Placed &title = *stripWidget(strip, kStripTitle);
    drawTextAt(screen, *title.widget, title.x, title.y, object.name, kLabelColor);
    const WindowLayout::Placed &heading = *stripWidget(strip, kStripHeading);
    drawTextAt(screen, *heading.widget, heading.x, heading.y, word(lex::kDescription, "Description"), kLabelColor);
    // Wrapped to the box's width, a line every 17 pixels; the sixth line
    // goes past the box's foot, as on the neutron star's screenshot.
    const WindowLayout::Placed &text = *stripWidget(strip, kStripValues);
    const Font *font = _assets.font(text.widget->fonts.empty() ? "" : text.widget->fonts[0].file);
    if (!font)
        return;
    int y = text.y + text.widget->textY + 1;
    for (const std::string &line : font->wrap(word(stellarStrip(object.type)->description, ""), text.widget->textW)) {
        font->drawInColor(screen, text.x + text.widget->textX, y, line, kDescriptionColor);
        y += kStripLinePitch;
    }
}

} // namespace reluva::botf
