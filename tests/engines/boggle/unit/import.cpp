// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/import.h"
#include "engines/boggle/resources.h"
#include "engines/boggle/classic/options.h"
#include "engines/registry.h"
#include "common/fs.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/support/test.h"
#include <set>
using namespace reluva;
using namespace reluva::boggle;
namespace {
const DetectedGame synthetic{"boggle","boggle1997","Synthetic import","en","windows"};
void install(const std::filesystem::path& root) {
    fixtures::presentationInstall(root);
    fixtures::write(root/"Language/English.dll",fixtures::language());
    for (const auto* name : classicMusicFiles)
        fixtures::write(root/"Waves"/(std::string(name)+".wav"),fixtures::wave());
    fixtures::write(root/"Identity.bin","independently invented identity");
    for (const auto* name : {"setup.exe","system.dll","EnglishUser.dic","data.z","Waves/unrelated.wav"})
        fixtures::write(root/name,"not part of this import");
}
void selection(const std::filesystem::path& tmp) {
    const auto root=tmp/"source",dest=tmp/"imported"; install(root);
    std::filesystem::rename(root/"Waves",root/"wAvEs");
    ImportPlan plan;
    CHECK(!classicImportPlan(root,synthetic,{{"Identity.bin","",-1}},plan));
    // Ten core resources, eight effects, fifteen tracks, one identity file.
    CHECK(plan.files.size()==34 && plan.notes.size()==4);
    std::set<std::string> names;
    uint64_t total=0;
    for (const auto& file : plan.files) {
        CHECK(!file.packed); CHECK(file.size>0);
        CHECK(!std::filesystem::path(file.to).is_absolute());
        CHECK(names.insert(fs::toLower(file.to)).second); total+=file.size;
    }
    CHECK(plan.totalBytes()==total);
    CHECK(!names.contains("setup.exe") && !names.contains("system.dll") && !names.contains("englishuser.dic"));
    CHECK(!names.contains("data.z") && !names.contains("waves/unrelated.wav"));
    // Name overlap with runtime dependencies must be copied just once.
    ImportPlan duplicate;
    CHECK(!classicImportPlan(root,synthetic,{{"Identity.bin","",-1},{"Dicts/English.dic","",-1}},duplicate));
    CHECK(duplicate.files.size()==plan.files.size());
    fixtures::write(root/"Bitmaps/Select1.bmp",fixtures::bitmap());
    ImportPlan art; CHECK(!classicImportPlan(root,synthetic,{{"Identity.bin","",-1}},art));
    CHECK(art.files.size()==plan.files.size()+1);
    uint64_t last=0;
    CHECK(!runImport(plan,dest,[&](uint64_t done,uint64_t expected){CHECK(done>=last && done<=expected);last=done;return true;}));
    CHECK(last==total);
    for (const auto& file : plan.files)
        CHECK(fs::md5File(file.from,0)==fs::md5File(dest/file.to,0));
    // Copy remains readable with the source moved away.
    std::filesystem::rename(root,tmp/"unavailable-source");
    Resources resources; CHECK(!resources.open(dest));
    Dictionary dictionary; CHECK(!resources.dictionary("Dicts/English.dic",dictionary));
    video::AviVideo1 clip; CHECK(!resources.animation("Anims/ClsM1All.avi",clip));
    audio::Sound sound; CHECK(!resources.sound("Waves/Texas.wav",sound));
    const auto* meta=findEngine("boggle"); CHECK(meta);
    if(meta) CHECK(meta->planImport(dest).empty()); // invented identity never authenticates a retail release
}
void incomplete(const std::filesystem::path& tmp) {
    install(tmp);
    ImportPlan plan;
    auto build=[&]{return classicImportPlan(tmp,synthetic,{{"Identity.bin","",-1}},plan);};
    CHECK(!build()); CHECK(!plan.files.empty());
    std::filesystem::remove(tmp/"Waves/Seven.wav");
    auto err=build(); CHECK(bool(err) && err.message.find("Seven.wav")!=std::string::npos);
    CHECK(plan.files.empty() && plan.notes.empty());
    fixtures::write(tmp/"Waves/Seven.wav","");
    CHECK(bool(build()) && plan.files.empty());
    std::filesystem::remove(tmp/"Waves/Seven.wav");
    std::filesystem::create_directory(tmp/"Waves/Seven.wav");
    CHECK(bool(build()) && plan.files.empty());
    const auto* meta=findEngine("boggle"); CHECK(meta);
    if(meta) { CHECK(meta->planImport(tmp).empty()); CHECK(meta->planImport(tmp/"missing").empty()); }
}
void linksAndCollisions(const std::filesystem::path& tmp) {
    install(tmp); ImportPlan plan;
    auto build=[&]{return classicImportPlan(tmp,synthetic,{{"Identity.bin","",-1}},plan);};
    // Filesystems that permit distinct case spellings must reject ambiguity.
    if (!std::filesystem::exists(tmp/"IDENTITY.BIN")) {
        fixtures::write(tmp/"IDENTITY.BIN","another identity");
        CHECK(bool(build()) && plan.files.empty());
        std::filesystem::remove(tmp/"IDENTITY.BIN");
    }
    std::filesystem::rename(tmp/"Waves/Texas.wav",tmp/"outside.wav");
    std::error_code ec;
    std::filesystem::create_symlink(tmp/"outside.wav",tmp/"Waves/Texas.wav",ec);
    if (!ec) CHECK(bool(build()) && plan.files.empty());
}
}
static const test::Suite suite("boggle.unit.import", {
    {"bounded selection, case handling, exact copy and independent resources",[](const auto& tmp){selection(tmp);}},
    {"missing, empty and non-file dependencies clear the plan",[](const auto& tmp){incomplete(tmp);}},
    {"ambiguous names and symbolic links",[](const auto& tmp){linksAndCollisions(tmp);}},
});
