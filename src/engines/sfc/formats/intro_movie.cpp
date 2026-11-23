// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/formats/intro_movie.h"

#include "common/bytes.h"
#include "common/pe.h"

#include <algorithm>

namespace reluva::sfc {

Error introMovie(const std::vector<uint8_t> &program, std::vector<uint8_t> &movie) {
    movie.clear();
    PeImage pe;
    if (Error err = parsePe(program, pe))
        return err;
    size_t end = 0;
    for (const PeImage::Section &s : pe.sections)
        end = std::max(end, size_t(s.rawOffset) + s.rawSize);
    // "BIK" and the revision, then the size of what follows these 8 bytes.
    if (end + 8 > program.size() || program[end] != 'B' || program[end + 1] != 'I' || program[end + 2] != 'K')
        return Error::fail("no Bink movie after the program");
    const size_t size = size_t(readLE32(program.data() + end + 4)) + 8;
    if (size > program.size() - end)
        return Error::fail("the Bink movie is cut short");
    movie.assign(program.begin() + long(end), program.begin() + long(end + size));
    return Error::success();
}

} // namespace reluva::sfc
