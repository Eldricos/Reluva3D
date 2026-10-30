// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool: developer commands for studying games' files and running
// their code outside the window.
//
//   reluva-tool                                 the commands for any game, and the engines
//   reluva-tool <command> [args...]             runs a command for any game
//   reluva-tool <engine> <command> [args...]    runs one of an engine's commands
//   reluva-tool help <engine>                   an engine's commands, and what they do
//   reluva-tool help [engine] <command>         what a command takes and does
//
// The commands are registered by the files that hold them (tools/tool.h):
// the shared ones in src/tools/, each engine's in src/engines/<engine>/tools/.

#include "tools/tool.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

using reluva::tool::Registered;

const Registered* find(const std::string& engine, const std::string& name) {
    for (const Registered& r : reluva::tool::registered())
        if (r.engine == engine && r.command.name == name)
            return &r;
    return nullptr;
}

// The engine a command name belongs to, if exactly one has it (for
// pointing at the right command line).
const Registered* findAnyEngine(const std::string& name) {
    const Registered* found = nullptr;
    for (const Registered& r : reluva::tool::registered())
        if (!r.engine.empty() && r.command.name == name) {
            if (found)
                return nullptr;
            found = &r;
        }
    return found;
}

// The commands of an engine ("" for the shared ones), by name.
std::vector<const Registered*> commandsOf(const std::string& engine) {
    std::vector<const Registered*> list;
    for (const Registered& r : reluva::tool::registered())
        if (r.engine == engine)
            list.push_back(&r);
    std::sort(list.begin(), list.end(),
              [](const Registered* a, const Registered* b) { return a->command.name < b->command.name; });
    return list;
}

// Engines with commands, and how many.
std::map<std::string, int> engines() {
    std::map<std::string, int> counts;
    for (const Registered& r : reluva::tool::registered())
        if (!r.engine.empty())
            ++counts[r.engine];
    return counts;
}

std::string commandLine(const Registered& r) {
    return "reluva-tool " + (r.engine.empty() ? "" : r.engine + " ") + r.command.name + " " + r.command.arguments;
}

// Prints `text` wrapped to the terminal's usual width, each line indented.
void printWrapped(FILE* out, const std::string& text, size_t indent) {
    const size_t width = 80 - indent;
    size_t p = 0;
    while (p < text.size()) {
        size_t end = std::min(text.size(), p + width);
        if (end < text.size()) {
            const size_t space = text.rfind(' ', end);
            if (space != std::string::npos && space > p)
                end = space;
        }
        std::fprintf(out, "%*s%s\n", int(indent), "", text.substr(p, end - p).c_str());
        p = end;
        while (p < text.size() && text[p] == ' ')
            ++p;
    }
}

// With no command, or one that isn't known: what there is.
int usage() {
    std::fprintf(stderr, "usage: reluva-tool <command> [arguments]            for any game\n"
                         "       reluva-tool <engine> <command> [arguments]   for an engine's games\n"
                         "       reluva-tool help <engine>                    an engine's commands\n"
                         "       reluva-tool help [engine] <command>          what a command takes and does\n"
                         "\nFor any game:\n");
    for (const Registered* r : commandsOf(""))
        std::fprintf(stderr, "  %s %s\n", r->command.name.c_str(), r->command.arguments.c_str());
    std::fprintf(stderr, "\nEngines (reluva-tool help <engine> lists their commands):\n");
    for (const auto& [engine, count] : engines())
        std::fprintf(stderr, "  %-12s %d commands\n", engine.c_str(), count);
    return 2;
}

int helpEngine(const std::string& engine) {
    std::printf("%s's commands: reluva-tool %s <command> [arguments]\n", engine.c_str(), engine.c_str());
    for (const Registered* r : commandsOf(engine)) {
        std::printf("\n  %s %s\n", r->command.name.c_str(), r->command.arguments.c_str());
        printWrapped(stdout, r->command.description, 6);
    }
    return 0;
}

int helpCommand(const Registered& r) {
    std::printf("%s\n", commandLine(r).c_str());
    printWrapped(stdout, r.command.description, 4);
    return 0;
}

// A command not found: says where it is, if it's another engine's.
int notFound(const std::string& engine, const std::string& name) {
    if (const Registered* r = findAnyEngine(name); r && r->engine != engine)
        std::fprintf(stderr, "%s is %s's: %s\n", name.c_str(), r->engine.c_str(), commandLine(*r).c_str());
    else if (engine.empty())
        std::fprintf(stderr, "no command or engine %s\n", name.c_str());
    else
        std::fprintf(stderr, "%s has no command %s (reluva-tool help %s lists them)\n", engine.c_str(),
                     name.c_str(), engine.c_str());
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2)
        return usage();
    const std::string first = argv[1];
    const bool isEngine = engines().count(first) > 0;

    if (first == "help") {
        if (argc == 2)
            return usage();
        const std::string a = argv[2];
        if (engines().count(a)) {
            if (argc == 3)
                return helpEngine(a);
            const Registered* r = find(a, argv[3]);
            return r ? helpCommand(*r) : notFound(a, argv[3]);
        }
        const Registered* r = find("", a);
        return r ? helpCommand(*r) : notFound("", a);
    }
    if (isEngine) {
        if (argc < 3) {
            helpEngine(first);
            return 2;
        }
        // The command sees its own name as argv[1], as a shared one does.
        if (const Registered* r = find(first, argv[2]))
            return r->command.run(argc - 1, argv + 1);
        return notFound(first, argv[2]);
    }
    if (const Registered* r = find("", first))
        return r->command.run(argc, argv);
    if (findAnyEngine(first))
        return notFound("", first);
    std::fprintf(stderr, "no command or engine %s\n\n", first.c_str());
    return usage();
}
