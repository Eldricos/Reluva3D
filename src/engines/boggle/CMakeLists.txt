# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Boggle (1997) (docs/engines/boggle/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(boggle
    ACCESSOR boggleMetaEngine
    SOURCES
        boggle.cpp
        frontend/game.cpp
        frontend/media.cpp
        import.cpp
        resources.cpp
        formats/text_resources.cpp
        formats/mesh.cpp
        formats/dictionary.cpp
        classic/presentation.cpp
        classic/layout.cpp
        classic/projection.cpp
        classic/options.cpp
        classic/player.cpp
        classic/user_dictionary.cpp
        classic/results.cpp
        classic/round.cpp
        classic/game.cpp)

# Its reluva-tool commands (reluva-tool help boggle lists them).
reluva_add_engine_tools(boggle
    SOURCES
        tools/layout.cpp
        tools/files.cpp
        tools/frontend.cpp
        tools/import.cpp
        tools/presentation.cpp
        tools/resources.cpp
        tools/dictionary.cpp
        tools/classic.cpp)

# Its tests (tests/engines/boggle/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/boggle)
reluva_add_engine_tests(boggle
    SOURCES
        ${tests}/unit/results.cpp
        ${tests}/unit/player.cpp
        ${tests}/unit/history.cpp
        ${tests}/unit/layout.cpp
        ${tests}/unit/detection.cpp
        ${tests}/unit/import.cpp
        ${tests}/unit/frontend.cpp
        ${tests}/unit/resources.cpp
        ${tests}/unit/dictionary.cpp
        ${tests}/unit/presentation.cpp
        ${tests}/unit/options.cpp
        ${tests}/unit/classic.cpp
        ${tests}/unit/user_dictionary.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.results unit.player unit.history unit.layout unit.detection unit.frontend unit.import unit.resources unit.dictionary unit.classic unit.options unit.presentation unit.user_dictionary engine)

# Optional tests with the owner's data; raw output stays outside Git.
reluva_add_gamedata_test(boggle
    GAME boggle1997
    ENV RELUVA_BOGGLE1997
    SCRIPT ${tests}/gamedata.cmake
    STEPS release resources dictionaries classic master presentation music import frontend movies artwork font)
