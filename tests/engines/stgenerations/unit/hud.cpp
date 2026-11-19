// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/engines/stgenerations/support.h"

#include "common/bytes.h"

#include <cmath>
#include <vector>

#include "engines/stgenerations/game/dissolve.h"
#include "engines/stgenerations/game/helditem.h"
#include "engines/stgenerations/game/hud.h"
#include "engines/stgenerations/game/render3d.h"

using namespace reluva;
using namespace reluva::stgenerations;

namespace {

struct Sent {
    uint16_t object, event;
    std::vector<int16_t> args;
};

} // namespace

static void hud_inventory() {
    std::vector<Sent> sent;
    Hud hud([&](uint16_t o, uint16_t e, const std::vector<int16_t> &a) { sent.push_back({o, e, a}); });
    CHECK_EQUAL(hud.addItem(0x2001, -1), 1); // the first free slot is 1, not 0
    CHECK_EQUAL(hud.addItem(0x2002, 0), 0);
    CHECK_EQUAL(sent.size(), (size_t)2);
    CHECK_EQUAL(sent[0].event, Hud::kEventAdded);
    // An item already in the slot asked for moves to a free one.
    CHECK_EQUAL(hud.addItem(0x2003, 1), 1);
    CHECK_EQUAL(hud.getSlot(2), 0x2001);
    hud.removeItem(0x2001);
    CHECK_EQUAL(hud.findItem(0x2001), -1);
    for (int i = 0; i < 10; i++)
        hud.addItem(0x2100 + i, -1);
    CHECK_EQUAL(hud.addItem(0x2200, -1), -1); // full
}

static void hud_panels() {
    std::vector<Sent> sent;
    Hud hud([&](uint16_t o, uint16_t e, const std::vector<int16_t> &a) { sent.push_back({o, e, a}); });
    hud.showPanel(0xA004, 12, 0, 10, 10);
    // Without buttons a click is event 45...
    hud.clickPanel(5, 5);
    CHECK_EQUAL(sent.back().event, Hud::kEventActivate);
    // ...with them, 47 and the button hit, or 0.
    hud.addButton(0xA004, 1, 35, Rect(126, 73, 169, 116), false);
    hud.clickPanel(130, 80);
    CHECK_EQUAL(sent.back().event, Hud::kEventButton);
    CHECK_EQUAL(sent.back().args[0], 1);
    hud.clickPanel(5, 5);
    CHECK_EQUAL(sent.back().args[0], 0);

    // A second panel shows instead; the cycle button goes back.
    hud.showPanel(0xA005, 24, 0, 10, 10);
    CHECK_EQUAL(hud.getVisiblePanel()->owner, 0xA005);
    hud.nextPanel();
    CHECK_EQUAL(hud.getVisiblePanel()->owner, 0xA004);

    // Button animation: frames 0-2 twice, then it stays on the last.
    hud.animateButton(0xA004, 1, 0, 2, 2, 100);
    sent.clear();
    hud.update(250, 10, 10);
    CHECK_EQUAL(hud.getVisiblePanel()->buttons[0].frame, 2);
    hud.update(400, 10, 10);
    CHECK_EQUAL(hud.getVisiblePanel()->buttons[0].frame, 2);
    CHECK(!hud.getVisiblePanel()->buttons[0].animation.running);
    CHECK(sent.empty());

    // Walking away: event 35 to both, until they are removed.
    hud.update(10, 13, 10);
    CHECK_EQUAL(sent.size(), (size_t)2);
    CHECK_EQUAL(sent[0].event, Hud::kEventWalkedAway);
    hud.removePanel(0xA004);
    CHECK_EQUAL(hud.getVisiblePanel()->owner, 0xA005);
    hud.removePanel(0xA005);
    CHECK(!hud.getVisiblePanel());
}

