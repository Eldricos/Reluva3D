// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/data.h"

#include <cstring>

namespace reluva::netimmerse {

namespace {

constexpr size_t kHeaderSize = 18;

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(le16(p)) | uint32_t(le16(p + 2)) << 16; }

} // namespace

Error decodeDds(const std::vector<uint8_t>& data, audio::Sound& out) {
    if (data.size() < kHeaderSize)
        return Error::fail("too short for a sound");
    const uint8_t* h = data.data();
    const uint16_t format = le16(h), channels = le16(h + 2), blockAlign = le16(h + 12), bits = le16(h + 14);
    const uint32_t rate = le32(h + 4), byteRate = le32(h + 8);
    if (format != 1)
        return Error::fail("format " + std::to_string(format) + ", not PCM");
    if ((channels != 1 && channels != 2) || bits != 16 || blockAlign != channels * 2 || rate == 0 ||
        rate > 192000 || byteRate != rate * blockAlign || le16(h + 16) != 0)
        return Error::fail("not a 16-bit mono or stereo sound header");
    // The retail files hold whole samples; a stray odd byte would be cut.
    const size_t samples = (data.size() - kHeaderSize) / 2;
    out.rate = int(rate);
    out.channels = channels;
    out.format = 1;
    out.samples.resize(samples - samples % channels);
    for (size_t i = 0; i < out.samples.size(); ++i)
        out.samples[i] = int16_t(le16(h + kHeaderSize + i * 2));
    return Error::success();
}

std::vector<std::pair<std::string, std::string>> parseLocaleText(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> entries;
    bool inEntry = false;
    auto finish = [&] {
        if (!inEntry)
            return;
        std::string& value = entries.back().second;
        while (!value.empty() && (value.back() == '\n' || value.back() == ' '))
            value.pop_back();
    };
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.size() >= 4 && line.substr(0, 2) == "==" && line.substr(line.size() - 2) == "==") {
            finish();
            entries.emplace_back(std::string(line.substr(2, line.size() - 4)), std::string());
            inEntry = true;
        } else if (!line.empty() && line[0] == '#') {
            continue;
        } else if (inEntry) {
            std::string& value = entries.back().second;
            if (!value.empty() || !line.empty())
                value.append(line).push_back('\n');
        }
    }
    finish();
    return entries;
}

} // namespace reluva::netimmerse
