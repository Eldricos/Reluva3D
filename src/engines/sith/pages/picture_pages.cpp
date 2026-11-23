// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages of pictures: the .bm pictures, the .mat materials
// and the .cmp colormaps.

#include "common/fs.h"
#include "engines/sith/formats/bitmap.h"
#include "engines/sith/formats/material.h"
#include "engines/sith/pages/pages.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace reluva::sith {

namespace {

constexpr Color kBackground = {40, 40, 48};
constexpr int kGap = 8;
// The area above the status line.
constexpr int kAreaH = kViewerH - 16;

image::Image scaled(const image::Image &img, int scale) {
    if (scale == 1)
        return img;
    image::Image out;
    out.w = img.w * scale;
    out.h = img.h * scale;
    out.rgba.resize(size_t(out.w) * out.h * 4);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x)
            std::copy_n(img.pixel(x / scale, y / scale), 4, out.pixel(x, y));
    return out;
}

// Where each of `images` goes, left to right and wrapping, at `scale`; false
// if they don't fit the area.
bool layOut(const std::vector<image::Image> &images, int scale, std::vector<std::pair<int, int>> &at) {
    at.clear();
    int x = kGap;
    int y = kGap;
    int rowH = 0;
    for (const image::Image &img : images) {
        const int w = img.w * scale;
        const int h = img.h * scale;
        if (w > kViewerW - 2 * kGap)
            return false;
        if (x + w > kViewerW - kGap && x > kGap) {
            x = kGap;
            y += rowH + kGap;
            rowH = 0;
        }
        at.emplace_back(x, y);
        x += w + kGap;
        rowH = std::max(rowH, h);
    }
    return y + rowH <= kAreaH;
}

// The images side by side, as large as they fit (up to four times), else
// at half or a quarter of their size; returns the scale, negative for 1/n.
int drawAll(Surface &screen, const std::vector<image::Image> &images) {
    std::vector<std::pair<int, int>> at;
    int scale = 4;
    while (scale > 1 && !layOut(images, scale, at))
        --scale;
    if (scale == 1 && !layOut(images, 1, at)) {
        for (int down = 2; down <= 8; ++down) {
            std::vector<image::Image> small;
            for (const image::Image &img : images) {
                image::Image s;
                s.w = std::max(1, img.w / down);
                s.h = std::max(1, img.h / down);
                s.rgba.resize(size_t(s.w) * s.h * 4);
                for (int y = 0; y < s.h; ++y)
                    for (int x = 0; x < s.w; ++x)
                        std::copy_n(img.pixel(x * down, y * down), 4, s.pixel(x, y));
                small.push_back(std::move(s));
            }
            if (layOut(small, 1, at) || down == 8) {
                for (size_t i = 0; i < small.size(); ++i)
                    image::blit(screen, small[i], at[i].first, at[i].second);
                return -down;
            }
        }
    }
    layOut(images, scale, at);
    for (size_t i = 0; i < images.size(); ++i)
        image::blit(screen, scaled(images[i], scale), at[i].first, at[i].second);
    return scale;
}

std::string scaleText(int scale) {
    return scale > 1 ? ", shown x" + std::to_string(scale) : scale < 0 ? ", shown 1/" + std::to_string(-scale) : "";
}

std::string fileLabel(const FileRef &f) {
    return f.first->describe() + ": " + f.second;
}

/**
 * The .bm pictures, every cel. Those without a palette of their own are
 * drawn with the interface's (uicolormap.cmp's) at first; C steps through
 * the colormaps' and the menus' backgrounds' (the Force powers' icons
 * take bkforce.bm's: docs/engines/sith/games/jk/formats/bm.md).
 */
class PicturePage : public Page {
public:
    explicit PicturePage(const ViewerData &data) : _data(data), _files(filesEnding(data.files, ".bm")) {}

    const char *name() const override { return "Pictures"; }
    size_t count() const override { return _files.size(); }
    const char *keys() const override { return "Left/Right, C: palette, K: transparency"; }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _files.size(), _index))
            return true;
        if (pressed == 'c' && !_data.palettes.empty()) {
            _palette = (_palette + 1) % _data.palettes.size();
            return true;
        }
        if (pressed == 'k') {
            _transparent = !_transparent;
            return true;
        }
        return false;
    }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const auto &file = _files[_index];
        std::vector<uint8_t> bytes;
        Bitmap bm;
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " + fileLabel(file);
        if (Error err = file.first->read(file.second, bytes); err || (err = parseBitmap(bytes, bm))) {
            drawStatus(screen, line + "  " + err.message);
            return;
        }
        const Palette pal = _data.palettes.empty() ? Palette{} : _data.palettes[_palette].second;
        std::vector<image::Image> images;
        for (const Bitmap::Cel &cel : bm.cels)
            images.push_back(bitmapToImage(bm, cel, pal, _transparent));
        const int scale = drawAll(screen, images);
        char info[160];
        std::snprintf(info, sizeof(info), "  %d-bit, %zu cels of %dx%d, x %d y %d%s", bm.bitsPerPixel, bm.cels.size(),
                      bm.cels.empty() ? 0 : bm.cels[0].w, bm.cels.empty() ? 0 : bm.cels[0].h, bm.x, bm.y,
                      scaleText(scale).c_str());
        line += info;
        if (bm.bitsPerPixel == 8)
            line += bm.palette ? ", own palette"
                               : ", flags " + std::to_string(bm.flags) + ", drawn with " +
                                     (_data.palettes.empty() ? std::string("none") : _data.palettes[_palette].first);
        drawStatus(screen, line);
    }

private:
    const ViewerData &_data;
    std::vector<FileRef> _files;
    size_t _index = 0;
    size_t _palette = 0;
    bool _transparent = true;
};

