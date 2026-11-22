# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# LEGO Loco (docs/engines/legoloco/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(legoloco
    ACCESSOR legolocoMetaEngine
    SOURCES
        legoloco.cpp
        game_files.cpp
        formats/area_layout.cpp
        formats/board.cpp
        formats/easter_eggs.cpp
        formats/object_definition.cpp
        formats/resource_file.cpp)

# Its data viewer, in reluva-viewer (never in the game).
reluva_add_engine_viewers(legoloco
    ACCESSOR legolocoMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/board_viewer.cpp
        pages/definition_viewer.cpp
        pages/draw.cpp
        pages/picture_viewer.cpp
        pages/sound_viewer.cpp)

# Its reluva-tool commands (reluva-tool help legoloco lists them).
reluva_add_engine_tools(legoloco
    SOURCES
        tools/boards.cpp
        tools/definitions.cpp
        tools/easter_eggs.cpp
        tools/files.cpp
        tools/resources.cpp
        tools/viewer.cpp)

# Its tests (tests/engines/legoloco/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/legoloco)
reluva_add_engine_tests(legoloco
    SOURCES
        ${tests}/unit/boards.cpp
        ${tests}/unit/definitions.cpp
        ${tests}/unit/detection.cpp
        ${tests}/unit/easter_eggs.cpp
        ${tests}/unit/game_files.cpp
        ${tests}/unit/resources.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.boards unit.definitions unit.detection unit.easter_eggs unit.game_files unit.resources engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(legoloco
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
