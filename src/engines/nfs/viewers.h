// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/image.h"
#include "common/types.h"
#include "engines/nfs/ffn.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/text_table.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Data viewers used by the Need for Speed engine until the game itself
// runs. Each draws into the engine's 640x480 RGBA32 screen when asked;
// Tab switches between them (nfs.cpp).

namespace reluva::nfs {

class Page {
public:
    virtual ~Page() = default;
    virtual const char* name() const = 0;
    // Whether there's anything to show; pages without are skipped.
    virtual bool empty() const { return false; }
    virtual void handleKey(uint32_t key, uint16_t mods) = 0;
    virtual void draw(Surface& screen) = 0;
};

// What the engine found: the folders, the CD's files, tracks and cars.
class InfoPage : public Page {
public:
    InfoPage(const char* gameName, GameFolders folders);
    const char* name() const override { return "Info"; }
    void handleKey(uint32_t, uint16_t) override {}
    void draw(Surface& screen) override;

private:
    std::string gameName_;
    GameFolders folders_;
    std::vector<std::string> tracks_, cars_, music_, movies_;
};

// Every picture in every .fsh/.qfs, loose or in a .viv.
//   Up/Down: bank   PageUp/PageDown: 10 banks   Left/Right: picture
//   Home/End: first/last picture   B: background   A: alpha on/off
class TexturePage : public Page {
public:
    explicit TexturePage(const GameFiles& files);
    const char* name() const override { return "Textures"; }
    bool empty() const override { return banks_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int bank);
    void decodeCurrent();

    const GameFiles& files_;
    std::vector<FileRef> banks_;
    int bank_ = -1, shape_ = 0;
    ShapeBank loaded_;
    std::string error_;
    image::Image image_;
    std::string imageError_;
    int background_ = 0; // checkerboard, black, white
    bool alpha_ = true;
};

// The front-end string tables, in the game's own font.
//   Left/Right: language   Up/Down: line   PageUp/PageDown: page
//   Home/End: first/last   F: the game's font or Reluva's
class StringPage : public Page {
public:
    explicit StringPage(const GameFiles& files);
    const char* name() const override { return "Strings"; }
    bool empty() const override { return tables_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int table);
    int linesPerPage() const;

    const GameFiles& files_;
    std::vector<FileRef> tables_;
    int table_ = -1, top_ = 0;
    TextTable loaded_;
    std::string error_;
    std::optional<Font> font_; // arial10b.ffn, if it reads
    bool gameFont_ = true;
};

// Every font, with all its characters.
//   Left/Right: font   +/-: size
class FontPage : public Page {
public:
    explicit FontPage(const GameFiles& files);
    const char* name() const override { return "Fonts"; }
    bool empty() const override { return fonts_.empty(); }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface& screen) override;

private:
    void load(int font);

    const GameFiles& files_;
    std::vector<FileRef> fonts_;
    int font_ = -1;
    Font loaded_;
    std::string error_;
    int scale_ = 1;
};

// Every page, in Tab order, those with nothing to show left out.
std::vector<std::unique_ptr<Page>> makePages(const char* gameName, const GameFolders& folders,
                                             const GameFiles& files);

} // namespace reluva::nfs
