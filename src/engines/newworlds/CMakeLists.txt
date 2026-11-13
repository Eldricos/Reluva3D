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
        fif.cpp
        levels.cpp)

# Its data viewer, in reluva-viewer: the game can't be played yet, so this
# is where what the engine reads is looked at.
reluva_add_engine_viewers(newworlds
    ACCESSOR newWorldsMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/mission_viewer.cpp
        pages/model_viewer.cpp
        pages/texture_viewer.cpp
        pages/map_viewer.cpp
        pages/sound_viewer.cpp
        pages/string_viewer.cpp
        pages/font_viewer.cpp)

# Its reluva-tool commands (reluva-tool lists them).
reluva_add_engine_tools(newworlds
    SOURCES
        tools/archives.cpp
        tools/textures.cpp
        tools/models.cpp
        tools/maps.cpp
        tools/media.cpp)

# Its tests (tests/engines/newworlds/), in reluva-tests.
reluva_add_engine_tests(newworlds
    SOURCES ${PROJECT_SOURCE_DIR}/tests/engines/newworlds/unit.cpp
    TOPICS unit.archives unit.files unit.game unit.render)
