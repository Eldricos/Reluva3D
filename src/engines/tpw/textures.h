// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "image/image.h"

#include <string>

namespace reluva::tpw {

/**
 * Where a model's texture is in the game's files, or empty. A model names
 * it as `deskpillars.tga`; it is in the model's `textures/` folder or its
 * world's `sharetex/`, mostly as an `.fsh` bank of the same name, else in
 * the smaller copies' `stexture/` and `ssharete/`. See
 * docs/engines/tpw/games/simcoaster/formats/md2.md (Finding the textures).
 * @param model The model's name in the files, as `levels/arabian/features/atmdesk/atmdesk.MD2`.
 */
std::string findTexture(const SearchSet &files, const std::string &model, const std::string &texture);

/** A picture of the game's decoded: an `.fsh` bank's first shape, or a `.tga`, `.png` or `.jpg`. */
Error loadPicture(const SearchSet &files, const std::string &name, image::Image &out);

} // namespace reluva::tpw
