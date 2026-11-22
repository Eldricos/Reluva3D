# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Sith (docs/engines/sith/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(sith
    ACCESSOR sithMetaEngine
    SOURCES
        sith.cpp
        folders.cpp
        games.cpp)

# Its reluva-tool commands (reluva-tool help sith lists them).
reluva_add_engine_tools(sith
    SOURCES
        tools/files.cpp)

# Its tests (tests/engines/sith/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/sith)
reluva_add_engine_tests(sith
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/folders.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.folders engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(sith
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
