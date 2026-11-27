// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/static_text.h"

#include "engines/stawayteam/ui/layout.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::stawayteam {

void StaticText::draw(Surface &screen, int originX, int originY) const {
    if (font.loaded())
        font.drawWrapped(screen, originX + x, originY + y, wrap, text, align, 2);
}

std::vector<StaticText> readStaticTexts(const Archive &data, const TextObject *definition) {
    std::vector<StaticText> out;
    const TextObject *nernies = definition ? definition->object("Nernie") : nullptr;
    for (const TextField *item : nernies ? nernies->items() : std::vector<const TextField *>{}) {
        const TextObject *nernie = item->object.get();
        if (!nernie || nernie->className != "CStaticTextNernie")
            continue;
        StaticText t;
        readPoint(nernie, "Position", t.x, t.y);
        t.text = readText(nernie, "Text");
        (void)t.font.load(data, readText(nernie, "Font"));
        const std::string justify = readText(nernie, "Justification");
        t.align = justify == "Right"    ? Font::Align::kRight
                  : justify == "Center" ? Font::Align::kCenter
                                        : Font::Align::kLeft;
        t.wrap = std::atoi(readText(nernie, "Wrap Width").c_str());
        out.push_back(std::move(t));
    }
    return out;
}

RandomNumbers::RandomNumbers(const Archive &data, const TextObject &nernie, uint32_t seed) : _random(seed) {
    readPoint(&nernie, "Position", _x, _y);
    (void)_font.load(data, readText(&nernie, "Font"));
    const auto whole = [&](const char *key) { return int(nernie.integer(key).value_or(0)); };
    const auto number = [&](const char *key) { return float(nernie.number(key).value_or(0.0)); };
    _digits = whole("Width of Numbers");
    _columns = whole("Column Width");
    _rows = whole("Column Height");
    _columnSpace = whole("Space Between Columns");
    _rowSpace = whole("Space Between Rows");
    _idle = number("Idle Time");
    _between = number("Time Between Changes");
    _changes = whole("Number of Changes");
    roll();
}

void RandomNumbers::roll() {
    // Each digit 0 to 9 by the random numbers (0x54caeb).
    _numbers.assign(size_t(std::max(0, _rows * _columns)), std::string());
    for (std::string &n : _numbers)
        for (int i = 0; i < _digits; ++i) {
            _random = _random * 1103515245u + 12345u;
            n.push_back(char('0' + (_random >> 16) % 10));
        }
}

void RandomNumbers::update(float seconds) {
    // Idle for its Idle Time; then changed at once and every Time Between
    // Changes after, Number of Changes in all, and idle again (0x54ca10).
    _time += seconds;
    if (_idling) {
        if (_time < _idle)
            return;
        _idling = false;
        _time = 0.0f;
        _changed = 1;
        roll();
        return;
    }
    if (_time < _between)
        return;
    _time = 0.0f;
    ++_changed;
    roll();
    if (_changed >= _changes)
        _idling = true;
}

void RandomNumbers::draw(Surface &screen, int originX, int originY) const {
    // A row's numbers each the width of so many 0s and the space apart,
    // the rows the font's height and the space apart (0x54cc50).
    if (!_font.loaded())
        return;
    int y = originY + _y;
    for (int r = 0; r < _rows; ++r) {
        int x = originX + _x;
        for (int c = 0; c < _columns; ++c) {
            _font.draw(screen, x, y, _numbers[size_t(r * _columns + c)]);
            x += _font.width("0") * _digits + _columnSpace;
        }
        y += _font.height() + _rowSpace;
    }
}

std::vector<RandomNumbers> readRandomNumbers(const Archive &data, const TextObject *definition, uint32_t seed) {
    std::vector<RandomNumbers> out;
    const TextObject *nernies = definition ? definition->object("Nernie") : nullptr;
    for (const TextField *item : nernies ? nernies->items() : std::vector<const TextField *>{}) {
        const TextObject *nernie = item->object.get();
        if (nernie && nernie->className == "CRandomNumberNernie")
            out.emplace_back(data, *nernie, seed + uint32_t(out.size()) * 7919u);
    }
    return out;
}

} // namespace reluva::stawayteam
