// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands, and helpers they share.
//
// Each file of commands registers them, with what they take and do, as a
// static object (as engines register themselves):
//
//     const reluva::tool::Commands commands("idtech3", {
//         {"bsp-check", "<game folder>", "load every map of a game folder", cmdBspCheck},
//     });
//
// The shared commands are in src/tools/; an engine's are in its tools/
// folder, listed in its CMakeLists.txt by reluva_add_engine_tools, so they
// are built only with the engine. A command is given reluva-tool's whole
// command line: argv[1] is its name, its arguments start at argv[2].

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tool {

struct Command {
    std::string name;
    std::string arguments;   // e.g. "<file> [offset] [length]"
    std::string description; // what it does, in a sentence or two
    int (*run)(int argc, char** argv);
};

struct Registered {
    std::string engine; // "" for the shared commands
    Command command;
};

// Every command registered, in no particular order.
std::vector<Registered>& registered();

// Registers a file's commands, for an engine or ("") for every one.
struct Commands {
    Commands(const std::string& engine, std::vector<Command> list);
};

using Bytes = std::vector<uint8_t>;

// A whole file, or false with a message.
bool readFile(const char* path, Bytes& out);
// Little-endian values, 0 past the end.
uint16_t rd16(const Bytes& d, size_t o);
uint32_t rd32(const Bytes& d, size_t o);
// Prints bytes as hex and text, 16 to a line.
void hexdump(const Bytes& d, size_t off, size_t len);
// A number in decimal, hex (0x..) or octal (0..).
long long parseNumber(const char* s);
// A name's extension, with its dot ("" if it has none); names may use
// backslashes.
std::string extensionOf(const std::string& name);

} // namespace reluva::tool
