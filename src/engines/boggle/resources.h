// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/archive.h"
#include "common/audio/sound.h"
#include "common/image.h"
#include "common/video/avi_video1.h"
#include "common/pe_resources.h"
#include "engines/boggle/formats/mesh.h"
#include "engines/boggle/formats/dictionary.h"
#include "engines/boggle/formats/text_resources.h"

namespace reluva::boggle {
struct Language {
    std::vector<PeResource> resources;
    std::vector<PeString> strings;
};
struct ResourceSummary {
    size_t images = 0, sounds = 0, palettes = 0, diceSets = 0, meshes = 0, languages = 0;
    size_t vertices = 0, faces = 0, strings = 0, dice = 0;
    // A deterministic checksum of decoded content, not retail content itself.
    std::string digest;
};
class Resources {
public:
    Error open(const std::filesystem::path& root) { return archive_.open(root); }
    Error palette(std::string_view name, Palette& out) const;
    Error diceSet(std::string_view name, DiceSet& out) const;
    Error mesh(std::string_view name, Mesh& out) const;
    Error dictionary(std::string_view name, Dictionary& out) const;
    Error language(std::string_view name, Language& out) const;
    Error image(std::string_view name, image::Image& out) const;
    Error animation(std::string_view name, video::AviVideo1& out) const;
    Error sound(std::string_view name, audio::Sound& out) const;
    // Validates every supported file in the known resource folders. Requires
    // at least one of each category; dictionaries/video/fonts are not counted
    // as supported by this check. Never writes files or executes game code.
    Error check(ResourceSummary& out) const;
private:
    DirectoryArchive archive_;
};
} // namespace reluva::boggle
