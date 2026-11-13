// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/hud.h"

#include <algorithm>
#include <cmath>

#include "common/serializer.h"

namespace reluva::stgenerations {

Hud::Hud(const EventSink &send) : _send(send) {
}

int Hud::addItem(uint16_t id, int slot) {
    // 0x446DD0: -1 is the first free slot of 1-11; an item already in the
    // slot asked for moves to a free one. The item hears of it (43).
    int current = findItem(id);
    if (current >= 0)
        _slots[current] = 0;
    auto firstFree = [this]() {
        for (int i = 1; i < kSlots; i++) {
            if (!_slots[i])
                return i;
        }
        return -1;
    };
    if (slot < 0 || slot >= kSlots) {
        slot = firstFree();
        if (slot < 0) {
            if (current >= 0)
                _slots[current] = id;
            return -1;
        }
    } else if (_slots[slot]) {
        int other = firstFree();
        if (other >= 0)
            _slots[other] = _slots[slot];
    }
    _slots[slot] = id;
    _send(id, kEventAdded, {});
    return slot;
}

void Hud::removeItem(uint16_t id) {
    int slot = findItem(id);
    if (slot >= 0)
        _slots[slot] = 0;
}

int Hud::findItem(uint16_t id) const {
    for (int i = 0; i < kSlots; i++) {
        if (id && _slots[i] == id)
            return i;
    }
    return -1;
}

void Hud::moveItem(int from, int to) {
    // 0x44763A: dropped on a slot of 1-11 it goes there, and what was there
    // to the first free one; dropped between slots, to the first free one.
    if (from < 1 || from >= kSlots || !_slots[from] || from == to)
        return;
    uint16_t id = _slots[from];
    _slots[from] = 0;
    auto firstFree = [this](int except) {
        for (int i = 1; i < kSlots; i++) {
            if (!_slots[i] && i != except)
                return i;
        }
        return -1;
    };
    if (to < 1 || to >= kSlots) {
        to = firstFree(-1);
        if (to < 0)
            to = from;
    } else if (_slots[to]) {
        // There is a free slot other than to: from, at least.
        _slots[firstFree(to)] = _slots[to];
    }
    _slots[to] = id;
}

Hud::Panel *Hud::findPanel(uint16_t owner) {
    for (Panel &panel : _panels) {
        if (panel.owner == owner)
            return &panel;
    }
    return nullptr;
}

Hud::Button *Hud::findButton(uint16_t owner, int number) {
    Panel *panel = findPanel(owner);
    if (!panel)
        return nullptr;
    for (Button &button : panel->buttons) {
        if (button.number == number)
            return &button;
    }
    return nullptr;
}

void Hud::showPanel(uint16_t owner, int16_t image, int frame, double playerX, double playerY) {
    Panel *panel = findPanel(owner);
    if (!panel) {
        _panels.push_back(Panel());
        panel = &_panels.back();
        panel->owner = owner;
        panel->x = playerX;
        panel->y = playerY;
    }
    panel->image = image;
    panel->frame = frame;
    panel->animation.running = false;
    _visible = (int)(panel - _panels.data());
}

void Hud::animatePanel(uint16_t owner, int first, int last, int loops, uint32_t ms) {
    Panel *panel = findPanel(owner);
    if (!panel)
        return;
    Animation &a = panel->animation;
    a.first = first;
    a.last = std::max(first, last);
    a.remaining = loops ? (a.last - a.first + 1) * loops : 0;
    a.ms = ms ? ms : 1;
    a.elapsed = 0;
    a.running = a.last != a.first;
    panel->frame = first;
    panel->notifyWhenDone = ms != 0;
}

void Hud::removePanel(uint16_t owner) {
    Panel *panel = findPanel(owner);
    if (!panel)
        return;
    int index = (int)(panel - _panels.data());
    _panels.erase(_panels.begin() + index);
    // The next one left shows instead.
    if (_panels.empty())
        _visible = -1;
    else
        _visible = std::min(index, (int)_panels.size() - 1);
}

const Hud::Panel *Hud::getVisiblePanel() const {
    return _visible >= 0 && _visible < (int)_panels.size() ? &_panels[_visible] : nullptr;
}

void Hud::nextPanel() {
    if (!_panels.empty())
        _visible = (_visible + 1) % (int)_panels.size();
}

void Hud::addButton(uint16_t owner, int number, int16_t image, const Rect &rect, bool hotspot) {
    Panel *panel = findPanel(owner);
    if (!panel)
        return;
    removeButton(owner, number);
    Button button;
    button.number = number;
    button.image = image;
    button.rect = rect;
    button.hotspot = hotspot;
    panel->buttons.push_back(button);
}

void Hud::setButton(uint16_t owner, int number, int16_t image, int frame) {
    Button *button = findButton(owner, number);
    if (!button)
        return;
    button->animation.running = false;
    button->image = image;
    button->frame = frame;
}

void Hud::animateButton(uint16_t owner, int number, int first, int last, int loops, uint32_t ms) {
    Button *button = findButton(owner, number);
    if (!button)
        return;
    button->frame = first;
    Animation &a = button->animation;
    // A last frame of 0 or the first just shows the first (0x9F).
    if (last == 0 || last == first) {
        a.running = false;
        return;
    }
    a.first = first;
    a.last = std::max(first, last);
    a.remaining = loops ? (a.last - a.first + 1) * loops : 0;
    a.ms = ms ? ms : 1;
    a.elapsed = 0;
    a.running = true;
}

void Hud::setButtonKeyed(uint16_t owner, int number, bool keyed) {
    Button *button = findButton(owner, number);
    if (button)
        button->keyed = keyed;
}

void Hud::removeButton(uint16_t owner, int number) {
    Panel *panel = findPanel(owner);
    if (!panel)
        return;
    panel->buttons.erase(std::remove_if(panel->buttons.begin(), panel->buttons.end(),
                                        [number](const Button &b) { return b.number == number; }),
                         panel->buttons.end());
}

void Hud::clickPanel(int x, int y) {
    const Panel *panel = getVisiblePanel();
    if (!panel)
        return;
    if (panel->buttons.empty()) {
        _send(panel->owner, kEventActivate, {});
        return;
    }
    int16_t hit = 0;
    for (const Button &button : panel->buttons) {
        if (button.rect.contains(x, y))
            hit = (int16_t)button.number;
    }
    _send(panel->owner, kEventButton, {hit});
}

void Hud::describePanel() {
    if (const Panel *panel = getVisiblePanel())
        _send(panel->owner, kEventDescribe, {});
}

void Hud::usePanel() {
    if (const Panel *panel = getVisiblePanel())
        _send(panel->owner, kEventActivate, {});
}

void Hud::removeMarker(uint16_t id, MarkerKind kind) {
    auto it = _markers.find(id);
    if (it != _markers.end() && it->second.kind == kind)
        _markers.erase(it);
}

void Hud::setMapLevels(const double levels[6]) {
    for (int i = 0; i < 6; i++)
        _levels[i] = levels[i];
}

void Hud::setEffect(int slot, uint8_t r, uint8_t g, uint8_t b, int percent, uint32_t fadeIn, uint32_t hold,
                    uint32_t fadeOut, uint16_t notify) {
    if (slot < 0 || slot >= kEffectSlots)
        return;
    Effect &e = _effects[slot];
    e.active = percent != 0;
    e.r = r;
    e.g = g;
    e.b = b;
    e.percent = percent;
    e.fadeIn = fadeIn;
    e.hold = hold;
    e.fadeOut = fadeOut;
    e.elapsed = 0;
    e.notify = notify;
}

int Hud::getEffectWeight(int slot, uint8_t &r, uint8_t &g, uint8_t &b) const {
    const Effect &e = _effects[slot];
    if (!e.active)
        return 0;
    r = e.r;
    g = e.g;
    b = e.b;
    double k = e.percent * 128.0 / 100;
    if (e.fadeIn + e.hold + e.fadeOut == 0)
        return (int)k;
    if (e.elapsed < e.fadeIn)
        return (int)(k * e.elapsed / e.fadeIn);
    if (e.elapsed < e.fadeIn + e.hold)
        return (int)k;
    uint32_t out = e.elapsed - e.fadeIn - e.hold;
    return out < e.fadeOut ? (int)(k * (e.fadeOut - out) / e.fadeOut) : 0;
}

void Hud::setShake(int mode) {
    if (mode == 3)
        return;
    _shake = mode;
    _jolt = mode == 4 ? 8 : 0;
}

void Hud::getShakeOffset(int &dx, int &dy) {
    dx = dy = 0;
    auto random = [this](int range) {
        _seed = _seed * 0x41C64E6D + 0x3039;
        return (int)((_seed >> 16) % (2 * range + 1)) - range;
    };
    switch (_shake) {
    case 1:
        dx = random(4);
        dy = random(4);
        break;
    case 2:
        dx = random(8);
        dy = random(8);
        break;
    case 4:
        // One jolt that dies away, a frame at a time as in the original.
        dy = (int)std::lround(_jolt);
        _jolt *= -0.7;
        if (std::fabs(_jolt) < 0.5)
            _shake = 0;
        break;
    default:
        break;
    }
}

bool Hud::runAnimation(Animation &a, int &frame, uint32_t ms) {
    if (!a.running)
        return false;
    a.elapsed += ms;
    while (a.elapsed >= a.ms) {
        a.elapsed -= a.ms;
        if (a.remaining > 0 && --a.remaining == 0) {
            a.running = false;
            frame = a.last;
            return true;
        }
        frame = frame >= a.last ? a.first : frame + 1;
    }
    return false;
}

void Hud::update(uint32_t ms, double playerX, double playerY) {
    std::vector<std::pair<uint16_t, uint16_t>> events;
    for (Panel &panel : _panels) {
        if (runAnimation(panel.animation, panel.frame, ms) && panel.notifyWhenDone)
            events.push_back({panel.owner, kEventPanelDone});
        for (Button &button : panel.buttons)
            runAnimation(button.animation, button.frame, ms);
        // Every frame until the script closes it.
        double dx = playerX - panel.x;
        double dy = playerY - panel.y;
        if (dx * dx + dy * dy > 2.0 * 2.0)
            events.push_back({panel.owner, kEventWalkedAway});
    }
    for (Effect &e : _effects) {
        if (!e.active || e.fadeIn + e.hold + e.fadeOut == 0)
            continue;
        e.elapsed += ms;
        if (e.elapsed >= e.fadeIn + e.hold + e.fadeOut) {
            e.active = false;
            if (e.notify)
                events.push_back({e.notify, kEventEffectDone});
        }
    }
    for (const auto &event : events)
        _send(event.first, event.second, {});
}

namespace {

void syncAnimation(Serializer &s, Hud::Animation &a) {
    s.syncAsBool(a.running);
    s.syncAsInt(a.first);
    s.syncAsInt(a.last);
    s.syncAsInt(a.remaining);
    s.syncAsUint32(a.ms);
    s.syncAsUint32(a.elapsed);
}

void syncRect(Serializer &s, Rect &r) {
    s.syncAsInt(r.left, 2);
    s.syncAsInt(r.top, 2);
    s.syncAsInt(r.right, 2);
    s.syncAsInt(r.bottom, 2);
}

} // namespace

void Hud::sync(Serializer &s) {
    for (uint16_t &slot : _slots)
        s.syncAsUint16(slot);
    s.syncVector(_panels, [](Serializer &ser, Panel &panel) {
        ser.syncAsUint16(panel.owner);
        ser.syncAsSint16(panel.image);
        ser.syncAsInt(panel.frame);
        ser.syncAsDouble(panel.x);
        ser.syncAsDouble(panel.y);
        ser.syncVector(panel.buttons, [](Serializer &ser2, Button &button) {
            ser2.syncAsInt(button.number);
            ser2.syncAsSint16(button.image);
            ser2.syncAsInt(button.frame);
            syncRect(ser2, button.rect);
            ser2.syncAsBool(button.keyed);
            ser2.syncAsBool(button.hotspot);
            syncAnimation(ser2, button.animation);
        });
        syncAnimation(ser, panel.animation);
        ser.syncAsBool(panel.notifyWhenDone);
    });
    s.syncAsInt(_visible);
    s.syncAsSint16(_bodyImage);
    s.syncAsInt(_bodyFrame);
    uint32_t markers = (uint32_t)_markers.size();
    s.syncAsUint32(markers);
    if (s.isLoading())
        _markers.clear();
    auto it = _markers.begin();
    for (uint32_t i = 0; i < markers && !s.err(); i++) {
        uint16_t id = s.isSaving() ? it->first : 0;
        int kind = s.isSaving() ? (int)it->second.kind : 0;
        int style = s.isSaving() ? it->second.style : 0;
        s.syncAsUint16(id);
        s.syncAsInt(kind);
        s.syncAsInt(style);
        if (s.isLoading())
            _markers[id] = {(MarkerKind)kind, style};
        else
            ++it;
    }
    for (double &level : _levels)
        s.syncAsDouble(level);
    for (double &zoom : _zoom)
        s.syncAsDouble(zoom);
    s.syncAsBool(_inventoryEnabled);
    std::vector<uint16_t> disabled(_disabledItems.begin(), _disabledItems.end());
    s.syncVector(disabled, [](Serializer &ser, uint16_t &v) { ser.syncAsUint16(v); });
    if (s.isLoading())
        _disabledItems = std::set<uint16_t>(disabled.begin(), disabled.end());
    for (Effect &e : _effects) {
        s.syncAsBool(e.active);
        s.syncAsByte(e.r);
        s.syncAsByte(e.g);
        s.syncAsByte(e.b);
        s.syncAsInt(e.percent);
        s.syncAsUint32(e.fadeIn);
        s.syncAsUint32(e.hold);
        s.syncAsUint32(e.fadeOut);
        s.syncAsUint32(e.elapsed);
        s.syncAsUint16(e.notify);
    }
    s.syncAsInt(_shake);
    s.syncAsDouble(_jolt);
    s.syncAsUint32(_seed);
    s.syncAsBool(_ripple);
}

} // namespace reluva::stgenerations
