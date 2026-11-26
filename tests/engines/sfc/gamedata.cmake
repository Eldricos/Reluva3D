# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Starfleet Command II's engine test with the game's own files: one step
# each time ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/sfc/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/sfc/games/sfc2/TESTING.md.
#
# The game's folder (the one holding StarFleet2.exe) comes from the
# environment variable named by GAME_ENV (RELUVA_SFC2). Without it the step
# is skipped.
#
# Each step runs reluva-tool on the game:
#   release  what the game is (detect)
#   files    every file by name, size and MD5, as one checksum
#   formats  every picture in sprites.q3 decoded and its objects found,
#            every font, model, sound, settings file and campaign map read
#            (sfc sprites, fonts, models, sounds, settings, maps)
#   intro    the opening movie decoded from SFC2Intro.exe, and one of its
#            frames (sfc intro)
#   host     StarFleet2.exe run on a fixed clock to its CD key dialog, a
#            made-up key typed and refused: the frames shown, where it
#            stopped, and the dialog before and after (sfc host)
#   import   the game imported from its two CDs (import, from the folder
#            RELUVA_SFC2_CDS names), each file compared with the install's
# A command that fails fails the step. The records, in
# tests/engines/sfc/data/gamedata/<release>/<step>.txt, hold only counts and
# checksums. The step passes when its record is the same as the committed
# one. Records so far: english-1.0 (an install by the CDs' setup, made on
# Linux).
#
# Environment: RELUVA_SFC2, the game's folder; RELUVA_SFC2_CDS, a folder
# holding its two CDs, each in a folder of its own (the import step); and
# those of the shared helpers (cmake/ReluvaGamedata.cmake):
# RELUVA_GAMEDATA_UPDATE=1, RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the one holding StarFleet2.exe")

# What the game is: the record's folder is named after the release.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "sfc/sfc2: Star Trek: Starfleet Command II \\(([^)]*)\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Starfleet Command II isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
if(release STREQUAL "1.0")
    set(slug "english-1.0")
else()
    string(TOLOWER "${release}" slug)
    string(REPLACE " " "-" slug "${slug}")
endif()

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
elseif(STEP STREQUAL "files")
    # Every file by its path, size and MD5, as one checksum.
    file(GLOB_RECURSE files LIST_DIRECTORIES false "${game_dir}/*")
    list(SORT files)
    set(listing "")
    set(count 0)
    foreach(f IN LISTS files)
        file(RELATIVE_PATH rel "${game_dir}" "${f}")
        file(SIZE "${f}" size)
        file(MD5 "${f}" md5)
        string(APPEND listing "${rel} ${size} ${md5}\n")
        math(EXPR count "${count} + 1")
    endforeach()
    string(SHA256 sum "${listing}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}files: ${sum} | ${count} files\n")
elseif(STEP STREQUAL "formats")
    record_run(sprites "images" sfc sprites "${game_dir}")
    record_run(fonts "fonts," sfc fonts "${game_dir}")
    record_run(models "models," sfc models "${game_dir}")
    record_run(sounds "failed" sfc sounds "${game_dir}")
    record_run(settings "settings files" sfc settings "${game_dir}")
    record_run(maps "maps," sfc maps "${game_dir}")
elseif(STEP STREQUAL "intro")
    set(out "${OUTPUT}/${STEP}")
    file(MAKE_DIRECTORY "${out}")
    record_run(intro "frames decoded" sfc intro "${game_dir}" -frame 1000 "${out}/frame-1000.png")
    record_file(frame-1000 "${out}/frame-1000.png")
