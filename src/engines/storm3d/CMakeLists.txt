# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Storm3D (docs/engines/storm3d/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(storm3d
    ACCESSOR storm3dMetaEngine
    SOURCES
        storm3d.cpp
        formats/config_file.cpp
        formats/odf.cpp
        formats/zfs_archive.cpp)

# Its reluva-tool commands (reluva-tool help storm3d lists them).
reluva_add_engine_tools(storm3d
    SOURCES
        tools/archives.cpp
        tools/files.cpp
        tools/text.cpp)

# Its tests (tests/engines/storm3d/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/storm3d)
reluva_add_engine_tests(storm3d
    SOURCES
        ${tests}/unit/config.cpp
        ${tests}/unit/detection.cpp
        ${tests}/unit/odf.cpp
        ${tests}/unit/zfs.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.config unit.detection unit.odf unit.zfs engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(storm3d
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
