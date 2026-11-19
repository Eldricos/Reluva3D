# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_video: movie decoders (Bink, Smacker, Cinepak, Indeo 5, AVI) and
# the movie player engines show them with.
add_library(reluva_video STATIC
    avi.cpp
    bink.cpp
    bink_tables.cpp
    cinepak.cpp
    indeo5.cpp
    indeo5_tables.cpp
    movie.cpp
    smacker.cpp)
target_link_libraries(reluva_video PUBLIC reluva_common reluva_audio reluva_graphics PRIVATE reluva_warnings)