elseif(STEP STREQUAL "host")
    # The CD key dialog by frame 590; then a key no copy has, typed into its
    # box and entered, refused by frame 900. The program keeps what it
    # writes in a save folder of the test's own, not the install.
    set(out "${OUTPUT}/${STEP}")
    file(REMOVE_RECURSE "${out}")
    file(MAKE_DIRECTORY "${out}/save")
    execute_process(COMMAND "${TOOL}" sfc host "${game_dir}" -quiet -fixed-clock -save "${out}/save"
                            -limit 2000000000 -shot 590 "${out}/dialog.png" -click 600 395 370
                            -type 620 AAAA-BBBB-CCCC-DDDD -click 700 395 405 -shot 900 "${out}/refused.png"
                    OUTPUT_VARIABLE log ERROR_VARIABLE log RESULT_VARIABLE rc TIMEOUT 1800)
    string(REPLACE "${game_dir}" "<game>" log "${log}")
    string(REPLACE "${OUTPUT}" "<output>" log "${log}")
    # The run ends at the instruction limit, which the tool reports as a
    # failure (1); anything else fails the step.
    if(NOT (rc EQUAL 0 OR (rc EQUAL 1 AND log MATCHES "stopped: instruction limit reached")))
        message(FATAL_ERROR "reluva-tool sfc host failed (${rc}):\n${log}")
    endif()
    file(WRITE "${out}/host.txt" "${log}")
    # What doesn't change from run to run: not the time it took.
    string(REGEX REPLACE " in [0-9.]+ s \\([0-9.]+ million a second\\)" "" log "${log}")
    string(REPLACE ";" "," flat "${log}")
    string(REGEX MATCHALL "(first frame|stopped|in StarFleet2|[0-9]+ exceptions|[0-9]+ instructions)[^\n]*" kept
           "${flat}")
    list(JOIN kept "\n" kept)
    string(SHA256 sum "${kept}")
    string(SUBSTRING "${sum}" 0 16 sum)
    string(REGEX MATCH "[0-9]+ frames shown" frames "${log}")
    set(record "${record}host: ${sum} | ${frames}\n")
    record_file(dialog "${out}/dialog.png")
    record_file(refused "${out}/refused.png")
    file(REMOVE_RECURSE "${out}/save")
elseif(STEP STREQUAL "import")
    # From the CDs (RELUVA_SFC2_CDS: a folder holding both, each in a
    # folder of its own), the one with data1.hdr planned and the other
    # added, as the launcher's "Add other disc..." does.
    set(cds "$ENV{${GAME_ENV}_CDS}")
    if(cds STREQUAL "")
        message("SKIPPED: set ${GAME_ENV}_CDS to a folder holding the game's two CDs to run this step")
        return()
    endif()
    file(REAL_PATH "${cds}" cds)
    file(GLOB discs LIST_DIRECTORIES true "${cds}/*")
    set(first "")
    set(others "")
    foreach(disc IN LISTS discs)
        file(GLOB header "${disc}/[Dd][Aa][Tt][Aa]1.[Hh][Dd][Rr]")
        if(header AND first STREQUAL "")
            set(first "${disc}")
        elseif(IS_DIRECTORY "${disc}")
            list(APPEND others -patch "${disc}")
        endif()
    endforeach()
    if(first STREQUAL "")
        message(FATAL_ERROR "No CD with data1.hdr in ${cds}")
    endif()
    set(into "${OUTPUT}/import")
    file(REMOVE_RECURSE "${into}")
    run_tool(out import "${first}" "${into}" ${others})
    string(REPLACE "${cds}" "<cds>" out "${out}")
    string(REGEX REPLACE "\n *[0-9]+%" "" out "${out}")
    string(SHA256 sum "${out}")
    string(SUBSTRING "${sum}" 0 16 sum)
    string(REGEX MATCH "sfc/sfc2: [^\n]*" found "${out}")
    set(record "${record}import: ${sum} | ${found}\n")
    # Each file as the CDs' own setup installed it, its name matched
    # ignoring case (Windows doesn't mind it).
    file(GLOB_RECURSE installed RELATIVE "${game_dir}" "${game_dir}/*")
    foreach(name IN LISTS installed)
        string(TOLOWER "${name}" lower)
        set("installed:${lower}" "${name}")
    endforeach()
    file(GLOB_RECURSE imported RELATIVE "${into}" "${into}/*")
    list(SORT imported)
    list(LENGTH imported count)
    set(same 0)
    foreach(name IN LISTS imported)
        string(TOLOWER "${name}" lower)
        set(key "installed:${lower}")
        if(DEFINED "${key}")
            file(SHA256 "${into}/${name}" a)
            file(SHA256 "${game_dir}/${${key}}" b)
            if(a STREQUAL b)
                math(EXPR same "${same} + 1")
            else()
                set(record "${record}different from the install: ${name}\n")
            endif()
        else()
            set(record "${record}not in the install: ${name}\n")
        endif()
    endforeach()
    set(record "${record}the same as the install: ${same} of ${count} files\n")
    file(REMOVE_RECURSE "${into}")
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
