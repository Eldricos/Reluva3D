# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_common: the platform-independent core every other library builds
# on (files, streams, archives, settings, logging, hashes, the System
# interface the backends implement). No SDL here. Includes are from src/
# ("common/fs.h"), for everything that links it.
add_library(reluva_common STATIC
    archive.cpp
    config.cpp
    cue.cpp
    dcl.cpp
    detection.cpp
    fs.cpp
    inflate.cpp
    log.cpp
    md4.cpp
    md5.cpp
    net.cpp
    pe.cpp
    settings.cpp
    stb_image.cpp
    stream.cpp
    text.cpp
    wise.cpp
    zip.cpp)
target_include_directories(reluva_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(reluva_common PRIVATE reluva_warnings)
# stb_image (third_party/, see its README) as a system include, so our
# warnings don't apply to it: stb_image.cpp builds it, zip.cpp uses its
# inflate.
target_include_directories(reluva_common SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
