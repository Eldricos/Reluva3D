# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Need for Speed III's engine test with the game's own files: one step each
# time ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/nfs/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/nfs/games/nfs3/TESTING.md.
#
# The game's install comes from the environment variable named by GAME_ENV
# (RELUVA_NFS3), with a copy of its CD's files beside it as NFS3-CD (as the
# engine finds it). Without the variable the step is skipped.
#
# Each step runs reluva-tool on the game:
#   release    what the game is (detect)
#   survey     every file of the install and the CD by name, size and MD5,
#              as one checksum; and every file the engine reads, read and
#              counted (nfs survey)
#   formats    every archive, picture, string table, font, model, track,
#              collision file, sound, music section and movie frame
#              decoded, each kind as a checksum (nfs survey -hash)
#   host       the retail game run on an instruction clock (nfs host
#              -deterministic 50000 -nomovie), natively drawn, from its menus
#              into a Hometown race by clicks: its end state, its system
#              calls and the last frame written, as checksums
#   host_glide the same with the game's own Voodoo 2 renderer on the Glide
#              stand-in (-glide)
# A command that fails fails the step. The records, in
# tests/engines/nfs/data/gamedata/<release>/<step>.txt, hold only counts and
# checksums. The step passes when its record is the same as the committed
# one. Records so far: classics (the US "Need for Speed III Classics" CD's
# install, nfs3.exe file version 27, 2.0, made on Linux).
#
# Environment: RELUVA_NFS3, the install; and those of the shared helpers
# (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the game's install, its CD's files beside it as NFS3-CD")

# The CD's files: beside the install, as the engine looks for them.
get_filename_component(parent "${game_dir}" DIRECTORY)
find_folder(cd_dir "${parent}" "NFS3-CD")

# What the game is: the record's folder is named after the release.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "nfs/nfs3: Need for Speed III: Hot Pursuit \\(([^)]*)\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Need for Speed III isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
string(TOLOWER "${release}" slug)
string(REPLACE " " "-" slug "${slug}")

# The hosted game into a Hometown race: clicks through the menus to a
# Single Race, at given times on the game's clock (instructions / 50000).
function(record_host name)
    set(out "${OUTPUT}/${STEP}")
    file(REMOVE_RECURSE "${out}/save" "${out}/frames")
    file(MAKE_DIRECTORY "${out}/save" "${out}/frames")
    # The run ends at the instruction limit, which the tool reports as a
    # failure (1); anything else fails the step.
    execute_process(COMMAND "${TOOL}" nfs host "${game_dir}" -args "nfs3.exe -nomovie" -save "${out}/save"
                    -deterministic 50000 -limit 3500000000 -frames "${out}/frames" -framesfrom 1500 -every 100
                    -keys "16000:click,24000:click,32000:move0/240,36000:click" ${ARGN}
                    OUTPUT_VARIABLE log ERROR_VARIABLE log RESULT_VARIABLE rc TIMEOUT 1800)
    string(REPLACE "${game_dir}" "<game>" log "${log}")
    string(REPLACE "${OUTPUT}" "<output>" log "${log}")
    if(NOT (rc EQUAL 0 OR (rc EQUAL 1 AND log MATCHES "stopped: instruction limit reached")))
        message(FATAL_ERROR "reluva-tool nfs host ${ARGN} failed (${rc}):\n${log}")
    endif()
    file(WRITE "${out}/${name}.txt" "${log}")
    # What doesn't change from run to run: not the time it took, nor the
    # idle time, which is the host's own.
    string(REPLACE ";" "," flat "${log}")
    string(REGEX MATCHALL "(state|stopped|THRASH[^\n]*frames|system calls|  calls)[^\n]*" kept "${flat}")
    list(JOIN kept "\n" kept)
    string(SHA256 sum "${kept}")
    string(SUBSTRING "${sum}" 0 16 sum)
    string(REGEX MATCH "state: [0-9a-f]+ after [0-9]+ instructions" state "${log}")
    string(REGEX MATCH "[0-9]+ frames, [0-9]+ textures" frames "${log}")
    set(record "${record}${name}: ${sum} | ${state}, ${frames}\n")
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
    set(record "${record}${name}.frame: ${frame} | ${leaf}\n")
    file(REMOVE_RECURSE "${out}/frames" "${out}/save")
    set(record "${record}" PARENT_SCOPE)
endfunction()

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
    record_run(formats "movie frames" nfs survey "${game_dir}" ${cd_dir} -hash)
elseif(STEP STREQUAL "host")
    record_host(host -native)
elseif(STEP STREQUAL "host_glide")
    record_host(host_glide -glide)
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
