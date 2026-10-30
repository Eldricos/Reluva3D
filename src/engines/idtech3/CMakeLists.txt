# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The id Tech 3 engine: Star Trek: Voyager - Elite Force
# (docs/engines/idtech3/).
reluva_add_engine(idtech3
    ACCESSOR idTech3MetaEngine
    SOURCES
        idtech3.cpp
        games.cpp
        filesystem.cpp
        lexer.cpp
        marks.cpp
        mdr.cpp
        botlib/aas.cpp
        botlib/bot_chat.cpp
        botlib/bot_config.cpp
        botlib/bot_goals.cpp
        botlib/bot_library.cpp
        botlib/bot_move.cpp
        botlib/bot_weapons.cpp
        botlib/bot_script.cpp
        botlib/character.cpp
        player_animation.cpp
        wire/message.cpp
        wire/netchan.cpp
        info_string.cpp
        pure.cpp
        server_browser.cpp
        sound_system.cpp
        raven/ambient_sets.cpp
        entities.cpp
        shader.cpp
        bindings.cpp
        bsp.cpp
        pages/page.cpp
        pages/file_viewer.cpp
        pages/texture_viewer.cpp
        pages/map_viewer.cpp
        patch.cpp
        world_renderer.cpp
        shader_cache.cpp
        md3.cpp
        light_grid.cpp
        model_cache.cpp
        pages/model_viewer.cpp
        pages/menu_layer.cpp
        pages/menu_viewer.cpp
        pages/play_viewer.cpp
        pages/sp_play_viewer.cpp
        pages/start_page.cpp
        client/scene_drawer.cpp
        client/player_input.cpp
        client/joystick_input.cpp
        server/server_world.cpp
        qvm.cpp
        cvars.cpp
        sounds.cpp
        collision.cpp
        vm_services.cpp
        eliteforce/game_info.cpp
        eliteforce/ui_host.cpp
        eliteforce/game_host.cpp
        eliteforce/sp_game_host.cpp
        eliteforce/sp_ui_host.cpp
        raven/savegame.cpp
        eliteforce/game_bots.cpp
        eliteforce/cgame_host.cpp
        client/scene.cpp
        eliteforce/net_fields.cpp
        eliteforce/net_server.cpp
        eliteforce/net_client.cpp
        pages/world_viewer.cpp)

# Its tests (tests/engines/idtech3/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files: a small install played
# through, its record compared (data/engine-record.txt). See
# docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/idtech3)
reluva_add_engine_tests(idtech3
    SOURCES
        ${tests}/unit/formats.cpp
        ${tests}/unit/files.cpp
        ${tests}/unit/qvm.cpp
        ${tests}/unit/collision.cpp
        ${tests}/unit/sound.cpp
        ${tests}/unit/input.cpp
        ${tests}/unit/bots.cpp
        ${tests}/unit/network.cpp
        ${tests}/unit/game_code.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.formats unit.files unit.qvm unit.collision unit.sound unit.input unit.bots unit.network
        unit.game-code engine)

# The engine test with Elite Force's own files: skipped unless RELUVA_STVEF
# names the game's folder (ctest -L gamedata). See gamedata.cmake there.
reluva_add_gamedata_test(idtech3
    GAME stvef
    ENV RELUVA_STVEF
    SCRIPT ${tests}/gamedata.cmake
    STEPS release maps shaders models images holomatch menus single_player expansion cinematics)
