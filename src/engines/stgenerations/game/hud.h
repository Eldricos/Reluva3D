// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>

#include "graphics/draw.h"
#include "common/types.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

// What the scripts put on the away-team console around the 3D view, and
// how it reacts: the inventory, close-up panels and their buttons, the
// body picture, automap markers and screen effects. Drawing is the
// viewer's (see HudScreen); this keeps the state and sends the events.
//
// Images are script resources: indices into GAME.BIN's resource names.
class Hud {
public:
    // Sends an event to a script object.
    typedef std::function<void(uint16_t object, uint16_t event, const std::vector<int16_t> &args)> EventSink;

    enum Event {
        kEventAdded = 43,      // an item went into the inventory
        kEventActivate = 45,   // an item or panel was used (clicked)
        kEventButton = 47,     // a panel was clicked: the button number, or 0
        kEventEffectDone = 55, // a screen effect finished
        kEventPanelDone = 60,  // a panel's animation finished
        kEventDescribe = 62,   // right click
        kEventDropped = 63,    // an item was dragged out of the inventory
        kEventWalkedAway = 35  // the player left a panel's object behind
    };

    // --- Inventory (0x446DD0): 12 slots, 3 across and 4 down; slot 0 is
    // only filled on purpose.
    static const int kSlots = 12;

    explicit Hud(const EventSink &send);

    // Put an item in a slot, or the first free one of 1-11 for -1. Returns the slot, or -1 if full.
    int addItem(uint16_t id, int slot);
    void removeItem(uint16_t id);
    uint16_t getSlot(int slot) const { return _slots[slot]; }
    // The slot an item is in, or -1.
    int findItem(uint16_t id) const;
    // SET_INVENTORY_ENABLED (0xD6): off greys out every slot and ignores clicks.
    void setInventoryEnabled(bool on) { _inventoryEnabled = on; }
    bool isInventoryEnabled() const { return _inventoryEnabled; }
    // SET_ITEM_ENABLED (0xD7): an item greyed out where it sits.
    void setItemEnabled(uint16_t id, bool on) {
        if (on)
            _disabledItems.erase(id);
        else
            _disabledItems.insert(id);
    }
    // Whether the item in a slot can be used.
    bool isSlotEnabled(int slot) const { return _inventoryEnabled && !_disabledItems.count(_slots[slot]); }

    // Move an item from a slot of 1-11 to another (dragging); what was there moves
    // to the first free slot. Any other slot means the first free one.
    void moveItem(int from, int to);

    // --- Close-up panels (0x6C..0x6F, 0x9D..0xA1, 0x8F, 0x90): one per
    // object, shown one at a time in the console's 211 x 148 viewer.
    static const int kPanelWidth = 211;
    static const int kPanelHeight = 148;

    struct Animation {
        bool running = false;
        int first = 0, last = 0;
        // Frames still to show; 0 runs forever.
        int remaining = 0;
        uint32_t ms = 100, elapsed = 0;
    };

    struct Button {
        int number = 0;
        int16_t image = -1;
        int frame = 0;
        // Relative to the panel.
        Rect rect;
        // Colour-keyed (0xA0): see-through where it matches its bitmap's first pixel.
        bool keyed = false;
        // Invisible: only reports clicks (0x8F).
        bool hotspot = false;
        Animation animation;
    };

    struct Panel {
        uint16_t owner = 0;
        int16_t image = -1;
        int frame = 0;
        // Where the player was when it opened.
        double x = 0, y = 0;
        std::vector<Button> buttons;
        Animation animation;
        bool notifyWhenDone = false;
    };

    // PANEL_SHOW_IMAGE: open (or reuse) the object's panel and show it.
    void showPanel(uint16_t owner, int16_t image, int frame, double playerX, double playerY);
    // NATIVE_6D: animate the panel's image; event 60 at the end unless ms is 0.
    void animatePanel(uint16_t owner, int first, int last, int loops, uint32_t ms);
    void removePanel(uint16_t owner);
    // The visible panel, or null.
    const Panel *getVisiblePanel() const;
    // The first panel's owner (NATIVE_6F), or 0.
    uint16_t getFirstPanelOwner() const { return _panels.empty() ? 0 : _panels.front().owner; }
    int getPanelCount() const { return (int)_panels.size(); }
    // The cycle button: show the next panel.
    void nextPanel();