static void hud_effects() {
    std::vector<Sent> sent;
    Hud hud([&](uint16_t o, uint16_t e, const std::vector<int16_t> &a) { sent.push_back({o, e, a}); });
    // 50% red over 100 ms in, 100 held, 100 out, then event 55.
    hud.setEffect(0, 200, 0, 0, 50, 100, 100, 100, 0xE001);
    uint8_t r, g, b;
    CHECK_EQUAL(hud.getEffectWeight(0, r, g, b), 0);
    hud.update(50, 0, 0);
    CHECK_EQUAL(hud.getEffectWeight(0, r, g, b), 32);
    CHECK_EQUAL(r, 200);
    hud.update(100, 0, 0);
    CHECK_EQUAL(hud.getEffectWeight(0, r, g, b), 64);
    hud.update(100, 0, 0);
    CHECK_EQUAL(hud.getEffectWeight(0, r, g, b), 32);
    CHECK(sent.empty());
    hud.update(60, 0, 0);
    CHECK_EQUAL(hud.getEffectWeight(0, r, g, b), 0);
    CHECK_EQUAL(sent.size(), (size_t)1);
    CHECK_EQUAL(sent[0].event, Hud::kEventEffectDone);
    CHECK_EQUAL(sent[0].object, 0xE001);
    // A tint stays.
    hud.setEffect(3, 0, 0, 200, 30, 0, 0, 0, 0);
    hud.update(10000, 0, 0);
    CHECK_EQUAL(hud.getEffectWeight(3, r, g, b), 38);
}

static void hud_drag_items() {
    Hud hud([](uint16_t, uint16_t, const std::vector<int16_t> &) {});
    hud.addItem(0x2001, -1); // slot 1
    hud.addItem(0x2002, -1); // slot 2
    hud.addItem(0x2004, 0);
    // Onto an empty slot.
    hud.moveItem(1, 7);
    CHECK_EQUAL(hud.getSlot(7), 0x2001);
    CHECK_EQUAL(hud.getSlot(1), 0);
    // Onto another item, which moves to the first free slot.
    hud.moveItem(2, 7);
    CHECK_EQUAL(hud.getSlot(7), 0x2002);
    CHECK_EQUAL(hud.getSlot(1), 0x2001);
    // Between slots: the first free one.
    hud.moveItem(7, -1);
    CHECK_EQUAL(hud.getSlot(2), 0x2002);
    // Slot 0's item stays where it is.
    hud.moveItem(0, 5);
    CHECK_EQUAL(hud.getSlot(0), 0x2004);
}

static void held_item_follows_the_cursor() {
    const HeldItem::Style *phaser = HeldItem::getStyle(2);
    CHECK(phaser != nullptr);
    CHECK(HeldItem::getStyle(1) == nullptr);
    // The original's numbers for the phaser in a 640 x 252 view: the gun's
    // hotspot runs from y = 167 to 225, and below y = 213 it can't be used.
    HeldItem::Placement p = HeldItem::place(*phaser, 320, 0, 640, 252);
    CHECK(p.usable);
    CHECK_EQUAL(p.frame, 8); // centre, held high
    CHECK_EQUAL(p.x, 320 - 75);
    CHECK_EQUAL(p.y, 167 - 0);
    p = HeldItem::place(*phaser, 600, 212, 640, 252);
    CHECK(p.usable);
    CHECK_EQUAL(p.frame, 6); // right, low: held high it would leave the view's bottom
    p = HeldItem::place(*phaser, 20, 213, 640, 252);
    CHECK(!p.usable);
    CHECK_EQUAL(p.frame, 10);
    CHECK_EQUAL(p.x, 320 + 140 * (20 - 320) / 320 - 35);

    // The muzzle moves with it: to the left for a cursor on the left.
    double ahead, up, left;
    HeldItem::getMuzzle(*phaser, 0, 126, 640, 252, ahead, up, left);
    CHECK(std::fabs(ahead - 1.5) < 1e-9);
    CHECK(std::fabs(left - 1.0) < 1e-9);
    CHECK(up < 0);
}

