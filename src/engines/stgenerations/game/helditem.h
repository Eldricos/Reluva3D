// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stgenerations/canvas.h"
#include "engines/stgenerations/formats/overlay.h"
#include "engines/stgenerations/formats/resources.h"

namespace reluva::stgenerations {

// The item in use, held up in the 3D view (the original's view+0x80):
// a gun such as NUPHASER.OVR that follows the cursor around the bottom of
// the view, and fires from its muzzle at what the cursor points at.
//
// Each overlay has 12 frames of 88 rows: 6..11 are poses (right-low,
// right-high, centre-high, left-high, left-low, centre-low) and 0..5 the
// same poses firing. The tables are sgens.exe's (0x5428D8 on).
class HeldItem {
public:
    static const int kFrameHeight = 88;
    static const int kPoses = 6;

    struct Style {
        const char *overlay;
        // Each pose's hotspot in its frame, poses 6..11.
        int hotspot[kPoses][2];
        // Where shots leave from, in world units: ahead, up and to the side.
        double muzzle[3];
    };
    // The style for an item kind (ATTACH_ITEM_BEHAVIOUR), or null if it isn't held up.
    static const Style *getStyle(int kind);

    // Where the gun goes for a cursor at (x, y) in a view w x h (view.vtFC 0x4310B8).
    struct Placement {
        int frame;
        // Top left of the frame.
        int x, y;
        // Below the gun's line the item can't be used and the cursor is an arrow.
        bool usable;
    };
    static Placement place(const Style &style, int cursorX, int cursorY, int w, int h);

    // Where a shot leaves from, relative to the eye (0x4320D1): ahead, up,
    // and to the left, for a cursor at (x, y) in a view w x h.
    static void getMuzzle(const Style &style, int cursorX, int cursorY, int w, int h, double &ahead, double &up,
                          double &left);

    // Load the overlay for an item kind, from the mission's folder or COMMON.DAT.
    void load(const ResourceManager &resources, const ResourceRef &level, int kind);
    int getKind() const { return _kind; }

    // Draw it for a cursor at (x, y); firing shows the flash frames.
    void draw(Canvas &view, int cursorX, int cursorY, bool firing) const;

private:
    int _kind = 0;
    const Style *_style = nullptr;
    Overlay _overlay;
    bool _loaded = false;
};

} // namespace reluva::stgenerations
