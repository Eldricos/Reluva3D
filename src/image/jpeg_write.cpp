// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The JPEG encoder, from stb_image_write (third_party/stb), compiled here
// and only its JPEG writer used.

#include "image/image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace reluva::image {

std::vector<uint8_t> encodeJpeg(const Image &img, int quality) {
    std::vector<uint8_t> out;
    if (img.w <= 0 || img.h <= 0)
        return out;
    auto append = [](void *context, void *data, int size) {
        auto *bytes = static_cast<std::vector<uint8_t> *>(context);
        const auto *p = static_cast<const uint8_t *>(data);
        bytes->insert(bytes->end(), p, p + size);
    };
    if (!stbi_write_jpg_to_func(append, &out, img.w, img.h, 4, img.rgba.data(), quality))
        out.clear();
    return out;
}

} // namespace reluva::image
