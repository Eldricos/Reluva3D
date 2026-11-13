// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Characters: the animator and the actor.

#include "tests/engines/awe/fixtures.h"

#include <filesystem>

using namespace reluva;
using namespace reluva::awe;
using namespace reluva::awe::testing;

namespace {

Model testSkeleton() {
    Model m;
    const char* names[] = {"Bip01", "Bip01 Spine", "Bip01 Head", "Jaw_Bones_Master"};
    for (int i = 0; i < 4; i++) {
        Bone b;
        b.name = names[i];
        b.parent = i - 1;
        b.inverseWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        m.bones.push_back(b);
    }
    return m;
}

// An animation holding each bone turned `degrees` about Z, `seconds`
// long, with a track mask.
std::shared_ptr<Animation> testTurn(float degrees, float seconds, std::map<std::string, float> mask) {
    auto a = std::make_shared<Animation>();
    a->duration = seconds;
    a->mask = std::move(mask);
    const float h = degrees / 57.29578f / 2;
    for (const char* bone : {"Bip01", "Bip01 Spine", "Bip01 Head", "Jaw_Bones_Master"}) {
        AnimationTrack t;
        t.bone = bone;
        t.orientation.knots = {0};
        t.orientation.controls = {0, 0, std::sin(h), std::cos(h)};
        a->tracks.push_back(t);
    }
    return a;
}

// A bone's turn about Z in its parent's axes, in degrees.
float boneTurn(const Pose& p, size_t bone) {
    const Mat4& w = p.world[bone];
    float parent = 0;
    if (bone > 0)
        parent = std::atan2(p.world[bone - 1][1], p.world[bone - 1][0]);
    return std::remainder(std::atan2(w[1], w[0]) - parent, 6.2831853f) * 57.29578f;
}

void testAnimator() {
    const Model m = testSkeleton();
    Animator an(m);
    // A base animation (0.01 on Bip01, all bones following) and a gesture
    // (0.99 on the spine and what's under it, but not the jaw's master).
    auto idle = testTurn(10, 2, {{"Bip01", 0.01f}});
    auto gesture = testTurn(50, 1, {{"Bip01 Spine", 0.99f}});
    CHECK(an.play("idle1", idle, Animator::Body, true, 0, 0));
    CHECK(an.play("agree", gesture, Animator::Body, false, 1, 0.25f));
    const Animator::Slot* g = an.find("agree");
    CHECK(g && std::abs(g->mask[1] - 0.99f) < 1e-6f && std::abs(g->mask[2] - 0.99f) < 1e-6f && g->mask[3] == 0);
    CHECK(std::abs(an.find("idle1")->mask[3] - 0.01f) < 1e-6f);
    std::vector<std::string> ended, finished;
    an.onEnding = [&](const Animator::Slot& s) { ended.push_back(s.name); };
    an.onFinished = [&](const Animator::Slot& s) { finished.push_back(s.name); };
    an.update(0.5f);
    Pose p;
    an.pose(p);
    // The root as the base; the spine almost the gesture's; the jaw's
    // master the base's.
    CHECK(std::abs(boneTurn(p, 0) - 10) < 0.1f);
    CHECK(std::abs(boneTurn(p, 1) - 50) < 1.0f && std::abs(boneTurn(p, 3) - 10) < 0.1f);
    an.update(0.3f); // within its ease of the end
    CHECK(ended.size() == 1 && ended[0] == "agree" && finished.empty());
    an.update(0.3f);
    CHECK(finished.size() == 1 && finished[0] == "agree" && !an.find("agree"));
    // A new base replaces the last, which stops without telling.
    CHECK(an.play("walk", testTurn(0, 1, {{"Bip01", 0.01f}}), Animator::Body, true, 0, 0.3f));
    CHECK(!an.find("idle1") && an.base() && an.base()->name == "walk");
    an.update(1.0f);
    CHECK(an.slots().size() == 1 && finished.size() == 1);
    // A finite base handing over to idle1 as it ends ends as it was, and
    // is told as done ("anim standup" waits on it).
    Animator standing(m);
    std::vector<std::string> done;
    standing.onEnding = [&](const Animator::Slot& s) {
        if (s.base)
            standing.play("idle1", idle, Animator::Body, true, 0, 0);
    };
    standing.onFinished = [&](const Animator::Slot& s) { done.push_back(s.name); };
    CHECK(standing.play("standup", testTurn(0, 1, {{"Bip01", 0.01f}}), Animator::Body, true, 1, 0.3f));
    standing.update(0.8f);
    CHECK(standing.find("idle1") && done.empty());
    standing.update(0.3f);
    CHECK(done.size() == 1 && done[0] == "standup" && standing.base() && standing.base()->name == "idle1");
    // A weight replaces the mask's: a light gesture.
    CHECK(an.play("fanger", gesture, Animator::Body, false, 1, 0, 0.02f));
    CHECK(std::abs(an.find("fanger")->mask[1] - 0.02f) < 1e-6f);
    // ... before the bones inherit: a base played at a weight leaves the
    // jaw's master out, as its parent's is no longer 0.01.
    CHECK(an.play("walk", testTurn(0, 1, {{"Bip01", 0.01f}}), Animator::Body, true, 0, 0.3f, 0.5f));
    CHECK(an.find("walk")->mask[2] == 0.5f && an.find("walk")->mask[3] == 0);
    // The pose's mask: the model's, inherited down; it multiplies what
    // plays over the base, not the base.
    Model seated = m;
    seated.masks["sit"] = {{"Bip01 Spine", 1.0f}, {"Bip01 Head", 0.0f}};
    const std::vector<float> sit = Animator::modelMask(seated, "SIT");
    CHECK(sit.size() == 4 && sit[0] == 0 && sit[1] == 1 && sit[2] == 1 && sit[3] == 1);
    CHECK(Animator::modelMask(seated, "head").empty());
    Animator sitting(seated);
    sitting.setPoseMask(sit);
    CHECK(sitting.play("sitidle1", idle, Animator::Body, true, 0, 0));
    CHECK(sitting.play("bow", testTurn(30, 1, {{"Bip01", 0.99f}}), Animator::Body, false, 1, 0));
    CHECK(sitting.find("sitidle1")->mask[0] == 0.01f);
    CHECK(sitting.find("bow")->mask[0] == 0 && std::abs(sitting.find("bow")->mask[1] - 0.99f) < 1e-6f);
    // The head's layer: pulled toward its pose by mask times ease.
    Animator head(m);
    head.play("idle1", testTurn(0, 2, {{"Bip01", 0.01f}}), Animator::Body, true, 0, 0);
    head.play("headleft", testTurn(90, 1, {{"Bip01 Head", 0.99f}}), Animator::Head, false, 0, 0.5f, 0.5f);
    head.update(1.0f);
    head.pose(p);
    CHECK(std::abs(boneTurn(p, 2) - 45) < 1.0f && std::abs(boneTurn(p, 1)) < 0.1f);
    // Ten at most.
    Animator full(m);
    for (int i = 0; i < Animator::kSlots; i++)
        CHECK(full.play("g" + std::to_string(i), gesture, Animator::Body, false, 0, 0));
    CHECK(!full.play("more", gesture, Animator::Body, false, 0, 0));
}

void testActor() {
    CHECK(Actor::isBase("idle1") && Actor::isBase("sitidle1") && !Actor::isBase("talkidle1"));
    CHECK(Actor::isBase("walk") && Actor::isBase("rotatel90") && !Actor::isBase("agree") && !Actor::isBase("idle2"));
    Model m = testSkeleton();
    m.masks["sit"] = {{"Bip01 Spine", 1.0f}};
    std::map<std::string, std::shared_ptr<Animation>> files = {
        {"idle1", testTurn(0, 2, {{"Bip01", 0.01f}})},
        {"bow", testTurn(30, 1, {{"Bip01", 0.99f}})},
        {"sitidle1", testTurn(5, 2, {{"Bip01", 0.01f}})},
        {"worried1", testTurn(20, 1, {{"Bip01 Spine", 0.99f}})},
        {"headleft", testTurn(90, 1, {{"Bip01 Head", 0.99f}})},
        {"headright", testTurn(-90, 1, {{"Bip01 Head", 0.99f}})},
    };
    std::vector<std::string> asked;
    auto loader = [&](const std::string& n) -> std::shared_ptr<const Animation> {
        asked.push_back(n);
        const auto it = files.find(n);
        return it == files.end() ? nullptr : it->second;
    };
    // A renamed animation (ANIM's list) and MODL's prefix.
    Ac2Record npc{"NPCX", {}, {}};
    Ac2Record anim{"ANIM", {}, {}};
    Ac2Value named;
    named.type = Ac2Value::Type::TextPairs;
    named.pairs.push_back({"", "worried01 / worried1"});
    anim.fields.push_back({"named", named});
    npc.parts.push_back(anim);
    Ac2Record modl{"MODL", {}, {}};
    Ac2Value sit;
    sit.type = Ac2Value::Type::Text;
    sit.text = "sit";
    modl.fields.push_back({"anim2", sit});
    modl.fields.push_back({"anim", sit});
    Ac2Value when;
    when.type = Ac2Value::Type::Text;
    when.text = "seated";
    modl.fields.push_back({"condition", when});
    npc.parts.push_back(modl);
    bool seated = false;
    Actor a(m, &npc, loader, [&](const std::string& c) { return c.empty() || (c == "seated" && seated); });
    CHECK(a.play("worried01", 1, 0) == "worried1");
    CHECK(a.play("idle1", 0, 0) == "idle1");
    seated = true;
    CHECK(a.play("idle1", 0, 0) == "sitidle1");
    // Seated, what plays over the base leaves the legs (MODL's sit mask).
    CHECK(a.play("bow", 1, 0) == "bow" && a.animator().find("bow")->mask[0] == 0);
    seated = false;
    CHECK(a.play("idle1", 0, 0) == "idle1" && a.animator().poseMask().empty());
    CHECK(a.play("bow", 1, 0) == "bow");
    CHECK(std::abs(a.animator().slots().back().mask[0] - 0.99f) < 1e-6f);
    // anim: what it plays as, and its end.
    std::vector<std::string> done;
    a.onAnimDone = [&](const std::string& n) { done.push_back(n); };
    CHECK(a.anim("nothing", 0, 1, 0).empty() && a.anim("worried01", 0, 1, 0) == "worried1");
    float heading = 0;
    for (int i = 0; i < 20; i++)
        a.update(0.1f, heading);
    CHECK(done.size() == 1 && done[0] == "worried1");
    // The head: the shorter way, a degree in 90 of weight; nothing within 2.
    a.turnHead(30, 0, 0);
    const Animator::Slot* h = a.animator().find("headleft");
    CHECK(h && std::abs(h->mask[2] - 30.0f / 90) < 1e-4f);
    a.turnHead(300, 0, 0);
    CHECK(a.animator().find("headright") && !a.animator().find("headleft"));
    a.headHome();
    CHECK(!a.animator().playing("head"));
    a.turnHead(1, 0, 0);
    CHECK(!a.animator().playing("head"));
    // Rotating without turning animations: 200 degrees a second, the
    // shorter way.
    bool turned = false;
    a.onRotateDone = [&] { turned = true; };
    CHECK(!a.rotateTo(1, 0) && a.rotateTo(270, 0));
    a.update(0.25f, heading);
    CHECK(std::abs(heading - 310) < 0.5f && !turned);
    a.update(0.25f, heading);
    CHECK(turned && heading == 270 && !a.rotating());
}

} // namespace

static const reluva::test::Suite suite("awe.unit.characters", {
    {"animator", [](const std::filesystem::path&) { testAnimator(); }},
    {"actor", [](const std::filesystem::path&) { testActor(); }},
});
