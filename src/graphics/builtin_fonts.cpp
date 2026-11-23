// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/builtin_fonts.h"

#include <cstddef>

namespace reluva::embedded {
// Made by reluva_embed (cmake/ReluvaEmbed.cmake).
extern const unsigned char kLiberationSansRegular[];
extern const std::size_t kLiberationSansRegularSize;
extern const unsigned char kLiberationSansBold[];
extern const std::size_t kLiberationSansBoldSize;
} // namespace reluva::embedded

namespace reluva::ttf {

std::vector<uint8_t> liberationSans(bool bold) {
    const unsigned char *data = bold ? embedded::kLiberationSansBold : embedded::kLiberationSansRegular;
    const std::size_t size = bold ? embedded::kLiberationSansBoldSize : embedded::kLiberationSansRegularSize;
    return {data, data + size};
}

} // namespace reluva::ttf
