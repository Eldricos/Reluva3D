// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/formats/resources.h"

#include "common/bytes.h"
#include "common/fs.h"
#include "common/log.h"
#include "common/text.h"

#include <algorithm>
#include <map>

namespace reluva::stharbinger {

void Resources::addGamePath(const std::string &nativePath) {
    // Each folder gets a block of priorities below the previous one.
    const int priority = nextPriority_;
    nextPriority_ -= 10;

    auto dir = [&](const std::filesystem::path &path, int depth, int prio) {
        auto folder = std::make_unique<DirectoryArchive>();
        if (folder->open(path, depth))
            return;
        search_.add(std::move(folder), prio);
    };
    auto isDir = [](const std::filesystem::path &path, const char *name) {
        return fs::findDirectory(path, name).has_value();
    };

    const std::filesystem::path root = nativePath;
    const auto ds9 = fs::findDirectory(root, "DS9");

    // The DS9 folder itself. (A disc's root is not: disc 1 also has an ART
    // folder at the top, holding the installer's ART.ALL.)
    if (!ds9 && (isDir(root, "ANIMS") || isDir(root, "PALETTES") || isDir(root, "SOUND"))) {
        dir(root, 8, priority);
        return;
    }

    // A disc (or a copy of one), or an installed or extracted game folder (one
    // level deep, for an installed ART\SHORT.FNT).
    dir(root, 1, priority);
    if (ds9)
        dir(*ds9, 8, priority - 1);
    // Disc 1's installer archive of the art\ files (fonts, mouse cursors).
    if (const auto art = fs::findDirectory(root, "ART")) {
        DirectoryArchive artDir;
        if (!artDir.open(*art, 0)) {
            if (auto all = LstArchive::open(artDir, "ART")) {
                all->setPrefix("ART/");
                search_.add(std::make_unique<SharedArchive>(all), priority - 4);
            }
        }
    }
    if (const auto sse = fs::findDirectory(root, "SSE")) {
        auto sseDir = std::make_unique<DirectoryArchive>();
        if (!sseDir->open(*sse, 0)) {
            // The installer's archive of the program files, scripts included.
            auto all = LstArchive::open(*sseDir, "SSE");
            search_.add(std::move(sseDir), priority - 2);
            if (all) {
                if (all->has("CC.ALL"))
                    search_.add(std::make_unique<SharedArchive>(all), priority - 3);
                else
                    log::warning("%s/SSE.ALL holds no CC.ALL", sse->string().c_str());
            }
        }
    }
}

std::unique_ptr<SeekableReadStream> Resources::openFile(const std::string &name) const {
    return stharbinger::openFile(search_, name);
}

std::shared_ptr<LstArchive> Resources::getArchive(const std::string &name) {
    std::string key = text::toUpper(name);
    auto it = archives_.find(key);
    if (it != archives_.end())
        return it->second;
    auto archive = LstArchive::open(search_, key);
    archives_[key] = archive;
    return archive;
}

bool Resources::hasResource(ListKind kind, const std::string &name) const {
    std::string path = getResourcePath(kind, name);
    return !path.empty() && search_.has(path);
}

bool Resources::loadBackground(const std::string &name, Surface &surface, Palette &palette, bool &paletteLoaded) {
    paletteLoaded = false;
    auto stream = openFile(getResourcePath(kListBackgrounds, name));
    if (!stream)
        return false;
    bool embedded = false;
    if (!loadCMP(*stream, surface, &palette, &embedded)) {
        log::warning("Cannot decode background %s", name.c_str());
        return false;
    }
    paletteLoaded = embedded;
    if (auto pal = openFile("PALETTES/" + text::toUpper(name) + ".PAL"))
        paletteLoaded = loadPAL(*pal, palette) || paletteLoaded;
    return true;
}

bool Resources::loadAnimation(const std::string &name, RleAnimation &anim) {
    auto stream = openFile(getResourcePath(kListAnims, name));
    return stream && anim.load(*stream);
}

bool Resources::loadSound(const std::string &name, std::vector<uint8_t> &data) {
    auto stream = openFile(getResourcePath(kListSounds, name));
    if (!stream)
        return false;
    data = stream->readAll();
    return !data.empty();
}

bool Resources::loadDialogue(int index, Dialogue &dialogue, std::string &error) {
    dialogue = Dialogue();
    dialogue.index = index;
    dialogue.name = getDialogueName(index);
    auto ccb = getArchive("CCB");
    if (!ccb || uint32_t(index) >= ccb->size()) {
        error = "CCB.ALL is missing or has no such dialogue";
        return false;
    }
    auto entry = ccb->getEntry(uint32_t(index));
    if (!readDialogue(entry->data(), uint32_t(entry->size()), dialogue, &error))
        return false;

    struct TextSource {
        const char *archive;
        char separator;
        std::vector<std::string> *out;
    } sources[] = {
        {"CCQ", '\0', &dialogue.text},
        {"CCF", '\n', &dialogue.clips},
        {"CCR", '\0', &dialogue.menuLabels},
    };
    for (const TextSource &src : sources) {
        auto archive = getArchive(src.archive);
        if (archive && uint32_t(index) < archive->size()) {
            auto e = archive->getEntry(uint32_t(index));
            *src.out = splitStrings(e->data(), uint32_t(e->size()), src.separator);
        } else {
            log::warning("%s.ALL is missing; dialogue %s has no %s", src.archive, dialogue.name.c_str(),
                         src.separator == '\n' ? "clip names" : "text");
        }
    }
    return true;
}

bool Resources::openSpeech(const std::string &dialogueName) {
    // Speech archives are large: keep only the current dialogue's in memory.
    std::string name = text::toUpper(dialogueName);
    if (name != speechName_) {
        speechName_ = name;
        speechArchive_ = LstArchive::open(search_, "DIALOGUE/" + name + "/" + name);
        phonemeArchive_ = LstArchive::open(search_, "PHONEMES/" + name + "/" + name);
    }
    return speechArchive_ && phonemeArchive_;
}

namespace {

bool readEntry(const std::shared_ptr<LstArchive> &archive, int line, std::vector<uint8_t> &data) {
    data.clear();
    if (!archive || line < 0 || uint32_t(line) >= archive->size())
        return false;
    data = archive->getEntry(uint32_t(line))->readAll();
    return !data.empty();
}

} // namespace

bool Resources::loadSpeech(int line, std::vector<uint8_t> &data) {
    return readEntry(speechArchive_, line, data);
}

bool Resources::loadPhonemes(int line, std::vector<uint8_t> &data) {
    return readEntry(phonemeArchive_, line, data);
}

void Resources::loadLinkTables() {
    if (linkLoaded_)
        return;
    linkLoaded_ = true;
    if (auto s = openFile("DS9.LCC")) {
        std::vector<uint8_t> d = s->readAll();
        for (std::string line : text::split(std::string(d.begin(), d.end()), '\n')) {
            line = text::trim(line);
            if (!line.empty())
                linkScenes_.push_back(line);
        }
    }
    // u16 count, then NUL-terminated names.
    auto readNames = [&](const char *file, std::vector<std::string> &out) {
        auto s = openFile(file);
        if (!s)
            return;
        uint16_t count = s->readUint16LE();
        for (uint16_t i = 0; i < count && !s->eos(); i++)
            out.push_back(s->readCString());
    };
    readNames("DS9.LVM", linkVariables_);
    readNames("DS9.LDM", linkDialogues_);
    if (linkScenes_.empty() || linkVariables_.empty())
        log::warning("DS9.LCC/DS9.LVM not found: saved games will not be compatible with the original");
}

const std::vector<std::string> &Resources::getLinkScenes() {
    loadLinkTables();
    return linkScenes_;
}

const std::vector<std::string> &Resources::getLinkVariables() {
    loadLinkTables();
    return linkVariables_;
}

const std::vector<std::string> &Resources::getLinkDialogues() {
    loadLinkTables();
    return linkDialogues_;
}

std::vector<std::string> Resources::loadCredits() {
    // DEEP.EXE is the DOS/4GW extender followed by the game: an MZ stub whose
    // e_lfanew points to an LE header. LE data page offsets are relative to that
    // stub. The credits are a table of 269 offsets into the data object, each to
    // a NUL-terminated line.
    const uint32_t kTableAddress = 0x55464;
    const int kLines = 269;
    std::vector<std::string> lines;
    auto s = openFile("DEEP.EXE");
    if (!s) {
        log::warning("DEEP.EXE not found: no credits text");
        return lines;
    }
    std::vector<uint8_t> exe = s->readAll();
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= exe.size() ? readLE32(&exe[o]) : 0; };
    size_t mz = 0, le = 0;
    for (size_t i = 0; i + 0x40 <= exe.size(); i++) {
        if (exe[i] != 'M' || exe[i + 1] != 'Z')
            continue;
        size_t candidate = i + u32(i + 0x3C);
        if (candidate + 0x84 <= exe.size() && exe[candidate] == 'L' && exe[candidate + 1] == 'E') {
            mz = i;
            le = candidate;
            break;
        }
    }
    if (!le) {
        log::warning("DEEP.EXE: no LE executable found: no credits text");
        return lines;
    }
    uint32_t pageSize = u32(le + 0x28);
    size_t objects = le + u32(le + 0x40);
    uint32_t numObjects = u32(le + 0x44);
    size_t dataPages = mz + u32(le + 0x80);
    for (uint32_t i = 0; i < numObjects; i++) {
        size_t o = objects + i * 24;
        uint32_t size = u32(o), base = u32(o + 4), firstPage = u32(o + 12);
        if (kTableAddress < base || kTableAddress >= base + size)
            continue;
        size_t start = dataPages + size_t(firstPage - 1) * pageSize;
        for (int k = 0; k < kLines; k++) {
            size_t p = start + u32(start + (kTableAddress - base) + k * 4);
            std::string line;
            while (p < exe.size() && exe[p])
                line += char(exe[p++]);
            lines.push_back(line);
        }
        break;
    }
    return lines;
}

