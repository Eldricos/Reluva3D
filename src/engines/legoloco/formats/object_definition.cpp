// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/formats/object_definition.h"

#include <charconv>
#include <limits>

namespace reluva::legoloco {

namespace {

// The game's own table's size (0x41efa0).
constexpr int kMaxColumns = 13;
constexpr int kMaxRows = 9;
constexpr int kMaxLayers = 7;

struct Token {
    std::string text;
    int line;
};

// The words of the text with their lines, without `//` comments. The game
// reads word by word; lines matter only to keep a keyword with its values.
std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> tokens;
    int line = 1;
    size_t p = 0;
    while (p < text.size()) {
        const char c = text[p];
        if (c == '\n') {
            ++line;
            ++p;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            ++p;
        } else if (c == '/' && p + 1 < text.size() && text[p + 1] == '/') {
            while (p < text.size() && text[p] != '\n')
                ++p;
        } else {
            const size_t start = p;
            while (p < text.size() && text[p] != ' ' && text[p] != '\t' && text[p] != '\r' && text[p] != '\n')
                ++p;
            tokens.push_back({std::string(text.substr(start, p - start)), line});
        }
    }
    return tokens;
}

bool sameWord(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

class Reader {
public:
    explicit Reader(std::vector<Token> tokens) : _tokens(std::move(tokens)) {}

    bool done() const { return _pos >= _tokens.size(); }
    const Token &peek() const { return _tokens[_pos]; }
    const Token &next() { return _tokens[_pos++]; }
    int line() const { return done() ? (_tokens.empty() ? 1 : _tokens.back().line) : peek().line; }

    template<typename T>
    Error number(std::string_view what, T &out) {
        if (done())
            return Error::fail("the file ends before " + std::string(what));
        const Token &t = next();
        long long v = 0;
        const auto [end, ec] = std::from_chars(t.text.data(), t.text.data() + t.text.size(), v);
        if (ec != std::errc() || end != t.text.data() + t.text.size())
            return Error::fail("line " + std::to_string(t.line) + ": " + std::string(what) +
                               " isn't a number: " + t.text);
        if (v < std::numeric_limits<T>::min() || v > std::numeric_limits<T>::max())
            return Error::fail("line " + std::to_string(t.line) + ": " + std::string(what) +
                               " out of range: " + t.text);
        out = T(v);
        return Error::success();
    }

private:
    std::vector<Token> _tokens;
    size_t _pos = 0;
};

// physical_occupancy: columns, rows, layers; bitmap_occupancy: columns,
// rows. Then each layer's rows of cells.
Error readOccupancy(Reader &r, bool layered, Occupancy &out) {
    if (Error err = r.number("the columns", out.columns))
        return err;
    if (Error err = r.number("the rows", out.rows))
        return err;
    out.layers = 1;
    if (layered)
        if (Error err = r.number("the layers", out.layers))
            return err;
    if (out.columns < 0 || out.columns > kMaxColumns || out.rows < 0 || out.rows > kMaxRows || out.layers < 0 ||
        out.layers > kMaxLayers)
        return Error::fail("line " + std::to_string(r.line()) + ": a grid of " + std::to_string(out.columns) + " by " +
                           std::to_string(out.rows) + " by " + std::to_string(out.layers) + ", larger than the game's");
    out.cells.resize(size_t(out.columns * out.rows * out.layers));
    for (int &cell : out.cells)
        if (Error err = r.number("a grid's cell", cell))
            return err;
    return Error::success();
}

// One frame set's line: its name and ten numbers, in the game's order
// (0x42522f).
Error readFrameSet(Reader &r, FrameSet &out) {
    if (r.done())
        return Error::fail("the file ends before a frame set");
    out.name = r.next().text;
    Error err;
    if ((err = r.number("a frame set's start", out.start)) || (err = r.number("a frame set's end", out.end)) ||
        (err = r.number("a frame set's delay", out.delay)) || (err = r.number("a frame set's split", out.split)) ||
        (err = r.number("a frame set's restart delay", out.restartDelay)) ||
        (err = r.number("a frame set's next set", out.next)) || (err = r.number("a frame set's sound", out.sound)) ||
        (err = r.number("a frame set's replay delay", out.replayDelay)) ||
        (err = r.number("a frame set's tenth field", out.unnamed)) || (err = r.number("a frame set's flip", out.flipX)))
        return err;
    if (out.delay == 0)
        out.delay = 1;
    return Error::success();
}

} // namespace

const DefinitionLine *ObjectDefinition::find(std::string_view keyword) const {
    for (const auto &part : parts)
        for (const auto &line : part)
            if (sameWord(line.keyword, keyword))
                return &line;
    return nullptr;
}

Error parseObjectDefinition(std::string_view text, ObjectDefinition &out) {
    out = ObjectDefinition();
    Reader r(tokenize(text));
    size_t frameSetCount = 0;
    out.parts.emplace_back();
    while (!r.done()) {
        const Token keyword = r.next();
        if (keyword.text == "-9") {
            out.parts.emplace_back();
            continue;
        }
        DefinitionLine line{keyword.text, {}, keyword.line};
        if (sameWord(keyword.text, "physical_occupancy") || sameWord(keyword.text, "bitmap_occupancy")) {
            const bool layered = sameWord(keyword.text, "physical_occupancy");
            Occupancy grid;
            if (Error err = readOccupancy(r, layered, grid))
                return err;
            line.values = {std::to_string(grid.columns), std::to_string(grid.rows)};
            if (layered)
                line.values.push_back(std::to_string(grid.layers));
            (layered ? out.physical : out.bitmap) = std::move(grid);
        } else if (sameWord(keyword.text, "total_number_of_frames")) {
            if (Error err = r.number("the number of frames", out.totalFrames))
                return err;
            line.values = {std::to_string(out.totalFrames)};
            // As the game does (0x425143).
            if (out.totalFrames == 0)
                out.totalFrames = 1;
        } else if (sameWord(keyword.text, "number_of_frame_sets")) {
            uint16_t count = 0;
            if (Error err = r.number("the number of frame sets", count))
                return err;
            frameSetCount = count;
            line.values = {std::to_string(count)};
        } else if (sameWord(keyword.text, "cursor_frame_set") || sameWord(keyword.text, "cursor/default_frame_set")) {
            // The table of frame sets follows these two values directly.
            if (Error err = r.number("the cursor's frame set", out.cursorFrameSets[0]))
                return err;
            if (Error err = r.number("the cursor's second frame set", out.cursorFrameSets[1]))
                return err;
            line.values = {std::to_string(out.cursorFrameSets[0]), std::to_string(out.cursorFrameSets[1])};
            out.frameSets.resize(frameSetCount);
            for (size_t i = 0; i < out.frameSets.size(); ++i) {
                FrameSet &set = out.frameSets[i];
                if (Error err = readFrameSet(r, set))
                    return err;
                // A one-frame set that names itself next is made to stop, as the game does (0x425315).
                if (set.start == set.end && set.next == int16_t(i))
                    set.next = -1;
            }
        } else {
            while (!r.done() && r.peek().line == keyword.line)
                line.values.push_back(r.next().text);
        }
        out.parts.back().push_back(std::move(line));
    }
    // What follows the last -9 is the files' comments, which the game never reads.
    if (out.parts.size() > 1 && out.parts.back().empty())
        out.parts.pop_back();
    return Error::success();
}

} // namespace reluva::legoloco
