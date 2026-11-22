# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Sith (docs/engines/sith/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(sith
    ACCESSOR sithMetaEngine
    SOURCES
        sith.cpp
        folders.cpp
        game_files.cpp
        games.cpp
        import.cpp
        formats/bitmap.cpp
        formats/colormap.cpp
        formats/episode.cpp
        formats/font.cpp
        formats/gob.cpp
        formats/material.cpp
        formats/sound_class.cpp
        formats/strings.cpp)

# Its data viewer, for reluva-viewer only: reluva, which players get,
# doesn't link it. See docs/DEVELOPMENT.md (The data viewer).
reluva_add_engine_viewers(sith
    ACCESSOR sithMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/movie_pages.cpp
        pages/picture_pages.cpp
        pages/sound_pages.cpp
        pages/text_pages.cpp)

# Its reluva-tool commands (reluva-tool help sith lists them).
reluva_add_engine_tools(sith
    SOURCES
        tools/files.cpp
        tools/formats.cpp
        tools/movies.cpp
        tools/pages.cpp)

# Its tests (tests/engines/sith/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/sith)
reluva_add_engine_tests(sith
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/folders.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/gob.cpp
        ${tests}/unit/import.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.folders unit.formats unit.gob unit.import engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(sith
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
