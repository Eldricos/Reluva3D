# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Star Trek: Birth of the Federation (docs/engines/botf/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(botf
    ACCESSOR botfMetaEngine
    SOURCES
        botf.cpp
        game_files.cpp
        menu_audio.cpp
        formats/alt_res.cpp
        formats/animation.cpp
        formats/planet_populations.cpp
        formats/data_table.cpp
        formats/font.cpp
        formats/lexicon.cpp
        formats/menu_file.cpp
        formats/save_game.cpp
        formats/save_writer.cpp
        formats/snd_archive.cpp
        formats/text_table.cpp
        formats/window_layout.cpp
        game/game_state.cpp
        ui/assets.cpp
        ui/empire_screen.cpp
        ui/fame_screen.cpp
        ui/galaxy_screen.cpp
        ui/game_menu.cpp
        ui/tool_tips.cpp
        ui/layout_screen.cpp
        ui/load_save_screen.cpp
        ui/main_menu.cpp
        ui/multiplayer_screen.cpp
        ui/options_screen.cpp
        ui/setup_screen.cpp
        ui/timers.cpp)

# Its data viewer, for reluva-viewer only: reluva, which players get,
# doesn't link it. See docs/DEVELOPMENT.md (The data viewer).
reluva_add_engine_viewers(botf
    ACCESSOR botfMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/data_viewer.cpp
        pages/font_viewer.cpp
        pages/image_viewer.cpp
        pages/layout_viewer.cpp
        pages/sound_viewer.cpp
        pages/text_viewer.cpp)

# Its reluva-tool commands (reluva-tool help botf lists them).
reluva_add_engine_tools(botf
    SOURCES
        tools/files.cpp
        tools/archives.cpp)

# Its tests (tests/engines/botf/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/botf)
reluva_add_engine_tests(botf
    SOURCES
        ${tests}/unit/helpers.cpp
        ${tests}/unit/detection.cpp
        ${tests}/unit/archives.cpp
        ${tests}/unit/saving.cpp
        ${tests}/unit/fonts.cpp
        ${tests}/unit/tables.cpp
        ${tests}/unit/layouts.cpp
        ${tests}/unit/screens.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.archives unit.saving unit.fonts unit.tables unit.layouts unit.screens engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(botf
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
