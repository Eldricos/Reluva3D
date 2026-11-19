// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the game's screens and the data viewer's pages draw with.

#include "engines/age/ui/screen.h"

#include "graphics/font.h"

namespace reluva::age {

void Page::clear(uint8_t r, uint8_t g, uint8_t b) {
    fill(0, 0, _ctx.screen.w, _ctx.screen.h, r, g, b);
}

void Page::fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    Surface &s = _ctx.screen;
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w);
    const int y1 = std::min(y + h, s.h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t *p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

void Page::text(int x, int y, const std::string &s, Color c) {
    font::drawText(_ctx.screen, x, y, s, c);
}

void Page::setTitle(const std::string &detail) {
    _ctx.sys.setWindowTitle(std::string("Reluva - ") + _ctx.game.name + " - " + name() + ": " + detail);
}

Error runPages(System &sys, Surface &screen, std::vector<std::unique_ptr<Page>> &pages, bool tabs) {
    size_t page = 0;
    while (page < pages.size() && pages[page]->count() == 0)
        ++page;
    if (page == pages.size())
        return Error::fail("No files of the game found");
    pages[page]->activate();
    // Leaving a 3D page: its renderer goes, the framebuffer comes back.
    auto switchTo = [&](size_t next) -> Error {
        const bool was3D = pages[page]->uses3D();
        pages[page]->deactivate();
        if (was3D)
            if (Error err = sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
                return err;
        page = next;
        pages[page]->activate();
        return Error::success();
    };
    struct Leave { // a 3D page's GPU objects go before the system's renderer
        std::vector<std::unique_ptr<Page>> &pages;
        ~Leave() {
            for (auto &p : pages)
                p->deactivate();
        }
    } leave{pages};

    while (true) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return Error::success();
            const bool pageKey = pages[page]->takesKey(ev);
            if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !pageKey)
                return Error::success();
            if (tabs && ev.type == EventType::kKeyDown && ev.key == key::kTab && !pageKey) {
                size_t next = page;
                do
                    next = (next + 1) % pages.size();
                while (pages[next]->count() == 0);
                if (Error err = switchTo(next))
                    return err;
                continue;
            }
            pages[page]->handleEvent(ev);
            if (pages[page]->wantsToQuit())
                return Error::success();
        }
        pages[page]->update(sys.getMillis());
        sys.updateScreen(screen);
        // 3D pages are paced by presenting (vsync); the rest needn't spin.
        if (!pages[page]->uses3D())
            sys.delayMillis(10);
    }
}

} // namespace reluva::age
