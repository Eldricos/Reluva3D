// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/formats/lexer.h"

namespace reluva::idtech3 {

bool Lexer::skipSpace(bool crossLines) {
    while (_pos < _text.size()) {
        const char c = _text[_pos];
        if (c == '\n') {
            if (!crossLines)
                return false;
            ++_line;
            ++_pos;
        } else if (static_cast<unsigned char>(c) <= ' ') {
            ++_pos;
        } else if (c == '/' && _pos + 1 < _text.size() && _text[_pos + 1] == '/') {
            while (_pos < _text.size() && _text[_pos] != '\n')
                ++_pos;
        } else if (c == '/' && _pos + 1 < _text.size() && _text[_pos + 1] == '*') {
            // A block comment counts as crossing the lines it spans.
            const size_t close = _text.find("*/", _pos + 2);
            const size_t stop = close == std::string_view::npos ? _text.size() : close + 2;
            for (size_t i = _pos; i < stop; ++i)
                if (_text[i] == '\n') {
                    if (!crossLines) {
                        _pos = i;
                        return false;
                    }
                    ++_line;
                }
            _pos = stop;
        } else {
            break;
        }
    }
    return true;
}

std::optional<std::string_view> Lexer::next(bool crossLines) {
    if (!skipSpace(crossLines) || _pos >= _text.size())
        return std::nullopt;
    if (_text[_pos] == '"') {
        const size_t start = ++_pos;
        while (_pos < _text.size() && _text[_pos] != '"') {
            if (_text[_pos] == '\n')
                ++_line;
            ++_pos;
        }
        std::string_view tok = _text.substr(start, _pos - start);
        if (_pos < _text.size())
            ++_pos; // closing quote
        return tok;
    }
    const size_t start = _pos;
    while (_pos < _text.size() && static_cast<unsigned char>(_text[_pos]) > ' ')
        ++_pos;
    return _text.substr(start, _pos - start);
}

std::optional<std::string_view> Lexer::peek(bool crossLines) {
    const size_t pos = _pos;
    const int line = _line;
    auto tok = next(crossLines);
    _pos = pos;
    _line = line;
    return tok;
}

void Lexer::skipLine() {
    while (_pos < _text.size() && _text[_pos] != '\n')
        ++_pos;
}

bool Lexer::skipBlock() {
    int depth = 1;
    while (auto tok = next()) {
        if (*tok == "{")
            ++depth;
        else if (*tok == "}" && --depth == 0)
            return true;
    }
    return false;
}

bool Lexer::atEnd() {
    skipSpace(true);
    return _pos >= _text.size();
}

} // namespace reluva::idtech3
