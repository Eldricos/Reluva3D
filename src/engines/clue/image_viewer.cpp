// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/viewers.h"

#include "common/font.h"

#include <algorithm>
#include <cstring>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26; // two lines of the built-in font

} // namespace

void fillRect(Surface& s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(s.w, x + w), y1 = std::min(s.h, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t* p = s.row(yy) + x0 * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

void drawCheckerboard(Surface& s, int y0, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < s.w; x += 8) {
            const uint8_t v = ((x / 8 + y / 8) & 1) ? 96 : 72;
            fillRect(s, x, y, 8, 1, {v, v, v});
        }
}

void drawInfo(Surface& s, const std::string& line1, const std::string& line2) {
    fillRect(s, 0, s.h - kInfoH, s.w, kInfoH, {16, 16, 24});
    font::drawText(s, 4, s.h - kInfoH + 2, line1, {230, 230, 230});
    if (!line2.empty())
        font::drawText(s, 4, s.h - kInfoH + 14, line2, {170, 170, 190});
}

void drawImage(Surface& s, const image::Image& img, int x, int y, int scale) {
    if (scale <= 1) {
        image::blit(s, img, x, y);
        return;
    }
    for (int sy = 0; sy < img.h; ++sy)
        for (int sx = 0; sx < img.w; ++sx) {
            const uint8_t* p = img.pixel(sx, sy);
            if (p[3])
                fillRect(s, x + sx * scale, y + sy * scale, scale, scale, {p[0], p[1], p[2]});
        }
}

ImageViewer::ImageViewer(System& sys, Surface& screen, const GameData& data)
    : sys_(sys), screen_(screen), data_(data) {
    for (const auto& path : data_.withExtension(".res")) {
        const DataDrive* d = data_.drive(path.stem().string(), 'I');
        // dialogInfo.res isn't a drive of images.
        if (d && !d->entries().empty()) {
            files_.push_back(path);
            total_ += d->entries().size();
        }
    }
}

void ImageViewer::activate() { draw(); }

void ImageViewer::handleKey(uint32_t key) {
    if (files_.empty())
        return;
    const auto imagesIn = [&](int f) {
        return int(data_.drive(files_[size_t(f)].stem().string(), 'I')->entries().size());
    };
    const int n = imagesIn(file_);
    switch (key) {
    case key::Right: image_ = std::min(n - 1, image_ + 1); break;
    case key::Left: image_ = std::max(0, image_ - 1); break;
    case key::PageDown: image_ = std::min(n - 1, image_ + 10); break;
    case key::PageUp: image_ = std::max(0, image_ - 10); break;
    case key::Home: image_ = 0; break;
    case key::End: image_ = n - 1; break;
    case key::Down:
        file_ = (file_ + 1) % int(files_.size());
        image_ = 0;
        break;
    case key::Up:
        file_ = (file_ + int(files_.size()) - 1) % int(files_.size());
        image_ = 0;
        break;
    case 'z': zoom_ = zoom_ == 4 ? 1 : zoom_ * 2; break;
    case 'b': background_ = (background_ + 1) % 3; break;
    default: return;
    }
    draw();
}

void ImageViewer::draw() {
    const int areaH = kScreenH - kInfoH;
    if (background_ == 0)
        drawCheckerboard(screen_, 0, areaH);
    else
        fillRect(screen_, 0, 0, kScreenW, areaH, background_ == 1 ? Color{0, 0, 0} : Color{255, 255, 255});
    if (files_.empty()) {
        drawInfo(screen_, "No images found");
        return;
    }
    const std::string drive = files_[size_t(file_)].stem().string();
    const DataDrive* d = data_.drive(drive, 'I');
    const DataDrive::Entry& e = d->entries()[size_t(image_)];
    const std::string full = drive + ":" + e.name;
    const Image* img = data_.image(full);
    std::string detail;
    if (!img) {
        detail = data_.lastError();
    } else {
        // Shrink the zoom to fit, then centre the image's cell.
        int z = zoom_;
        while (z > 1 && (img->cellW * z > kScreenW || img->cellH * z > areaH))
            z /= 2;
        const int x = (kScreenW - img->cellW * z) / 2 + img->offsetX * z;
        const int y = (areaH - img->cellH * z) / 2 + img->offsetY * z;
        drawImage(screen_, img->pixels, x, y, z);
        detail = std::to_string(img->pixels.w) + "x" + std::to_string(img->pixels.h);
        if (img->version == 0x100)
            detail += " in a " + std::to_string(img->cellW) + "x" + std::to_string(img->cellH) + " cell at " +
                      std::to_string(img->offsetX) + "," + std::to_string(img->offsetY);
        detail += "  version " + std::string(img->version == 0x100 ? "0x100" : "0x200");
        if (z > 1)
            detail += "  zoom " + std::to_string(z) + "x";
    }
    drawInfo(screen_,
             full + "   " + std::to_string(image_ + 1) + "/" + std::to_string(d->entries().size()) + " in " +
                 files_[size_t(file_)].filename().string() + " (file " + std::to_string(file_ + 1) + "/" +
                 std::to_string(files_.size()) + ")",
             detail);
    sys_.setWindowTitle("Clue images: " + full);
}

} // namespace reluva::clue