/**
 * The .mat materials, every cel: a texture, or a plain colour as a square,
 * through a colormap (dflt.cmp's at first; C steps through them) at a
 * light level.
 */
class MaterialPage : public Page {
public:
    explicit MaterialPage(const ViewerData &data) : _data(data), _files(filesEnding(data.files, ".mat")) {
        _colormap = std::max(0, data.findColormap("dflt.cmp"));
    }

    const char *name() const override { return "Materials"; }
    size_t count() const override { return _data.colormaps.empty() ? 0 : _files.size(); }
    const char *keys() const override {
        return "Left/Right, C: colormap, [/]: light level, M: mip level, K: transparency";
    }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _files.size(), _index))
            return true;
        if (pressed == 'c')
            _colormap = (_colormap + 1) % int(_data.colormaps.size());
        else if (pressed == '[')
            _light = std::max(0, _light - 4);
        else if (pressed == ']')
            _light = std::min(Colormap::kLightLevels - 1, _light + 4);
        else if (pressed == 'm')
            _mip = (_mip + 1) % 4;
        else if (pressed == 'k')
            _transparent = !_transparent;
        else
            return false;
        return true;
    }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const auto &file = _files[_index];
        std::vector<uint8_t> bytes;
        Material mat;
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " + fileLabel(file);
        if (Error err = file.first->read(file.second, bytes); err || (err = parseMaterial(bytes, mat))) {
            drawStatus(screen, line + "  " + err.message);
            return;
        }
        const auto &[cmpName, cmp] = _data.colormaps[size_t(_colormap)];
        Palette lit;
        for (int i = 0; i < 256; ++i)
            lit[i] = cmp.palette[cmp.lit(_light, uint8_t(i))];
        std::vector<image::Image> images;
        int mip = 0;
        for (const Material::Cel &cel : mat.cels) {
            if (cel.texture >= 0) {
                const Material::Texture &tex = mat.textures[size_t(cel.texture)];
                mip = std::min(_mip, int(tex.mips.size()) - 1);
                images.push_back(materialToImage(tex, mip, lit, _transparent));
            } else {
                image::Image swatch;
                swatch.w = swatch.h = 32;
                swatch.rgba.resize(32 * 32 * 4);
                const Color c = lit[cel.color];
                for (size_t p = 0; p < swatch.rgba.size(); p += 4) {
                    swatch.rgba[p] = c.r;
                    swatch.rgba[p + 1] = c.g;
                    swatch.rgba[p + 2] = c.b;
                    swatch.rgba[p + 3] = 255;
                }
                images.push_back(std::move(swatch));
            }
        }
        const int scale = drawAll(screen, images);
        line += "  " + std::to_string(mat.cels.size()) + " cels";
        if (!mat.textures.empty())
            line += ", " + std::to_string(mat.textures[0].w) + "x" + std::to_string(mat.textures[0].h) + " mip " +
                    std::to_string(mip) + "/" + std::to_string(mat.textures[0].mips.size());
        line += ", " + cmpName + " light " + std::to_string(_light) + scaleText(scale);
        drawStatus(screen, line);
    }

private:
    const ViewerData &_data;
    std::vector<FileRef> _files;
    size_t _index = 0;
    int _colormap = 0;
    int _light = Colormap::kLightLevels - 1;
    int _mip = 0;
    bool _transparent = true;
};

/**
 * The colormaps: the palette in a 16x16 grid, the light levels from dark
 * (top) to bright, and the blend table when there is one.
 */
class ColormapPage : public Page {
public:
    explicit ColormapPage(const ViewerData &data) : _data(data) {}

    const char *name() const override { return "Colormaps"; }
    size_t count() const override { return _data.colormaps.size(); }
    const char *keys() const override { return "Left/Right"; }

    bool handleKey(uint32_t pressed) override { return stepIndex(pressed, _data.colormaps.size(), _index); }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const auto &[cmpName, cmp] = _data.colormaps[_index];
        for (int i = 0; i < 256; ++i)
            fillRect(screen, kGap + (i % 16) * 16, kGap + (i / 16) * 16, 15, 15, cmp.palette[i]);

        // The light levels: a row of 256 colours for each, two pixels a colour.
        const int lx = kGap * 2 + 256;
        for (int level = 0; level < Colormap::kLightLevels; ++level)
            for (int i = 0; i < 256; ++i)
                fillRect(screen, lx + i * 2, kGap + level * 4, 2, 4, cmp.palette[cmp.lit(level, uint8_t(i))]);

        if (!cmp.blend.empty()) {
            const int by = kGap * 2 + 256;
            for (int a = 0; a < 256; ++a)
                for (int b = 0; b < 256; ++b)
                    if (by + a < kAreaH)
                        fillRect(screen, kGap + b, by + a, 1, 1, cmp.palette[cmp.blend[size_t(a) * 256 + b]]);
        }
        char info[160];
        std::snprintf(info, sizeof(info), "%zu/%zu  %s  tint %.2f %.2f %.2f%s", _index + 1, _data.colormaps.size(),
                      cmpName.c_str(), cmp.tint[0], cmp.tint[1], cmp.tint[2],
                      cmp.blend.empty() ? ", no blend table" : ", blend table below");
        drawStatus(screen, info);
    }

private:
    const ViewerData &_data;
    size_t _index = 0;
};

} // namespace

std::unique_ptr<Page> makePicturePage(const ViewerData &data) {
    return std::make_unique<PicturePage>(data);
}

std::unique_ptr<Page> makeMaterialPage(const ViewerData &data) {
    return std::make_unique<MaterialPage>(data);
}

std::unique_ptr<Page> makeColormapPage(const ViewerData &data) {
    return std::make_unique<ColormapPage>(data);
}

} // namespace reluva::sith
