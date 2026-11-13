// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/harbinger.h"

#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/game/game.h"
#include "engines/stharbinger/pages/viewer.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace reluva::stharbinger {

void fatal(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    throw FatalError(buf);
}

Harbinger::Harbinger(Platform *system, Options options) : system_(system), options_(std::move(options)) {
}

void Harbinger::initResources() {
    for (const std::string &path : options_.paths)
        resources_.addGamePath(path);
    std::string err;
    if (!resources_.loadScenes(scenes_, err))
        fatal("Cannot start Harbinger: %s", err.c_str());
}

int Harbinger::findScene(const std::string &nameOrSlot) const {
    for (size_t i = 0; i < scenes_.size(); i++)
        if (text::equalsIgnoreCase(scenes_[i].name, nameOrSlot))
            return int(i);
    char *end = nullptr;
    long slot = std::strtol(nameOrSlot.c_str(), &end, 10);
    if (!nameOrSlot.empty() && end && !*end) {
        for (size_t i = 0; i < scenes_.size(); i++)
            if (scenes_[i].slot == slot)
                return int(i);
    }
    return -1;
}

int Harbinger::findSceneByNumber(int number) const {
    for (size_t i = 0; i < scenes_.size(); i++)
        if (scenes_[i].number == number)
            return int(i);
    return -1;
}

void Harbinger::run() {
    initResources();

    const std::string &mode = options_.mode;
    bool viewer = text::equalsIgnoreCase(mode, "viewer");
    if (text::equalsIgnoreCase(mode, "video")) {
        // Developer: play one video from the DS9 folder (start_scene=INTRO/VNM.SMK).
        if (options_.startScene.empty())
            fatal("Playing one video needs it named in start_scene (in reluva.ini), e.g. INTRO/VNM.SMK");
        Game game(this);
        game.playVideoFile(options_.startScene);
        return;
    }
    if (!viewer && !mode.empty() && !text::equalsIgnoreCase(mode, "game"))
        fatal("Unknown start mode '%s' (game, viewer or video)", mode.c_str());

    int start = -1;
    if (!options_.startScene.empty()) {
        start = findScene(options_.startScene);
        if (start < 0)
            fatal("Unknown scene '%s' (reluva-tool stharbinger info lists them)", options_.startScene.c_str());
    }

    if (viewer) {
        SceneViewer sceneViewer(this);
        sceneViewer.run(start < 0 ? 0 : start);
        return;
    }
    Game game(this);
    game.run(start);
}

void Harbinger::printInfo() {
    initResources();
    for (const std::string &path : options_.paths)
        std::printf("path: %s\n", path.c_str());
    std::printf("\n%-4s %-9s %5s %6s  %-11s %-11s %-11s %4s %4s %4s %4s\n", "slot", "scene", "SCENE", "cmds",
                "backgrounds", "animations", "sounds", "dlg", "tmr", "hot", "var");

    int totals[3][2] = {};
    for (const Scene &scene : scenes_) {
        std::string cols[3];
        const ListKind kinds[3] = {kListBackgrounds, kListAnims, kListSounds};
        for (int k = 0; k < 3; k++) {
            int found = 0;
            int count = int(scene.lists[kinds[k]].size());
            for (const NameEntry &e : scene.lists[kinds[k]])
                found += resources_.hasResource(kinds[k], e.name) ? 1 : 0;
            totals[k][0] += found;
            totals[k][1] += count;
            cols[k] = text::format("%d/%d", found, count);
        }
        std::printf("%4d %-9s %5d %6u  %-11s %-11s %-11s %4u %4u %4u %4u\n", scene.slot, scene.name.c_str(),
                    scene.number, unsigned(scene.script.size()), cols[0].c_str(), cols[1].c_str(), cols[2].c_str(),
                    unsigned(scene.lists[kListDialogues].size()), unsigned(scene.lists[kListTimers].size()),
                    unsigned(scene.lists[kListHotspots].size()), unsigned(scene.lists[kListVariables].size()));
    }
    std::printf("\n%u scenes; files found: backgrounds %d/%d, animations %d/%d, sounds %d/%d\n",
                unsigned(scenes_.size()), totals[0][0], totals[0][1], totals[1][0], totals[1][1], totals[2][0],
                totals[2][1]);
    if (auto ccb = resources_.getArchive("CCB")) {
        int parsed = 0;
        int items = 0;
        int lines = 0;
        for (uint32_t i = 0; i < ccb->size(); i++) {
            Dialogue dialogue;
            std::string err;
            if (!resources_.loadDialogue(int(i), dialogue, err)) {
                std::printf("  dialogue %u (%s): %s\n", i, getDialogueName(int(i)), err.c_str());
                continue;
            }
            parsed++;
            lines += int(dialogue.text.size());
            for (const auto *tree : {&dialogue.npcLines, &dialogue.choices})
                for (const DialogueGroup &g : *tree)
                    items += int(g.size());
        }
        std::printf("%u dialogue scripts in CCB.ALL: %d read, %d items, %d lines of text\n", ccb->size(), parsed, items,
                    lines);
    }
    std::vector<std::string> credits = resources_.loadCredits();
    int headings = 0;
    for (const std::string &line : credits)
        headings += !line.empty() && line[0] == '*';
    std::printf("%u lines of credits in DEEP.EXE (%d headings)\n", unsigned(credits.size()), headings);
}

} // namespace reluva::stharbinger
