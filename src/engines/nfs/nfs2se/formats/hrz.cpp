// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/formats/hrz.h"

#include "engines/nfs/formats/commented_text.h"

#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

namespace {

// The values in order, as the readers take them.
class Values {
public:
    explicit Values(std::vector<int> v) : _v(std::move(v)) {}
    int next() { return _v[_at++]; }
    Rgb rgb() { return {next(), next(), next()}; }
    template<size_t N>
    std::array<int, N> some() {
        std::array<int, N> a{};
        for (int &x : a)
            x = next();
        return a;
    }

private:
    std::vector<int> _v;
    size_t _at = 0;
};

} // namespace

Error parseSky(std::string_view text, Sky &out) {
    out = {};
    std::vector<int> n = commentedNumbers(text);
    if (n.size() != 26 && n.size() != 27)
        return Error::fail("Not a valid NFS II sky file: " + std::to_string(n.size()) + " numbers");
    out.kind = n.size() == 27 ? Sky::kGlide : Sky::kSoftware;
    Values v(std::move(n));
    if (out.kind == Sky::kGlide) {
        out.cloudDomeOffset = v.next();
        out.cloudDomeHeight = v.next();
    }
    out.blackHorizon = v.next() != 0;
    out.mirror = v.next() != 0;
    out.radius = v.next();
    out.angle = v.next();
    out.flatDistance = v.next();
    out.base = v.next();
    out.height = v.next();
    if (out.kind == Sky::kGlide) {
        out.midpoint = v.next();
        out.pixmapTop = v.next();
        out.pixmapBottom = v.next();
        out.earthTop = v.rgb();
        out.earthBase = v.rgb();
        out.skyTop = v.rgb();
        out.skyBaseSun = v.rgb();
        out.skyBaseOpposite = v.rgb();
    } else {
        out.pixmapBase = v.next();
        out.skyColour = v.rgb();
        out.baseColour = v.rgb();
        out.depthDistances = v.some<4>();
        out.depthLights = v.some<4>();
        out.colourSystem = v.next();
        out.colourValue = v.rgb();
    }
    return Error::success();
}

Error parseLights(std::string_view text, Lights &out) {
    out = {};
    std::vector<int> n = commentedNumbers(text);
    if (n.size() != 65)
        return Error::fail("Not a valid .lgt file: " + std::to_string(n.size()) + " numbers");
    Values v(std::move(n));
    for (auto &pair : out.depthCueing)
        pair = v.some<2>();
    for (Rgb &c : out.palette)
        c = v.rgb();
    out.maximum = v.next();
    out.minimum = v.next();
    out.effectLight = v.some<7>();
    return Error::success();
}

} // namespace reluva::nfs::nfs2se
