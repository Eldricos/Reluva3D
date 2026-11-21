# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# LEGO Island (docs/engines/legoisland/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(legoisland
    ACCESSOR legoislandMetaEngine
    SOURCES
        detection.cpp
        formats/si.cpp
        legoisland.cpp)

# Its data viewer, in reluva-viewer only (docs/DEVELOPMENT.md, The data
# viewer).
reluva_add_engine_viewers(legoisland
    ACCESSOR legoislandMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/si_viewer.cpp)

# Its reluva-tool commands (reluva-tool help legoisland lists them).
reluva_add_engine_tools(legoisland
    SOURCES
        tools/files.cpp
        tools/si.cpp)

# Its tests (tests/engines/legoisland/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/legoisland)
reluva_add_engine_tests(legoisland
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/import.cpp
        ${tests}/unit/si.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.import unit.si engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(legoisland
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
