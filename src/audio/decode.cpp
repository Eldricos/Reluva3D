// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/decode.h"

#include "audio/compressed.h"
#include "audio/mp3.h"
#include "audio/wav.h"

#include <cstring>

namespace reluva::audio {

Error decodeSound(const std::vector<uint8_t> &data, Sound &out) {
    if (data.size() >= 12 && std::memcmp(data.data(), "RIFF", 4) == 0 && std::memcmp(data.data() + 8, "WAVE", 4) == 0)
        return decodeWav(data, out);
    if (data.size() >= 4 && std::memcmp(data.data(), "OggS", 4) == 0)
        return decodeVorbis(data, out);
    if (data.size() >= 4 && std::memcmp(data.data(), "fLaC", 4) == 0)
        return decodeFlac(data, out);
    return decodeMp3(data, out);
}

const char *formatName(int format) {
    switch (format) {
    case 1:
        return "PCM";
    case 0x11:
        return "IMA ADPCM";
    case kFormatMp3:
        return "MP3";
    case kFormatVorbis:
        return "Ogg Vorbis";
    case kFormatFlac:
        return "FLAC";
    case kFormatEaXa:
        return "EA-XA";
    case kFormatMicroTalk:
        return "MicroTalk";
    default:
        return "?";
    }
}

} // namespace reluva::audio
