# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_embed(target symbol file): builds a file's bytes into a target, as
#   namespace reluva::embedded {
#   extern const unsigned char <symbol>[];
#   extern const std::size_t <symbol>Size;
#   }
# made again when the file changes. For data Reluva itself ships (fonts),
# never a game's.
set(RELUVA_EMBED_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/ReluvaEmbedFile.cmake)

function(reluva_embed target symbol file)
    set(out ${CMAKE_CURRENT_BINARY_DIR}/embedded/${symbol}.cpp)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -DIN=${file} -DOUT=${out} -DSYMBOL=${symbol} -P ${RELUVA_EMBED_SCRIPT}
        DEPENDS ${file} ${RELUVA_EMBED_SCRIPT}
        COMMENT "Embedding ${file}"
        VERBATIM)
    target_sources(${target} PRIVATE ${out})
endfunction()
