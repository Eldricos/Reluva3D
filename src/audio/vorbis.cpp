// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/vorbis.h"

#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace reluva::audio {

namespace {

// The file in memory, read through vorbisfile's callbacks.
struct Reader {
    const std::vector<uint8_t> *data;
    size_t at = 0;
};

size_t readCallback(void *out, size_t size, size_t count, void *source) {
    auto *r = static_cast<Reader *>(source);
    if (size == 0)
        return 0;
    const size_t items = std::min(count, (r->data->size() - r->at) / size);
    std::memcpy(out, r->data->data() + r->at, items * size);
    r->at += items * size;
    return items;
}

int seekCallback(void *source, ogg_int64_t offset, int whence) {
    auto *r = static_cast<Reader *>(source);
    const auto size = int64_t(r->data->size());
    int64_t to = offset;
    if (whence == SEEK_CUR)
        to += int64_t(r->at);
    else if (whence == SEEK_END)
        to += size;
    if (to < 0 || to > size)
        return -1;
    r->at = size_t(to);
    return 0;
}

long tellCallback(void *source) {
    return long(static_cast<Reader *>(source)->at);
}

} // namespace

Error decodeVorbis(const std::vector<uint8_t> &data, Sound &out) {
    out = Sound{};
    out.format = kFormatVorbis;
    Reader reader{&data};
    const ov_callbacks callbacks = {readCallback, seekCallback, nullptr, tellCallback};
    OggVorbis_File file;
    if (ov_open_callbacks(&reader, &file, nullptr, 0, callbacks) != 0)
        return Error::fail("not an Ogg Vorbis file, or a damaged one");
    const vorbis_info *info = ov_info(&file, -1);
    out.channels = info->channels;
    out.rate = int(info->rate);
    if (out.channels < 1 || out.channels > 2) {
        ov_clear(&file);
        return Error::fail("Ogg Vorbis with " + std::to_string(out.channels) + " channels (only 1 or 2 are played)");
    }
    const ogg_int64_t total = ov_pcm_total(&file, -1);
    if (total > 0)
        out.samples.reserve(size_t(total) * size_t(out.channels));
    int16_t buffer[4096];
    for (;;) {
        int link = 0;
        // Little-endian, 16-bit, signed: Sound's samples as they are.
        const long bytes = ov_read(&file, reinterpret_cast<char *>(buffer), int(sizeof buffer), 0, 2, 1, &link);
        if (bytes == 0)
            break;
        if (bytes == OV_HOLE)
            continue; // a gap in the data; vorbisfile carries on after it
        if (bytes < 0) {
            ov_clear(&file);
            return Error::fail("the Ogg Vorbis data is damaged");
        }
        // A chained file may change format between its links.
        const vorbis_info *now = ov_info(&file, link);
        if (now->channels != out.channels || int(now->rate) != out.rate) {
            ov_clear(&file);
            return Error::fail("the Ogg Vorbis file changes its rate or channels partway");
        }
        out.samples.insert(out.samples.end(), buffer, buffer + bytes / 2);
    }
    ov_clear(&file);
    return Error::success();
}

} // namespace reluva::audio
