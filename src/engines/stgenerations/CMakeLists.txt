# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Star Trek: Generations (docs/engines/stgenerations/), ported from
# STG-Engine. See the main README (Adding an engine) and
# docs/ADDING_AN_ENGINE.md.
reluva_add_engine(stgenerations
    ACCESSOR stgenerationsMetaEngine
    SOURCES
        stgenerations.cpp
        detection.cpp
        audio.cpp
        canvas.cpp
        debug.cpp
        platform.cpp
        formats/bitmap.cpp
        formats/combatdata.cpp
        formats/disasm.cpp
        formats/font.cpp
        formats/framedimage.cpp
        formats/ini.cpp
        formats/level.cpp
        formats/locations.cpp
        formats/opcodes.cpp
        formats/overlay.cpp
        formats/resources.cpp
        formats/rgbimage.cpp
        formats/script.cpp
        formats/sprite.cpp
        game/actors.cpp
        game/bridge.cpp
        game/campaign.cpp
        game/combat.cpp
        game/combatrender.cpp
        game/dissolve.cpp
        game/frontend.cpp
        game/generations.cpp
        game/helditem.cpp
        game/hud.cpp
        game/hudscreen.cpp
        game/keymap.cpp
        game/mission.cpp
        game/movers.cpp
        game/optionsmenu.cpp
        game/projectiles.cpp
        game/random.cpp
        game/render3d.cpp
        game/screen.cpp
        game/sound.cpp
        game/vm.cpp
        game/walker.cpp)

# Its data viewer, the resource viewer, for reluva-viewer only: reluva, which
# players get, doesn't link it. See docs/DEVELOPMENT.md (The data viewer).
reluva_add_engine_viewers(stgenerations
    ACCESSOR stgenerationsMetaViewer
    SOURCES
        pages/meta_viewer.cpp
        pages/resource_viewer.cpp)

# Its reluva-tool commands (reluva-tool help stgenerations lists them).
reluva_add_engine_tools(stgenerations
    SOURCES
        tools/files.cpp
        tools/data.cpp
        tools/play.cpp)

# Its tests (tests/engines/stgenerations/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/stgenerations)
reluva_add_engine_tests(stgenerations
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/script.cpp
        ${tests}/unit/level.cpp
        ${tests}/unit/hud.cpp
        ${tests}/unit/combat.cpp
        ${tests}/unit/campaign.cpp
        ${tests}/unit/sound.cpp
        ${tests}/unit/canvas.cpp
        ${tests}/unit/audio.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.formats unit.script unit.level unit.hud unit.combat unit.campaign unit.sound unit.canvas unit.audio engine)

# Its engine test with the game's own files (tests/engines/stgenerations/
# gamedata.cmake): RELUVA_STGENERATIONS=<the game's folder> ctest -L
# gamedata. See docs/DEVELOPMENT.md (Engine tests).
reluva_add_gamedata_test(stgenerations
    GAME stgenerations
    ENV RELUVA_STGENERATIONS
    SCRIPT ${tests}/gamedata.cmake
    STEPS release archives pictures levels sounds movies front_end mission combat)
