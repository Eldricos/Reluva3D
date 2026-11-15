// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/overlay.h"

#include "engines/awe/attwn/inventory.h"
#include "engines/awe/attwn/journal.h"
#include "engines/awe/game/conditions.h"

#include <cstdlib>

namespace reluva::awe::attwn {

namespace {

class Windows : public GameOverlay {
public:
    explicit Windows(OverlayHost &host) : _journal(host), _inventory(host) {}

    void update(float seconds) override {
        _journal.update(seconds);
        _inventory.update(seconds);
    }
    void draw(Surface &dst, const GameFonts &fonts) override {
        _inventory.draw(dst, fonts);
        _journal.draw(dst, fonts);
    }
    bool click(int x, int y) override { return _inventory.click(x, y) || _journal.click(x, y); }
    bool rightClick() override {
        // A right click opens or closes the inventory (0x416de0).
        _inventory.toggle();
        return true;
    }
    bool key(uint32_t code) override {
        // I: the inventory (0x4170f5).
        if (code != 'i')
            return false;
        _inventory.toggle();
        return true;
    }
    void mouseMove(int x, int y) override { _inventory.hover(x, y); }
    bool combine(const std::vector<std::string> &parts) override {
        if (!_inventory.toTray(parts))
            return false;
        _inventory.combineButton();
        return true;
    }
    std::vector<std::vector<std::string>> examinable() const override { return _inventory.examinable(); }
    bool examine(const std::vector<std::string> &items) override {
        if (!_inventory.toTray(items))
            return false;
        _inventory.examineButton();
        return true;
    }
    OverlayHand hand(OverlayWant want, const std::vector<std::string> &items) override {
        return _inventory.hand(want, items);
    }
    bool covers(int x, int y) const override { return _inventory.covers(x, y) || _journal.covers(x, y); }
    void dismiss(bool click) override {
        // Escape says nothing of a movie's close-up (0x416fa3).
        if (_inventory.showing())
            _inventory.closeExamine(0, click);
    }
    bool verb(const std::vector<std::string> &words) override {
        if (!words.empty() && words[0] == "examine") {
            _inventory.examine();
            return true;
        }
        if (_journal.verb(words)) {
            // The inventory's close-up goes too, a document read into
            // the Docs tab used up (0x41c305).
            _inventory.closeExamine(words.size() > 1 ? std::atoi(words[1].c_str()) : 0, true);
            return true;
        }
        return false;
    }

private:
    Journal _journal;
    Inventory _inventory;
};

} // namespace

std::unique_ptr<GameOverlay> makeOverlay(OverlayHost &host) {
    return std::make_unique<Windows>(host);
}

} // namespace reluva::awe::attwn
