# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Star Trek: New Worlds (docs/engines/newworlds/). Two of its files hold
# string literals longer than Visual Studio accepts (error C2026), so
# engines.cfg leaves it disabled for now.
reluva_add_engine(newworlds
    ACCESSOR newWorldsMetaEngine
    SOURCES
        newworlds.cpp
        cat_archive.cpp
        string_table.cpp
        texture.cpp
        geo.cpp
        gmf.cpp
        model_textures.cpp
        ground_textures.cpp
        mdl.cpp
        arf.cpp
        map_viewer.cpp
        mission_viewer.cpp
        briefing.cpp
        mission_script.cpp
        mission_scripts.cpp
        game_ui.cpp
        colony.cpp
        model_render.cpp
        star_system.cpp
        world.cpp
        model_pose.cpp
        economy.cpp
        model_viewer.cpp
        texture_viewer.cpp
        sound_viewer.cpp
        string_viewer.cpp
        fif.cpp
        font_viewer.cpp
        levels.cpp)

reluva_add_engine_test(newworlds
    NAME newworlds_cat
    SOURCES ${PROJECT_SOURCE_DIR}/tests/test_newworlds_cat.cpp)
