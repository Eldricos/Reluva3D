// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <optional>
#include <string_view>

namespace reluva::idtech3 {

/**
 * Splits the text files id Tech 3 games use (shader scripts, the BSP entity
 * lump, .arena/.skin/.cfg files) into tokens, as described in
 * docs/engines/idtech3/formats/text.md: a token is a "quoted string" (without its
 * quotes) or a run of characters up to the next whitespace; line (//) and
 * block comments are skipped.
 */
class Lexer {
public:
    /** Over `text`, which must outlive it: the tokens point into it. */
    explicit Lexer(std::string_view text) : _text(text) {}

    /**
     * The next token, or nullopt at the end of the text. With
     * crossLines = false, also nullopt (without consuming anything) when
     * the next token is on a later line, for directives that take the
     * rest of their line.
     */
    std::optional<std::string_view> next(bool crossLines = true);
    /** The next token without consuming it. */
    std::optional<std::string_view> peek(bool crossLines = true);
    /** Skips whatever is left of the current line. */
    void skipLine();
    /**
     * Skips tokens up to and including the "}" matching an already consumed
     * "{".
     * @return False if the text ends first.
     */
    bool skipBlock();

    /** The 1-based line of the last token read. */
    int line() const { return _line; }
    /** Whether only white space and comments are left. */
    bool atEnd();

private:
    // Skips whitespace and comments; stops before a newline when
    // crossLines is false. Returns false if it stopped at a newline.
    bool skipSpace(bool crossLines);

    std::string_view _text;
    size_t _pos = 0;
    int _line = 1;
};

} // namespace reluva::idtech3
