# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The Sith games' game-files tests' shared step: the game run in the
# emulator (sith host), for gamedata.cmake (Jedi Knight's) and
# gamedata_mots.cmake (Mysteries of the Sith's). Needs game_dir, OUTPUT,
# STEP and record, as the helpers (cmake/ReluvaGamedata.cmake) set them.

# The game run in the emulator from a new state, for `seconds`, with the
# tool's other arguments: the lines a run decides itself, up to the first
# file opened matching `until` ("" for all of them), kept as <name>. The
# tool's whole output is left in host_log.
function(record_host name seconds until)
    set(out "${OUTPUT}/${STEP}")
    file(REMOVE_RECURSE "${out}/write")
    file(MAKE_DIRECTORY "${out}/write")
    run_tool(log sith host "${game_dir}" -seconds ${seconds} -files -write "${out}/write" ${ARGN})
    file(REMOVE_RECURSE "${out}/write")
    if(EXISTS "${out}/${name}.txt")
        file(RENAME "${out}/${name}.txt" "${out}/${name}.previous.txt")
    endif()
    file(WRITE "${out}/${name}.txt" "${log}")
    # Not the seconds a level or cut scene came at, nor how far the
    # machine's speed took the run past `until`: what's said while it runs
    # comes before the files opened, so after the level or cut scene
    # `until` names neither the steps reached nor the debug output (cut
    # scenes say theirs), only the registry's writes, the settings saved
    # as the run is stopped.
    string(REPLACE ";" "," flat "${log}")
    string(REGEX REPLACE "\n[0-9]+\\.[0-9] s: " "\n" flat "\n${flat}")
    string(REGEX MATCHALL "(debug output|registry written|cut scene|level|opened)[^\n]*" lines "${flat}")
    set(kept "")
    set(steps "")
    set(files 0)
    set(past FALSE)
    foreach(line IN LISTS lines)
        if(line MATCHES "^opened: ")
            math(EXPR files "${files} + 1")
        elseif(past AND NOT line MATCHES "^registry written")
            continue()
        elseif(line MATCHES "^(cut scene|level) ")
            if(NOT until STREQUAL "" AND line MATCHES "${until}$")
                set(past TRUE)
            else()
                list(APPEND steps "${line}")
            endif()
        endif()
        string(APPEND kept "${line}\n")
        if(NOT until STREQUAL "" AND line MATCHES "^opened: .*${until}$")
            break()
        endif()
    endforeach()
    string(SHA256 sum "${kept}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}${name}: ${sum} | ${files} files opened\n")
    foreach(step IN LISTS steps)
        set(record "${record}${name}.reached: ${step}\n")
    endforeach()
    set(record "${record}" PARENT_SCOPE)
    set(host_log "${log}" PARENT_SCOPE)
endfunction()

# Widescreen (sith host -wide): the game into its first level in a mode
# `width` by 480 (854 for 16:9, 768 for 16:10, 1120 for 21:9), kept as
# <name> as record_host would, with whether the view was widened and the
# sizes of the frames shown: the menus' and cut scenes' 640 by 480, play's
# the mode's.
function(record_wide name width seconds until)
    record_host(${name} ${seconds} "${until}" -wide ${width} ${ARGN})
    string(REGEX MATCH "widescreen: [^\n]*" widened "${host_log}")
    string(REGEX MATCH "frame sizes: [^\n]*" sizes "${host_log}")
    set(record "${record}${widened}\n${sizes}\n" PARENT_SCOPE)
endfunction()
