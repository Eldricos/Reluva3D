// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/assets.h"

#include <algorithm>
#include <vector>

namespace reluva::botf {

namespace {

// Loads `name` into `cache` with `parse` (data -> Error), once.
template<typename T, typename Parse>
const T *cached(const Archive &archive, const std::string &name, std::map<std::string, T> &cache,
                std::map<std::string, std::string> &failed, Parse parse) {
    if (auto it = cache.find(name); it != cache.end())
        return &it->second;
    if (failed.count(name))
        return nullptr;
    std::vector<uint8_t> data;
    if (Error err = archive.read(name, data)) {
        failed[name] = "missing";
        return nullptr;
    }
    T value;
    if (Error err = parse(data, value)) {
        failed[name] = "can't read: " + err.message;
        return nullptr;
    }
    return &cache.emplace(name, std::move(value)).first->second;
}

} // namespace

const image::Image *Assets::picture(const std::string &name) {
    return cached(_archive, name, _pictures, _failed, [](const std::vector<uint8_t> &data, image::Image &img) {
        if (Error err = image::decode(data, img))
            return err;
        applyColorKey(img);
        return Error::success();
    });
}

const Font *Assets::font(const std::string &name) {
    return cached(_archive, name, _fonts, _failed,
                  [](const std::vector<uint8_t> &data, Font &f) { return f.load(data); });
}

const MenuFile *Assets::menu(const std::string &name) {
    return cached(_archive, name, _menus, _failed,
                  [](const std::vector<uint8_t> &data, MenuFile &m) { return m.load(data); });
}

std::string Assets::why(const std::string &name) const {
    auto it = _failed.find(name);
    return it == _failed.end() ? std::string() : it->second;
}

size_t applyColorKey(image::Image &img) {
    size_t n = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        uint8_t *p = img.rgba.data() + i;
        // 5-bit magenta (31, 0, 31) is 255, 0, 255 once widened; allow for
        // a decoder that widens by shifting (248, 0, 248).
        if (p[0] >= 248 && p[1] < 8 && p[2] >= 248) {
            p[3] = 0;
            ++n;
        }
    }
    return n;
}

void blitClipped(Surface &dst, const image::Image &img, int x, int y, int cx, int cy, int cw, int ch) {
    const int x0 = std::max({x, cx, 0});
    const int y0 = std::max({y, cy, 0});
    const int x1 = std::min({x + img.w, cx + cw, dst.w});
    const int y1 = std::min({y + img.h, cy + ch, dst.h});
    if (x0 >= x1 || y0 >= y1)
        return;
    image::Image part;
    part.w = x1 - x0;
    part.h = y1 - y0;
    part.rgba.resize(size_t(part.w) * part.h * 4);
    for (int py = 0; py < part.h; ++py)
        std::copy_n(img.pixel(x0 - x, y0 - y + py), size_t(part.w) * 4, part.pixel(0, py));
    image::blit(dst, part, x0, y0);
}

} // namespace reluva::botf
