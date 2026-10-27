// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pages/file_viewer.h"

#include "common/font.h"

#include <algorithm>
#include <filesystem>

namespace reluva::idtech3 {

// --- FileViewer -------------------------------------------------------------------------

FileViewer::FileViewer(ViewerContext& ctx) : Page(ctx) {
    for (const std::string& n : ctx.files.list()) {
        const Archive* a = ctx.files.find(n);
        files_.push_back({n, a ? std::filesystem::path(a->describe()).filename().string() : std::string()});
    }
}

void FileViewer::activate() {
    setTitle(std::to_string(files_.size()) + " files in " + std::to_string(ctx_.files.size()) + " archives");
    draw();
}

void FileViewer::handleEvent(const Event& ev) {
    const int rows = (ctx_.screen.h - 4 * font::kLineHeight) / font::kLineHeight;
    if (ev.type == EventType::MouseWheel)
        top_ -= ev.wheelY * 3;
    else if (ev.type != EventType::KeyDown)
        return;
    else if (ev.key == key::Up)
        --top_;
    else if (ev.key == key::Down)
        ++top_;
    else if (ev.key == key::PageUp)
        top_ -= rows;
    else if (ev.key == key::PageDown)
        top_ += rows;
    else if (ev.key == key::Home)
        top_ = 0;
    else if (ev.key == key::End)
        top_ = static_cast<int>(files_.size());
    else
        return;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(files_.size()) - rows));
    draw();
}

void FileViewer::draw() {
    clear(16, 18, 24);
    const int lh = font::kLineHeight;
    text(4, 4, "Search path, first match wins:", {150, 200, 255});
    std::string order;
    for (const Archive* a : ctx_.files.archives()) {
        if (!order.empty())
            order += ", ";
        order += std::filesystem::path(a->describe()).filename().string();
    }
    text(4, 4 + lh, order, {150, 200, 255});
    if (!ctx_.warnings.empty())
        text(4, 4 + 2 * lh,
             std::to_string(ctx_.warnings.size()) + " problems, the first: " + ctx_.warnings.front(),
             {255, 120, 120});
    const int rows = (ctx_.screen.h - 4 * lh) / lh;
    const int sourceX = ctx_.screen.w - 20 * font::kAdvance;
    for (int i = 0; i < rows && top_ + i < static_cast<int>(files_.size()); ++i) {
        const Row& r = files_[size_t(top_ + i)];
        const int y = 4 * lh + i * lh;
        text(4, y, r.name);
        text(sourceX, y, r.source, {140, 140, 140});
    }
}

} // namespace reluva::idtech3
