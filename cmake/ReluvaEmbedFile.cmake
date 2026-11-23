# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Run by reluva_embed (ReluvaEmbed.cmake) as `cmake -P`: writes IN's bytes
# to OUT as the C++ array SYMBOL, with its size.
file(READ ${IN} hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE ${OUT}
     "// Made by cmake/ReluvaEmbedFile.cmake from ${IN}; not to be edited.\n"
     "#include <cstddef>\n"
     "namespace reluva::embedded {\n"
     "extern const unsigned char ${SYMBOL}[];\n"
     "extern const std::size_t ${SYMBOL}Size;\n"
     "const unsigned char ${SYMBOL}[] = {${bytes}};\n"
     "const std::size_t ${SYMBOL}Size = ${size};\n"
     "} // namespace reluva::embedded\n")
