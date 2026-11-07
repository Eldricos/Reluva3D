# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The NetImmerse engine: LEGO Alpha Team (docs/engines/netimmerse/). The
# game's own program and DLLs run in the x86 emulator (src/common/x86/);
# without them, its data viewer. See docs/ADDING_AN_ENGINE.md.
reluva_add_engine(netimmerse
    ACCESSOR netImmerseMetaEngine
    SOURCES
        alphateam/game_process.cpp
        alphateam/player.cpp
        data.cpp
        ddf.cpp
        fin.cpp
        model_page.cpp
        netimmerse.cpp
        pac_archive.cpp
        viewers.cpp)

# Its reluva-tool commands (reluva-tool help netimmerse lists them).
reluva_add_engine_tools(netimmerse
    SOURCES
        tools/archives.cpp
        tools/run.cpp)

# Its tests (tests/engines/netimmerse/), in reluva-tests: the unit tests
# on files built in the tests, and the engine test, without game files (a
# small install of its own shown in the data viewer, its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/netimmerse)
reluva_add_engine_tests(netimmerse
    SOURCES
        ${tests}/unit/formats.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.formats engine)

# Its engine test with the game's own files (tests/engines/netimmerse/
# gamedata.cmake): RELUVA_ALPHATEAM=<the game's folder> ctest -L gamedata.
# See docs/engines/netimmerse/TESTING.md.
reluva_add_gamedata_test(netimmerse
    GAME alphateam
    ENV RELUVA_ALPHATEAM
    SCRIPT ${tests}/gamedata.cmake
    STEPS release archives models start mission-one mission-one-1024)
