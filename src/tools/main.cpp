// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool: developer commands for studying games' files and running
// their code outside the window.
//
//   reluva-tool                      lists the commands
//   reluva-tool help <command>       what a command takes and does
//   reluva-tool <command> [args...]  runs it
//
// The commands are registered by the files that hold them (tools/tool.h):
// the shared ones in src/tools/, each engine's in src/engines/<engine>/tools/.

#include "tools/tool.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using reluva::tool::Registered;

const Registered* find(const char* name) {
    for (const Registered& r : reluva::tool::registered())
        if (r.command.name == name)
            return &r;
    return nullptr;
}

// Prints `text` wrapped to the terminal's usual width, each line indented.
void printWrapped(const std::string& text, size_t indent) {
    const size_t width = 80 - indent;
    size_t p = 0;
    while (p < text.size()) {
        size_t end = std::min(text.size(), p + width);
        if (end < text.size()) {
            const size_t space = text.rfind(' ', end);
            if (space != std::string::npos && space > p)
                end = space;
        }
        std::printf("%*s%s\n", int(indent), "", text.substr(p, end - p).c_str());
        p = end;
        while (p < text.size() && text[p] == ' ')
            ++p;
    }
}

int list() {
    std::vector<const Registered*> sorted;
    for (const Registered& r : reluva::tool::registered())
        sorted.push_back(&r);
    std::sort(sorted.begin(), sorted.end(), [](const Registered* a, const Registered* b) {
        return a->engine != b->engine ? a->engine < b->engine : a->command.name < b->command.name;
    });
    std::fprintf(stderr, "usage: reluva-tool <command> [arguments], or reluva-tool help <command>\n");
    const std::string* engine = nullptr;
    for (const Registered* r : sorted) {
        if (!engine || *engine != r->engine) {
            engine = &r->engine;
            std::fprintf(stderr, "\n%s:\n", r->engine.empty() ? "For any game" : r->engine.c_str());
        }
        std::fprintf(stderr, "  %s %s\n", r->command.name.c_str(), r->command.arguments.c_str());
    }
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "help") == 0) {
        const Registered* r = find(argv[2]);
        if (!r) {
            std::fprintf(stderr, "no command %s\n", argv[2]);
            return list();
        }
        std::printf("reluva-tool %s %s\n", r->command.name.c_str(), r->command.arguments.c_str());
        printWrapped(r->command.description, 4);
        return 0;
    }
    if (argc >= 2)
        if (const Registered* r = find(argv[1]))
            return r->command.run(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "help") != 0)
        std::fprintf(stderr, "no command %s\n", argv[1]);
    return list();
}
