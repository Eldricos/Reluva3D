// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/software_sky.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace reluva::nfs::nfs2se {

std::array<uint8_t, kDepthBuckets> depthCueLights(const Sky &sky) {
    // The curve: a uniform cubic B-spline over the points, the first and
    // last three times each, 20 samples a segment (t = k / 19), truncated
    // to whole buckets and lights, joined by lines.
    struct P {
        int d;
        int l;
    };
    std::vector<P> control;
    for (int i = 0; i < 4; ++i) {
        const P p{sky.depthDistances[size_t(i)], sky.depthLights[size_t(i)]};
        const int times = i == 0 || i == 3 ? 3 : 1;
        for (int k = 0; k < times; ++k)
            control.push_back(p);
    }
    std::vector<P> points;
    for (size_t s = 0; s + 3 < control.size(); ++s)
        for (int k = 0; k < 20; ++k) {
            const double t = k / 19.0;
            const double w[4] = {(1 - t) * (1 - t) * (1 - t) / 6, (3 * t * t * t - 6 * t * t + 4) / 6,
                                 (-3 * t * t * t + 3 * t * t + 3 * t + 1) / 6, t * t * t / 6};
            double d = 0;
            double l = 0;
            for (int i = 0; i < 4; ++i) {
                d += w[i] * control[s + size_t(i)].d;
                l += w[i] * control[s + size_t(i)].l;
            }
            const P p{static_cast<int>(d), static_cast<int>(l)};
            if (points.empty() || points.back().d != p.d)
                points.push_back(p);
        }
    // The light at each distance along the lines, then where it changes,
    // walked from the far end (at most 16 changes, the farthest the last
    // bucket), each light from its bucket down to the next change.
    std::array<uint8_t, kDepthBuckets> out{};
    if (points.size() < 2)
        return out;
    auto lightAt = [&](int d) {
        size_t i = 0;
        while (i + 2 < points.size() && d > points[i + 1].d)
            ++i;
        const P &a = points[i];
        const P &b = points[i + 1];
        const int q = (d - a.d) * (b.l - a.l);
        const int r = std::abs(q) / std::max(1, b.d - a.d);
        return a.l + (q >= 0 ? r : -r);
    };
    struct Change {
        int bucket;
        int light;
    };
    std::vector<Change> changes;
    int previous = -1;
    for (int d = points.back().d; d >= points.front().d && changes.size() < 16; --d) {
        const int l = lightAt(d);
        if (l != previous)
            changes.push_back({d, l});
        previous = l;
    }
    if (changes.empty())
        return out;
    changes.front().bucket = kDepthBuckets - 1;
    for (int b = 0; b < kDepthBuckets; ++b) {
        int light = changes.back().light;
        for (const Change &c : changes)
            if (c.bucket >= b)
                light = c.light;
        out[size_t(b)] = static_cast<uint8_t>(std::clamp(light, 0, 15));
    }
    return out;
}

} // namespace reluva::nfs::nfs2se
