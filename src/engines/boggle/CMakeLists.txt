# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Boggle (1997) (docs/engines/boggle/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(boggle
    ACCESSOR boggleMetaEngine
    SOURCES
        boggle.cpp
        resources.cpp
        formats/text_resources.cpp
        formats/mesh.cpp
        formats/dictionary.cpp
        classic/round.cpp
        classic/game.cpp)

# Its reluva-tool commands (reluva-tool help boggle lists them).
reluva_add_engine_tools(boggle
    SOURCES
        tools/files.cpp
        tools/resources.cpp
        tools/dictionary.cpp)

# Its tests (tests/engines/boggle/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/boggle)
reluva_add_engine_tests(boggle
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/resources.cpp
        ${tests}/unit/dictionary.cpp
        ${tests}/unit/classic.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.resources unit.dictionary unit.classic engine)

# Optional tests with the owner's data; raw output stays outside Git.
reluva_add_gamedata_test(boggle
    GAME boggle1997
    ENV RELUVA_BOGGLE1997
    SCRIPT ${tests}/gamedata.cmake
    STEPS release resources dictionaries classic)
