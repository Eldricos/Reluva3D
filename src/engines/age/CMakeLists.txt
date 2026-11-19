# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The Angel Game Engine: Midtown Madness 2 (docs/engines/age/). See
# docs/ADDING_AN_ENGINE.md.
reluva_add_engine(age
    ACCESSOR ageMetaEngine
    SOURCES
        age.cpp
        filesystem.cpp
        games.cpp
        audio/ambient.cpp
        audio/engine_sound.cpp
        audio/music.cpp
        formats/aimap.cpp
        formats/ar_archive.cpp
        formats/bai.cpp
        formats/bound.cpp
        formats/instances.cpp
        formats/lang.cpp
        formats/materials.cpp
        formats/pathset.cpp
        formats/pkg.cpp
        formats/psdl.cpp
        formats/pvs.cpp
        formats/race.cpp
        formats/replay.cpp
        formats/texture.cpp
        formats/tune.cpp
        game/car.cpp
        game/city_collision.cpp
        game/city_life.cpp
        game/collision.cpp
        game/cops_robbers.cpp
        game/damage.cpp
        game/driving.cpp
        game/gizmos.cpp
        game/lesson.cpp
        game/net_session.cpp
        game/opponent.cpp
        game/options.cpp
        game/pedestrians.cpp
        game/police.cpp
        game/props.cpp
        game/race_cruise.cpp
        game/race_net.cpp
        game/race_replay.cpp
        game/race_screen.cpp
        game/record.cpp
        game/road_graph.cpp
        game/traffic.cpp
        game/traffic_lights.cpp
        render/car_actor.cpp
        render/city_mesh.cpp
        render/city_scene.cpp
        render/dashboard.cpp
        render/ped_model.cpp
        render/render_cache.cpp
        ui/front_end.cpp
        ui/game_menus.cpp
        ui/game_menus_drivers.cpp
        ui/help_viewer.cpp
        ui/hud_map.cpp
        ui/hud_pictures.cpp
        ui/net_menus.cpp
        ui/pad_input.cpp
        ui/race_menus.cpp
        ui/screen.cpp
        ui/view_controls.cpp)

# Its data viewer (a cruise round the cities, the cities flown through,
# the models, textures and files, beside the race screen), in
# reluva-viewer.
reluva_add_engine_viewers(age
    ACCESSOR ageMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/drive_viewer.cpp
        pages/file_viewer.cpp
        pages/model_viewer.cpp
        pages/texture_viewer.cpp
        pages/world_viewer.cpp)

# Its reluva-tool commands (reluva-tool help age lists them).
reluva_add_engine_tools(age
    SOURCES
        tools/files.cpp
        tools/play.cpp)

# Its tests (tests/engines/age/), in reluva-tests: the unit tests by topic,
# on files built in the tests, and the engine test, without game files (a
# small install of its own run as a game and in the data viewer; its
# record in data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/age)
reluva_add_engine_tests(age
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/driving.cpp
        ${tests}/unit/city.cpp
        ${tests}/unit/game.cpp
        ${tests}/unit/network.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.formats unit.driving unit.city unit.game unit.network engine)

# Its engine test with the game's own files (tests/engines/age/gamedata.cmake):
# RELUVA_MM2=<the game's folder> ctest -L gamedata. See docs/DEVELOPMENT.md
# (Engine tests).
reluva_add_gamedata_test(age
    GAME mm2
    ENV RELUVA_MM2
    SCRIPT ${tests}/gamedata.cmake
    STEPS release archives files play viewer)
