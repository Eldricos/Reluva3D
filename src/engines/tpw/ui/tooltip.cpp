// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/tooltip.h"

#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/text.h"
#include "engines/tpw/ui/ui_model.h"

#include <algorithm>
#include <vector>

namespace reluva::tpw {

namespace {

// It fades in through the model's four meshes (guessed); it lies a little
// left of the pointer and above it, as the recording's two do, give or
// take, 12 wider than its text, 11 high and 10 a line, its text dark.
constexpr uint32_t kFadeStep = 50;
constexpr size_t kFadeMeshes = 4;
constexpr int kLeft = -6;
constexpr int kAbove = -27;
constexpr int kPadding = 6;
constexpr int kLine = 10;
constexpr int kEdge = 11;
constexpr Color kText{48, 48, 40};

} // namespace

Error Tooltip::load(const SearchSet &files) {
    if (Error err = _box.load(files, "ui/!tooltip.MD2"))
        return err;
    const std::string language = languageOf(files);
    if (language.empty())
        return Error::fail("no language's folder (Language/<language>/MBToUni.dat)");
    const std::string folder = "Language/" + language + "/";
    std::vector<uint8_t> data;
    if (Error err = files.read(folder + "MBToUni.dat", data))
        return err;
    if (Error err = _characters.parse(data))
        return err;
    if (Error err = readGameFont(files, folder, "GAME8AA", _font))
        return err;
    return readStringTable(files, folder, "UIHELPTEXT", _characters, _help);
}

std::string Tooltip::help(uint32_t index, const std::map<int, std::string> &values) const {
    return values.empty() ? _help.text(index) : fillGameString(_help, index, values);
}

void Tooltip::draw(Surface &screen, int mouseX, int mouseY, const std::string &text, uint32_t rested) const {
    if (text.empty() || rested < kDelay)
        return;
    std::vector<std::string> lines;
    for (size_t at = 0;;) {
        const size_t end = text.find('\n', at);
        lines.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos)
            break;
        at = end + 1;
    }
    int width = 0;
    for (const std::string &line : lines)
        width = std::max(width, gameTextWidth(_font, _characters, line));
    ScreenRect box{mouseX + kLeft, mouseY + kAbove, width + 2 * kPadding, kEdge + kLine * int(lines.size())};
    box.x = std::clamp(box.x, 0, std::max(0, screen.w - box.w));
    box.y = std::clamp(box.y, 0, std::max(0, screen.h - box.h));
    const size_t fade = std::min<size_t>(kFadeMeshes - 1, (rested - kDelay) / kFadeStep);
    drawUiMesh(screen, _box, fade, box);
    for (size_t i = 0; i < lines.size(); ++i) {
        const int w = gameTextWidth(_font, _characters, lines[i]);
        drawGameText(screen, _font, _characters, box.x + (box.w - w) / 2, box.y + kEdge / 2 + kLine * int(i), lines[i],
                     kText);
    }
}

} // namespace reluva::tpw
