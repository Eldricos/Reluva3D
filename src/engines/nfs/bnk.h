// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace reluva::nfs {

// A sound's "PT" header: tagged values describing its audio, as
// docs/formats/nfs/bnk.md describes. Values not given take the defaults.
struct SoundHeader {
    enum Compression { kPcm16 = -1, kEaXa = 7, kMicroTalk = 9 };
    int compression = kPcm16;
    int channels = 1;
    int rate = 22050;
    uint32_t samples = 0;
    uint32_t dataOffset = 0; // in a bank: from the bank's start
    // The loop, in samples: its first and its end (the last sample, or in
    // some banks the count); both 0 if it doesn't loop.
    uint32_t loopStart = 0, loopEnd = 0;
    std::map<int, uint32_t> tags; // every tag as read, the known ones too
};

// Reads a PT header from `data` at `at` (at "PT\0\0"); `end` gets the
// offset after it.
Error parseSoundHeader(const uint8_t* data, size_t size, size_t at, SoundHeader& out, size_t* end = nullptr);

// A .bnk bank ("BNKl"): slots of sounds, some empty.
struct SoundBank {
    int version = 0;
    std::vector<std::optional<SoundHeader>> slots;
};

Error parseSoundBank(const std::vector<uint8_t>& data, SoundBank& out);

// A bank's sound, decoded.
Error decodeBankSound(const std::vector<uint8_t>& data, const SoundHeader& header, audio::Sound& out);

// The pitch, in cents from the sound's own rate, that the game plays a
// sound at (its patch parameters, bnk.md): the fine tune (0x10, a signed
// byte) less the root key's (0x07) distance from the key played, in
// hundreds, plus the bend (0-127, 64 none) over its range (0x0A,
// semitones).
int patchCents(const SoundHeader& h, int key, int bend);
// The bend the game gives the player's engine at `rpm` (nfs3.exe 0x4119a9,
// 0x417390): 127 x rpm / (redline + 1000, 500 less while the car's field
// at +0x164, its gear by the look of it, is above 5), at most 100. The
// engine is played at key 60.
int engineBend(int rpm, int redline, int gear = 1);

} // namespace reluva::nfs
