// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "engines/tpw/formats/sign.h"
#include "engines/tpw/ui/model_view.h"
#include "graphics/truetype.h"
#include "image/image.h"

#include <array>
#include <map>
#include <memory>
#include <string>

namespace reluva::tpw {

/**
 * Letters attractions' signs: their names drawn as their `.sgn` files
 * have them, over the `SIGN1` and `SIGN2` textures of their models. See
 * docs/engines/tpw/games/simcoaster/formats/sgn.md.
 */
class SignPainter {
public:
    /** A canvas's size, in the halves the models' two textures show. */
    static constexpr int kCanvasWidth = 2 * SignStyle::kCanvasHeight;

    /**
     * The two lines drawn as the style has them, on a canvas
     * kCanvasWidth × SignStyle::kCanvasHeight, clear where they aren't.
     * A line whose font the game hasn't is left out.
     */
    image::Image paint(const SearchSet &files, const SignStyle &style, const std::array<std::string, 2> &lines);

    /**
     * Reads the sign beside a model (`rides/robot/robot.sgn` for
     * `robot.MD2`) and puts its lines over the model's `sign1` and `sign2`
     * textures, the canvas's left and right halves.
     * @return False if it hasn't a sign, or its model those textures.
     */
    bool letter(const SearchSet &files, const std::string &modelPath, const std::array<std::string, 2> &lines,
                LoadedModel &model);

private:
    ttf::Font *font(const SearchSet &files, const std::string &face, const std::string &file);

    std::map<std::string, std::unique_ptr<ttf::Font>> _fonts; // by their family names, lower case
    bool _fontsRead = false;
};

} // namespace reluva::tpw
