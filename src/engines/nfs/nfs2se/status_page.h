// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/folders.h"
#include "engines/nfs/nfs2se/files.h"
#include "engines/nfs/ui/page.h"

#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * What the game runs while it can't be played yet: what Reluva found of it
 * (its folders, programs, music, movies, tracks and car models), and that
 * it can't play it yet. Esc goes back.
 */
class StatusPage : public Page {
public:
    /** Looks through `folders` for the game's files, once. */
    explicit StatusPage(GameFolders folders);
    const char *name() const override { return "Status"; }
    void draw(Surface &screen) override;

private:
    GameFolders _folders;
    Programs _programs;
    std::vector<std::string> _music;
    std::vector<std::string> _movies;
    std::vector<std::string> _tracks;
    std::vector<std::string> _cars;
};

} // namespace reluva::nfs::nfs2se
