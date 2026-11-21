// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#include "engines/stgenerations/audio.h"
#include "common/archive.h"
#include "common/fs.h"
#include "graphics/palette.h"
#include "engines/stgenerations/formats/bitmap.h"
#include "common/text.h"
#include "common/zip.h"

namespace reluva::stgenerations {

// The game's data lives in ZIP archives (*.DAT, MUSIC.1/2) in the
// installed DATA folder and on the CDs. Images are 8-bit BMPs, speech and
// music are PCM WAVs, and each speech sample has a .TXT subtitle beside it.

/** A resource inside a data archive, or a loose file if archive is empty. */
struct ResourceRef {
    std::string archive;
    std::string name;

    /** "ARCHIVE.DAT:name", or the loose file's name, for messages. */
    std::string toString() const { return archive.empty() ? name : archive + ":" + name; }
};

/**
 * Which disc a data file is on, for the 2 CD release (from the discs'
 * listings). Files on both discs are the same. Those the installer copies
 * into DATA/ are marked as installed.
 */
struct DataFileLocation {
    const char *name;
    bool installed;
    bool onDisc1;
    bool onDisc2;
};
/** The game's data files, ending with a null name. */
const DataFileLocation *dataFiles();

/** Finds and opens the game's data: its archives, and loose files in its folders. */
class ResourceManager {
public:
    /**
     * Sets the game's folders: the installed game (its DATA folder, or the
     * folder itself) and each disc (its GENS folder, or the folder itself).
     * An import keeps the discs in disc1/ and disc2/ of the game's folder,
     * searched when a disc's folder (cd1, cd2) isn't given here. The 2 CD
     * release keeps COMBAT.DAT, STELLAR.DAT, the music and the movies on
     * the discs; both discs are searched at once, so the game never needs
     * a disc swapped.
     */
    void addSearchPaths(const std::filesystem::path &gameDir, const std::filesystem::path &cd1Path,
                        const std::filesystem::path &cd2Path);
    /** Warns about any known data file that can't be found; true if all are there. */
    bool checkDataFiles() const;
    /** The data files that aren't there, for the launcher to say. */
    std::vector<std::string> missingDataFiles() const;
    /**
     * Opens the game's data archives (the .DAT files and MUSIC.1/2 are ZIP
     * files). The shared COMMON.DAT and OPTIONS.DAT are searched by name
     * after the loose files; the rest are opened by their names.
     */
    void openArchives();
    /** A data archive by file name, e.g. "VERID1.DAT", or nullptr if missing. */
    std::shared_ptr<ZipArchive> getArchive(const std::string &name) const;
    /** Every archive opened, by its upper case name. */
    const std::map<std::string, std::shared_ptr<ZipArchive>> &getArchives() const { return _archives; }

    /** Indexes every image (.BMP, .OVR, .GS2, .LV2) and sound (.WAV), in the archives and as loose files. */
    void scan();
    /** What scan() found: archives in name order first, then the loose files. */
    ///@{
    const std::vector<ResourceRef> &getImages() const { return _images; }
    const std::vector<ResourceRef> &getSounds() const { return _sounds; }
    ///@}
    /** Logs a summary of each archive's contents by file type. */
    void logInventory() const;

    /** Whether the resource is there. */
    bool hasResource(const ResourceRef &ref) const;
    /**
     * The resource's bytes, or nullptr if it isn't there. A member whose CRC
     * doesn't match is read all the same, with a warning.
     */
    std::unique_ptr<SeekableReadStream> open(const ResourceRef &ref) const;
    /** Reads and decodes a bitmap; false (with a warning) if it can't. */
    bool loadImage(const ResourceRef &ref, BitmapDecoder &decoder) const;
    /**
     * The palette of the first 8-bit image in an archive, for resources
     * that have none of their own (such as overlays); false if it has none.
     */
    bool loadArchivePalette(const std::string &archive, Palette &palette) const;
    /** A .WAV resource to play, or nullptr if it can't be read. */
    std::unique_ptr<SoundStream> loadSound(const ResourceRef &ref) const;
    /**
     * The subtitle for a speech sample: the text of the .TXT file next to
     * it (e.g. SOUNDS/RIKE0107.TXT for SOUNDS/RIKE0107.WAV), its lines
     * joined with spaces, or empty.
     */
    std::string loadSubtitle(const ResourceRef &soundRef) const;

    /** The folders searched, the installed game's first. */
    const std::vector<std::filesystem::path> &getDataDirs() const { return _dataDirs; }

private:
    // Installed DATA folder, then each disc's GENS folder.
    std::vector<std::filesystem::path> _dataDirs;
    // Loose files by name, then COMMON.DAT and OPTIONS.DAT.
    SearchSet _files;
    std::map<std::string, std::shared_ptr<ZipArchive>> _archives;
    std::vector<ResourceRef> _images;
    std::vector<ResourceRef> _sounds;
};

} // namespace reluva::stgenerations
