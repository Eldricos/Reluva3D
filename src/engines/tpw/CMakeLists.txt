# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Theme Park World (docs/engines/tpw/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(tpw
    ACCESSOR tpwMetaEngine
    SOURCES
        tpw.cpp
        games.cpp)

# Its reluva-tool commands (reluva-tool help tpw lists them).
reluva_add_engine_tools(tpw
    SOURCES
        tools/files.cpp)

# Its tests (tests/engines/tpw/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/tpw)
reluva_add_engine_tests(tpw
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/import.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.import engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(tpw
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
