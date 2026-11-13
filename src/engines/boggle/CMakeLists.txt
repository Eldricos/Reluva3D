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
        formats/cursor_image.cpp
        formats/mesh.cpp
        formats/dictionary.cpp
        classic/presentation.cpp
        classic/cube_geometry.cpp
        classic/layout.cpp
        classic/projection.cpp
        classic/options.cpp
        classic/player.cpp
        classic/user_dictionary.cpp
        classic/solver.cpp
        classic/highscores.cpp
        classic/cursor.cpp
        classic/computer.cpp
        classic/ai_policy.cpp
        classic/ai_motion.cpp
        classic/competition.cpp
        classic/results.cpp
        classic/turn_clock.cpp
        classic/seed.cpp
        classic/round.cpp
        classic/game.cpp)

# Its reluva-tool commands (reluva-tool help boggle lists them).
reluva_add_engine_tools(boggle
    SOURCES
        tools/background.cpp
        tools/computer.cpp
        tools/competition.cpp
        tools/solver.cpp
        tools/cube_geometry.cpp
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
        ${tests}/unit/cursor.cpp
        ${tests}/unit/background.cpp
        ${tests}/unit/computer.cpp
        ${tests}/unit/chess.cpp
        ${tests}/unit/traditional.cpp
        ${tests}/unit/competition.cpp
        ${tests}/unit/highscores.cpp
        ${tests}/unit/seed.cpp
        ${tests}/unit/solver.cpp
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
        unit.cursor unit.background unit.computer unit.chess unit.traditional unit.competition unit.highscores unit.seed unit.solver unit.results unit.player unit.history unit.layout unit.detection unit.frontend unit.import unit.resources unit.dictionary unit.classic unit.options unit.presentation unit.user_dictionary engine)

# Optional tests with the owner's data; raw output stays outside Git.
reluva_add_gamedata_test(boggle
    GAME boggle1997
    ENV RELUVA_BOGGLE1997
    SCRIPT ${tests}/gamedata.cmake
    STEPS release resources dictionaries classic master presentation cursor music import frontend movies artwork font allwords competition computer background)
