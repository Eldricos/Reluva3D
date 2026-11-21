# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Star Trek: Away Team (docs/engines/stawayteam/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(stawayteam
    ACCESSOR stawayteamMetaEngine
    SOURCES
        stawayteam.cpp)

# Its reluva-tool commands (reluva-tool help stawayteam lists them).
reluva_add_engine_tools(stawayteam
    SOURCES
        tools/archive.cpp
        tools/files.cpp)

# Its tests (tests/engines/stawayteam/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/stawayteam)
reluva_add_engine_tests(stawayteam
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(stawayteam
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
