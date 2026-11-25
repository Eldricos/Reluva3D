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
# the engine finds it: the one holding NFS2SEN.EXE and the music). Without
# the variable the step is skipped.
#
# Each step runs reluva-tool on the game:
#   release  what the game is (detect)
#   survey   the game's files in the install and on the CD by name, size
#            and MD5, as one checksum (not the CD's REDIST and UTILITY, nor
#            the folders the game writes); and every file the engine
#            reads, read and counted (nfs survey)
#   formats  every archive, picture, string table, font, track, model,
#            sound, music section and movie frame decoded, each kind as a
#            checksum (nfs survey -hash)
#   host     the retail nfs2sen.exe run on an instruction clock (nfs host
#            -deterministic 300000): the title movie skipped, the menu
#            taken to RACE with the keys, a race at Proving Grounds with
#            the accelerator held; its end state, its system calls and the
#            last frame written, as checksums
#   host_glide  the same with the 3dfx program, nfs2sea.exe (the
#            install's, or the CD's), on Reluva's Glide stand-in as a
#            Voodoo Graphics, drawn by its software rasterizer
#   drive    nfs2sen.exe again, a knockout's first round at Proving
#            Grounds (2 laps, 9 cars) driven to its end by the road
#            driver (-roaddrive): each lap's place and the finishing
#            place, with the end state and last frame as for host
# A command that fails fails the step. The records, in
# tests/engines/nfs/data/gamedata/nfs2se-<release>/<step>.txt, hold only
# counts and checksums. The step passes when its record is the same as the
# committed one. Records so far: nfs2se-en-windows (the US CD's English
# install, made on Linux and the same on Windows).
#
# Environment: RELUVA_NFS2SE, the install; and those of the shared helpers
# (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the game's install, its CD's files in a folder beside it")

# The CD's files: beside the install, as the engine looks for them.
find_cd_beside(cd_dir "${game_dir}" "nfs2sen.exe" "GameData/Audio/pc" ".mus")

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

# The hosted game from its title movie into a race at Proving Grounds, on
# a clock of instructions: Enter skips the movie, four Downs and Enter
# start the race, Up held from 40 s
# (docs/engines/nfs/games/nfs2se/research/hosted-spike.md); or the keys
# in host_keys. ARGN: the program, the limit and which frames are
# written. Its end state, its
# system calls and the last frame written go into the record as
# checksums.
function(record_host name)
    set(out "${OUTPUT}/${STEP}")
    file(REMOVE_RECURSE "${out}/save" "${out}/frames")
    file(MAKE_DIRECTORY "${out}/save" "${out}/frames")
    # The run ends at the instruction limit, which the tool reports as a
    # failure (1); anything else fails the step.
    execute_process(COMMAND "${TOOL}" nfs host "${game_dir}" -save "${out}/save" -deterministic 300000
                    -frames "${out}/frames" ${ARGN}
                    -keys "${host_keys}"
                    OUTPUT_VARIABLE log ERROR_VARIABLE log RESULT_VARIABLE rc TIMEOUT 3000)
    string(REPLACE "${game_dir}" "<game>" log "${log}")
    if(NOT cd_dir STREQUAL "")
        string(REPLACE "${cd_dir}" "<cd>" log "${log}")
    endif()
    string(REPLACE "${OUTPUT}" "<output>" log "${log}")
    if(NOT (rc EQUAL 0 OR (rc EQUAL 1 AND log MATCHES "stopped: instruction limit reached")))
        message(FATAL_ERROR "reluva-tool nfs host failed (${rc}):\n${log}")
    endif()
    file(WRITE "${out}/${name}.txt" "${log}")
    # What doesn't change from run to run: not the time it took, nor the
    # idle time, which is the host's own.
    string(REPLACE ";" "," flat "${log}")
    string(REGEX MATCHALL "(state|stopped|system calls)[^\n]*" kept "${flat}")
    list(JOIN kept "\n" kept)
    string(SHA256 sum "${kept}")
    string(SUBSTRING "${sum}" 0 16 sum)
    string(REGEX MATCH "state: [0-9a-f]+ after [0-9]+ instructions" state "${log}")
    set(record "${record}${name}: ${sum} | ${state}\n")
    # The road driver's laps and places, as it reports them.
    string(REGEX MATCHALL "road driver: lap [^\n]*" laps "${flat}")
    foreach(lap IN LISTS laps)
        set(record "${record}${name}.${lap}\n")
    endforeach()
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

set(host_keys "6000:enter,14000:down,14500:down,15000:down,15500:down,17000:enter,40000:+up")

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
    if(NOT cd_dir)
        set(record "${record}cd: none beside the install\n")
    else()
        set(record "${record}cd: beside the install\n")
    endif()
elseif(STEP STREQUAL "survey")
    # The game's files by their path, size and MD5, as one checksum: not
    # the CD's set-up extras (REDIST, DirectX's installers; UTILITY), which
    # a copy of the CD may leave out, nor the folders the game writes as
    # it's played (its settings, records, saves and the track it copies to
    # race). The listing is left in the step's output folder (files.txt),
    # to compare with another copy's.
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
            string(TOLOWER "${rel}" lower)
            if(lower MATCHES "^(redist|utility)/"
               OR lower MATCHES "(^|/)fedata/pc/(config|stats|save)/"
               OR lower MATCHES "(^|/)gamedata/(prefs|tmptrk)/")
                continue()
            endif()
            file(SIZE "${f}" size)
            file(MD5 "${f}" md5)
            string(APPEND listing "${lower} ${size} ${md5}\n")
            math(EXPR count "${count} + 1")
        endforeach()
    endforeach()
    file(MAKE_DIRECTORY "${OUTPUT}/${STEP}")
    file(WRITE "${OUTPUT}/${STEP}/files.txt" "${listing}")
    string(SHA256 sum "${listing}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}files: ${sum} | ${count} files\n")
    record_run(survey "music segments" nfs survey "${game_dir}" ${cd_dir})
elseif(STEP STREQUAL "formats")
    record_run(formats ".dct frames" nfs survey "${game_dir}" ${cd_dir} -hash)
elseif(STEP STREQUAL "host")
    record_host(host -limit 24000000000 -framesfrom 2000 -every 100)
elseif(STEP STREQUAL "host_glide")
    # The 3dfx program, the install's or else the CD's.
    set(program "")
    foreach(candidate IN ITEMS "${game_dir}/nfs2sea.exe" "${game_dir}/NFS2SEA.EXE" "${cd_dir}/NFS2SEA.EXE"
                               "${cd_dir}/nfs2sea.exe")
        if(NOT program AND EXISTS "${candidate}")
            set(program "${candidate}")
        endif()
    endforeach()
    if(NOT program)
        message(FATAL_ERROR "No nfs2sea.exe in the install or on the CD")
    endif()
    record_host(host_glide -program "${program}" -limit 16000000000 -every 50)
elseif(STEP STREQUAL "drive")
    # Enter skips the movie, Enter opens game setup, Down and Right twice
    # make the race type a knockout, four Downs and Enter are DONE, two
    # Downs and Enter RACE; the road driver from the start, round the
    # road's middle (docs/engines/nfs/games/nfs2se/research/road-driver.md).
    # The race is over by 170 s of the game's clock.
    set(host_keys "6000:enter,14000:enter,15000:down,15700:right,16400:right,17100:down,17800:down,18500:down,19200:down,19900:enter,22000:down,22700:down,23400:enter")
    record_host(drive -limit 60000000000 -every 1000 -roaddrive 0:999999999:0:3)
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
