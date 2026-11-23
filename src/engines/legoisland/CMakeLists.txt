# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# LEGO Island (docs/engines/legoisland/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(legoisland
    ACCESSOR legoislandMetaEngine
    SOURCES
        detection.cpp
        formats/dta.cpp
        formats/gph.cpp
        formats/si.cpp
        formats/wdb.cpp
        host/game_process.cpp
        host/player.cpp
        host/turning.cpp
        legoisland.cpp)

# Its data viewer, in reluva-viewer only (docs/DEVELOPMENT.md, The data
# viewer).
reluva_add_engine_viewers(legoisland
    ACCESSOR legoislandMetaViewer
    SOURCES
        pages/dta_viewer.cpp
        pages/scene.cpp
        pages/viewer.cpp
        pages/si_scene_viewer.cpp
        pages/si_viewer.cpp
        pages/wdb_viewer.cpp)

# Its reluva-tool commands (reluva-tool help legoisland lists them).
reluva_add_engine_tools(legoisland
    SOURCES
        tools/dta.cpp
        tools/files.cpp
        tools/run.cpp
        tools/obj.cpp
        tools/si.cpp
        tools/wdb.cpp)

# Its tests (tests/engines/legoisland/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/legoisland)
reluva_add_engine_tests(legoisland
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/dta.cpp
        ${tests}/unit/import.cpp
        ${tests}/unit/obj.cpp
        ${tests}/unit/scene.cpp
        ${tests}/unit/si.cpp
        ${tests}/unit/turning.cpp
        ${tests}/unit/wdb.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.dta unit.import unit.obj unit.scene unit.si unit.turning unit.wdb engine)

# Its engine test with the game's own files (tests/engines/legoisland/
# gamedata.cmake; docs/DEVELOPMENT.md, Engine tests).
reluva_add_gamedata_test(legoisland
    GAME legoisland
    ENV RELUVA_LEGOISLAND
    SCRIPT ${tests}/gamedata.cmake
    STEPS release scripts world animations island turning build elevator players)
