// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <vector>

namespace reluva::ttf {

/**
 * Liberation Sans (third_party/liberation/), built into Reluva: a font
 * file for a ttf::Font to load, regular or bold. Its letters are as wide
 * as Arial's, which Windows programs ask for.
 */
std::vector<uint8_t> liberationSans(bool bold);

} // namespace reluva::ttf
