// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pages/file_viewer.h"

#include "graphics/font.h"

#include <algorithm>
#include <filesystem>

namespace reluva::idtech3 {

// --- FileViewer -------------------------------------------------------------------------

FileViewer::FileViewer(PageContext &ctx) : Page(ctx) {
    for (const std::string &n : ctx.files.list()) {
        const Archive *a = ctx.files.find(n);
        _files.push_back({n, a ? std::filesystem::path(a->describe()).filename().string() : std::string()});
    }
}

void FileViewer::activate() {
    setTitle(std::to_string(_files.size()) + " files in " + std::to_string(_ctx.files.size()) + " archives");
    draw();
}

void FileViewer::handleEvent(const Event &ev) {
    const int rows = (_ctx.screen.h - 4 * font::kLineHeight) / font::kLineHeight;
    if (ev.type == EventType::kMouseWheel)
        _top -= ev.wheelY * 3;
    else if (ev.type != EventType::kKeyDown)
        return;
    else if (ev.key == key::kUp)
        --_top;
    else if (ev.key == key::kDown)
        ++_top;
    else if (ev.key == key::kPageUp)
        _top -= rows;
    else if (ev.key == key::kPageDown)
        _top += rows;
    else if (ev.key == key::kHome)
        _top = 0;
    else if (ev.key == key::kEnd)
        _top = static_cast<int>(_files.size());
    else
        return;
    _top = std::clamp(_top, 0, std::max(0, static_cast<int>(_files.size()) - rows));
    draw();
}

void FileViewer::draw() {
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    text(4, 4, "Search path, first match wins:", {150, 200, 255});
    std::string order;
    for (const Archive *a : _ctx.files.archives()) {
        if (!order.empty())
            order += ", ";
        order += std::filesystem::path(a->describe()).filename().string();
    }
    text(4, 4 + lh, order, {150, 200, 255});
    if (!_ctx.warnings.empty())
        text(4, 4 + 2 * lh, std::to_string(_ctx.warnings.size()) + " problems, the first: " + _ctx.warnings.front(),
             {255, 120, 120});
    const int rows = (_ctx.screen.h - 4 * lh) / lh;
    const int sourceX = _ctx.screen.w - 20 * font::kAdvance;
    for (int i = 0; i < rows && _top + i < static_cast<int>(_files.size()); ++i) {
        const Row &r = _files[size_t(_top + i)];
        const int y = 4 * lh + i * lh;
        text(4, y, r.name);
        text(sourceX, y, r.source, {140, 140, 140});
    }
}

} // namespace reluva::idtech3
