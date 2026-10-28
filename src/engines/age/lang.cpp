// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/lang.h"

#include "common/pe.h"

namespace reluva::age {

Error GameStrings::load(const SearchSet& files) {
    strings_.clear();
    std::vector<uint8_t> data;
    if (Error err = files.read("mmlang.dll", data))
        return err;
    PeImage image;
    if (Error err = parsePe(data, image))
        return Error::fail("mmlang.dll: " + err.message);
    return readPeStrings(data, image, strings_);
}

std::string GameStrings::get(uint32_t id, const std::string& fallback) const {
    auto it = strings_.find(id);
    return it == strings_.end() ? fallback : it->second;
}

} // namespace reluva::age
