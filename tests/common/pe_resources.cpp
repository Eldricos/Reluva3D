// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "common/pe_resources.h"
#include "tests/support/test.h"
#include "tests/support/pe_resources_fixture.h"

using namespace reluva;
using namespace reluva::test::fixtures;
namespace {
std::vector<uint8_t> example(bool plus = false) {
    std::array<std::u16string, 16> strings{};
    strings[3] = u"Synthetic \u00e9 \U0001f642";
    return resourcePe(stringBlock(strings), 6, 2, plus);
}
void valid() {
    for (bool plus : {false, true}) {
        std::vector<PeResource> resources;
        CHECK(!readPeResources(example(plus), resources));
        CHECK(resources.size() == 1);
        CHECK(findPeResource(resources, 6, 2, 1033) != nullptr);
        CHECK(findPeResource(resources, 6, 2, 1036) == nullptr);
        std::vector<PeString> strings;
        CHECK(!readPeStrings(resources, strings));
        CHECK(strings.size() == 1);
        if (!strings.empty()) {
            CHECK(strings[0].id == 19);
            CHECK(strings[0].language == 1033);
            CHECK(strings[0].text == "Synthetic \xc3\xa9 \xf0\x9f\x99\x82");
        }
        auto second = resources[0]; second.language = 1036;
        resources.push_back(second);
        CHECK(!readPeStrings(resources, strings));
        CHECK(strings.size() == 2);
        resources.push_back(second);
        CHECK(bool(readPeStrings(resources, strings)));
        CHECK(strings.empty());
    }
}
void named() {
    auto file = example();
    put16(file, 512 + 12, 1); put16(file, 512 + 14, 0);
    put32(file, 512 + 16, 0x80000058);
    put16(file, 512 + 88, 2); put16(file, 512 + 90, 'U'); put16(file, 512 + 92, 'I');
    std::vector<PeResource> resources;
    CHECK(!readPeResources(file, resources));
    CHECK(resources.size() == 1 && resources[0].type.named && resources[0].type.text == u"UI");
    put16(file, 512 + 88, 65535);
    CHECK(bool(readPeResources(file, resources)));
    CHECK(resources.empty());
}
void corrupt() {
    std::vector<PeResource> out;
    const auto good = example();
    // Every header/payload truncation fails transactionally; section raw ranges
    // are checked even if a shorter prefix would contain the directory header.
    for (size_t n = 0; n < good.size(); ++n) {
        auto file = good; file.resize(n);
        CHECK(bool(readPeResources(file, out)));
        CHECK(out.empty());
    }
    for (auto [at, value] : std::vector<std::pair<size_t, uint32_t>>{
             {0x3c, 0xfffffff0}, {0x98 + 96 + 16, 0xfffffff0},
             {512 + 20, 0x80000000}, // cycle / invalid depth
             {512 + 44, 0x80000000}, {512 + 68, 0x80000000},
             {512 + 72, 0xfffffff0}, {512 + 76, 0xffffffff},
             {512 + 64, 0x10000}, {512 + 16, 0x8000ffff}}) {
        auto file = good; put32(file, at, value);
        CHECK(bool(readPeResources(file, out)));
        CHECK(out.empty());
    }
    auto file = good; put16(file, 512 + 14, 65535);
    CHECK(bool(readPeResources(file, out)));
    file = good; put32(file, 0x98 + 96 + 20, 16);
    CHECK(bool(readPeResources(file, out)));
    file = good; put32(file, 0x98 + 96 + 16, 0); put32(file, 0x98 + 96 + 20, 0);
    CHECK(!readPeResources(file, out));
    CHECK(out.empty());
}
void badStrings() {
    std::vector<PeResource> resources;
    CHECK(!readPeResources(example(), resources));
    auto resource = resources[0];
    std::vector<PeString> strings;
    resource.name.id = 0;
    CHECK(bool(readPeStrings({resource}, strings)));
    resource = resources[0]; resource.data.resize(3);
    CHECK(bool(readPeStrings({resource}, strings)));
    std::array<std::u16string, 16> text{}; text[0] = std::u16string(1, char16_t(0xd800));
    resource.data = stringBlock(text);
    CHECK(bool(readPeStrings({resource}, strings)));
    text[0] = std::u16string(1, char16_t(0xdc00)); resource.data = stringBlock(text);
    CHECK(bool(readPeStrings({resource}, strings)));
    resource = resources[0]; resource.data.push_back(7);
    CHECK(bool(readPeStrings({resource}, strings)));
    CHECK(strings.empty());
}
}
static const test::Suite suite("common.unit.pe_resources", {
    {"PE32/PE32+ strings and languages", [](const auto&) { valid(); }},
    {"named resources", [](const auto&) { named(); }},
    {"truncations and invalid directory ranges", [](const auto&) { corrupt(); }},
    {"invalid UTF-16 and string blocks", [](const auto&) { badStrings(); }},
});
