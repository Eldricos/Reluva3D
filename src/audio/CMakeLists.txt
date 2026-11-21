# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_audio: the mixer, streams and decoders (WAV, MP3, Ogg Vorbis,
# ADPCM, ...), tracker music (MOD, S3M, XM, IT), and DirectMusic's player
# with its DLS synthesizer.
add_library(reluva_audio STATIC
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
    vorbis.cpp
    wav.cpp)
# libvorbis is built by the top CMakeLists.txt, from Xiph's sources.
target_link_libraries(reluva_audio PUBLIC reluva_common PRIVATE reluva_warnings vorbis)
# minimp3 (third_party/, see its README) as a system include, so our
# warnings don't apply to it.
target_include_directories(reluva_audio SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/minimp3)
