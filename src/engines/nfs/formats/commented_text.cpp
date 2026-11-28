// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/formats/commented_text.h"

#include <cctype>

namespace reluva::nfs {

std::vector<int> commentedNumbers(std::string_view text) {
    std::vector<int> out;
    size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, "/*") == 0) {
            const size_t end = text.find("*/", i + 2);
            i = end == std::string_view::npos ? text.size() : end + 2;
            continue;
        }
        const bool minus =
            text[i] == '-' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1]));
        if (minus || std::isdigit(static_cast<unsigned char>(text[i]))) {
            size_t j = i + (minus ? 1 : 0);
            long v = 0;
            while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])) && v < 1000000000)
                v = v * 10 + (text[j++] - '0');
            out.push_back(static_cast<int>(minus ? -v : v));
            i = j;
            continue;
        }
        ++i;
    }
    return out;
}

} // namespace reluva::nfs
