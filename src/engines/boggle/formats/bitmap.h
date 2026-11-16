// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/image.h"
namespace reluva::boggle {
// Packed RT_BITMAP DIB: BITMAPINFOHEADER, palette and padded BI_RGB rows.
// Supports the installed 8-bit indexed and 24-bit BGR resources only.
Error readBitmapResource(const std::vector<uint8_t>& bytes,image::Image& out);
} // namespace reluva::boggle
