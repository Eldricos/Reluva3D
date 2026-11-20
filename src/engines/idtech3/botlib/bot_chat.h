// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/bot_script.h"

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

/**
 * A chat line matched against the match templates (match.c): its type and
 * subtype, and where each variable (a name, a place...) is in the text.
 */
struct BotMatch {
    static constexpr int kVariables = 8;
    std::string text;
    int type = 0;
    int subtype = 0;
    // offset -1: not set
    std::array<int, kVariables> offset{};
    std::array<int, kVariables> length{};
    BotMatch() { offset.fill(-1); }
};

/**
 * The bots' chat: what each says when something happens (its chat file,
 * bots/`name`_t.c, by type: "game_enter", "kill_insult"...), its replies to
 * what others say (rchat.c), and the files they share: synonyms (syn.c),
 * random words (rnd.c) and the templates for understanding chat
 * (match.c). Behaviour and formats read from the GPL ioquake3 bot library
 * (be_ai_chat.c); docs/engines/idtech3/formats/aas.md; new code.
 *
 * Each bot's chat state has a handle from 1, as the bot library hands
 * them out; calls with a handle that isn't one do nothing.
 */
class BotChat {
public:
    static constexpr size_t kMessageSize = 256;
    static constexpr char kEscape = 1; // starts and ends a variable or random word in a message

    /**
     * Reads the shared files, their names from the library's settings
     * (`libVar`), each a default when unset; problems go to `log`.
     */
    void setup(const BotScript::Loader &load, const std::function<std::string(const std::string &)> &libVar,
               std::string *log = nullptr);
    std::function<float()> time; // the library's clock
    std::function<void(int client, const std::string &command)> command;

    /** A new chat state's handle, and freeing one for reuse. */
    ///@{
    int allocState();
    void freeState(int handle);
    ///@}
    /**
     * Gives a bot the chat called `name` in `file`, read once and shared by
     * the bots using it.
     * @return False if the handle isn't one, or the chat isn't there or can't be read.
     */
    bool loadChatFile(int handle, const BotScript::Loader &load, const std::string &file, const std::string &name);
    /** The bot's gender (1 female, 2 male, anything else none), which replies can ask for. */
    void setGender(int handle, int gender);
    /** The bot's name (up to 31 characters), which replies look for. */
    void setName(int handle, const std::string &name);

    /** A console message a bot has been sent (the game queues them). */
    struct ConsoleMessage {
        int handle = 0;
        float time = 0;
        int type = 0;
        std::string text;
    };
    /**
     * The bot's console messages: queued (up to 1024, each cut to
     * kMessageSize - 1 and given a handle from 1), removed by that handle,
     * the oldest (null if none) and how many.
     */
    ///@{
    void queueConsoleMessage(int handle, int type, const std::string &text);
    void removeConsoleMessage(int handle, int message);
    const ConsoleMessage *nextConsoleMessage(int handle) const;
    int numConsoleMessages(int handle) const;
    ///@}

    /**
     * Choosing a line: of a type from the bot's chat file, or the reply
     * that fits a message best. Kept until entered (said) or taken.
     * `vars` are the game's words (names, places) filling the line's
     * variables. A line just used isn't chosen again for a while.
     */
    ///@{
    int numInitialChats(int handle, const std::string &type) const;
    void initialChat(int handle, const std::string &type, int mcontext, const std::vector<const char *> &vars);
    bool replyChat(int handle, const std::string &message, int mcontext, int vcontext,
                   const std::vector<const char *> &vars);
    ///@}
    /** The chosen line's length (0: none). */
    int chatLength(int handle) const;
    /** Says the chosen line through `command`: to everyone (0) or the team (1). */
    void enterChat(int handle, int client, int sendTo);
    /** Takes the chosen line, without its '~'s. */
    std::string takeChatMessage(int handle);

    /**
     * Matches a line against the templates of `context` (bits), the first
     * that fits.
     * @return False if none does.
     */
    bool findMatch(const std::string &text, BotMatch &match, int context) const;
    /** The text of a match's variable, or empty if it isn't set. */
    static std::string matchVariable(const BotMatch &match, int variable);
    /**
     * Makes every run of white space one space, none at either end; any
     * character but letters, digits and ()?:'/,.[]-_+= counts as white
     * space.
     */
    static void unifyWhiteSpaces(std::string &s);
    /** Every synonym of the context's lists (bits) in `s` replaced by its list's first word. */
    void replaceSynonyms(std::string &s, int context) const;
    /** Where `b` first is in `a`, or -1. */
    static int stringContains(const std::string &a, const std::string &b, bool caseSensitive);

private:
    struct Message {
        std::string text;
        float time = -40; // when last used
    };
    struct Piece {
        bool variable = false;
        int index = 0;
        std::vector<std::string> strings; // alternatives
    };
    struct Template {
        int context = 0;
        int type = 0;
        int subtype = 0;
        std::vector<Piece> pieces;
    };
    struct Synonyms {
        int context = 0;
        std::vector<std::pair<std::string, float>> words; // the first is the one others become
        float total = 0;
    };
    struct ReplyKey {
        enum Kind { kString, kName, kBotNames, kFemale, kMale, kGenderless, kMatch } kind = kString;
        bool must = false;
        bool mustNot = false;
        std::string text;
        std::vector<Piece> match;
    };
    struct Reply {
        std::vector<ReplyKey> keys;
        float priority = 0;
        std::vector<Message> messages;
    };
    struct ChatType {
        std::string name;
        std::vector<Message> messages;
    };
    struct State {
        int gender = 0;
        std::string name;
        std::string message;
        std::shared_ptr<std::vector<ChatType>> chat;
        std::deque<ConsoleMessage> console;
        int nextHandle = 0;
    };
    State *state(int handle);
    const State *state(int handle) const;
    bool stringsMatch(const std::vector<Piece> &pieces, BotMatch &match) const;
    // A message's variables and random words filled in, up to ten times
    // over (random words may hold more), then weighted synonyms.
    std::string construct(const std::string &message, int mcontext, const BotMatch &match, int vcontext, bool reply);
    bool expand(std::string &out, const std::string &message, int mcontext, const BotMatch &match, int vcontext,
                bool reply);
    void replaceWeightedSynonyms(std::string &s, int context);
    void replaceReplySynonyms(std::string &s, int context) const;
    static BotMatch withVariables(BotMatch match, const std::vector<const char *> &vars);
    float now() const { return time ? time() : 0; }

    std::vector<Synonyms> _synonyms;
    std::vector<std::pair<std::string, std::vector<std::string>>> _randoms;
    std::vector<Template> _templates;
    std::vector<Reply> _replies;
    std::vector<std::unique_ptr<State>> _states;
    std::vector<std::pair<std::string, std::shared_ptr<std::vector<ChatType>>>> _chatFiles; // "file|name"
    std::mt19937 _random{12345};
};

} // namespace reluva::idtech3::botlib