    void addButton(uint16_t owner, int number, int16_t image, const Rect &rect, bool hotspot);
    void setButton(uint16_t owner, int number, int16_t image, int frame);
    void animateButton(uint16_t owner, int number, int first, int last, int loops, uint32_t ms);
    void setButtonKeyed(uint16_t owner, int number, bool keyed);
    void removeButton(uint16_t owner, int number);

    // A click at (x, y) in the visible panel: event 45, or 47 with the button hit.
    void clickPanel(int x, int y);
    // A right click on the visible panel: event 62.
    void describePanel();
    // Space: event 45 to the visible panel's object.
    void usePanel();

    // --- Body picture (SET_BODY_IMAGE): -1 is the mission's BODY.BMP.
    void setBodyImage(int16_t image, int frame) {
        _bodyImage = image;
        _bodyFrame = frame;
    }
    int16_t getBodyImage() const { return _bodyImage; }
    int getBodyFrame() const { return _bodyFrame; }

    // --- Automap.
    enum MarkerKind { kMarkerLife, kMarkerEnergy, kMarkerTrilithium };
    struct Marker {
        MarkerKind kind;
        int style;
    };
    void setMarker(uint16_t id, MarkerKind kind, int style) { _markers[id] = {kind, style}; }
    void removeMarker(uint16_t id, MarkerKind kind);
    const std::map<uint16_t, Marker> &getMarkers() const { return _markers; }
    // Height bands relative to the player (MAP_SET_LEVELS): low/high pairs.
    void setMapLevels(const double levels[6]);
    const double *getMapLevels() const { return _levels; }
    // MAP_ZOOM_LEVELS (0xC8): the map's three scales, pixels per unit.
    void setMapZoom(int level, double scale) { _zoom[level] = scale; }
    double getMapZoom(int level) const { return _zoom[level]; }

    // --- Screen effects (0x43199A): four colour slots blended over the
    // palette, slot 0 on top. Times of 0 make a slot stay; 0 percent clears it.
    static const int kEffectSlots = 4;
    struct Effect {
        bool active = false;
        uint8_t r = 0, g = 0, b = 0;
        int percent = 0;
        uint32_t fadeIn = 0, hold = 0, fadeOut = 0, elapsed = 0;
        uint16_t notify = 0;
    };
    void setEffect(int slot, uint8_t r, uint8_t g, uint8_t b, int percent, uint32_t fadeIn, uint32_t hold,
                   uint32_t fadeOut, uint16_t notify);
    // A slot's colour and weight now, 0-128 (0 = no effect).
    int getEffectWeight(int slot, uint8_t &r, uint8_t &g, uint8_t &b) const;

    // VIEW_SHAKE: 0 off, 1 and 2 jitter by 4 and 8 pixels, 4 a single jolt.
    void setShake(int mode);
    int getShakeMode() const { return _shake; }
    // The view's offset this frame, in pixels.
    void getShakeOffset(int &dx, int &dy);
    // VIEW_OVERLAY: the underwater ripple.
    void setRipple(bool on) { _ripple = on; }
    bool hasRipple() const { return _ripple; }

    // Save or load everything above.
    void sync(Serializer &s);

    // Advance by ms: animations, effects (event 55), and panels whose
    // object the player has walked away from (event 35, 2 units).
    void update(uint32_t ms, double playerX, double playerY);

private:
    Panel *findPanel(uint16_t owner);
    Button *findButton(uint16_t owner, int number);
    // Step an animation; returns true when it has just ended.
    static bool runAnimation(Animation &animation, int &frame, uint32_t ms);

    EventSink _send;
    uint16_t _slots[kSlots] = {};
    std::vector<Panel> _panels;
    int _visible = -1;
    int16_t _bodyImage = -1;
    int _bodyFrame = 0;
    std::map<uint16_t, Marker> _markers;
    double _levels[6] = {-1e9, -6, -6, 6, 6, 1e9};
    double _zoom[3] = {0.5, 1.0, 2.0};
    bool _inventoryEnabled = true;
    std::set<uint16_t> _disabledItems;
    Effect _effects[kEffectSlots];
    int _shake = 0;
    double _jolt = 0;
    uint32_t _seed = 1;
    bool _ripple = false;
};

} // namespace reluva::stgenerations
