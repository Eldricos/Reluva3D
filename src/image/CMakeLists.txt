# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_image: decoding pictures (TGA, JPEG, PNG through stb_image) and
# Windows bitmaps.
add_library(reluva_image STATIC
    bmp.cpp
    image.cpp)
target_link_libraries(reluva_image PUBLIC reluva_common reluva_graphics PRIVATE reluva_warnings)
# stb_image (third_party/, see its README) as a system include.
target_include_directories(reluva_image SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
