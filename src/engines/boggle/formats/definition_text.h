// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <string_view>
namespace reluva::boggle {
enum DefinitionStyle : unsigned { DefinitionBold=1, DefinitionItalic=2, DefinitionSuper=4, DefinitionSub=8 };
struct DefinitionSpan {
    std::string text; // UTF-8, never interpreted as HTML
    unsigned style=0;
    std::string reference;
    bool root=false;
};
struct DefinitionText {
    std::vector<DefinitionSpan> spans;
    std::vector<std::string> unsupported;
    std::string plain() const;
};
// The archive uses Latin-1 text and named glyph recipes. No original rendering
// code, font bytecode, HTML renderer or platform text API is executed.
Error definitionText(std::string_view markup,DefinitionText& out);
std::string definitionSymbol(std::string_view name);
uint32_t definitionCodepoint(std::string_view text,size_t& at);
std::string definitionUtf8(uint32_t cp);
}