bool Resources::loadScenes(std::vector<Scene> &scenes, std::string &error) {
    scenes.clear();
    auto cc = getArchive("CC");
    if (!cc) {
        error = "the scene scripts (CC.ALL and CC.LST, or SSE/SSE.ALL on disc 1) were not found";
        return false;
    }
    std::shared_ptr<LstArchive> lists[kListKindCount];
    for (int k = 0; k < kListKindCount; k++) {
        lists[k] = getArchive(getListArchiveName(ListKind(k)));
        if (!lists[k])
            log::warning("%s.ALL is missing; %s will have no names", getListArchiveName(ListKind(k)),
                         getListTitle(ListKind(k)));
    }
    auto xref = getArchive("CCS");

    for (uint32_t slot = 0; slot < cc->size(); slot++) {
        auto entry = cc->getEntry(slot);
        if (!entry || !entry->size())
            continue;
        Scene scene;
        scene.slot = int(slot);
        uint32_t pos = 0;
        std::string err;
        if (!readDirectiveList(entry->data(), uint32_t(entry->size()), pos, scene.script, &err)) {
            log::warning("Scene slot %u: %s", slot, err.c_str());
            continue;
        }
        if (!scene.script.empty() && scene.script[0].command == kCmdScene && !scene.script[0].args.empty())
            scene.number = int(scene.script[0].args[0].value);

        for (int k = 0; k < kListKindCount; k++) {
            if (lists[k] && slot < lists[k]->size()) {
                auto e = lists[k]->getEntry(slot);
                scene.lists[k] = readNameList(e->data(), uint32_t(e->size()));
            }
        }
        if (xref && slot < xref->size()) {
            auto e = xref->getEntry(slot);
            scene.xref = readXrefList(e->data(), uint32_t(e->size()));
        }

        // The scene is named after the .CCG file most of its names come from.
        std::map<std::string, int> sources;
        for (const auto &list : scene.lists)
            for (const NameEntry &n : list) {
                std::string src = text::toUpper(n.source);
                if (src.size() > 4 && src.compare(src.size() - 4, 4, ".CCG") == 0)
                    sources[src.substr(0, src.size() - 4)]++;
            }
        auto best = std::max_element(sources.begin(), sources.end(),
                                     [](const auto &a, const auto &b) { return a.second < b.second; });
        scene.name = best != sources.end() ? best->first : text::format("SCENE%02u", slot);
        scenes.push_back(std::move(scene));
    }
    if (scenes.empty()) {
        error = "CC.ALL holds no readable scene scripts";
        return false;
    }
    return true;
}

} // namespace reluva::stharbinger
