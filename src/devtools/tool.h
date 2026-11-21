// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands, and helpers they share.
//
// Each file of commands registers them, with what they take and do, as a
// static object (as engines register themselves):
//
//     const reluva::tool::Commands kCommands("idtech3", {
//         {"bsp-check", "<game folder>", "load every map of a game folder", cmdBspCheck},
//     });
//
// The shared commands are in src/devtools/; an engine's are in its tools/
// folder, listed in its CMakeLists.txt by reluva_add_engine_tools, so they
// are built only with the engine. A command is given reluva-tool's command
// line from its name on (without an engine's name before it): argv[1] is
// its name, its arguments start at argv[2].

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tool {

/** One command: its name, how it's used, and the function that runs it. */
struct Command {
    std::string name;
    std::string arguments;   // e.g. "<file> [offset] [length]"
    std::string description; // what it does, in a sentence or two
    int (*run)(int argc, char **argv);
};

/** A command as registered, with the engine it's for. */
struct Registered {
    std::string engine; // "" for the shared commands
    Command command;
};

/** Every command registered, in no particular order. */
std::vector<Registered> &registered();

/**
 * reluva-tool's own path (its argv[0]), for commands that run it again
 * (several runs at once, say); "reluva-tool" until main sets it.
 */
///@{
const char *programPath();
void setProgramPath(const char *path);
///@}

/** Registers a file's commands, for an engine or ("") for every one. */
struct Commands {
    /** Adds `list` to registered(), each command with `engine`. */
    Commands(const std::string &engine, std::vector<Command> list);
};

using Bytes = std::vector<uint8_t>;

/** Reads a whole file; false, with a message on stderr, if it can't be opened. */
bool readFile(const char *path, Bytes &out);
/** A little-endian value at offset `o`; 0 if it runs past the end. */
///@{
uint16_t rd16(const Bytes &d, size_t o);
uint32_t rd32(const Bytes &d, size_t o);
///@}
/** Prints `len` bytes from `off` as hex and text, 16 to a line, stopping at the end. */
void hexdump(const Bytes &d, size_t off, size_t len);
/** A number in decimal, hex (0x..) or octal (0..); 0 if it isn't one. */
long long parseNumber(const char *s);
/**
 * A name's extension, with its dot ("" if it has none); names may use
 * backslashes.
 */
std::string extensionOf(const std::string &name);

} // namespace reluva::tool
