# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Need for Speed II SE's engine test with the game's own files: one step
# each time ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/nfs/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/nfs/games/nfs2se/TESTING.md.
#
# The game's install comes from the environment variable named by GAME_ENV
# (RELUVA_NFS2SE), with a copy of its CD's files in a folder beside it (as
# the engine finds it: the one holding NFS2SEN.EXE). Without the variable
# the step is skipped.
#
# Each step runs reluva-tool on the game:
#   release  what the game is (detect)
#   survey   every file of the install and the CD by name, size and MD5,
#            as one checksum; and every file the engine reads, read and
#            counted (nfs survey)
#   formats  every archive, picture, string table, font, track, model,
#            sound, music section and movie frame decoded, each kind as a
#            checksum (nfs survey -hash)
#   host     the retail nfs2sen.exe run on an instruction clock (nfs host
#            -deterministic 300000): the title movie skipped, the menu
#            taken to RACE with the keys, a race at Proving Grounds with
#            the accelerator held; its end state, its system calls and the
#            last frame written, as checksums
# A command that fails fails the step. The records, in
# tests/engines/nfs/data/gamedata/nfs2se-<release>/<step>.txt, hold only
# counts and checksums. The step passes when its record is the same as the
# committed one. Records so far: nfs2se-en-windows (the US CD's English
# install, made on Linux).
#
# Environment: RELUVA_NFS2SE, the install; and those of the shared helpers
# (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the game's install, its CD's files in a folder beside it")

# The CD's files: the folder beside the install with the CD's program.
get_filename_component(parent "${game_dir}" DIRECTORY)
set(cd_dir "")
file(GLOB siblings LIST_DIRECTORIES true "${parent}/*")
foreach(sibling IN LISTS siblings)
    if(IS_DIRECTORY "${sibling}" AND NOT sibling STREQUAL game_dir
       AND (EXISTS "${sibling}/NFS2SEN.EXE" OR EXISTS "${sibling}/nfs2sen.exe"))
        set(cd_dir "${sibling}")
    endif()
endforeach()

# What the game is: the record's folder is named after the release.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "nfs/nfs2se: Need for Speed II SE \\(([^)]*)\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Need for Speed II SE isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
string(TOLOWER "${release}" slug)
string(REGEX REPLACE "[^a-z0-9]+" "-" slug "${slug}")
set(slug "nfs2se-${slug}")

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
    if(NOT cd_dir)
        set(record "${record}cd: none beside the install\n")
    else()
        set(record "${record}cd: beside the install\n")
    endif()
elseif(STEP STREQUAL "survey")
    # Every file by its path, size and MD5, as one checksum.
    set(listing "")
    set(count 0)
    foreach(root IN ITEMS "${game_dir}" "${cd_dir}")
        if(root STREQUAL "")
            continue()
        endif()
        file(GLOB_RECURSE files LIST_DIRECTORIES false "${root}/*")
        list(SORT files)
        foreach(f IN LISTS files)
            file(RELATIVE_PATH rel "${root}" "${f}")
            file(SIZE "${f}" size)
            file(MD5 "${f}" md5)
            string(APPEND listing "${rel} ${size} ${md5}\n")
            math(EXPR count "${count} + 1")
        endforeach()
    endforeach()
    string(SHA256 sum "${listing}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}files: ${sum} | ${count} files\n")
    record_run(survey "music segments" nfs survey "${game_dir}" ${cd_dir})
elseif(STEP STREQUAL "formats")
    record_run(formats ".dct frames" nfs survey "${game_dir}" ${cd_dir} -hash)
elseif(STEP STREQUAL "host")
    set(out "${OUTPUT}/${STEP}")
    file(REMOVE_RECURSE "${out}/save" "${out}/frames")
    file(MAKE_DIRECTORY "${out}/save" "${out}/frames")
    # Enter skips the title movie; four Downs and Enter start the race
    # (docs/engines/nfs/games/nfs2se/research/hosted-spike.md). The run
    # ends at the instruction limit, which the tool reports as a failure
    # (1); anything else fails the step.
    execute_process(COMMAND "${TOOL}" nfs host "${game_dir}" -save "${out}/save" -deterministic 300000
                    -limit 24000000000 -frames "${out}/frames" -framesfrom 20000 -every 500
                    -keys "6000:enter,14000:down,14500:down,15000:down,15500:down,17000:enter,40000:+up"
                    OUTPUT_VARIABLE log ERROR_VARIABLE log RESULT_VARIABLE rc TIMEOUT 1800)
    string(REPLACE "${game_dir}" "<game>" log "${log}")
    string(REPLACE "${OUTPUT}" "<output>" log "${log}")
    if(NOT (rc EQUAL 0 OR (rc EQUAL 1 AND log MATCHES "stopped: instruction limit reached")))
        message(FATAL_ERROR "reluva-tool nfs host failed (${rc}):\n${log}")
    endif()
    file(WRITE "${out}/host.txt" "${log}")
    # What doesn't change from run to run: not the time it took, nor the
    # idle time, which is the host's own.
    string(REPLACE ";" "," flat "${log}")
    string(REGEX MATCHALL "(state|stopped|system calls)[^\n]*" kept "${flat}")
    list(JOIN kept "\n" kept)
    string(SHA256 sum "${kept}")
    string(SUBSTRING "${sum}" 0 16 sum)
    string(REGEX MATCH "state: [0-9a-f]+ after [0-9]+ instructions" state "${log}")
    set(record "${record}host: ${sum} | ${state}\n")
    # The last frame written (in the race), then the frames go.
    file(GLOB written "${out}/frames/*.tga")
    list(SORT written)
    if(NOT written)
        message(FATAL_ERROR "nfs host wrote no frames into ${out}/frames")
    endif()
    list(GET written -1 last)
    get_filename_component(leaf "${last}" NAME_WE)
    file(SHA256 "${last}" frame)
    string(SUBSTRING "${frame}" 0 16 frame)
    set(record "${record}host.frame: ${frame} | ${leaf}\n")
    file(REMOVE_RECURSE "${out}/frames" "${out}/save")
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