static void level_unproject() {
    Camera camera;
    camera.x = 10;
    camera.y = 20;
    camera.z = 5;
    camera.yaw = 3.14159265358979 / 2; // along +y
    double x, y, z;
    // The middle of the view is straight ahead...
    LevelRenderer::unproject(camera, 640, 252, 319.5, 125.5, 8, x, y, z);
    CHECK(std::fabs(x - 10) < 1e-9 && std::fabs(y - 28) < 1e-9 && std::fabs(z - 5) < 1e-9);
    // ...its right edge 45 degrees to the right (+x when facing +y), and
    // looking up moves the horizon down.
    LevelRenderer::unproject(camera, 640, 252, 639.5, 125.5, 8, x, y, z);
    CHECK(std::fabs(x - 18) < 1e-9 && std::fabs(y - 28) < 1e-9);
    camera.pitch = 0.1;
    LevelRenderer::unproject(camera, 640, 252, 319.5, 125.5 + 32, 8, x, y, z);
    CHECK(std::fabs(z - 5) < 1e-9);
}

static void transition_dissolve() {
    RandomSource random(1);
    auto count = [](const Canvas &s, uint8_t value) {
        int n = 0;
        for (int y = 0; y < s.h; y++)
            for (int x = 0; x < s.w; x++)
                n += *(const uint8_t *)s.getBasePtr(x, y) == value;
        return n;
    };
    Canvas a, b, out;
    a.create(64, 16, Canvas::kClut8);
    b.create(64, 16, Canvas::kClut8);
    out.create(64, 16, Canvas::kClut8);
    a.fillRect(Rect(0, 0, 64, 16), 1);
    b.fillRect(Rect(0, 0, 64, 16), 2);

    // Nothing at 0; a quarter of each row asked for at 256, fewer landing
    // where picks repeat.
    Dissolve::speckle(a, 9, 0, 1, random);
    CHECK_EQUAL(count(a, 9), 0);
    Dissolve::speckle(a, 9, 256, 1, random);
    int speckled = count(a, 9);
    CHECK(speckled > 0 && speckled <= 16 * 16);
    // Past half, the colour with the picture showing through.
    a.fillRect(Rect(0, 0, 64, 16), 1);
    Dissolve::speckle(a, 9, 0x3FF, 1, random);
    CHECK_EQUAL(count(a, 9), 64 * 16);
    // A streak is a sixteenth of the width (less at the right edge).
    a.fillRect(Rect(0, 0, 64, 16), 1);
    Dissolve::speckle(a, 9, 4, 2, random);
    CHECK(count(a, 9) > 0 && count(a, 9) <= 4);

    // The crossfade goes from all of one to all of the other.
    a.fillRect(Rect(0, 0, 64, 16), 1);
    Dissolve::crossfade(out, a, b, 0, random);
    CHECK_EQUAL(count(out, 1), 64 * 16);
    Dissolve::crossfade(out, a, b, 600, random);
    CHECK(count(out, 2) > count(out, 1));
    Dissolve::crossfade(out, a, b, 0x3FF, random);
    CHECK_EQUAL(count(out, 2), 64 * 16);
}

static const reluva::test::Suite
    suite("stgenerations.unit.hud",
          {
              {"hud inventory", [](const std::filesystem::path &) { hud_inventory(); }},
              {"hud panels", [](const std::filesystem::path &) { hud_panels(); }},
              {"hud effects", [](const std::filesystem::path &) { hud_effects(); }},
              {"hud drag items", [](const std::filesystem::path &) { hud_drag_items(); }},
              {"held item follows the cursor", [](const std::filesystem::path &) { held_item_follows_the_cursor(); }},
              {"level unproject", [](const std::filesystem::path &) { level_unproject(); }},
              {"transition dissolve", [](const std::filesystem::path &) { transition_dissolve(); }},
          });
