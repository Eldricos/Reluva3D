// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/formats/fsh.h"
#include "image/image.h"

#include <cstdint>

namespace reluva::nfs::nfs2se {

/**
 * The 3dfx program's track pictures are 16-bit 5:6:5, their see-through
 * texels 0x07C0, a green that's in no other picture (1,677,495 texels of
 * 1,360 pictures): as Glide's chroma key, which nfs2sea.exe imports, would
 * leave them out (docs/engines/nfs/formats/fsh.md, NFS II SE). Makes those
 * texels of `image`, decoded from `shape`, clear.
 */
inline void clearChromaKey(const Shape &shape, image::Image &image) {
    if (shape.type != kShape16Bit565)
        return;
    for (size_t i = 0; i * 2 + 1 < shape.pixels.size() && i * 4 + 3 < image.rgba.size(); ++i)
        if ((shape.pixels[i * 2] | shape.pixels[i * 2 + 1] << 8) == 0x07c0)
            image.rgba[i * 4 + 3] = 0;
}

} // namespace reluva::nfs::nfs2se
