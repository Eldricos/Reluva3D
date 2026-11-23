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

// The galaxy screen's map: its grid, the sectors the empire has seen and
// scans, the claims, ranges, stars, names and task forces
// (docs/engines/botf/systems/galaxy-screen.md, "The map").

namespace reluva::botf {

using namespace galaxy;

bool GalaxyScreen::scanned(int x, int y) const {
    const SavedGalaxy &g = game().galaxy;
    return x >= 0 && y >= 0 && x < g.width && y < g.height && g.at(x, y).scanStrength[size_t(_empire)] > 0;
}

bool GalaxyScreen::revealed(int x, int y) const {
    const SavedGalaxy &g = game().galaxy;
    if (x < 0 || y < 0 || x >= g.width || y >= g.height)
        return false;
    const SavedSector &s = g.at(x, y);
    return ((s.revealed | s.explored) >> _empire & 1) != 0;
}

bool GalaxyScreen::view(int &x0, int &y0, int &x1, int &y1) const {
    const Placed *map = find(kMap);
    if (!map || game().galaxy.width <= 0)
        return false;
    // The zoomed-out grid, lines included, within the map's place.
    const int gx = map->x + (map->widget->w - game().galaxy.width * kOutSector) / 2;
    const int gy = map->y + (map->widget->h - game().galaxy.height * kOutSector) / 2;
    x0 = std::max(gx, map->x);
    y0 = std::max(gy, map->y);
    x1 = std::min(gx + game().galaxy.width * kOutSector, map->x + map->widget->w - 1);
    y1 = std::min(gy + game().galaxy.height * kOutSector, map->y + map->widget->h - 1);
    return true;
}

bool GalaxyScreen::gridCorner(int &gx, int &gy) const {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (!view(x0, y0, x1, y1))
        return false;
    const SavedGalaxy &g = game().galaxy;
    const int sector = kZooms[_zoom].sector;
    // Zoomed out, the grid is the view. Zoomed in, the corner of the sector
    // selected when zooming in is at the view's middle (Sol's on the
    // screenshot, at 511), the grid kept over the whole view (there, at the
    // galaxy's top); selecting another doesn't move it (a screenshot with
    // Babel selected has Sol's place).
    if (_zoom == 0) {
        const Placed *map = find(kMap);
        gx = map->x + (map->widget->w - g.width * sector) / 2;
        gy = map->y + (map->widget->h - g.height * sector) / 2;
        return true;
    }
    const int sx = std::max(_zoomCentre.first, 0);
    const int sy = std::max(_zoomCentre.second, 0);
    auto place = [&](int middle, int s, int first, int last, int sectors) {
        const int at = middle - s * sector;
        return std::clamp(at, std::min(first, last - sectors * sector), first);
    };
    gx = place((x0 + x1) / 2, sx, x0, x1, g.width);
    gy = place((y0 + y1) / 2, sy, y0, y1, g.height);
    return true;
}

void GalaxyScreen::drawRanges(Surface &screen, int gx, int gy) const {
    // Each range's border: a line along every sector edge between a sector
    // in it and one out of it (or the grid's edge), the shorter ranges over
    // the longer.
    const SavedGalaxy &g = game().galaxy;
    const int sector = kZooms[_zoom].sector;
    for (size_t r = std::size(kRanges); r-- > 0;) {
        auto in = [&](int x, int y) {
            // Not into the sectors of an empire whose territory it has no
            // access to (the thirty-fourth turn's stop at the Cardassians'
            // and Romulans', with whom the Federation has non-aggression
            // treaties, and run into the Klingons' and Ferengi's).
            if (!_state.mayEnter(_empire, x, y))
                return false;
            for (const auto &[cx, cy] : _rangeCentres) {
                const int dx = std::abs(x - cx);
                const int dy = std::abs(y - cy);
                if (std::max(dx, dy) + std::min(dx, dy) / 2 <= kRanges[r].sectors)
                    return true;
            }
            return false;
        };
        for (int y = 0; y <= g.height; ++y)
            for (int x = 0; x <= g.width; ++x) {
                // Along the galaxy's top edge, a pixel lower: inside the
                // grid's line (both screenshots with a range reaching it).
                if (in(x, y) != in(x, y - 1) && x < g.width)
                    for (int k = 0; k <= sector; ++k)
                        put(screen, gx + x * sector + k, gy + y * sector + (y == 0 ? 1 : 0), kRanges[r].color);
                if (in(x, y) != in(x - 1, y) && y < g.height)
                    for (int k = 0; k <= sector; ++k)
                        put(screen, gx + x * sector, gy + y * sector + k, kRanges[r].color);
            }
    }
}

void GalaxyScreen::drawMap(Surface &screen) const {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    int gx = 0;
    int gy = 0;
    if (!view(x0, y0, x1, y1) || !gridCorner(gx, gy))
        return;
    // Drawn on a surface the view's size, which cuts off what falls outside
    // it, over what the screen has there.
    Surface view;
    view.create(x1 - x0 + 1, y1 - y0 + 1, PixelFormat::kRGBA32);
    for (int y = 0; y < view.h; ++y)
        std::copy_n(screen.row(y0 + y) + size_t(x0) * 4, size_t(view.w) * 4, view.row(y));
    drawGrid(view, gx - x0, gy - y0);
    for (int y = 0; y < view.h; ++y)
        std::copy_n(view.row(y), size_t(view.w) * 4, screen.row(y0 + y) + size_t(x0) * 4);
}

void GalaxyScreen::drawClaims(Surface &screen, uint16_t claims, int left, int top, int sector) const {
    std::vector<int> claimants;
    for (int e = 0; e < int(std::size(kClaims)); ++e)
        if (claims >> e & 1)
            claimants.push_back(e);
    if (claimants.size() == 1) {
        const Claim &c = kClaims[claimants.front()];
        for (int y = 1; y < sector; y += 2)
            for (int x = 1; x < sector; ++x)
                if ((x + (y - 1) / 2 * c.shift) % c.period == c.phase)
                    put(screen, left + x, top + y, c.color);
    } else if (claimants.size() > 1) {
        for (int y = 1; y < sector; ++y)
            for (int x = 1; x < sector; ++x)
                for (const Stripe &stripe : kContestedStripes)
                    if ((x + y) % kContestedPeriod == stripe.at)
                        put(screen, left + x, top + y, stripe.color);
    }
}

void GalaxyScreen::drawGrid(Surface &screen, int gx, int gy) const {
    const Zoom &z = kZooms[_zoom];
    const int sector = z.sector;
    const int width = game().galaxy.width * sector;
    const int height = game().galaxy.height * sector;
    const SavedGalaxy &g = game().galaxy;
    // The backdrop, from the view's corner whichever the zoom, only inside
    // the sectors the empire hasn't seen: the grid's lines and the sectors
    // seen show the screen's background (hm_scrn.tga), as on the
    // screenshots, every pixel of them.
    const image::Image *backdrop = _assets.picture(std::string(1, _letter) + "m_glmap.tga");
    for (int sy = 0; sy < g.height; ++sy)
        for (int sx = 0; sx < g.width; ++sx) {
            const int left = gx + sx * sector;
            const int top = gy + sy * sector;
            if (!revealed(sx, sy)) {
                if (!backdrop)
                    continue;
                for (int y = std::max(top + 1, 0); y < std::min({top + sector, screen.h, backdrop->h}); ++y)
                    for (int x = std::max(left + 1, 0); x < std::min({left + sector, screen.w, backdrop->w}); ++x) {
                        const uint8_t *p = backdrop->pixel(x, y);
                        put(screen, x, y, Color{p[0], p[1], p[2]});
                    }
                continue;
            }
            drawClaims(screen, g.at(sx, sy).claims, left, top, sector);
        }
    // The lines next to a sector the scanners reach now are dotted: every
    // other pixel from each sector's corner.
    for (int row = 0; row <= g.height; ++row)
        for (int x = std::max(gx, 0); x < std::min(gx + width, screen.w); ++x)
            if ((x - gx) % sector % 2 == 0 && (scanned((x - gx) / sector, row) || scanned((x - gx) / sector, row - 1)))
                put(screen, x, gy + row * sector, kScannedGridColor);
    for (int col = 0; col <= g.width; ++col)
        for (int y = std::max(gy, 0); y < std::min(gy + height, screen.h); ++y)
            if ((y - gy) % sector % 2 == 0 && (scanned(col, (y - gy) / sector) || scanned(col - 1, (y - gy) / sector)))
                put(screen, gx + col * sector, y, kScannedGridColor);
    // The stars and other objects the empire has seen, with the names of
    // those it has explored under them (the first turn shows Sol's, not its
    // neighbours'; the eleventh, every explored sector's). An object's
    // picture is centred where a star's would be.
    const Font *font = _assets.font("human9.fnt");
    const image::Image *starSized = _assets.picture(std::string("s-y-") + z.starSuffix);
    for (int sy = 0; sy < g.height; ++sy)
        for (int sx = 0; sx < g.width; ++sx) {
            if (!revealed(sx, sy))
                continue;
            const SavedSector &here = g.at(sx, sy);
            const image::Image *picture = nullptr;
            std::string name;
            int mapX = 0;
            int mapY = 0;
            bool own = false;
            int owner = -1;
            if (here.system >= 0) {
                const SavedSystem &sys = g.systems[size_t(here.system)];
                picture = _assets.picture(sys.starAnimation + z.starSuffix);
                name = sys.name;
                mapX = sys.mapX;
                mapY = sys.mapY;
                own = sys.controllingRace == _empire;
                owner = sys.controllingRace;
            } else if (here.stellarObject >= 0) {
                const SavedStellarObject &o = g.stellarObjects[size_t(here.stellarObject)];
                if (const char *file = stellarMapPicture(o.type))
                    picture = _assets.picture(file + std::string(z.stellarSuffix));
                name = o.name;
                mapX = o.mapX;
                mapY = o.mapY;
            }
            if (!picture)
                continue;
            const int slot = starSized ? starSized->w : picture->w;
            int x = gx + sx * sector + z.starStep * (mapX - 5 * sx) + z.starOffset + (slot - picture->w) / 2;
            int y = gy + sy * sector + z.starStep * (mapY - 5 * sy) + z.starOffset + (slot - picture->h) / 2;
            drawKeyed(screen, *picture, x, y);
            if (!font || !(here.explored >> _empire & 1))
                continue;
            // Centred on the picture less a pixel, rounding down: every name
            // on both zooms' screenshots, of odd widths and even.
            const int spare = picture->w - 1 - font->measure(name);
            const int nameX = x + (spare - (spare < 0 && spare % 2 != 0 ? 1 : 0)) / 2;
            font->drawInColor(screen, nameX, y + picture->h, name,
                              own                  ? kNameColor
                              : owner == kRomulans ? kRomulanNameColor
                                                   : kOtherNameColor);
        }
    // The selected system: an orange square just inside its sector's lines,
    // under the ranges (at the galaxy's top edge, the range covers its top).
    if (_selected.first >= 0)
        for (int k = 1; k < sector; ++k) {
            const int x0 = gx + _selected.first * sector;
            const int y0 = gy + _selected.second * sector;
            put(screen, x0 + k, y0 + 1, kSelectedColor);
            put(screen, x0 + k, y0 + sector - 1, kSelectedColor);
            put(screen, x0 + 1, y0 + k, kSelectedColor);
            put(screen, x0 + sector - 1, y0 + k, kSelectedColor);
        }
    drawRanges(screen, gx, gy);
    // The empire's outposts: its outpost emblem (i_ho11.tga) a pixel right
    // of the sector's bottom-left corner, at its foot (the Federation's two
    // at the thirty-fourth turn); the other empires' aren't shown.
    if (const image::Image *icon = _assets.picture(std::string("i_") + _letter + "o11.tga"))
        for (const SavedOutpost &o : g.outposts)
            if (o.owner == _empire && g.width > 0)
                image::blit(screen, *icon, gx + o.sector % g.width * sector + 1,
                            gy + o.sector / g.width * sector + sector - icon->h);
    // Where the empire's task forces are ordered to: a marker in the middle
    // of the sector.
    if (const image::Image *mark = _assets.picture("movs.tga"))
        for (const SavedTaskForce &t : game().taskForces)
            if (t.owner == _empire && t.destinationX >= 0 && (t.destinationX != t.x || t.destinationY != t.y))
                image::blit(screen, *mark, gx + t.destinationX * sector + (sector - mark->w) / 2,
                            gy + t.destinationY * sector + (sector - mark->h) / 2);
    // Task forces: the empire's emblem at their sector's corner, one for the
    // sector however many there are; the others' where its scanners reach
    // them, their small emblem a pixel right of and below the corner.
    std::set<std::pair<int, int>> drawn;
    for (const SavedTaskForce &t : game().taskForces) {
        if (!drawn.insert({t.x, t.y}).second)
            continue;
        if (t.owner == _empire) {
            if (const image::Image *icon = _assets.picture(std::string("i_") + _letter + z.fleet))
                image::blit(screen, *icon, gx + t.x * sector + z.fleetX, gy + t.y * sector + z.fleetY);
        } else if (scanned(t.x, t.y)) {
            if (const image::Image *icon = _assets.picture(otherFleetPicture(t.owner)))
                image::blit(screen, *icon, gx + t.x * sector + 1, gy + t.y * sector + 1);
        }
    }
}

} // namespace reluva::botf
