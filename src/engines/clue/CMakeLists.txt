# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The Clue engine: Clue: Murder at Boddy Mansion (docs/engines/clue/). See
# docs/ADDING_AN_ENGINE.md.
reluva_add_engine(clue
    ACCESSOR clueMetaEngine
    SOURCES
        clue.cpp
        game_data.cpp
        formats/level.cpp
        formats/resi.cpp
        formats/resources.cpp
        game/ai.cpp
        game/board.cpp
        game/mansion.cpp
        game/netplay.cpp
        game/play.cpp
        game/rules.cpp
        pages/animation_viewer.cpp
        pages/image_viewer.cpp
        pages/mansion_viewer.cpp
        pages/sound_viewer.cpp
        pages/text_viewer.cpp
        pages/video_viewer.cpp
        ui/frontend.cpp
        ui/frontend_net.cpp
        ui/movie.cpp
        ui/ui.cpp)

# Its reluva-tool commands (reluva-tool help clue lists them).
reluva_add_engine_tools(clue
    SOURCES
        tools/files.cpp
        tools/board.cpp
        tools/play.cpp)

# Its tests (tests/engines/clue/), in reluva-tests: the unit tests by topic,
# on files built in the tests, and the engine test, without game files (a
# small install of its own played through, its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/clue)
reluva_add_engine_tests(clue
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/rules.cpp
        ${tests}/unit/board.cpp
        ${tests}/unit/mansion.cpp
        ${tests}/unit/frontend.cpp
        ${tests}/unit/saves.cpp
        ${tests}/unit/network.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.formats unit.rules unit.board unit.mansion unit.frontend unit.saves unit.network engine)

# Its engine test with the game's own files (tests/engines/clue/gamedata.cmake):
# RELUVA_CLUE=<the game's folder> ctest -L gamedata. See docs/DEVELOPMENT.md
# (Engine tests).
reluva_add_gamedata_test(clue
    GAME clue
    ENV RELUVA_CLUE
    SCRIPT ${tests}/gamedata.cmake
    STEPS release files levels boards play)
