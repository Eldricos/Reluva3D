// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/options.h"
#include "engines/registry.h"
#include "tests/support/test.h"
#include <set>

using namespace reluva;
using namespace reluva::boggle;
namespace {
void settings() {
    ClassicOptions options;
    CHECK(!readClassicOptions({}, options)); CHECK(options.side() == 4 && options.duration == 180000);
    CHECK(!readClassicOptions({{"classic_variant","master"}}, options)); CHECK(options.side() == 5 && options.duration == 180000);
    CHECK(std::string(options.dice()) == "Dicts/EngMas.dst"); CHECK(std::string(options.palette()) == "Palettes/ClsM1Pal.pal");
    for (int seconds = 60; seconds <= 300; seconds += 30) {
        CHECK(!readClassicOptions({{"classic_seconds",std::to_string(seconds)}}, options));
        CHECK(options.duration == uint64_t(seconds)*1000);
    }
    CHECK(!readClassicOptions({{"classic_seconds","0"}}, options)); CHECK(options.duration == 0);
    for (const char* bad : {"", "-1", "1", "59", "61", "119", "149", "301", "3600", "180ms", " 180", "180 ", "0xb4", "999999999999999999999999"}) {
        CHECK(bool(readClassicOptions({{"classic_seconds",bad}}, options)));
        CHECK(options.side() == 4 && options.duration == 180000);
    }
    CHECK(options.music == musicNone);
    for (const char* bad : {"", "-1", "15", "01", "+1", " 1", "1x", "999999999999999999"})
        CHECK(bool(readClassicOptions({{"classic_music",bad}}, options)));
    CHECK(bool(readClassicOptions({{"classic_variant",""}}, options)));
    CHECK(bool(readClassicOptions({{"classic_variant","unknown"}}, options)));
}
void advertised(const std::filesystem::path& tmp) {
    const auto* meta = findEngine("boggle"); CHECK(meta); if (!meta) return;
    const auto options = meta->options("boggle1997", tmp);
    CHECK(options.size() == 7); CHECK(meta->options("unknown",tmp).empty());
    std::map<std::string,std::string> defaults;
    std::set<std::string> seconds;
    for (const auto& option : options) {
        if (option.choices.empty()) {
            defaults[option.key] = option.defaultOn ? "1" : "0";
            for (const auto* value : {"0","1"}) { ClassicOptions parsed; CHECK(!readClassicOptions({{option.key,value}},parsed)); }
            ClassicOptions bad; CHECK(bool(readClassicOptions({{option.key,"2"}},bad))); continue;
        }
        defaults[option.key] = option.choices.front().first;
        for (const auto& [value,label] : option.choices) {
            CHECK(!label.empty()); ClassicOptions parsed;
            CHECK(!readClassicOptions({{option.key,value}},parsed));
            if (option.key == "classic_seconds") seconds.insert(value);
        }
    }
    CHECK(seconds == std::set<std::string>({"0","60","90","120","150","180","210","240","270","300"}));
    ClassicOptions parsed; CHECK(!readClassicOptions(defaults, parsed));
    CHECK(parsed.variant == ClassicVariant::Original && parsed.duration == 180000);
}
}
static const test::Suite suite("boggle.unit.options", {
    {"Classic variants, durations and invalid saved settings", [](const auto&) { settings(); }},
    {"launcher choices and defaults match engine parsing", [](const auto& tmp) { advertised(tmp); }},
});
