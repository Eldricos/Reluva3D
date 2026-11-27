# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Star Trek: Away Team (docs/engines/stawayteam/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(stawayteam
    ACCESSOR stawayteamMetaEngine
    SOURCES
        stawayteam.cpp
        formats/binary_object.cpp
        formats/class_table.cpp
        formats/frame.cpp
        formats/object_schema.cpp
        ui/movies.cpp
        ui/game_hud.cpp
        ui/main_menu.cpp
        ui/map_combat.cpp
        ui/map_enemies.cpp
        ui/map_gadgets.cpp
        ui/map_interface.cpp
        ui/map_items.cpp
        ui/map_meld.cpp
        ui/map_particles.cpp
        ui/map_mission.cpp
        ui/map_noise.cpp
        ui/map_screen.cpp
        ui/net_menus.cpp
        ui/save_menu.cpp
        ui/map_triggers.cpp
        ui/map_vision.cpp
        ui/map_state.cpp
        ui/music.cpp
        ui/objectives_window.cpp
        ui/settings.cpp
        ui/sound_options.cpp
        ui/static_text.cpp
        ui/animation.cpp
        ui/briefing.cpp
        ui/debriefing.cpp
        ui/escape_menu.cpp
        ui/team_selection.cpp
        game/campaign.cpp
        game/ground_fractal.cpp
        game/controls.cpp
        game/options.cpp
        game/map.cpp
        game/netplay.cpp
        game/particles.cpp
        game/state_io.cpp
        game/objectives.cpp
        game/team.cpp
        game/water_warp.cpp
        ui/button.cpp
        ui/credits.cpp
        ui/difficulty.cpp
        ui/font.cpp
        ui/key_config.cpp
        ui/layout.cpp
        ui/widgets.cpp
        formats/rds_script.cpp
        formats/text_object.cpp)

# Its data viewer, in reluva-viewer (never in reluva): docs/DEVELOPMENT.md.
reluva_add_engine_viewers(stawayteam
    ACCESSOR stawayteamMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/animation.cpp
        pages/draw.cpp
        pages/file_list.cpp
        pages/font_viewer.cpp
        pages/model_viewer.cpp
        pages/object_viewer.cpp
        pages/picture_viewer.cpp
        pages/sequence_viewer.cpp
        pages/sound_viewer.cpp)

# Its reluva-tool commands (reluva-tool help stawayteam lists them).
reluva_add_engine_tools(stawayteam
    SOURCES
        tools/archive.cpp
        tools/files.cpp
        tools/images.cpp
        tools/objects.cpp
        tools/text.cpp)

# Its tests (tests/engines/stawayteam/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/stawayteam)
reluva_add_engine_tests(stawayteam
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/maps.cpp
        ${tests}/unit/menus.cpp
        ${tests}/unit/network.cpp
        ${tests}/unit/particles.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.formats unit.maps unit.menus unit.network unit.particles engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(stawayteam
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
