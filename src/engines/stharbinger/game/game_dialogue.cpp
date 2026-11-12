// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Conversations, after DEEP.EXE's dialogue system (CONVERSE 0x1189F, dlg_run
// 0x14518, dlg_turn 0x169DD). See docs/engines/stharbinger/systems/script-reference.md, "Conversations".
//
// A conversation alternates NPC turns (a line, its sub-lines and their lists)
// and menu turns (the player picks a choice, says its line and runs its list).
// The steps are queued as operations so that speech, the menu and animation
// waits can block without stopping the game loop.

#include "engines/stharbinger/game/game.h"

#include "common/audio/stream.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/formats/audio.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace reluva::stharbinger {

namespace {

// Dialogue commands (the directive-list interpreter, table 0x149B0).
enum DialogueCommand {
    kDlgGoto = 0x32,
    kDlgReply = 0x33,
    kDlgHook = 0x34,
    kDlgTHook = 0x35,
    kDlgOn = 0x36,
    kDlgOff = 0x37,
    kDlgSet = 0x38,
    kDlgExit = 0x39,
    kDlgTalkHead = 0x3A,
    kDlgRude = 0x3B,
    kDlgDone = 0x3C,
    kDlgAddLine = 0x3D,
    kDlgMood = 0x3E,
    kDlgUnspeak = 0x3F,
    kDlgBack = 0x40,
    kDlgEmpty = 0x41,
    kDlgIfOn = 0x42,
    kDlgIfOff = 0x43,
    kDlgIfSet = 0x44,
    kDlgDoneX = 0x45,
    kDlgEmptyX = 0x46,
    kDlgProtagonist = 0x47,
    kDlgIfReply = 0x48,
    kDlgIfGoto = 0x49,
    kDlgTOff = 0x4A,
    kDlgTOn = 0x4B,
    kDlgIfTOff = 0x4C,
    kDlgIfTOn = 0x4D,
    kDlgCountChoices = 0x4E,
    kDlgSpeak = 0x4F,
    kDlgAnimPhoneme = 0x50,
    kDlgTalker = 0x51,
    kDlgSay = 0x7C, // 0x92, 0x93 and 0x94 do the same
    kDlgSay2 = 0x92,
    kDlgSay3 = 0x93,
    kDlgSay4 = 0x94
};

const int kMaxTalkers = 20;
const size_t kMaxProtagonistLines = 25;
const size_t kMaxMenuEntries = 20;
const size_t kMaxLabelLength = 24;
const uint32_t kLinePauseMillis = 250;
const int kRestMouth = 5;
// Mouth frame for each phoneme code, for mouths with 5 frames (0x246CA).
const uint8_t kMouthFrames[13] = {2, 0, 2, 1, 4, 3, 3, 4, 0, 3, 4, 3, 3};

// Scene commands that also work inside dialogue lists; the others do nothing.
bool isSceneCommandAllowed(uint8_t command) {
    switch (command) {
    case kCmdBackground:
    case kCmdAnim:
    case kCmdAnimPos:
    case kCmdAnimSeq:
    case kCmdAnimSetFrame:
    case kCmdAnimPrio:
    case kCmdAnimRate:
    case kCmdAnimSpd:
    case kCmdAnimDest:
    case kCmdAnimsOff:
    case kCmdAnimDump:
    case kCmdAnimSuspend:
    case kCmdAnimUnsus:
    case kCmdAnimComposit:
    case kCmdAnimPristine:
    case kCmdNop:
    case kCmdSound:
    case kCmdLet:
    case kCmdAdd:
    case kCmdSub:
    case kCmdMultiply:
    case kCmdDivide:
    case kCmdRandom:
    case kCmdHotOn:
    case kCmdHotOff:
    case kCmdTimerOff:
    case kCmdTimerOn:
    case kCmdTimerSet:
    case kCmdTimerGet:
    case kCmdTimerReload:
    case kCmdMapOn:
    case kCmdMapOff:
    case kCmdFadeOut:
    case kCmdFadeIn:
    case kCmdRequireDisc: return true;
    default: return false;
    }
}

uint8_t byteArg(const Argument* a) { return a ? uint8_t(a->value & 0xFF) : 0xFF; }

// The palette entry nearest to a 6-bit grey, skipping the reserved 1..5 (0x19720).
uint8_t nearestColor(const Palette& palette, int r, int g, int b) {
    int best = 0, bestDist = 1 << 30;
    for (int i = 0; i < 255; i++) {
        if (i >= 1 && i <= 5)
            continue;
        const uint8_t* c = palette.data() + i * 3;
        int d = std::abs((c[0] >> 2) - r) + std::abs((c[1] >> 2) - g) + std::abs((c[2] >> 2) - b);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return uint8_t(best);
}

} // namespace

Game::DialogueState::DialogueState() {
    std::memset(done, 0xFF, sizeof(done));
    std::memset(emptyGoto, 0xFF, sizeof(emptyGoto));
    std::memset(emptyReply, 0xFF, sizeof(emptyReply));
}

// ---- State --------------------------------------------------------------------

Game::DialogueState* Game::getDialogueState(int index) {
    auto it = dialogues_.find(index);
    if (it != dialogues_.end())
        return &it->second;
    DialogueState st;
    std::string err;
    if (!resources_.loadDialogue(index, st.data, err)) {
        log::warning("Cannot load dialogue %s: %s", getDialogueName(index), err.c_str());
        return nullptr;
    }
    st.current.assign(st.data.npcLines.size(), 0);
    for (const auto& g : st.data.npcLines)
        st.npcFlags.emplace_back(g.size());
    for (const auto& g : st.data.choices)
        st.choiceFlags.emplace_back(g.size());
    return &(dialogues_[index] = std::move(st));
}

Game::ItemRef Game::getItemRef(const Argument* arg) const {
    // x g.i: item i of choice group g; y/z g.i: of NPC group g.
    ItemRef ref;
    if (!arg || !(arg->type & (kArgPair08 | kArgPair10)))
        return ref;
    ref.npc = (arg->type & kArgPair10) != 0;
    ref.group = int(arg->value & 0xFF);
    ref.item = int(arg->value >> 8);
    return ref;
}

Game::DialogueState::ItemFlags* Game::getItemFlags(DialogueState& st, const ItemRef& ref) {
    auto& flags = ref.npc ? st.npcFlags : st.choiceFlags;
    if (ref.group < 0 || ref.group >= int(flags.size()) || ref.item < 0 || ref.item >= int(flags[ref.group].size()))
        return nullptr;
    return &flags[ref.group][ref.item];
}

void Game::setItemOn(DialogueState& st, const ItemRef& ref, bool on, bool temporary) {
    DialogueState::ItemFlags* flags = getItemFlags(st, ref);
    if (!flags)
        return;
    if (temporary) {
        conv_.tempFlags.push_back({ref, flags->on});
    } else {
        // A permanent change also survives the undoing of TON/TOFF.
        for (auto& t : conv_.tempFlags)
            if (t.item == ref)
                t.saved = on;
    }
    flags->on = on;
}

void Game::restoreTempFlags() {
    if (conv_.dialogue < 0)
        return;
    DialogueState& st = dialogues_[conv_.dialogue];
    for (const auto& t : conv_.tempFlags)
        if (DialogueState::ItemFlags* flags = getItemFlags(st, t.item))
            flags->on = t.saved;
    conv_.tempFlags.clear();
}

bool Game::isExhausted(const DialogueState& st, int group) const { return isExhausted(st, group, ItemRef()); }

bool Game::isExhausted(const DialogueState& st, int group, const ItemRef& except) const {
    // No choice of the group, or of a group hooked to it, is on and unsaid.
    auto check = [&](int g) {
        if (g < 0 || g >= int(st.choiceFlags.size()))
            return true;
        for (int i = 0; i < int(st.choiceFlags[g].size()); i++) {
            const auto& f = st.choiceFlags[g][i];
            ItemRef ref;
            ref.group = g;
            ref.item = i;
            if (f.on && !f.said && !(ref == except))
                return false;
        }
        return true;
    };
    if (!check(group))
        return false;
    if (group >= 0 && group < kMaxGroups)
        for (const auto& h : st.hooks[group])
            if (!check(h.group))
                return false;
    return true;
}

int Game::applyRedirect(const DialogueState& st, int group, const uint8_t* table,
                        const DialogueState::Redirect* redirects) const {
    if (group >= 0 && group < kMaxGroups && table[group] != 0xFF && isExhausted(st, group))
        group = table[group];
    if (group >= 0 && group < kMaxGroups && redirects[group].target >= 0 &&
        isExhausted(st, group, redirects[group].except))
        group = redirects[group].target;
    return group;
}

std::vector<Game::ItemRef> Game::availableChoices(DialogueState& st, int group, bool consumeHooks) {
    std::vector<ItemRef> out;
    auto add = [&](int g) {
        if (g < 0 || g >= int(st.choiceFlags.size()))
            return;
        for (int i = 0; i < int(st.choiceFlags[g].size()); i++) {
            const auto& f = st.choiceFlags[g][i];
            if (f.on && !f.said) {
                ItemRef ref;
                ref.group = g;
                ref.item = i;
                out.push_back(ref);
            }
        }
    };
    add(group);
    if (group >= 0 && group < kMaxGroups) {
        for (auto& h : st.hooks[group]) {
            if (h.state == 2)
                continue;
            if (consumeHooks && h.state == 1)
                h.state = 2; // a THOOK shows up in one menu only
            add(h.group);
        }
    }
    return out;
}

// ---- Running a conversation ---------------------------------------------------------

void Game::loadFont() {
    // art\short.fnt: the reply menu and the game menu's text.
    if (fontLoaded_)
        return;
    fontLoaded_ = true;
    auto stream = resources_.openFile("ART/SHORT.FNT");
    if (!stream || !font_.load(*stream))
        log::warning("art\\short.fnt not found: menus will have no text");
}

void Game::startConversation(int cdIndex, int hotspot) {
    const auto& cd = scene_->lists[kListDialogues];
    if (cdIndex < 0 || cdIndex >= int(cd.size()))
        return;
    const std::string& name = cd[cdIndex].name;
    int index = findDialogue(name);
    if (index < 0) {
        log::warning("CONVERSE: unknown dialogue %s", name.c_str());
        return;
    }
    if (!resources_.openSpeech(name)) {
        // The game skips a conversation whose speech files are missing.
        log::warning("Conversation %s skipped: DIALOGUE/%s or PHONEMES/%s not found", name.c_str(), name.c_str(),
                     name.c_str());
        return;
    }
    DialogueState* st = getDialogueState(index);
    if (!st)
        return;
    loadFont();
    log::debug(1, "Conversation %s (dialogue %d)", name.c_str(), index);

    conv_ = Conversation();
    conv_.active = true;
    conv_.dialogue = index;
    conv_.hotspot = hotspot;
    conv_.lastPick.assign(kMaxGroups, ItemRef());

    // Where it starts: group 0, or the BACK / RUDE group after an earlier talk.
    auto pick = [&](uint8_t g) {
        if (g != 0xFF)
            return int(g);
        for (int i = 0; i < int(st->data.npcLines.size()); i++)
            if (!st->data.npcLines[i].empty())
                return i;
        return st->cur;
    };
    st->cur = 0;
    st->replyMode = false;
    if (st->exit)
        st->cur = pick(st->backGroup);
    if (st->rude) {
        st->cur = pick(st->rudeGroup);
        st->rude = false;
    }
    ConversationOp op;
    op.type = ConversationOp::kRunList;
    op.list = &st->data.listA;
    conv_.ops.push_back(op);
    op.list = &st->data.listB;
    conv_.ops.push_back(op);
    op = ConversationOp();
    op.type = ConversationOp::kLoop;
    op.pos = 1; // first time round: clear the EXIT flag (EXIT in lists A/B does nothing)
    conv_.ops.push_back(op);

    thread_.wait = kWaitConversation;
    status_.clear();
    updateCaption();
    processConversationOps();
}

void Game::updateConversation() {
    if (conv_.waitAnim >= 0 && (conv_.waitAnim >= int(anims_.size()) || !anims_[conv_.waitAnim].suspended))
        conv_.waitAnim = -1;
    if (conv_.waitFade && !fade_.active)
        conv_.waitFade = false;
    if (conv_.speaking)
        updateSpeech();
    if (conv_.menuWaiting && stepCount_ % 5 == 0)
        tickConversationTimers();
    processConversationOps();
}

void Game::processConversationOps() {
    while (conv_.active) {
        if (conv_.speaking || conv_.waitAnim >= 0 || conv_.waitFade || conv_.menuWaiting)
            return;
        if (conv_.ops.empty()) {
            finishConversation();
            return;
        }
        DialogueState& st = dialogues_[conv_.dialogue];
        ConversationOp& op = conv_.ops.front();
        switch (op.type) {
        case ConversationOp::kRunList:
            if (!runDialogueList(op))
                break; // blocked, or a line to say was queued in front
            conv_.ops.pop_front();
            break;
        case ConversationOp::kSpeak: {
            int line = op.line;
            conv_.ops.pop_front();
            startSpeech(line);
            break;
        }
        case ConversationOp::kMenu:
            conv_.menuWaiting = true;
            conv_.menuVisible = true;
            conv_.picked = -1;
            conv_.hover = menuEntryAt(cursorHitPoint(true).x, cursorHitPoint(true).y);
            break;
        case ConversationOp::kAfterPick:
            conv_.ops.pop_front();
            if (!conv_.jumpTaken)
                st.cur++;
            break;
        case ConversationOp::kCloseMenu:
            conv_.ops.pop_front();
            conv_.menuVisible = false;
            break;
        case ConversationOp::kLoop: {
            bool first = op.pos == 1;
            conv_.ops.pop_front();
            if (first)
                st.exit = false;
            if (st.exit)
                break; // no more operations: finished
            st.cur = applyRedirect(st, st.cur, st.done, st.doneX);
            conv_.jumpTaken = false;
            dialogueTurn();
            ConversationOp loop;
            loop.type = ConversationOp::kLoop;
            conv_.ops.push_back(loop);
            break;
        }
        }
    }
}

bool Game::runDialogueList(ConversationOp& op) {
    // Directives run until a GOTO or REPLY, which skips the rest of every list
    // until the next turn.
    while (op.pos < op.list->size() && !conv_.jumpTaken) {
        const Directive& d = (*op.list)[op.pos++];
        log::debug(4, "  dialogue: %s", describe(d).c_str());
        if (!executeDialogueDirective(d))
            return false;
        if (!conv_.active)
            return false;
    }
    return true;
}

void Game::dialogueTurn() {
    DialogueState& st = dialogues_[conv_.dialogue];
    if (st.replyMode)
        menuTurn(st);
    else
        npcTurn(st);
}

void Game::npcTurn(DialogueState& st) {
    restoreTempFlags();
    int g = -1;
    for (int i = std::max(st.cur, 0); i < kMaxGroups - 1 && i < int(st.data.npcLines.size()); i++) {
        if (!st.data.npcLines[i].empty()) {
            g = i;
            break;
        }
    }
    if (g < 0) {
        st.exit = true;
        return;
    }
    st.cur = g;
    st.replyMode = true; // then the menu of the same group, unless a list says otherwise
    int index = std::min<int>(st.current[g], int(st.data.npcLines[g].size()) - 1);
    const DialogueItem& item = st.data.npcLines[g][index];

    ConversationOp op;
    op.type = ConversationOp::kSpeak;
    op.line = item.line;
    conv_.ops.push_back(op);
    for (const auto& sub : item.subItems) {
        ConversationOp list;
        list.type = ConversationOp::kRunList;
        list.list = &sub.script;
        conv_.ops.push_back(list);
        op.line = sub.a;
        conv_.ops.push_back(op);
    }
    ConversationOp list;
    list.type = ConversationOp::kRunList;
    list.list = &item.script;
    conv_.ops.push_back(list);
}

void Game::menuTurn(DialogueState& st) {
    // An exhausted menu can send the conversation to an NPC group ...
    int g0 = st.cur;
    st.cur = applyRedirect(st, st.cur, st.emptyGoto, st.emptyXGoto);
    if (st.cur != g0) {
        st.replyMode = false;
        return;
    }
    // ... or to another menu.
    for (int n = 0; n < kMaxGroups; n++) {
        int before = st.cur;
        st.cur = applyRedirect(st, st.cur, st.emptyReply, st.emptyXReply);
        if (st.cur == before)
            break;
    }
    bool exists = st.cur >= 0 && st.cur < kMaxGroups && !st.hooks[st.cur].empty();
    for (int i = std::max(st.cur, 0); !exists && i < kMaxGroups - 1 && i < int(st.data.choices.size()); i++)
        exists = !st.data.choices[i].empty();
    if (!exists) {
        st.exit = true;
        return;
    }
    st.replyMode = false; // after the player's line, the next NPC group speaks
    conv_.menu = availableChoices(st, st.cur, true);
    if (conv_.menu.empty()) {
        log::debug(1, "Conversation: no replies are active or unspoken in group %d", st.cur);
        st.exit = true;
        return;
    }
    if (conv_.menu.size() > kMaxMenuEntries)
        conv_.menu.resize(kMaxMenuEntries);
    layoutMenu(st);

    ConversationOp op;
    op.type = ConversationOp::kMenu;
    conv_.ops.push_back(op);
    op.type = ConversationOp::kCloseMenu;
    conv_.ops.push_back(op);
    if (conv_.menu.size() == 1)
        pickChoice(0); // a single choice is taken without showing the menu
}

void Game::pickChoice(int entry) {
    if (entry < 0 || entry >= int(conv_.menu.size()))
        return;
    DialogueState& st = dialogues_[conv_.dialogue];
    ItemRef ref = conv_.menu[entry];
    const DialogueItem& item = st.data.choices[ref.group][ref.item];
    if (DialogueState::ItemFlags* flags = getItemFlags(st, ref))
        flags->said = true;
    if (st.cur >= 0 && st.cur < kMaxGroups)
        conv_.lastPick[st.cur] = ref;
    conv_.menuWaiting = false;
    conv_.picked = entry;
    conv_.hover = entry;

    // Replace the menu operation with: say the line, run its list, next group.
    auto it = std::find_if(conv_.ops.begin(), conv_.ops.end(),
                           [](const ConversationOp& o) { return o.type == ConversationOp::kMenu; });
    if (it == conv_.ops.end())
        return;
    it = conv_.ops.erase(it);
    ConversationOp speak, list, after;
    speak.type = ConversationOp::kSpeak;
    speak.line = item.line;
    list.type = ConversationOp::kRunList;
    list.list = &item.script;
    after.type = ConversationOp::kAfterPick;
    it = conv_.ops.insert(it, after);
    it = conv_.ops.insert(it, list);
    conv_.ops.insert(it, speak);
}

void Game::abortConversation(bool rude, int hotspot) {
    // Clicking another hotspot, or a timer ending the talk (0x17459).
    DialogueState& st = dialogues_[conv_.dialogue];
    st.exit = true;
    st.rude = rude;
    if (st.cur >= 0 && st.cur < kMaxGroups)
        conv_.lastPick[st.cur] = ItemRef();
    // Choices picked in other groups during this conversation come back.
    for (const ItemRef& ref : conv_.lastPick)
        if (ref.valid())
            if (DialogueState::ItemFlags* flags = getItemFlags(st, ref))
                flags->said = false;
    conv_.ops.clear();
    conv_.result = hotspot;
    finishConversation();
}

void Game::finishConversation() {
    restoreTempFlags();
    if (conv_.speechHandle != audio::kNoSound && system_->getMixer())
        system_->getMixer()->stop(conv_.speechHandle);
    speedStep_ = 1;
    int result = conv_.result;
    conv_ = Conversation();
    log::debug(1, "Conversation ended");

    // The scene script carries on after CONVERSE, or with the clicked hotspot.
    thread_.wait = kWaitNone;
    if (result >= 0 && result < int(hotspots_.size()))
        jumpToLabel(hotspots_[result].label);
}

// ---- Dialogue commands ----------------------------------------------------------------

void Game::registerTalker(int slot, const std::string& code, int mouth, int head) {
    if (slot >= kMaxTalkers)
        return;
    if (slot >= int(conv_.talkers.size()))
        conv_.talkers.resize(slot + 1);
    // Values outside the scene's animation list (QUARK020 uses 4095) mean none.
    if (mouth >= int(anims_.size()))
        mouth = -1;
    if (head >= int(anims_.size()))
        head = -1;
    Talker& t = conv_.talkers[slot];
    t.code = code;
    t.mouth = mouth;
    t.head = head;
    // The mouth shows the resting shape and the head its first frame (0x247DA).
    if (mouth >= 0 && mouth < int(anims_.size())) {
        if (!anims_[mouth].loaded)
            loadAnim(mouth, true);
        Anim& a = anims_[mouth];
        a.flags = kAnimFlagStop;
        a.frame = phonemeFrame(mouth, kRestMouth);
        a.priority = 2;
        a.countdown = 0;
    }
    if (head >= 0 && head < int(anims_.size())) {
        if (!anims_[head].loaded)
            loadAnim(head, true);
        Anim& a = anims_[head];
        a.flags = kAnimFlagStop;
        a.frame = 0;
        a.priority = 1;
    }
}

bool Game::executeDialogueDirective(const Directive& d) {
    DialogueState& st = dialogues_[conv_.dialogue];
    const Argument *a0 = d.getArg(0), *a1 = d.getArg(1), *a2 = d.getArg(2);
    auto test = [&]() { return getValue(a0) == getValue(a1); };
    auto speak = [&](int line) {
        ConversationOp op;
        op.type = ConversationOp::kSpeak;
        op.line = line;
        conv_.ops.push_front(op);
        return false; // the list resumes after the line
    };
    auto xrefSymbol = [&](const Argument* a) {
        if (!a || a->type != kArgSymbol || a->value >= scene_->xref.size())
            return std::string();
        return scene_->xref[a->value].symbol;
    };

    switch (d.command) {
    case kDlgGoto:
    case kDlgReply:
        st.cur = byteArg(a0);
        st.replyMode = d.command == kDlgReply;
        conv_.jumpTaken = true;
        return true;
    case kDlgIfGoto:
    case kDlgIfReply:
        if (test()) {
            st.cur = byteArg(a2);
            st.replyMode = d.command == kDlgIfReply;
            conv_.jumpTaken = true;
        }
        return true;
    case kDlgHook:
    case kDlgTHook: {
        int g = byteArg(a0), t = byteArg(a1);
        if (t >= kMaxGroups)
            return true;
        auto& chain = st.hooks[t];
        if (std::none_of(chain.begin(), chain.end(), [&](const DialogueState::Hook& h) { return h.group == g; }))
            chain.push_back({g, d.command == kDlgTHook ? 1 : 0});
        return true;
    }
    case kDlgOn:
    case kDlgOff: setItemOn(st, getItemRef(a0), d.command == kDlgOn, false); return true;
    case kDlgTOn:
    case kDlgTOff: setItemOn(st, getItemRef(a0), d.command == kDlgTOn, true); return true;
    case kDlgIfOn:
    case kDlgIfOff:
        if (test())
            setItemOn(st, getItemRef(a2), d.command == kDlgIfOn, false);
        return true;
    case kDlgIfTOn:
    case kDlgIfTOff:
        if (test())
            setItemOn(st, getItemRef(a2), d.command == kDlgIfTOn, true);
        return true;
    case kDlgSet:
    case kDlgIfSet: {
        if (d.command == kDlgIfSet && !test())
            return true;
        ItemRef ref = getItemRef(d.command == kDlgSet ? a0 : a2);
        if (ref.group >= 0 && ref.group < int(st.current.size()) && ref.item < int(st.data.npcLines[ref.group].size()))
            st.current[ref.group] = ref.item;
        return true;
    }
    case kDlgUnspeak:
    case kDlgSpeak:
        if (DialogueState::ItemFlags* flags = getItemFlags(st, getItemRef(a0)))
            flags->said = d.command == kDlgSpeak;
        return true;
    case kDlgExit: st.exit = true; return true;
    case kDlgRude: st.rudeGroup = byteArg(a0); return true;
    case kDlgBack: st.backGroup = byteArg(a0); return true;
    case kDlgDone:
        if (byteArg(a0) < kMaxGroups)
            st.done[byteArg(a0)] = byteArg(a1);
        return true;
    case kDlgEmpty:
        if (byteArg(a0) < kMaxGroups)
            (byteArg(a2) == 2 ? st.emptyGoto : st.emptyReply)[byteArg(a0)] = byteArg(a1);
        return true;
    case kDlgDoneX:
    case kDlgEmptyX: {
        int g = byteArg(a0);
        if (g >= kMaxGroups)
            return true;
        DialogueState::Redirect* table = d.command == kDlgDoneX      ? st.doneX
                                         : byteArg(d.getArg(3)) == 2 ? st.emptyXGoto
                                                                     : st.emptyXReply;
        table[g].target = byteArg(a1);
        table[g].except = getItemRef(a2);
        return true;
    }
    case kDlgAddLine:
        if (conv_.protagonistLines.size() < kMaxProtagonistLines)
            conv_.protagonistLines.push_back(int(a0 ? a0->value & 0xFF : 0));
        return true;
    case kDlgProtagonist: {
        uint16_t i = getValue(a0);
        if (i < conv_.protagonistLines.size())
            return speak(conv_.protagonistLines[i]);
        return true;
    }
    case kDlgMood: return speak(a0 ? int(a0->value) : 0);
    case kDlgSay:
    case kDlgSay2:
    case kDlgSay3:
    case kDlgSay4: return speak(a2 ? int(a2->value) : 0);
    case kDlgCountChoices: {
        int g = byteArg(a0);
        setVariable(a1, uint16_t(availableChoices(st, g, false).size()));
        return true;
    }
    case kDlgTalkHead: {
        conv_.talkers.clear();
        const Argument *mouth = d.getArg(4), *head = d.getArg(5);
        if (mouth && head && mouth->type == kArgSymbol && head->type == kArgSymbol)
            registerTalker(0, xrefSymbol(d.getArg(3)), int(mouth->value), int(head->value));
        return true;
    }
    case kDlgAnimPhoneme:
        if (a0 && a1) {
            std::string code = conv_.talkers.empty() ? std::string() : conv_.talkers[0].code;
            if (code.empty() && !st.data.listA.empty())
                code = xrefSymbol(st.data.listA[0].getArg(3));
            registerTalker(0, code, int(a0->value), int(a1->value));
        }
        return true;
    case kDlgTalker: {
        std::string code = xrefSymbol(a2);
        if (!a0 || !a1 || code.empty())
            return true;
        int slot = int(conv_.talkers.size());
        for (int i = 0; i < int(conv_.talkers.size()); i++)
            if (text::equalsIgnoreCase(conv_.talkers[i].code, code))
                slot = i;
        if (slot >= kMaxTalkers)
            log::warning("Too many talkers");
        else
            registerTalker(slot, code, int(a0->value), int(a1->value));
        return true;
    }
    default: break;
    }

    if (!isSceneCommandAllowed(d.command))
        return true;
    // A scene command: the scene interpreter does the work; its waits (ANIMSUSPEND,
    // fades) hold up the conversation instead of the scene script.
    WaitType saved = thread_.wait;
    thread_.wait = kWaitNone;
    executeDirective(d);
    if (thread_.wait == kWaitAnim)
        conv_.waitAnim = thread_.waitArg;
    else if (thread_.wait == kWaitFade)
        conv_.waitFade = true;
    thread_.wait = saved;
    thread_.active = true;
    return conv_.waitAnim < 0 && !conv_.waitFade;
}

// ---- Speech and lip-sync --------------------------------------------------------------

int Game::phonemeFrame(int anim, int code) const {
    if (anim < 0 || anim >= int(anims_.size()) || code < 0 || code > 12)
        return 0;
    int count = anims_[anim].rle.frameCount();
    int frame = count == 13 ? code : kMouthFrames[code];
    return count ? std::min(frame, count - 1) : 0;
}

void Game::startSpeech(int line) {
    DialogueState& st = dialogues_[conv_.dialogue];
    conv_.speaking = true;
    conv_.speechStart = conv_.nodeStart = millis_;
    conv_.speechEnd = millis_;
    conv_.mouth = -1;
    conv_.phonemes = Phonemes();
    conv_.phonemeNode = 0;

    // The speaker code is in the clip name: <conversation 3><speaker 3><index 2>.
    std::string clip = line >= 0 && line < int(st.data.clips.size()) ? st.data.clips[line] : std::string();
    std::string code = clip.size() >= 6 ? clip.substr(3, 3) : std::string();
    const Talker* talker = nullptr;
    for (const Talker& t : conv_.talkers)
        if (!code.empty() && text::equalsIgnoreCase(t.code, code))
            talker = &t;
    log::debug(2, "Line %d (%s)%s", line, clip.c_str(), talker ? " with lip-sync" : "");

    std::vector<uint8_t> data;
    resources_.loadSpeech(line, data);
    if (talker) {
        // A talker's line plays only with its lip-sync data (0x24964).
        std::vector<uint8_t> phonemes;
        if (!resources_.loadPhonemes(line, phonemes) ||
            !readPhonemes(phonemes.data(), uint32_t(phonemes.size()), conv_.phonemes))
            data.clear();
        conv_.mouth = talker->mouth;
        if (!data.empty())
            speedStep_ = 3;
    }
    if (!data.empty()) {
        conv_.speechEnd = millis_ + uint32_t(uint64_t(data.size()) * 1000 / kSampleRate);
        if (audio::Mixer* mixer = system_->getMixer())
            conv_.speechHandle = mixer->play(
                std::make_unique<audio::RawStream>(std::move(data), audio::RawStream::Format::U8, kSampleRate),
                audio::SoundType::Speech, mixerVolume(kVolumeSpeech));
    }
    conv_.pauseEnd = conv_.speechEnd + kLinePauseMillis;
    updateSpeech();
}

void Game::updateSpeech() {
    if (millis_ < conv_.speechEnd) {
        // Move the mouth along the phoneme timeline; it starts two units in.
        const Phonemes& ph = conv_.phonemes;
        if (conv_.mouth >= 0 && !ph.nodes.empty()) {
            uint32_t clock = millis_ - conv_.speechStart + 2 * ph.unitMillis;
            while (conv_.phonemeNode + 1 < ph.nodes.size() &&
                   clock - (conv_.nodeStart - conv_.speechStart) >= ph.getNodeMillis(conv_.phonemeNode)) {
                conv_.nodeStart += ph.getNodeMillis(conv_.phonemeNode);
                conv_.phonemeNode++;
            }
            Anim& mouth = anims_[conv_.mouth];
            mouth.flags = kAnimFlagStop;
            mouth.rate = 0;
            mouth.frame = phonemeFrame(conv_.mouth, ph.nodes[conv_.phonemeNode].code);
        }
        return;
    }
    if (conv_.mouth >= 0 && conv_.mouth < int(anims_.size())) {
        anims_[conv_.mouth].frame = phonemeFrame(conv_.mouth, kRestMouth);
        conv_.mouth = -1;
    }
    speedStep_ = 1;
    if (millis_ >= conv_.pauseEnd)
        conv_.speaking = false;
}

// ---- The menu ---------------------------------------------------------------------------

void Game::layoutMenu(DialogueState& st) {
    auto label = [&](const ItemRef& ref) {
        const DialogueItem& item = st.data.choices[ref.group][ref.item];
        std::string text =
            item.choice < st.data.menuLabels.size() ? st.data.menuLabels[item.choice] : st.data.getMenuLabel(item.line);
        return text.substr(0, kMaxLabelLength);
    };
    auto width = [&](const std::string& text) {
        return font_.empty() ? int(text.size()) * 8 : font_.getStringWidth(text);
    };
    // The widest label of the whole dialogue decides which side the menu goes.
    int widest = 0;
    for (const std::string& l : st.data.menuLabels)
        widest = std::max(widest, width(l.substr(0, kMaxLabelLength)));
    conv_.lineHeight = font_.empty() ? 10 : std::max(1, font_.getLineHeight());
    conv_.menuWidths.clear();
    for (const ItemRef& ref : conv_.menu)
        conv_.menuWidths.push_back(width(label(ref)));

    int hx = 0, hy = 0, hw = 0;
    if (conv_.hotspot >= 0 && conv_.hotspot < int(hotspots_.size())) {
        hx = hotspots_[conv_.hotspot].x;
        hy = hotspots_[conv_.hotspot].y;
        hw = hotspots_[conv_.hotspot].w;
    }
    conv_.menuX = hx + hw + widest + 20 < kScreenWidth ? hx + hw + 10 : std::max(0, hx - (widest + 10));
    conv_.menuY = hy;
    int count = int(conv_.menu.size());
    if (conv_.menuY + (count + 1) * conv_.lineHeight > kScreenHeight)
        conv_.menuY = kScreenHeight - (count + 1) * conv_.lineHeight;
    conv_.menuY = std::max(0, conv_.menuY);
}

int Game::menuEntryAt(int x, int y) const {
    for (int i = 0; i < int(conv_.menu.size()); i++) {
        int top = conv_.menuY + i * conv_.lineHeight;
        if (x >= conv_.menuX && x <= conv_.menuX + conv_.menuWidths[i] && y >= top && y < top + conv_.lineHeight)
            return i;
    }
    return -1;
}

void Game::drawMenu(Surface& dst) {
    if (!conv_.active || !conv_.menuVisible)
        return;
    const DialogueState& st = dialogues_[conv_.dialogue];
    uint8_t normal = nearestColor(palette_, 40, 40, 40);
    uint8_t bright = nearestColor(palette_, 63, 63, 63);
    uint8_t shadow = nearestColor(palette_, 0, 0, 0);
    for (int i = 0; i < int(conv_.menu.size()); i++) {
        const ItemRef& ref = conv_.menu[i];
        const DialogueItem& item = st.data.choices[ref.group][ref.item];
        std::string text =
            item.choice < st.data.menuLabels.size() ? st.data.menuLabels[item.choice] : st.data.getMenuLabel(item.line);
        text = text.substr(0, kMaxLabelLength);
        int y = conv_.menuY + i * conv_.lineHeight;
        uint8_t color = (i == conv_.hover || i == conv_.picked) ? bright : normal;
        if (font_.empty())
            draw::frameRect(dst, conv_.menuX, y + 1, conv_.menuWidths[i], conv_.lineHeight - 2, color);
        else
            font_.drawString(dst, conv_.menuX, y, text, color, shadow);
    }
}

bool Game::handleConversationClick(int /*x*/, int /*y*/) {
    // (The click is taken where the cursor points, as in the game.)
    // While lines are spoken the mouse is ignored.
    if (!conv_.menuWaiting)
        return true;
    Point m = cursorHitPoint(true);
    int entry = menuEntryAt(m.x, m.y);
    if (entry >= 0) {
        pickChoice(entry);
        processConversationOps();
        return true;
    }
    // Another enabled hotspot ends the conversation, rudely, and is used.
    Point p = cursorHitPoint(false);
    int hotspot = findHotspotAt(p.x, p.y);
    if (hotspot >= 0 && hotspot != conv_.hotspot) {
        conv_.menuWaiting = false;
        abortConversation(true, hotspot);
    }
    return true;
}

std::vector<std::string> Game::getMenuLabels() const {
    std::vector<std::string> out;
    if (!conv_.active || !conv_.menuWaiting)
        return out;
    const DialogueState& st = dialogues_.at(conv_.dialogue);
    for (const ItemRef& ref : conv_.menu) {
        const DialogueItem& item = st.data.choices[ref.group][ref.item];
        out.push_back(item.choice < st.data.menuLabels.size() ? st.data.menuLabels[item.choice]
                                                              : st.data.getMenuLabel(item.line));
    }
    return out;
}

void Game::pickMenuEntry(int entry) {
    if (conv_.active && conv_.menuWaiting) {
        pickChoice(entry);
        processConversationOps();
    }
}

bool Game::debugTalk(const std::string& dialogue) {
    for (int i = 0; i < int(scenes_.size()); i++) {
        const auto& cd = scenes_[i].lists[kListDialogues];
        for (int k = 0; k < int(cd.size()); k++) {
            if (!text::equalsIgnoreCase(cd[k].name, dialogue))
                continue;
            if (conv_.active) {
                conv_.ops.clear();
                finishConversation();
            }
            if (scene_)
                leaveScene();
            start(i);
            if (conv_.active) {
                conv_.ops.clear();
                finishConversation();
            }
            thread_ = Thread();
            thread_.active = true;
            startConversation(k, -1);
            return conv_.active;
        }
    }
    return false;
}

// ---- Timers during a conversation ------------------------------------------------------

void Game::tickConversationTimers() {
    // Only global timers run while the menu waits. A handler ends with RETURNVAL:
    // #0 keeps waiting, x g.i / y g.i continue with menu / NPC group g, anything
    // else ends the conversation (rudely if even).
    for (Timer& t : timers_) {
        if (!t.defined || !t.active || t.count == 0 || (t.kind != kTimerGlobal && t.kind != 3))
            continue;
        if (--t.count != 0)
            continue;
        uint16_t v = runNestedHandler(t.label);
        if (v == 0x8000)
            return;
        DialogueState& st = dialogues_[conv_.dialogue];
        conv_.menuWaiting = false;
        if (v & 0xC0) {
            st.cur = (v & 0x7FFF) >> 8;
            st.replyMode = (v & 0x80) != 0;
            conv_.menuVisible = false;
            auto it = std::find_if(conv_.ops.begin(), conv_.ops.end(),
                                   [](const ConversationOp& o) { return o.type == ConversationOp::kMenu; });
            if (it != conv_.ops.end())
                conv_.ops.erase(it);
        } else {
            abortConversation((v & 1) == 0, -1);
        }
        return;
    }
}

uint16_t Game::runNestedHandler(int label) {
    Thread saved = thread_;
    thread_ = Thread();
    thread_.nested = true;
    jumpToLabel(label);
    runThread();
    uint16_t value = thread_.returnValue;
    thread_ = saved;
    return value;
}

} // namespace reluva::stharbinger
