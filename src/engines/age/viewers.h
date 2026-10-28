// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Angel Game Engine data viewer's pages. Tab switches pages (once
// there is more than one); Esc goes back to the launcher.
//
// Files: every file in the search path with its size and archive.
// Up/Down, PageUp/PageDown and Home/End move, the wheel scrolls,
// Left/Right show one top-level folder at a time (anim, aud, bound,
// city...), Enter opens the file: text as text, anything else as a hex
// dump, scrolled the same way; Esc or Backspace goes back to the list.

#include "common/archive.h"
#include "common/system.h"
#include "engines/age/games.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::age {

struct ViewerContext {
    System& sys;
    Surface& screen; // RGBA32
    const SearchSet& files;
    const GameInfo& game;
    const std::vector<std::string>& warnings; // problems loading the game's files
};

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;

    virtual const char* name() const = 0;
    virtual size_t count() const = 0; // pages with nothing to show are skipped
    virtual void activate() = 0;      // redraw and retitle after switching here
    virtual void handleEvent(const Event& ev) = 0;
    // Whether Esc and Tab (which otherwise leave and switch pages) go to
    // the page instead.
    virtual bool takesKey(const Event& /*ev*/) const { return false; }

protected:
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void text(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void setTitle(const std::string& detail);

    ViewerContext& ctx_;
};

// A file's contents as lines to show: its text (tabs expanded, CR LF and
// LF ends) if it looks like text, else a hex dump of 16 bytes a line.
std::vector<std::string> fileLines(const std::vector<uint8_t>& data, bool& isText);

class FileViewer : public Page {
public:
    explicit FileViewer(ViewerContext& ctx);
    const char* name() const override { return "Files"; }
    size_t count() const override { return files_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;
    bool takesKey(const Event& ev) const override;

private:
    struct Row {
        std::string name;
        std::string source; // the archive's file name
        uint64_t size = 0;
    };

    void applyFolder();
    void open();
    void draw();
    int rows() const;

    std::vector<Row> files_;
    std::vector<std::string> folders_; // top-level folders, for Left/Right
    int folder_ = -1;                  // index into folders_, -1: all
    std::vector<size_t> shown_;        // files_ indices in the folder
    int selected_ = 0, top_ = 0;

    // The open file, if any.
    bool viewing_ = false, isText_ = false;
    std::string viewName_;
    std::vector<std::string> lines_;
    int lineTop_ = 0;
};

} // namespace reluva::age
