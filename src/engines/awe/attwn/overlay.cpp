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
