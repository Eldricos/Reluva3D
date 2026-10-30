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

reluva_add_engine_test(idtech3
    NAME idtech3
    SOURCES
        ${PROJECT_SOURCE_DIR}/tests/test_idtech3.cpp
        ${PROJECT_SOURCE_DIR}/src/backends/sockets/socket_net.cpp
    NETWORK)
