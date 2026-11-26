# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Starfleet Command (docs/engines/sfc/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(sfc
    ACCESSOR sfcMetaEngine
    SOURCES
        sfc.cpp
        formats/bft_font.cpp
        formats/intro_movie.cpp
        formats/model.cpp
        formats/sprite_archive.cpp
        formats/text_tables.cpp
        formats/settings.cpp
        host/game_process.cpp
        host/patcher.cpp
        host/player.cpp
        host/system.cpp)

# Its data viewer, for reluva-viewer only: reluva, which players get,
# doesn't link it. See docs/DEVELOPMENT.md (The data viewer).
reluva_add_engine_viewers(sfc
    ACCESSOR sfcMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/font_page.cpp
        pages/image_page.cpp
        pages/model_page.cpp
        pages/sound_page.cpp
        pages/table_page.cpp)

# Its reluva-tool commands (reluva-tool help sfc lists them).
reluva_add_engine_tools(sfc
    SOURCES
        tools/files.cpp
        tools/formats.cpp
        tools/host.cpp)

# Its tests (tests/engines/sfc/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/sfc)
reluva_add_engine_tests(sfc
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/formats.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.formats engine)

# Its engine test with the game's own files (see docs/DEVELOPMENT.md,
# Engine tests, and tests/engines/sfc/gamedata.cmake).
reluva_add_gamedata_test(sfc
    GAME sfc2
    ENV RELUVA_SFC2
    SCRIPT ${tests}/gamedata.cmake
    STEPS release files formats intro host import)
