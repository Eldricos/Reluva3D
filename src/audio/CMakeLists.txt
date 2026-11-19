# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_audio: the mixer, streams and decoders (WAV, MP3, ADPCM, ...).
add_library(reluva_audio STATIC
    decode.cpp
    mixer.cpp
    mp3.cpp
    stream.cpp
    wav.cpp)
target_link_libraries(reluva_audio PUBLIC reluva_common PRIVATE reluva_warnings)
# minimp3 (third_party/, see its README) as a system include, so our
# warnings don't apply to it.
target_include_directories(reluva_audio SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/minimp3)
