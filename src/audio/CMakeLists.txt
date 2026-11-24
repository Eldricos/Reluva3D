# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_audio: the mixer, streams and decoders (WAV, MP3, ADPCM, ...),
# tracker music (MOD, S3M, XM, IT), and DirectMusic's player with its DLS
# synthesizer.
add_library(reluva_audio STATIC
    compressed.cpp
    decode.cpp
    directmusic.cpp
    dls.cpp
    ea_audio.cpp
    mixer.cpp
    mp3.cpp
    prologic.cpp
    stream.cpp
    synth.cpp
    tracker.cpp
    wav.cpp)
target_link_libraries(reluva_audio PUBLIC reluva_common PRIVATE reluva_warnings)
# minimp3, stb_vorbis and dr_flac (third_party/, see their READMEs) as
# system includes, so our warnings don't apply to them.
target_include_directories(reluva_audio SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/minimp3
    ${PROJECT_SOURCE_DIR}/third_party/stb ${PROJECT_SOURCE_DIR}/third_party/dr_flac)
