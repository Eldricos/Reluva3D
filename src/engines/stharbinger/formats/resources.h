// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "engines/stharbinger/formats/archive.h"
#include "engines/stharbinger/formats/dialogue.h"
#include "engines/stharbinger/formats/graphics.h"
#include "engines/stharbinger/formats/script.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stharbinger {

// One scene slot of CC.ALL with its name lists and cross-references.
struct Scene {
    int slot = 0;
    std::string name; // from the .CCG source file names, e.g. BRIDGE
    int number = -1;  // argument of the SCENE command
    DirectiveList script;
    std::vector<NameEntry> lists[kListKindCount];
    std::vector<XrefEntry> xref;
};

// Finds game files across the game directories, wherever they are:
//   - a CD (or a copy of one): DS9/... for data, SSE/SSE.ALL for the scripts;
//   - the DS9 directory itself;
//   - an installed or extracted SSE directory holding CC.ALL, CA.ALL ...
// Data files are looked up relative to the DS9 directory ("ART/B/R/BRDG0000.CMP"),
// script archives by bare name ("CC.ALL").
class Resources {
public:
    // Adds a game directory; earlier directories take precedence.
    void addGamePath(const std::string &nativePath);

    const SearchSet &getSearchSet() const { return search_; }
    bool hasFile(const std::string &name) const { return search_.has(name); }
    std::unique_ptr<SeekableReadStream> openFile(const std::string &name) const;

    // NAME.ALL/.LST(/.CAT), cached; nullptr if missing.
    std::shared_ptr<LstArchive> getArchive(const std::string &name);

    bool hasResource(ListKind kind, const std::string &name) const;

    // Loads a background. If PALETTES/NAME.PAL exists it is loaded into `palette`
    // and `paletteLoaded` is set; otherwise the current palette stays, as in the game.
    bool loadBackground(const std::string &name, Surface &surface, Palette &palette, bool &paletteLoaded) const;
    bool loadAnimation(const std::string &name, RleAnimation &anim) const;
    bool loadSound(const std::string &name, std::vector<uint8_t> &data) const;

    // Loads dialogue `index` (CCB.ALL order) with its text (CCQ, CCF, CCR).
    bool loadDialogue(int index, Dialogue &dialogue, std::string &error);
    // Opens the speech and lip-sync archives of a dialogue (DIALOGUE/NAME/NAME
    // and PHONEMES/NAME/NAME). The game skips a conversation without them.
    bool openSpeech(const std::string &dialogueName);
    // Entry `line` of the open speech / lip-sync archive (empty if none).
    bool loadSpeech(int line, std::vector<uint8_t> &data);
    bool loadPhonemes(int line, std::vector<uint8_t> &data);

    // The linker's tables (DS9.LCC, DS9.LVM, DS9.LDM): scene names in link order,
    // global variable names (their index is the slot in saved games) and the
    // dialogue slot names. Empty if the files are missing.
    const std::vector<std::string> &getLinkScenes();
    const std::vector<std::string> &getLinkVariables();
    const std::vector<std::string> &getLinkDialogues();

    // The 269 credits lines, read from DEEP.EXE (pointer table at 0x55464 in its
    // data object). Empty if the executable is not found.
    std::vector<std::string> loadCredits() const;

    // Reads every scene slot that has a script. Returns false (with `error`) if the
    // script archives cannot be found.
    bool loadScenes(std::vector<Scene> &scenes, std::string &error);

private:
    SearchSet search_;
    int nextPriority_ = 0;
    std::map<std::string, std::shared_ptr<LstArchive>> archives_;
    void loadLinkTables();
    bool linkLoaded_ = false;
    std::vector<std::string> linkScenes_, linkVariables_, linkDialogues_;
    std::string speechName_;
    std::shared_ptr<LstArchive> speechArchive_, phonemeArchive_;
};

} // namespace reluva::stharbinger
