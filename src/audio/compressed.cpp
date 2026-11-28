// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/compressed.h"

#include <cstdlib>
#include <limits>

// The decoders' own code, here only: stb_vorbis is a single source file,
// dr_flac a header whose implementation one file includes.
#include "stb_vorbis.c"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

namespace reluva::audio {

Error decodeVorbis(const std::vector<uint8_t> &data, Sound &out) {
    out = Sound{};
    out.format = kFormatVorbis;
    if (data.size() > size_t(std::numeric_limits<int>::max()))
        return Error::fail("too large an Ogg Vorbis file");
    int channels = 0;
    int rate = 0;
    short *pcm = nullptr;
    const int frames = stb_vorbis_decode_memory(data.data(), int(data.size()), &channels, &rate, &pcm);
    if (frames < 0 || !pcm || channels <= 0)
        return Error::fail("not an Ogg Vorbis file, or a broken one");
    out.channels = channels;
    out.rate = rate;
    out.samples.assign(pcm, pcm + size_t(frames) * size_t(channels));
    std::free(pcm); // stb_vorbis's malloc
    return Error::success();
}

Error decodeFlac(const std::vector<uint8_t> &data, Sound &out) {
    out = Sound{};
    out.format = kFormatFlac;
    unsigned int channels = 0;
    unsigned int rate = 0;
    drflac_uint64 frames = 0;
    drflac_int16 *pcm =
        drflac_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &channels, &rate, &frames, nullptr);
    if (!pcm || channels == 0)
        return Error::fail("not a FLAC file, or a broken one");
    out.channels = int(channels);
    out.rate = int(rate);
    out.samples.assign(pcm, pcm + size_t(frames) * channels);
    drflac_free(pcm, nullptr);
    return Error::success();
}

} // namespace reluva::audio
