// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Where things go and what they say is the original's, as recordings show
// it (docs/engines/clue/systems/screen.md); what follows what is the game's own texts'
// (Data/<lang>/Text); the pictures, sounds and dialogs are its files'.

#include "engines/clue/game/play.h"

#include "audio/decode.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <istream>
#include <ostream>
#include <iterator>
#include <sstream>

namespace reluva::clue {

namespace {

constexpr int kW = 640;
constexpr int kBarTop = 413;
constexpr int kTickerTop = 462;
// The columns over the view: the cards and the notepad on the left, the
// choices, disproving and videos on the right.
constexpr int kLeftX = 5;
constexpr int kLeftY = 5;
constexpr int kRightX = 434;
constexpr int kRightY = 4;
constexpr int kVideoFrameX = 427;
constexpr int kVideoFrameY = 5;
constexpr int kVideoFrameW = 208;
constexpr int kVideoFrameH = 408;
// The card shown to a human, wider: from further left.
// Clue.exe 0x435AA1: dialog 33
constexpr int kShownX = 398;
constexpr int kShownY = 5;
// The drawer's three places, over the logo: the die (then the number
// rolled, then the end of the turn), the bulb (suggest), the handcuffs.
constexpr int kDrawerX = 511;
constexpr int kSlotY = 420;
constexpr int kSlotX[3] = {535, 574, 606};
constexpr int kSlotW[3] = {33, 27, 32};
constexpr int kSlotH = 42;
// How fast suspects walk, in world units a second: Clue.exe's 8 units a
// step, a step each 66 ms (0x4253C3, 1/66 at 0x451920): a square in 0.53 s.
constexpr double kWalkSpeed = 8000.0 / 66.0;
// How long each "can / cannot disprove" box shows (Clue.exe's 3000 ms,
// 0x437503).
constexpr int kToldMs = 3000;
// Between turns the camera goes to the next suspect, 2000 ms (0x4181F8).
constexpr int kTurnPanMs = 2000;
// The ticker: a sentence stays centred 8 s, then scrolls left at 3.5 pixels
// a second, coming round again from the right (as in recordings), between
// the ticker's ends.
// its text: Clue.exe 0x435630, 41 in, 558 wide
constexpr int kTickerStillMs = 8000;
constexpr int kTickerSpeed = 35;
constexpr int kTickerLeft = 41;
constexpr int kTickerRight = 599;

// dialogInfo.res's in-game dialogs.
enum {
    kChooseDialog = 30,
    kToldDialog = 31,
    kPickDialog = 32,
    kShownDialog = 33,
    kSolutionDialog = 34,
    kLoserDialog = 40
};
constexpr int kReturn = 10000;
constexpr int kOk = 10001;

// TickerStrings.txt: 663 lines a suspect, in the suspects' order.
constexpr int kTickerBlock = 663;
enum TickerLine {
    kYourTurn = 0,
    kRoll = 1,
    kRollPassage = 2,
    kRollSuggest = 3,
    kRollPassageSuggest = 4,
    kChooseObject = 5,
    kMakeMove = 6,
    kMaySuggest = 7,
    kAccuseOrEnd = 8,
    kEndTurnLine = 9,
    kTrapped = 10,
    kCanDisprove = 11,
    kCannotDisprove = 12,
    kAccusedRight = 13,
    kAccusedWrong = 14,
    kSuggests = 15,
    kAccuses = 339,
};

// The suspects in resource names, in sound and video names, and in the
// level.
const char *const kKey[kSuspectCount] = {"sca", "mus", "whi", "gre", "pea", "plu"};
const char *const kVoice[kSuspectCount] = {"MS", "CM", "MW", "MG", "MP", "PP"};
const char *const kCharacter[kSuspectCount] = {"character:Oscarlet", "character:Omustard", "character:Owhite",
                                               "character:Ogreen",   "character:Opeacock", "character:Oplum"};
const bool kFemale[kSuspectCount] = {true, false, true, false, true, false};
// The cards in the small cards' names (UI:A<key>card), in card order.
const char *const kCardKey[kCardCount] = {"sca", "mus", "whi", "gre", "pea", "plu", "kni", "can", "rev", "rop", "pip",
                                          "wre", "hal", "lou", "din", "kit", "bal", "con", "bil", "lib", "stu"};
// The weapons in the videos' names (CM_rope.avi).
const char *const kVideoWeapon[kWeaponCount] = {"dagger", "candle", "gun", "rope", "pipe", "wrench"};
// The prompts' spoken lines. Clue.exe speaks only the four that offer the
// die: its one voice queue (0x438F80) takes a turn's "..., it is your turn"
// (voice 34 + the suspect), a prompt chosen from the turn's options
// (0x439140: the die, with a suggestion, a passage or both, voices 29-32;
// anything without the die, none) and the disproving and accusing lines
// (40-63). Voices 28 ("please move"), 33 ("end turn", in the UK's files
// alone) and 64 ("choose object") are in its table of names (0x45E698) but
// nothing plays them, in 2.0 or 2.2; recordings of the original with the
// butler's voice on have the four prompts and never those three.
const char *promptVoice(int line) {
    switch (line) {
    case kRoll:
        return "roll die.WAV";
    case kRollPassage:
        return "roll die or use passage.WAV";
    case kRollSuggest:
        return "roll die or suggest.WAV";
    case kRollPassageSuggest:
        return "roll die passage or suggest.WAV";
    default:
        return nullptr;
    }
}

// Where cards go in the cards window (from the bottom right), and the
// holes in the choice window's summary for the suspect, room and weapon.
const int kHandX[6] = {111, 18, 111, 18, 111, 18};
const int kHandY[6] = {246, 246, 129, 129, 12, 12};
const UiRect kSummarySuspect{8, 40, 65, 173};
const UiRect kSummaryRoom{89, 104, 98, 76};
const UiRect kSummaryWeapon{90, 213, 73, 86};
// The notepad: its picture (UI:A...notepad) is ruled, each kind's rows 15
// apart below its line (at 22, 131 and 240), the columns of the colour, the
// two marks and the note's triangle 15 wide from 131: each row's cells are
// inside those lines.
const int kNoteRowY[3] = {23, 132, 241};
const int kNoteHeadY[3] = {4, 113, 223};
constexpr int kNoteTextX = 16;
constexpr int kNoteColourX = 132;
constexpr int kNoteMarkX[2] = {147, 162};
constexpr int kNoteTriangleX = 177;
constexpr int kNoteRowH = 14;
int noteRowY(int kind, int row) {
    return kNoteRowY[kind] + row * 15;
}
// The marks a click goes round, after none.
const char *const kMark[5] = {"UI:Anoteschk", "UI:Anotesx", "UI:Anoteso", "UI:Anoteschko", "UI:Anotesxo"};

// The board from above (UI:Aoverhead): where it goes, and its squares,
// 13 pixels each, column 9 and row 9 at (8, 15) in it.
constexpr int kMapX = 208;
constexpr int kMapY = 5;
constexpr int kMapW = 425;
constexpr int kMapH = 407;
constexpr int kMapSquare = 13;
constexpr int kMapCol0X = 8;
constexpr int kMapRow0Y = 15;
constexpr int kMapFirst = 9;

// The sounds the objects' reactions play: Clue.exe's table of sounds from
// its 61st (bear_g.wav) on, numbered as the objects' timings number them.
const char *const kObjectSound[] = {
    "bear_g.wav",     "bear_b.wav",     "rhino_b.wav",      "chest_open.wav",  "chest_close.wav", "pooltbl_n.wav",
    "rdgtbl_n.wav",   "book_fall.wav",  "stdesk_close.wav", "stdesk_open.wav", "clock_b.wav",     "phono_b.wav",
    "phonetbl_g.wav", "phonetbl_b.wav", "srvcrt_g.wav",     "vase.wav",        "pot_b.wav",       "trky_b.wav",
    "piano_b.wav",    "blsofa_b.wav",   "pitcher.wav",      "mnky_b.wav"};
// A hot spot's reactions: states 20 (good), 21 (bad) and 22 (neutral); the
// sound of each is the object's timing 1500, 1600 or 1700.
constexpr uint16_t kReactionFirst = 20;
constexpr uint16_t kReactionLast = 22;
// The weather, as Clue.exe has it (0x41C690, docs/engines/clue/systems/sounds.md): a
// cycle of 300 s, run each 66 ms tick. At set times the drapes blow
// (state 23, light; 24, heavy; 0, still) and the wind's gusts loop; between
// them, now and then (one tick in 100 or 80), a thunder. Each step: from
// the cycle's ms on, to a state, the drapes' state then (-1 unchanged), the
// gust looping then (0 none, 1 light, 2 heavy, -1 unchanged), and the
// thunders heard by chance until the next step (0 none, 1 small, 2 middle).
struct WeatherStep {
    int fromMs;
    int drapes;
    int gust;
    int chance;
};
constexpr WeatherStep kWeather[] = {
    {0, -1, -1, 0},      // still
    {60000, 23, 1, 1},   // light gusts
    {75000, 0, 0, 0},    // still
    {90000, 23, 1, 1},   // light gusts
    {120000, 24, 2, 2},  // heavy gusts
    {150000, -1, -1, 2}, // (a big thunder as it begins)
    {180000, 23, 1, 1},  // light gusts
    {210000, 0, 0, 0},   // still
    {225000, 23, 1, 1},  // light gusts
    {240000, 0, 0, 0},   // still to 300 s
};
constexpr int kWeatherCycleMs = 300000;
constexpr int kTickMs = 66;
const char *const kSmallThunder[] = {"SmallThunder6.wav", "SmallThunder7.wav", "SmallThunder8.wav"};
const char *const kMidThunder[] = {"MidThunder1.wav", "MidThunder2.wav", "MidThunder3.wav"};
const char *const kBigThunder[] = {"BigThunder4.wav", "BigThunder5.wav"};
const char *const kGust[] = {"", "wind light gusts.wav", "wind heavy gusts.wav"};
// Sounds heard near things in the house (furniture with a timing at 1900,
// its value which), as Clue.exe plays them (0x42F1C6): from the nearest
// such thing to where the camera looks, at 100 of 127 less 100 * d / 500,
// silent from 500 away; panned by its x on the floor.
const char *const kAmbientSound[] = {"LBfirepl_n.wav", "fntn_n.wav"};

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

std::vector<uint8_t> readFile(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::string> readLines(const GameData &data, const char *file) {
    std::vector<std::string> lines;
    if (const std::filesystem::path *p = data.find(file)) {
        const auto bytes = readFile(*p);
        std::istringstream in(std::string(bytes.begin(), bytes.end()));
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
    }
    return lines;
}

// The facing (0 n ... 7 nw; n is -y, e +x) of a step.
int facingOf(int dx, int dy) {
    const int sx = (dx > 0) - (dx < 0);
    const int sy = (dy > 0) - (dy < 0);
    static const int kFacing[3][3] = {{7, 6, 5}, {0, 2, 4}, {1, 2, 3}}; // [sx + 1][sy + 1]
    if (sx == 0 && sy == 0)
        return -1;
    if (sx == 0)
        return sy < 0 ? 0 : 4;
    return kFacing[sx + 1][sy + 1] == 2 && sy != 0 ? (sy < 0 ? 1 : 3) : kFacing[sx + 1][sy + 1];
}

} // namespace

Play::Play(System &sys, Surface &screen, const GameData &data, audio::Mixer *mixer, const Resi &resi,
           const Fonts &fonts, Mansion &mansion, const Level &level)
    : _sys(sys), _screen(screen), _data(data), _mixer(mixer), _resi(resi), _fonts(fonts), _mansion(mansion),
      _level(level) {
}

Play::~Play() {
    stopSounds();
}

Error Play::startNetwork(netplay::Session &session) {
    // The players as the host's lobby has them; the same deal everywhere.
    std::array<Player, kSuspectCount> players;
    for (int s = 0; s < kSuspectCount; ++s) {
        const netplay::Seat &seat = session.lobby().seats[size_t(s)];
        players[size_t(s)].kind = seat.kind == netplay::Seat::kHuman      ? Player::kHuman
                                  : seat.kind == netplay::Seat::kComputer ? Player::kComputer
                                                                          : Player::kNone;
        players[size_t(s)].level = seat.level;
        players[size_t(s)].name = seat.name;
        _owner[size_t(s)] = seat.owner;
    }
    _net = &session;
    return start(players, session.lobby().noDieRoll, session.seed());
}

Error Play::start(const std::array<Player, kSuspectCount> &players, bool noDieRoll, uint32_t seed) {
    if (Error err = _board.build(_level))
        return err;
    _players = players;
    _seed = seed;
    std::array<bool, kSuspectCount> playing{};
    for (int s = 0; s < kSuspectCount; ++s)
        playing[size_t(s)] = players[size_t(s)].kind != Player::kNone;
    if (Error err = _game.start(_board, playing, noDieRoll, seed))
        return err;
    // Every seat has a computer's mind: for the computer players, and for
    // humans out of the game who let it disprove for them.
    for (int s = 0; s < kSuspectCount; ++s)
        if (playing[size_t(s)])
            _ai[size_t(s)].reset(_game, s, seed * 7919u + uint32_t(s),
                                 players[size_t(s)].kind == Player::kComputer ? players[size_t(s)].level
                                                                              : ComputerPlayer::kExpert);
    for (int s = 0; s < kSuspectCount; ++s) {
        Board::standingPoint(_board.start(s), _walkers[size_t(s)].x, _walkers[size_t(s)].y);
        _walkers[size_t(s)].facing = 4;
    }
    buildWalkGrid();
    for (auto &m : _shownBy)
        m.fill(-1);
    // Each player's notepad starts empty, Auto Notes or not (Clue.exe's
    // only use of it is for the cards shown).
    for (auto &pad : _notes)
        for (NoteRow &row : pad)
            row = {};
    readTexts();
    beginTurn();
    return Error::success();
}

void Play::readTexts() {
    _tickerLines = readLines(_data, "TickerStrings.txt");
    // Notepad.txt: each kind's heading, then its cards.
    _cardNames.clear();
    _kindNames = {"Suspects", "Weapons", "Rooms"};
    size_t kind = 0;
    bool heading = true;
    for (const std::string &l : readLines(_data, "Notepad.txt")) {
        if (l.empty()) {
            heading = true;
            continue;
        }
        if (l.rfind("//", 0) == 0)
            continue;
        if (heading && kind < _kindNames.size())
            _kindNames[kind++] = l;
        else
            _cardNames.push_back(l);
        heading = false;
    }
    _cardNames.resize(kCardCount);
}

bool Play::canSave() const {
    return !_net && _window == Window::kNone && _steps.empty() && !busy() && humanTurn() &&
           _game.phase() != Game::Phase::kOver && _game.phase() != Game::Phase::kDisprove;
}

void Play::save(std::ostream &out) const {
    out << "play 1\n";
    for (const Player &p : _players) {
        out << int(p.kind) << ' ' << p.level << ' ';
        saveio::putString(out, p.name);
        out << '\n';
    }
    out << _seed << '\n';
    _game.save(out);
    for (int s = 0; s < kSuspectCount; ++s)
        if (_players[size_t(s)].kind != Player::kNone)
            _ai[size_t(s)].save(out);
    for (int s = 0; s < kSuspectCount; ++s) {
        const Walker &w = _walkers[size_t(s)];
        out << _autoDisprove[size_t(s)] << ' ' << w.x << ' ' << w.y << ' ' << w.facing << '\n';
        for (int c = 0; c < kCardCount; ++c) {
            const NoteRow &row = _notes[size_t(s)][size_t(c)];
            out << row.colour << ' ' << row.marks[0] << ' ' << row.marks[1] << ' ' << _shownBy[size_t(s)][size_t(c)]
                << ' ';
            saveio::putString(out, row.text);
        }
        out << '\n';
    }
}

Error Play::load(std::istream &in) {
    const Error damaged = Error::fail("the saved game is damaged");
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version) || tag != "play" || version != 1)
        return Error::fail("not a saved game of Reluva's");
    for (Player &p : _players) {
        int kind = 0;
        if (!(in >> kind >> p.level) || kind < Player::kNone || kind > Player::kComputer || p.level < 0 ||
            p.level > 2 || in.get() != ' ' || !saveio::getString(in, p.name))
            return damaged;
        p.kind = Player::Kind(kind);
    }
    if (!(in >> _seed))
        return damaged;
    if (Error err = _board.build(_level))
        return err;
    buildWalkGrid();
    if (Error err = _game.load(in, _board))
        return err;
    for (int s = 0; s < kSuspectCount; ++s)
        if (_players[size_t(s)].kind != Player::kNone)
            if (Error err = _ai[size_t(s)].load(in, _game))
                return err;
    for (int s = 0; s < kSuspectCount; ++s) {
        Walker &w = _walkers[size_t(s)];
        bool autoDisprove = false;
        if (!(in >> autoDisprove >> w.x >> w.y >> w.facing) || w.facing < 0 || w.facing > 7)
            return damaged;
        _autoDisprove[size_t(s)] = autoDisprove;
        w.walking = false;
        w.route.clear();
        for (int c = 0; c < kCardCount; ++c) {
            NoteRow &row = _notes[size_t(s)][size_t(c)];
            if (!(in >> row.colour >> row.marks[0] >> row.marks[1] >> _shownBy[size_t(s)][size_t(c)]) ||
                in.get() != ' ' || !saveio::getString(in, row.text) || row.colour < -1 || row.colour >= kSuspectCount ||
                row.marks[0] < 0 || row.marks[0] > 5 || row.marks[1] < 0 || row.marks[1] > 5)
                return damaged;
        }
    }
    readTexts();
    // On, from the start of the turn's sentence; a move half planned is
    // planned again.
    _plan.clear();
    say(line(_game.turn(), kYourTurn));
    centreOn(_game.turn());
    return Error::success();
}

// ---- The sequence of events ------------------------------------------------

void Play::pause(int ms) {
    then([this, ms] { _waitUntil = _now + uint64_t(ms); });
}

bool Play::busy() const {
    if (_now < _waitUntil || _video)
        return true;
    if (_window == Window::kPick || _window == Window::kShown || _window == Window::kSolution ||
        _window == Window::kLoser || _awaitingShow)
        return true;
    return std::any_of(_walkers.begin(), _walkers.end(), [](const Walker &w) { return w.walking; });
}

void Play::runSteps() {
    while (!busy() && !_steps.empty()) {
        auto step = std::move(_steps.front());
        _steps.pop_front();
        step();
    }
}

bool Play::humanTurn() const {
    return _players[size_t(_game.turn())].kind == Player::kHuman;
}

bool Play::local(int seat) const {
    return _players[size_t(seat)].kind == Player::kHuman && (!_net || _owner[size_t(seat)] == _net->me());
}

bool Play::localTurn() const {
    return local(_game.turn());
}

int Play::viewer() const {
    // Over a network, this machine's player.
    if (_net) {
        for (int s = 0; s < kSuspectCount; ++s)
            if (local(s))
                return s;
        return -1;
    }
    // At one machine, the human whose turn it is; in a computer's turn,
    // the human who played last before it.
    for (int k = 0; k < kSuspectCount; ++k) {
        const int s = (_game.turn() - k + kSuspectCount) % kSuspectCount;
        if (_players[size_t(s)].kind == Player::kHuman)
            return s;
    }
    return -1;
}

void Play::beginTurn() {
    const int s = _game.turn();
    _plan.clear();
    _cpuPending = false;
    // Hot seat: one human's cards aren't left open for the next.
    if (_panel != Panel::kNone && viewer() != _panelOwner)
        _panel = Panel::kNone;
    then([this, s] { centreOn(s); });
    pause(kTurnPanMs);
    then([this, s] {
        say(line(s, kYourTurn));
        sound(std::string("turn_") + kVoice[s] + ".WAV", true);
    });
    if (localTurn())
        then([this] { promptHuman(); });
}

int Play::promptLine() const {
    switch (_game.phase()) {
    case Game::Phase::kStart:
        if (_game.trapped())
            return kTrapped;
        if (_game.noDieRoll())
            return _game.canSuggest() ? kMaySuggest : kMakeMove;
        if (_game.canTakePassage())
            return _game.canSuggest() ? kRollPassageSuggest : kRollPassage;
        return _game.canSuggest() ? kRollSuggest : kRoll;
    case Game::Phase::kMoving:
        return kMakeMove;
    case Game::Phase::kMoved:
        return _game.canSuggest() ? kMaySuggest : kAccuseOrEnd;
    default:
        return kAccuseOrEnd;
    }
}

void Play::promptHuman() {
    // Spoken only: the ticker keeps "..., it is your turn." through a
    // human's turn, as recorded (the prompts' sentences are in
    // TickerStrings.txt all the same).
    if (const char *v = promptVoice(promptLine()))
        sound(v, true);
}

void Play::afterAction() {
    if (_game.phase() != Game::Phase::kOver && localTurn())
        promptHuman();
}

// ---- Actions ----------------------------------------------------------------------

void Play::perform(const netplay::Command &c) {
    if (!_net) {
        apply(c);
        return;
    }
    // Over a network: to the host, to be played out by everyone in its order.
    _net->submit(c);
    _pending = true;
}

namespace {

netplay::Command command(netplay::Command::Type type, int seat, std::vector<int16_t> args = {}) {
    netplay::Command c;
    c.type = type;
    c.seat = uint8_t(seat);
    c.args = std::move(args);
    return c;
}

} // namespace

void Play::apply(const netplay::Command &c) {
    using netplay::Command;
    if (c.seat == _game.turn() || c.type == Command::kShow)
        _pending = false;
    // Whoever's it is: the player whose turn it is, but for showing a card
    // (the disprover's) and a player gone (any).
    if (c.type != Command::kShow && c.type != Command::kTakeOver && c.seat != _game.turn())
        return;
    const int s = c.seat;
    switch (c.type) {
    case Command::kRoll:
        if (!_game.canRoll())
            return;
        _game.rollDie();
        _rollStart = _now;
        _plan.clear();
        sound("dice shake and roll.WAV");
        pause(900);
        then([this] { afterAction(); });
        break;
    case Command::kWalk: {
        std::vector<Place> path;
        for (size_t i = 0; i + 1 < c.args.size(); i += 2)
            path.push_back(c.args[i] < 0 ? Place{{}, c.args[i + 1]} : Place{{c.args[i], c.args[i + 1]}, kNoRoom});
        _plan.clear();
        if (_game.walk(path)) {
            if (local(s))
                sound("nogo.wav");
            return;
        }
        startWalk(path);
        then([this] { afterAction(); });
        break;
    }
    case Command::kPassage:
        if (_game.takePassage())
            return;
        _plan.clear();
        sound("door open.wav");
        placeWalkers(true); // out of the passage
        centreOn(s);
        pause(500);
        then([this] { afterAction(); });
        break;
    case Command::kSuggest:
        if (c.args.size() < 2 || _game.suggest(c.args[0], c.args[1]))
            return;
        suggestionMade();
        break;
    case Command::kAccuse:
        if (c.args.size() < 3 || !_game.canAccuse())
            return;
        accusationMade({c.args[0], c.args[1], c.args[2]});
        break;
    case Command::kEndTurn:
        if (!_game.canEndTurn() && _game.phase() != Game::Phase::kMoved && _game.phase() != Game::Phase::kSuggested)
            return;
        endTurn();
        break;
    case Command::kShow:
        if (c.args.empty() || _game.phase() != Game::Phase::kDisprove || _game.disprover() != s ||
            _game.show(c.args[0]))
            return;
        _awaitingShow = false;
        if (_window == Window::kPick) {
            _window = Window::kNone;
            _dialog.reset();
        }
        break;
    case Command::kRoomWalk: {
        // About its room (Clue.exe sends the click to every machine, 0x41BD90).
        if (c.args.size() < 2 || !_game.place(s).inRoom())
            return;
        auto route = roomRoute(c.args[0], c.args[1]);
        if (route.empty())
            return;
        if (!_plan.empty()) {
            _plan.clear();
            if (local(s))
                sound("unting.wav");
        }
        Walker &w = _walkers[size_t(s)];
        w.route = std::move(route);
        if (!_look.walkAnimations) {
            w.x = w.route.back().first;
            w.y = w.route.back().second;
            w.route.clear();
            w.walking = false;
            break;
        }
        w.walking = true;
        w.fx = w.x;
        w.fy = w.y;
        _follow = s;
        break;
    }
    case Command::kTakeOver:
        // A player gone: the computer plays on for them.
        if (_players[size_t(s)].kind != Player::kHuman)
            return;
        _players[size_t(s)].kind = Player::kComputer;
        _players[size_t(s)].level = ComputerPlayer::kNormal;
        say(_players[size_t(s)].name + " has left the game.");
        if (_awaitingShow && _game.phase() == Game::Phase::kDisprove && _game.disprover() == s) {
            _game.show(_ai[size_t(s)].chooseShow(_game, _game.turn()));
            _awaitingShow = false;
            if (_window == Window::kPick) {
                _window = Window::kNone;
                _dialog.reset();
            }
        }
        break;
    }
}

void Play::computerTurn() {
    const int s = _game.turn();
    ComputerPlayer &me = _ai[size_t(s)];
    // Clue.exe's computer players wait before each step, 2 on a clock of
    // whole seconds (1-2 s, 0x423775 with 0x462B7C): once before
    // suggesting at the start of a turn, twice before rolling (the turn's
    // start, then the roll), once before moving; not before suggesting on
    // arriving, nor accusing.
    if (!_cpuPending) {
        _pendingAction = me.decide(_game);
        _cpuPending = true;
        _waits = 0;
    }
    using A = ComputerPlayer::Action;
    const bool atStart = _game.phase() == Game::Phase::kStart;
    const int need = _pendingAction == A::kRoll || _pendingAction == A::kPassage                ? 2
                     : _pendingAction == A::kWalk || (_pendingAction == A::kSuggest && atStart) ? 1
                                                                                                : 0;
    if (_waits < need) {
        ++_waits;
        then([this] { _waitUntil = (_now / 1000 + 2) * 1000; });
        return;
    }
    _cpuPending = false;
    switch (_pendingAction) {
    case ComputerPlayer::Action::kRoll:
        _game.rollDie();
        _rollStart = _now;
        sound("dice shake and roll.WAV");
        break;
    case ComputerPlayer::Action::kWalk: {
        const auto path = me.chooseWalk(_game);
        if (_game.walk(path))
            _game.endTurn(); // can't be: the path is one the rules gave
        startWalk(path);
        break;
    }
    case ComputerPlayer::Action::kPassage:
        _game.takePassage();
        placeWalkers(true); // out of the passage
        centreOn(s);
        pause(800);
        break;
    case ComputerPlayer::Action::kSuggest: {
        const Accusation a = me.chooseSuggestion(_game);
        _game.suggest(a.suspect, a.weapon);
        suggestionMade();
        break;
    }
    case ComputerPlayer::Action::kAccuse:
        accusationMade(me.chooseAccusation());
        break;
    case ComputerPlayer::Action::kEndTurn:
        endTurn();
        break;
    }
    pause(500);
}

void Play::endTurn() {
    if (_game.endTurn())
        return;
    beginTurn();
}

namespace {

// Clue.exe's divisions towards 0 (its grids).
int cell32(int v) {
    return (v + (v < 0 ? 31 : 0)) >> 5;
}

} // namespace

void Play::buildWalkGrid() {
    // As each kind of object marks the cells under its box (0x427FC0, from
    // its set-up): furniture but the wall lamps (secondary type 1), the hot
    // spots, the secret passages, and walls standing on the floor (their
    // height and box's under 6).
    _blocked.clear();
    _passages.clear();
    for (const Mansion::Item &it : _mansion.items()) {
        const Animation::Frame &f0 = it.animation->frames[0];
        bool blocks = false;
        switch (it.kind) {
        case 0:
            blocks = it.box.z + f0.solid <= 5;
            break;
        case 3:
            blocks = it.subKind != 1;
            break;
        case 4:
        case 5:
        case 8:
            blocks = true;
            break;
        default:
            break;
        }
        if (blocks && f0.solid != 0)
            for (int x = it.box.x; x < it.box.x + it.box.sizeX; x += 32)
                for (int y = it.box.y; y < it.box.y + it.box.sizeY; y += 32)
                    _blocked.insert({cell32(x), cell32(y)});
        if (it.kind == 8) {
            Passage p;
            p.x = it.worldX;
            p.y = it.worldY;
            // Its timings: 1000 its room's zone (rooms 1-9), 1300 the way it
            // faces.
            for (const Timing &t : _mansion.level()->placements[it.placement].timings) {
                if (t.time == 1000)
                    p.room = int(t.raw[3]) - 1;
                if (t.time == 1300)
                    p.facing = int(t.raw[3]);
            }
            _passages.push_back(p);
        }
    }
}

std::deque<std::pair<int, int>> Play::roomRoute(int worldX, int worldY) const {
    // As Clue.exe 0x423A90 and 0x424900 do a click on the room's own floor:
    // refused on its floor pieces with key 1100; else the suspect walks
    // there, round what stands in the room, moving nowhere in the game.
    // Every machine works the same way out from the same places.
    const int s = _game.turn();
    const int room = _game.place(s).room;
    for (const Mansion::Item &it : _mansion.items())
        if (it.kind == 1 && worldX >= it.box.x && worldX < it.box.x + it.box.sizeX && worldY >= it.box.y &&
            worldY < it.box.y + it.box.sizeY)
            for (const Timing &t : _mansion.level()->placements[it.placement].timings)
                if (t.time == 1100)
                    return {};
    // A click on the floor goes round the others too, with no second try
    // (0x424A38: the cells' mask 0xFF).
    const Walker &w = _walkers[size_t(s)];
    return wayFind(s, room, w.x, w.y, worldX, worldY, true);
}

std::set<std::pair<int, int>> Play::othersCells(int suspect) const {
    std::set<std::pair<int, int>> cells;
    for (int k = 0; k < kSuspectCount; ++k)
        if (k != suspect) {
            const Walker &w = _walkers[size_t(k)];
            const int ex = w.route.empty() ? w.x : w.route.back().first;
            const int ey = w.route.empty() ? w.y : w.route.back().second;
            cells.insert({cell32(ex), cell32(ey)});
        }
    return cells;
}

std::deque<std::pair<int, int>> Play::wayFind(int suspect, int room, int fromX, int fromY, int toX, int toY,
                                              bool aroundOthers) const {
    // Clue.exe's way-finder (0x41A0C0), over the house's 32-unit cells (their
    // middles the points, 0x421100). Each cell has flags: 1 where a thing's
    // box stands (0x427FC0), and a suspect's own bit, 2 << suspect, where it
    // stands (0x424F10, 0x425DC0; cleared as it leaves, 0x425710). A cell is
    // blocked if its flags meet the mask the caller gives: 0xFF, round
    // things and suspects alike; 1, round things only. From the start's
    // cell, breadth first, until the end's is reached. A step goes up,
    // down, right, left, then up-right, down-right, down-left, up-left, to a
    // cell not blocked and in the same room; a slanting step only when
    // neither cell it passes between is blocked. Then back from the end,
    // each time to the first cell in that order that is free, in the room
    // and a step nearer (0x41A7D4); the way is those cells, the start's
    // left out.
    using Cell = std::pair<int, int>;
    const std::set<Cell> others = aroundOthers ? othersCells(suspect) : std::set<Cell>{};
    const auto blocked = [&](const Cell &c) { return _blocked.count(c) != 0 || others.count(c) != 0; };
    const auto open = [&](const Cell &c) {
        return !blocked(c) && _board.roomAt(Board::squareAt(c.first * 32 + 16, c.second * 32 + 16)) == room;
    };
    static const int kStep[8][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
    const Cell from{cell32(fromX), cell32(fromY)};
    const Cell to{cell32(toX), cell32(toY)};
    if (!open(to) || from == to)
        return {};
    std::map<Cell, int> steps{{from, 0}};
    std::deque<Cell> queue{from};
    bool reached = false;
    while (!queue.empty() && steps.size() < 4096) {
        const Cell c = queue.front();
        queue.pop_front();
        if (c == to) {
            reached = true;
            break;
        }
        for (const auto &d : kStep) {
            const Cell n{c.first + d[0], c.second + d[1]};
            if (steps.count(n) || !open(n))
                continue;
            if (d[0] && d[1] && (blocked({c.first, c.second + d[1]}) || blocked({c.first + d[0], c.second})))
                continue;
            steps[n] = steps.at(c) + 1;
            queue.push_back(n);
        }
    }
    if (!reached)
        return {};
    std::deque<std::pair<int, int>> route;
    Cell c = to;
    for (int k = steps.at(to); k > 0; --k) {
        route.push_front({c.first * 32 + 16, c.second * 32 + 16});
        // The start's cell isn't part of the way, so it needn't be free: it
        // may stand under a thing (a door's square's corner can). Clue.exe's
        // loop (0x41A7F4) stops by its count and returns the way whatever.
        if (k == 1)
            break;
        bool back = false;
        for (const auto &d : kStep) {
            const Cell n{c.first + d[0], c.second + d[1]};
            const auto it = steps.find(n);
            if (it != steps.end() && it->second == k - 1 && open(n)) {
                c = n;
                back = true;
                break;
            }
        }
        if (!back)
            return {};
    }
    return route;
}

std::pair<int, int> Play::roomSpot(int suspect, int room, int fromX, int fromY) const {
    // The cells the others stand on (where they are going) are taken.
    const std::set<std::pair<int, int>> taken = othersCells(suspect);
    // Squares of 128, 160 ... 288 around the point, 32 apart, each from its
    // largest y and x: the first cell free, in the room, not a door's.
    for (int r = 128; r < 320; r += 32)
        for (int y = fromY + r; y >= fromY - r; y -= 32)
            for (int x = fromX + r; x >= fromX - r; x -= 32) {
                const std::pair<int, int> c{cell32(x), cell32(y)};
                if (_blocked.count(c) || taken.count(c))
                    continue;
                const Square sq = Board::squareAt(c.first * 32 + 32, c.second * 32 + 32);
                if (_board.roomAt(sq) != room)
                    continue;
                const bool door = std::any_of(_board.doors().begin(), _board.doors().end(),
                                              [&](const Board::Door &d) { return d.inside == sq; });
                if (!door)
                    return {x, y};
            }
    // None: the room's first square (it can't be, with six suspects).
    int x = 0;
    int y = 0;
    if (!_board.roomSquares(room).empty())
        Board::standingPoint(_board.roomSquares(room).front(), x, y);
    return {x, y};
}

void Play::startWalk(const std::vector<Place> &path) {
    // The game has the suspect there already; this walks the picture.
    const int s = _game.turn();
    Walker &w = _walkers[size_t(s)];
    w.route.clear();
    const auto point = [](const Square &sq) {
        int x;
        int y;
        Board::standingPoint(sq, x, y);
        return std::pair<int, int>{x, y};
    };
    // Out of a room: to the inside of the door taken first.
    if (!path.empty() && !path.front().inRoom())
        for (const Board::Door &d : _board.doors())
            if (d.outside == path.front().square && _board.roomAt(_board.squareAt(w.x, w.y)) == d.room) {
                w.route.push_back(point(d.inside));
                break;
            }
    for (size_t i = 0; i < path.size(); ++i) {
        const Place &p = path[i];
        if (!p.inRoom()) {
            w.route.push_back(point(p.square));
            continue;
        }
        // Into a room: through its door, to a free place in it looked for
        // from the door's square's middle (Clue.exe's 0x4242A2), the way
        // there the way-finder's from the door's square (0x424309): round
        // things and suspects, else round things only (0x42434F); straight,
        // if neither finds one.
        int fromX = w.x;
        int fromY = w.y;
        const Board::Door *door = nullptr;
        for (const Board::Door &d : _board.doors())
            if (d.room == p.room && i > 0 && d.outside == path[i - 1].square) {
                door = &d;
                w.route.push_back(point(d.inside));
                fromX = Board::kGridOrigin + d.inside.x * Board::kSquareSize + 32;
                fromY = Board::kGridOrigin + d.inside.y * Board::kSquareSize + 32;
                break;
            }
        const std::pair<int, int> spot = roomSpot(s, p.room, fromX, fromY);
        std::deque<std::pair<int, int>> way;
        if (door) {
            const int cornerX = fromX - 32;
            const int cornerY = fromY - 32;
            way = wayFind(s, p.room, cornerX, cornerY, spot.first, spot.second, true);
            if (way.empty())
                way = wayFind(s, p.room, cornerX, cornerY, spot.first, spot.second, false);
        }
        if (way.empty())
            w.route.push_back(spot);
        else
            w.route.insert(w.route.end(), way.begin(), way.end());
    }
    if (!_look.walkAnimations || w.route.empty()) {
        if (!w.route.empty()) {
            w.x = w.route.back().first;
            w.y = w.route.back().second;
        }
        w.route.clear();
        centreOn(s);
        return;
    }
    w.walking = true;
    w.fx = w.x;
    w.fy = w.y;
    _follow = s;
}

void Play::placeWalkers(bool walk) {
    // Each suspect to where the game has them: after a passage, after being
    // brought in by a suggestion, after being moved off a door. At once,
    // but with `walk` the one whose turn it is, out of a passage, walks to
    // its place as Clue.exe has it (0x425CC0: put 32 in front of the
    // passage, its place looked for from there, the way by the way-finder
    // through 0x424900, round things and suspects; at once if there's none).
    for (int s = 0; s < kSuspectCount; ++s) {
        Walker &w = _walkers[size_t(s)];
        if (w.walking)
            continue;
        const Place &p = _game.place(s);
        if (p.inRoom()) {
            if (_board.roomAt(_board.squareAt(w.x, w.y)) != p.room) {
                // Through a secret passage: from 32 in front of the one in
                // the room (0x425CC0); brought in by a suggestion: from
                // where the suggester stands (0x41BEA0).
                int fromX = _walkers[size_t(_game.turn())].x;
                int fromY = _walkers[size_t(_game.turn())].y;
                bool passage = false;
                if (s == _game.turn())
                    for (const Passage &ps : _passages)
                        if (ps.room == p.room) {
                            fromX = ps.x + (ps.facing == 2 ? 32 : ps.facing == 6 ? -32 : 0);
                            fromY = ps.y + (ps.facing == 4 ? 32 : ps.facing == 0 ? -32 : 0);
                            passage = true;
                        }
                const auto [x, y] = roomSpot(s, p.room, fromX, fromY);
                std::deque<std::pair<int, int>> way;
                if (walk && passage && _look.walkAnimations)
                    way = wayFind(s, p.room, fromX, fromY, x, y, true);
                if (way.empty()) {
                    w.x = x;
                    w.y = y;
                    continue;
                }
                w.x = fromX;
                w.y = fromY;
                w.route = std::move(way);
                w.walking = true;
                w.fx = w.x;
                w.fy = w.y;
                _follow = s;
            }
        } else
            Board::standingPoint(p.square, w.x, w.y);
    }
}

void Play::suggestionMade() {
    const int s = _game.turn();
    const Accusation a = _game.suggestion();
    say(line(s, kSuggests + a.suspect * kWeaponCount * kRoomCount + a.weapon * kRoomCount + a.room));
    placeWalkers(false); // the suspect named, brought in
    pause(1800);
    if (_look.suggestMovies) {
        // Clue.exe 0x41B9EA: the CD stopped for the video, and after it one
        // of the game's tracks by chance (0x41BA19).
        then([this, a] {
            _musicCue = -1;
            startVideo(std::string(kVoice[a.suspect]) + "_" + kVideoWeapon[a.weapon] + ".avi");
        });
        then([this] { _musicCue = 2 + int(_ambientRng() % 3); });
    }
    // Those who can't disprove it, in turn, and the one who can.
    for (int p : _game.passed()) {
        then([this, p] {
            showTold(p, false);
            say(line(p, kCannotDisprove));
            sound(std::string("cannot disprove ") + kVoice[p] + ".WAV", true);
        });
        pause(kToldMs);
    }
    const int d = _game.disprover();
    if (d >= 0) {
        then([this, d] {
            showTold(d, true);
            say(line(d, kCanDisprove));
            sound(std::string("can disprove ") + kVoice[d] + ".WAV", true);
        });
        pause(kToldMs);
        then([this, d, s] {
            if (_players[size_t(d)].kind != Player::kHuman) {
                _game.show(_ai[size_t(d)].chooseShow(_game, s));
                return;
            }
            // A human's: their choice, by hand or (out of the game, if
            // they chose so) by the computer, on their own machine; the
            // others wait for it.
            _awaitingShow = true;
            if (!local(d))
                return;
            if (_autoDisprove[size_t(d)])
                perform(command(netplay::Command::kShow, d, {int16_t(_ai[size_t(d)].chooseShow(_game, s))}));
            else
                openPick();
        });
        then([this, s] {
            _told.reset();
            if (local(s))
                openShown();
        });
    }
    then([this, s, a, d] {
        _told.reset();
        for (int k = 0; k < kSuspectCount; ++k)
            if (_players[size_t(k)].kind != Player::kNone)
                _ai[size_t(k)].observe(_game);
        if (_game.shown() >= 0) {
            _shownBy[size_t(s)][size_t(_game.shown())] = d;
            // Auto Notes, as Clue.exe's (0x43a070, after the card is shown):
            // the card's row in the colour of who showed it, and ticked.
            NoteRow &row = _notes[size_t(s)][size_t(_game.shown())];
            if (_look.autoNotes) {
                row.colour = d;
                row.marks[0] = 1;
            }
        }
        afterAction();
    });
}

void Play::accusationMade(const Accusation &a) {
    const int s = _game.turn();
    say(line(s, kAccuses + a.suspect * kWeaponCount * kRoomCount + a.weapon * kRoomCount + a.room));
    pause(2000);
    then([this, s, a] {
        _game.accuse(a);
        if (_game.winner() == s) {
            // Right: the solution, then (Continue) the murderer in prison,
            // then the main menu (as recorded).
            say(line(s, kAccusedRight));
            sound(std::string("accused correct ") + kVoice[s] + ".WAV", true);
            pause(1500);
            then([this] { showSolution(3); });
            if (_look.suggestMovies) {
                const int murderer = _game.solution().suspect;
                then([this, murderer] { startVideo(std::string(kVoice[murderer]) + "_prison.avi"); });
            }
            then([this] { _finished = true; });
            return;
        }
        for (int k = 0; k < kSuspectCount; ++k)
            if (_players[size_t(k)].kind != Player::kNone)
                _ai[size_t(k)].observeAccusation(a, s);
        say(line(s, kAccusedWrong));
        sound(std::string("accuse incorrect ") + kVoice[s] + ".WAV", true);
        placeWalkers(false); // off a doorway
        pause(1500);
        // With no human left in the game, it is over: the solution, then
        // the main menu. Else a human out is asked how to go on disproving.
        bool humansIn = false;
        for (int k = 0; k < kSuspectCount; ++k)
            humansIn = humansIn || (_players[size_t(k)].kind == Player::kHuman && !_game.seat(k).out);
        if (_game.phase() == Game::Phase::kOver || !humansIn) {
            const bool everyone = _game.phase() == Game::Phase::kOver;
            then([this, everyone] { showSolution(everyone ? 5 : 6); });
            then([this] { _finished = true; });
            return;
        }
        if (local(s))
            then([this, s] {
                _viewerOut = s;
                openLoser();
            });
        beginTurn(); // the rules have gone on to the next player
    });
}

// ---- The windows ----------------------------------------------------------------

void Play::showTold(int suspect, bool can) {
    // Dialog 31, with the suspect's own portrait: the box at the top right.
    const ResiDialog *d = _resi.dialog(kToldDialog);
    if (!d) {
        _told.reset();
        return;
    }
    _toldInfo = *d;
    _toldInfo.background = std::string("dialog:A") + kKey[suspect] + "Dis1Bg";
    _told = std::make_unique<DialogView>(_data, _fonts, _toldInfo, kRightX, kRightY);
    _told->setGroupVisible(1, false);
    _told->setText(0, line(suspect, can ? kCanDisprove : kCannotDisprove));
}

void Play::openChoose(bool accuse) {
    const ResiDialog *d = _resi.dialog(kChooseDialog);
    if (!d)
        return;
    _dialog = std::make_unique<DialogView>(_data, _fonts, *d, kRightX, kRightY);
    _window = Window::kChoose;
    _chooseAccuse = accuse;
    _choosePage = 0;
    _chosenSet = {false, false, !accuse};
    _chosen = {0, 0, accuse ? 0 : _game.place(_game.turn()).room};
    refreshChoose();
}

void Play::refreshChoose() {
    // The window's pages are its controls' groups of ids: the suspects
    // (0-7), the weapons (8-15), the rooms (16-26), the summary (27-35).
    _dialog->setGroupVisible(1, false);
    _dialog->setVisible(0, _choosePage == 0);
    const auto show = [&](int from, int to) {
        for (int id = from; id <= to; ++id)
            _dialog->setVisible(id, true);
    };
    switch (_choosePage) {
    case 0:
        show(1, 7);
        break;
    case 1:
        show(8, 15);
        break;
    case 2:
        show(16, 26);
        break;
    default:
        for (int id : {29, 30, 31, 32, 35})
            _dialog->setVisible(id, true);
        _dialog->setVisible(_chooseAccuse ? 33 : 27, true); // Accuse or Suggest
        _dialog->setVisible(_chooseAccuse ? 34 : 28, true); // "You have accused..." or "suggested..."
        break;
    }
}

void Play::clickChoose(int id) {
    const auto next = [&]() {
        if (!_chosenSet[0])
            return 0;
        if (!_chosenSet[1])
            return 1;
        if (!_chosenSet[2])
            return 2;
        return 3;
    };
    if (id >= 2 && id <= 7) {
        _chosen.suspect = id - 2;
        _chosenSet[0] = true;
    } else if (id >= 10 && id <= 15) {
        _chosen.weapon = id - 10;
        _chosenSet[1] = true;
    } else if (id >= 18 && id <= 26) {
        _chosen.room = id - 18;
        _chosenSet[2] = true;
    } else if (id == 35) {
        _window = Window::kNone;
        _dialog.reset();
        return;
    } else if (id == 27 || id == 33) {
        _window = Window::kNone;
        _dialog.reset();
        const int s = _game.turn();
        if (id == 27)
            perform(command(netplay::Command::kSuggest, s, {int16_t(_chosen.suspect), int16_t(_chosen.weapon)}));
        else
            perform(command(netplay::Command::kAccuse, s,
                            {int16_t(_chosen.suspect), int16_t(_chosen.weapon), int16_t(_chosen.room)}));
        return;
    } else
        return;
    sound("click.wav");
    _choosePage = next();
    refreshChoose();
}

void Play::openPick() {
    const ResiDialog *d = _resi.dialog(kPickDialog);
    if (!d)
        return;
    const int who = _game.disprover();
    _dialog = std::make_unique<DialogView>(_data, _fonts, *d, kRightX, kRightY);
    _window = Window::kPick;
    _pickCards = _game.disproving();
    _dialog->setGroupVisible(1, false);
    _dialog->setText(16, "");
    // As Clue.exe's DisproveDlg (0x441c60): one card in the middle on the
    // disprover's background, with Done, a click on either showing it; two
    // or three in the window's other places on the background for more,
    // the one clicked shown at once. Its words for the disprover (17-22)
    // it never shows.
    const bool one = _pickCards.size() == 1;
    _dialog->setVisible(one ? who : who + 6, true);
    for (size_t i = 0; i < _pickCards.size() && i < 3; ++i)
        _dialog->setVisible(pickSlot(i), true);
    _dialog->setVisible(kOk, one);
}

int Play::pickSlot(size_t i) const {
    static const int kOne[] = {12};
    static const int kMore[] = {13, 14, 15};
    return _pickCards.size() == 1 ? kOne[0] : kMore[std::min<size_t>(i, 2)];
}

void Play::clickPick(int id) {
    // A card clicked, or Done (one card): that card shown (0x442250).
    int chosen = -1;
    for (size_t i = 0; i < _pickCards.size(); ++i)
        if (id == pickSlot(i))
            chosen = int(i);
    if (id == kOk && _pickCards.size() == 1)
        chosen = 0;
    if (chosen < 0)
        return;
    sound("click.wav");
    const int card = _pickCards[size_t(chosen)];
    _window = Window::kNone;
    _dialog.reset();
    perform(command(netplay::Command::kShow, _game.disprover(), {int16_t(card)}));
}

void Play::openShown() {
    const ResiDialog *d = _resi.dialog(kShownDialog);
    if (!d || _game.shown() < 0)
        return;
    _dialog = std::make_unique<DialogView>(_data, _fonts, *d, kShownX, kShownY);
    _window = Window::kShown;
    sound("SDcard.WAV");
}

void Play::showSolution(int text) {
    const ResiDialog *d = _resi.dialog(kSolutionDialog);
    if (!d)
        return;
    _dialog = std::make_unique<DialogView>(_data, _fonts, *d, 0, 0);
    _window = Window::kSolution;
    _solutionText = text;
    for (int id : {3, 4, 5, 6})
        _dialog->setVisible(id, id == text);
    if (text == 3 && _game.winner() >= 0)
        _dialog->setText(3, line(_game.winner(), kAccusedRight));
}

void Play::openLoser() {
    const ResiDialog *d = _resi.dialog(kLoserDialog);
    if (!d)
        return;
    // As the exit window over the game (Clue.exe 0x435C7F): (205, 11).
    _dialog = std::make_unique<DialogView>(_data, _fonts, *d, kW / 2 - 231 / 2, 5 + (kBarTop - 5 - 395) / 2);
    _window = Window::kLoser;
}

// ---- Clicks -------------------------------------------------------------------

void Play::handleEvent(const Event &ev) {
    if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp) {
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
    }
    if (ev.type == EventType::kKeyDown) {
        const int step = (ev.mods & keymod::kShift) ? 128 : 32;
        if (ev.key == key::kLeft)
            _camX -= step;
        else if (ev.key == key::kRight)
            _camX += step;
        else if (ev.key == key::kUp)
            _camY -= step;
        else if (ev.key == key::kDown)
            _camY += step;
    }
    // Typing a note.
    if (_editing >= 0 && _panelOwner >= 0) {
        std::string &text = _notes[size_t(_panelOwner)][size_t(_editing)].text;
        if (ev.type == EventType::kTextInput) {
            text += typedText(ev.text);
            if (text.size() > 40)
                text.resize(40);
            return;
        }
        if (ev.type == EventType::kKeyDown && ev.key == key::kBackspace) {
            if (!text.empty())
                text.pop_back();
            return;
        }
        if (ev.type == EventType::kKeyDown && (ev.key == key::kReturn || ev.key == key::kEscape)) {
            editNote(-1);
            return;
        }
        if (ev.type == EventType::kMouseUp &&
            !(ev.mouseX >= kLeftX && ev.mouseX < kLeftX + 202 && ev.mouseY >= kLeftY && ev.mouseY < kLeftY + 408))
            editNote(-1);
    }
    // A window takes the clicks while it is open.
    if (_window != Window::kNone && _dialog) {
        if (_window == Window::kChoose && _choosePage == 3 && ev.type == EventType::kMouseUp) {
            // The summary's pictures: click one to choose it again.
            const int x = ev.mouseX - kRightX;
            const int y = ev.mouseY - kRightY;
            const int page = kSummarySuspect.contains(x, y)                 ? 0
                             : kSummaryWeapon.contains(x, y)                ? 1
                             : kSummaryRoom.contains(x, y) && _chooseAccuse ? 2
                                                                            : -1;
            if (page >= 0) {
                _choosePage = page;
                refreshChoose();
                return;
            }
        }
        const int id = _dialog->handleEvent(ev);
        if (id < 0)
            return;
        switch (_window) {
        case Window::kChoose:
            clickChoose(id);
            break;
        case Window::kPick:
            clickPick(id);
            break;
        case Window::kShown:
        case Window::kSolution:
            if (id == kOk) {
                _window = Window::kNone;
                _dialog.reset();
            }
            break;
        case Window::kLoser:
            if (id == kOk || id == kReturn) {
                _autoDisprove[size_t(_viewerOut)] = id == kReturn;
                _window = Window::kNone;
                _dialog.reset();
            }
            break;
        default:
            break;
        }
        // A click in the window is the window's alone; elsewhere, the view's.
        if (_window == Window::kChoose)
            return;
        if (_window != Window::kNone)
            return;
    }
    if (ev.type == EventType::kMouseMove && ev.mouseY < kBarTop &&
        !(_panel != Panel::kNone && ev.mouseX >= kLeftX && ev.mouseX < kLeftX + 202 && ev.mouseY >= kLeftY &&
          ev.mouseY < kLeftY + 408))
        reactAt(ev.mouseX, ev.mouseY, true);
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _pressedAction = actionAt(ev.mouseX, ev.mouseY);
        return;
    }
    if (ev.type != EventType::kMouseUp || ev.button != MouseButton::kLeft)
        return;
    const int pressed = _pressedAction;
    _pressedAction = -1;
    const int x = ev.mouseX;
    const int y = ev.mouseY;
    if (y >= kBarTop) {
        // The bar: the notes and cards, the actions.
        if (x >= 39 && x < 84) {
            _panel = _panel == Panel::kNone ? Panel::kCards : Panel::kNone;
            _panelOwner = viewer();
            sound("click.wav");
        } else if (x >= 84 && x < 139) {
            _map = !_map;
            sound("click.wav");
        }
        const int slot = actionAt(x, y);
        if (slot >= 0 && slot == pressed)
            clickAction(slot);
        return;
    }
    if (_panel != Panel::kNone && x >= kLeftX && x < kLeftX + 202 && y >= kLeftY && y < kLeftY + 408) {
        clickPanel(x - kLeftX, y - kLeftY);
        return;
    }
    if (reactAt(x, y, false))
        return;
    if (busy() || !_steps.empty() || !localTurn() || _pending)
        return;
    if (_map && x >= kMapX && x < kMapX + kMapW && y >= kMapY && y < kMapY + kMapH)
        clickMap(x - kMapX, y - kMapY);
    else
        clickView(x, y);
}

int Play::actionAt(int x, int y) const {
    if (!localTurn() || _game.phase() == Game::Phase::kOver)
        return -1;
    for (int k = 0; k < 3; ++k)
        if (x >= kSlotX[k] && x < kSlotX[k] + kSlotW[k] && y >= kSlotY && y < kSlotY + kSlotH)
            return k;
    return -1;
}

void Play::clickAction(int slot) {
    if (busy() || !_steps.empty() || _window != Window::kNone || _pending)
        return;
    if (slot == 0) {
        if (_game.canRoll())
            perform(command(netplay::Command::kRoll, _game.turn()));
        else if (_game.canEndTurn()) {
            sound("click.wav");
            perform(command(netplay::Command::kEndTurn, _game.turn()));
        } else if (_game.noDieRoll() && _game.canMove() && !_plan.empty() && _game.canWalk(_plan))
            walkPlan();
        else
            sound("nogo.wav");
    } else if (slot == 1) {
        if (_game.canSuggest())
            openChoose(false);
        else
            sound("nogo.wav");
    } else if (slot == 2) {
        if (_game.canAccuse())
            openChoose(true);
        else
            sound("nogo.wav");
    }
}

void Play::walkPlan() {
    if (!_game.canWalk(_plan)) {
        _plan.clear();
        sound("nogo.wav");
        return;
    }
    std::vector<int16_t> args;
    for (const Place &p : _plan) {
        args.push_back(int16_t(p.inRoom() ? -1 : p.square.x));
        args.push_back(int16_t(p.inRoom() ? p.room : p.square.y));
    }
    perform(command(netplay::Command::kWalk, _game.turn(), args));
}

bool Play::reactAt(int x, int y, bool hover) {
    // Clue.exe 0x4196A0 (a click) and 0x419600 (the pointer moving), then
    // 0x419980: only on the turn of this machine's human, with nothing being
    // carried out and their suspect in one of the rooms (0x428080); then the
    // first hot spot in that room, not reacting already (+0xC8), whose box's
    // outline the point falls in. Those with a timing at 2100 (+0xC9, the
    // credits painting's portraits) react to the pointer passing over them,
    // the others to a click; a click that finds none is the move's.
    if (!localTurn() || busy() || !_steps.empty() || _game.phase() == Game::Phase::kOver)
        return false;
    const Place &at = _game.place(_game.turn());
    if (!at.inRoom())
        return false;
    if (_map && (x < kMapX || x >= kMapX + kMapW || y < kMapY || y >= kMapY + kMapH))
        return false;
    const auto findTiming = [](const std::vector<Timing> &ts, uint16_t time) -> const Timing * {
        for (const Timing &t : ts)
            if (t.time == time)
                return &t;
        return nullptr;
    };
    // The placement's timing, else the object's (0x4280B0, 0x428100).
    const auto timing = [&](const Mansion::Item &item, uint16_t time) {
        const Timing *t = findTiming(_mansion.level()->placements[item.placement].timings, time);
        return t ? t : findTiming(item.object->timings, time);
    };
    for (size_t i = 0; i < _mansion.items().size(); ++i) {
        const Mansion::Item &item = _mansion.items()[i];
        std::vector<uint16_t> reactions;
        for (const ObjectDef::State &st : item.object->states)
            if (st.id >= kReactionFirst && st.id <= kReactionLast)
                reactions.push_back(st.id);
        if (reactions.empty() || (timing(item, 2100) != nullptr) != hover || _mansion.playingState(i, _now))
            continue;
        if (_board.roomAt(Board::squareAt(item.worldX, item.worldY)) != at.room)
            continue;
        if (_map) {
            // On the plan from above (0x419980's other branch): the box's
            // footprint, 13 pixels to a square of 64.
            const auto mapX = [](int wx) {
                return kMapX + kMapCol0X +
                       (wx - Board::kGridOrigin - kMapFirst * Board::kSquareSize) * kMapSquare / Board::kSquareSize;
            };
            const auto mapY = [](int wy) {
                return kMapY + kMapRow0Y +
                       (wy - Board::kGridOrigin - kMapFirst * Board::kSquareSize) * kMapSquare / Board::kSquareSize;
            };
            if (x < mapX(item.box.x) || x >= mapX(item.box.x + item.box.sizeX) || y < mapY(item.box.y) ||
                y >= mapY(item.box.y + item.box.sizeY))
                continue;
        } else if (!onBox(item.box, x + _camX, y + _camY))
            continue;
        // One of its reactions by chance (0x42A301).
        const uint16_t state = reactions[_ambientRng() % reactions.size()];
        if (!_mansion.playState(_data, i, state, _now))
            return false;
        if (const Timing *t = timing(item, uint16_t(1500 + 100 * (state - kReactionFirst))))
            if (t->state < std::size(kObjectSound))
                sound(kObjectSound[t->state]);
        return true;
    }
    return false;
}

void Play::updateAmbience() {
    // The weather's ticks, 66 ms apart.
    _weatherTickMs += _now - std::min(_now, _lastAmbience);
    _lastAmbience = _now;
    if (_weatherTickMs > 1000)
        _weatherTickMs = 1000; // after a pause, not a burst of ticks
    for (; _weatherTickMs >= kTickMs; _weatherTickMs -= kTickMs) {
        _weatherMs += kTickMs;
        if (_weatherMs > kWeatherCycleMs) {
            _weatherMs = 0;
            _weatherStep = 0;
        }
        const int next = _weatherStep + 1;
        if (next < int(std::size(kWeather)) && _weatherMs > kWeather[next].fromMs) {
            _weatherStep = next;
            const WeatherStep &w = kWeather[next];
            if (w.drapes >= 0)
                for (size_t i = 0; i < _mansion.items().size(); ++i)
                    if (_mansion.items()[i].kind == ObjectDef::kDrapes) {
                        if (w.drapes == 0)
                            _mansion.stopState(i);
                        else // from a frame by chance (0x428EF0)
                            _mansion.playState(_data, i, uint16_t(w.drapes), _now - _ambientRng() % 1000, true);
                    }
            if (w.gust >= 0 && _mixer) {
                if (_gust != audio::kNoSound)
                    _mixer->stop(_gust);
                _gust = audio::kNoSound;
                if (w.gust > 0)
                    if (const auto loop = loadSound(kGust[w.gust]))
                        _gust = _mixer->play(loop, audio::SoundType::kEffects, kClueSoundVolume, 0, true);
            }
            if (next == 5)
                sound(kBigThunder[_ambientRng() % 2]);
            continue;
        }
        const int chance = kWeather[_weatherStep].chance;
        if (chance == 1 && _ambientRng() % 100 == 0)
            sound(kSmallThunder[_ambientRng() % 3]);
        else if (chance == 2 && _ambientRng() % 80 == 0)
            sound(kMidThunder[_ambientRng() % 3]);
    }
    if (!_mixer)
        return;
    // Where the camera looks, on the floor: the view's middle, x - y and
    // (x + y) / 2 undone.
    const int sx = _camX + kW / 2;
    const int sy = _camY + kBarTop / 2;
    const int lx = sy + floorDiv(sx, 2);
    const int ly = sy - floorDiv(sx, 2);
    for (size_t k = 0; k < std::size(kAmbientSound); ++k) {
        long best = LONG_MAX;
        int dx = 0;
        for (const Mansion::Item &item : _mansion.items()) {
            const auto &pl = _mansion.level()->placements[item.placement];
            const std::vector<Timing> &ts = pl.timings.empty() ? item.object->timings : pl.timings;
            for (const Timing &t : ts)
                if (t.time == 1900 && t.raw[3] == k) {
                    const long d2 =
                        long(item.worldX - lx) * (item.worldX - lx) + long(item.worldY - ly) * (item.worldY - ly);
                    if (d2 < best) {
                        best = d2;
                        dx = item.worldX - lx;
                    }
                }
        }
        audio::SoundHandle &h = _ambient[k];
        if (best >= 500L * 500L) {
            if (h != audio::kNoSound)
                _mixer->stop(h);
            h = audio::kNoSound;
            continue;
        }
        const double d = std::sqrt(double(best));
        int volume = 100;
        int pan = 64;
        if (d > 1) {
            volume = int(100 - 100 * d / 500);
            pan = int(64 + 52 * (dx / d));
        }
        const int v = audio::kMaxVolume * volume / 127;
        const int p = (pan - 64) * audio::kMaxPan / 63;
        if (h == audio::kNoSound || !_mixer->isPlaying(h)) {
            if (const auto loop = loadSound(kAmbientSound[k]))
                h = _mixer->play(loop, audio::SoundType::kEffects, v, p, true);
        } else {
            _mixer->setVolume(h, v);
            _mixer->setPan(h, p);
        }
    }
}

void Play::clickView(int x, int y) {
    const int s = _game.turn();
    // A secret passage, clicked in a room that has one.
    if (_game.canTakePassage())
        for (const Mansion::Item &item : _mansion.items())
            if (item.object->type == ObjectDef::kSecretPassage && item.rect.contains(x + _camX, y + _camY) &&
                _board.roomAt(Board::squareAt(item.box.x, item.box.y)) == _game.place(s).room) {
                perform(command(netplay::Command::kPassage, s));
                return;
            }
    // The floor's square under the pointer: x - y and (x + y) / 2 undone.
    const int sx = x + _camX;
    const int sy = y + _camY;
    const int wx = sy + floorDiv(sx, 2);
    const int wy = sy - floorDiv(sx, 2);
    const Square sq = Board::squareAt(wx, wy);
    // The floor of the room the suspect is in: a walk about it (Clue.exe
    // 0x423B5B), whether or not it may move.
    if (_game.place(s).inRoom() && _board.roomAt(sq) == _game.place(s).room &&
        std::none_of(_board.doors().begin(), _board.doors().end(),
                     [&](const Board::Door &d) { return d.inside == sq; })) {
        const Walker &w = _walkers[size_t(s)];
        if (cell32(wx) == cell32(w.x) && cell32(wy) == cell32(w.y))
            return; // where it stands: a way of no steps, nothing to do
        if (roomRoute(wx, wy).empty())
            sound("nogo.wav");
        else
            perform(command(netplay::Command::kRoomWalk, s, {int16_t(wx), int16_t(wy)}));
        return;
    }
    if (!_game.canMove())
        return;
    if (_board.isCorridor(sq))
        clickSquare({sq, kNoRoom});
    else if (const int room = _board.roomAt(sq); room != kNoRoom)
        clickSquare({{}, room});
    else
        sound("nogo.wav");
}

void Play::clickMap(int x, int y) {
    const Square sq{kMapFirst + floorDiv(x - kMapCol0X, kMapSquare), kMapFirst + floorDiv(y - kMapRow0Y, kMapSquare)};
    const int room = _board.roomAt(sq);
    // From a corner room, the room in the opposite corner: the passage.
    if (room != kNoRoom && _game.canTakePassage() && _board.passage(_game.place(_game.turn()).room) == room) {
        perform(command(netplay::Command::kPassage, _game.turn()));
        return;
    }
    if (!_game.canMove())
        return;
    if (_board.isCorridor(sq))
        clickSquare({sq, kNoRoom});
    else if (room != kNoRoom)
        clickSquare({{}, room});
    else
        sound("nogo.wav");
}

void Play::clickSquare(const Place &p) {
    const int s = _game.turn();
    // A square of the plan: back to it (the last one again: go).
    const auto it = std::find(_plan.begin(), _plan.end(), p);
    if (it != _plan.end()) {
        if (it + 1 == _plan.end() && _game.canWalk(_plan))
            walkPlan();
        else {
            _plan.erase(it + 1, _plan.end());
            sound("unting.wav"); // numbers taken back (Clue.exe 0x423C8E)
        }
        return;
    }
    if (p == _game.place(s)) {
        if (!_plan.empty())
            sound("unting.wav");
        _plan.clear();
        return;
    }
    const auto next = _game.nextSteps(_plan);
    if (std::find(next.begin(), next.end(), p) == next.end()) {
        sound("nogo.wav");
        return;
    }
    _plan.push_back(p);
    sound("ting.wav"); // a number put down (Clue.exe 0x423EE8)
    // The view goes to the newest square, as the original's does.
    if (!p.inRoom()) {
        int wx;
        int wy;
        Board::standingPoint(p.square, wx, wy);
        _camX = (wx - wy) - kW / 2;
        _camY = floorDiv(wx + wy, 2) - kBarTop / 2;
    }
    // The die's number reached, or a room: the walk begins.
    const int limit = _game.noDieRoll() ? _game.moves() : _game.roll();
    if (p.inRoom() || int(_plan.size()) == limit)
        walkPlan();
}

void Play::clickPanel(int x, int y) {
    if (_panel == Panel::kCards) {
        if (x < 50 && y >= 352)
            _panel = Panel::kNotepad;
        return;
    }
    if (x < 45 && y >= 372) {
        _panel = Panel::kCards;
        return;
    }
    // A row of the notepad: its colour goes round the suspects', each mark
    // round the marks; its triangle opens the row's note.
    const int who = _panelOwner;
    if (who < 0)
        return;
    static const int kFirst[3] = {suspectCard(0), weaponCard(0), roomCard(0)};
    static const int kCount[3] = {kSuspectCount, kWeaponCount, kRoomCount};
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < kCount[k]; ++r) {
            const int top = noteRowY(k, r);
            if (y < top || y >= top + 15)
                continue;
            const int card = kFirst[k] + r;
            NoteRow &row = _notes[size_t(who)][size_t(card)];
            if (x >= kNoteColourX && x < kNoteMarkX[0])
                row.colour = row.colour + 1 < kSuspectCount ? row.colour + 1 : -1;
            else if (x >= kNoteMarkX[0] && x < kNoteMarkX[1])
                row.marks[0] = (row.marks[0] + 1) % 6;
            else if (x >= kNoteMarkX[1] && x < kNoteTriangleX)
                row.marks[1] = (row.marks[1] + 1) % 6;
            else if (x >= kNoteTriangleX)
                editNote(_editing == card ? -1 : card);
            else
                return;
            sound("click.wav");
            return;
        }
}

void Play::editNote(int card) {
    _editing = card;
    _sys.setTextInput(card >= 0);
}

// ---- Time --------------------------------------------------------------------

void Play::update(uint64_t nowMs) {
    const uint64_t dt = _now == 0 ? 0 : std::min<uint64_t>(nowMs - _now, 100);
    _now = nowMs;
    updateAmbience();
    // The walks.
    for (int s = 0; s < kSuspectCount; ++s) {
        Walker &w = _walkers[size_t(s)];
        if (!w.walking)
            continue;
        double left = double(kWalkSpeed) * double(dt) / 1000.0;
        while (left > 0 && !w.route.empty()) {
            const auto [tx, ty] = w.route.front();
            const double dx = tx - w.fx;
            const double dy = ty - w.fy;
            const double d = std::hypot(dx, dy);
            if (const int f = facingOf(tx - int(std::lround(w.fx)), ty - int(std::lround(w.fy))); f >= 0)
                w.facing = f;
            if (d <= left) {
                w.fx = tx;
                w.fy = ty;
                left -= d;
                w.route.pop_front();
            } else {
                w.fx += dx / d * left;
                w.fy += dy / d * left;
                left = 0;
            }
        }
        w.x = int(std::lround(w.fx));
        w.y = int(std::lround(w.fy));
        // A footstep on the walk's frames that have key 5000 (Clue.exe
        // 0x426660): one of the five men's or seven women's by chance.
        if (const ObjectDef *o = _data.object(kCharacter[s]))
            if (const ObjectDef::State *st = o->state(uint16_t(9 + w.facing)))
                if (const Animation *a = _data.animation(st->animation); a && a->frameCount() > 0) {
                    const int f = int(a->frameAt(_now + uint64_t(s) * 337, true));
                    if (f != w.stepFrame) {
                        w.stepFrame = f;
                        for (const auto &key : a->frames[size_t(f)].extras)
                            if ((key[0] | key[1] << 8) == 5000) {
                                char step[32];
                                std::snprintf(step, sizeof step, "%s_step%d.wav", kFemale[s] ? "female" : "male",
                                              1 + int(_ambientRng() % (kFemale[s] ? 7 : 5)));
                                sound(step);
                            }
                    }
                }
        if (w.route.empty())
            w.walking = false;
        if (s == _follow)
            centreOn(s);
    }
    // The video, in the right column.
    if (_video) {
        const double t = double(_now - _videoStart) / 1000.0;
        const bool soundOn = _mixer && _videoSound != audio::kNoSound && _mixer->isPlaying(_videoSound);
        if (_video->seek(t) || (_video->finished(t) && !soundOn))
            stopVideo();
    }
    runSteps();
    if (_net) {
        // The players' actions, in the host's order, each when this
        // machine has played out what went before it (a card shown, or a
        // player gone, as soon as it's waited for).
        netplay::Command c;
        while (_net->nextCommand(c))
            _incoming.push_back(c);
        while (!_incoming.empty() && _game.phase() != Game::Phase::kOver) {
            const netplay::Command &next = _incoming.front();
            const bool waitedFor =
                _awaitingShow && (next.type == netplay::Command::kShow || next.type == netplay::Command::kTakeOver);
            if (!waitedFor && (busy() || !_steps.empty()))
                break;
            const netplay::Command now = next;
            _incoming.pop_front();
            apply(now);
            runSteps();
        }
    }
    if (!busy() && _steps.empty() && _window == Window::kNone && _game.phase() != Game::Phase::kOver && !humanTurn())
        computerTurn();
    runSteps();
}

// ---- Sounds, words, videos -----------------------------------------------------

std::string Play::line(int suspect, int index) const {
    const size_t n = size_t(suspect) * kTickerBlock + size_t(index);
    return n < _tickerLines.size() ? _tickerLines[n] : std::string(); // Windows-1252, as the fonts
}

std::shared_ptr<audio::Sound> Play::loadSound(const std::string &file) {
    auto it = _sounds.find(file);
    if (it == _sounds.end()) {
        std::shared_ptr<audio::Sound> s;
        if (const std::filesystem::path *p = _data.find(file)) {
            auto decoded = std::make_shared<audio::Sound>();
            if (!audio::decodeSound(readFile(*p), *decoded)) {
                playAsClueExe(*decoded);
                s = decoded;
            }
        }
        it = _sounds.emplace(file, s).first;
    }
    return it->second;
}

void Play::sound(const std::string &file, bool voice) {
    if (!_mixer || (voice && !_look.butlerVoice))
        return;
    const auto s = loadSound(file);
    if (!s)
        return;
    if (voice) {
        if (_voice != audio::kNoSound)
            _mixer->stop(_voice);
        _voice = _mixer->play(s, audio::SoundType::kEffects, kClueSoundVolume); // through Miles, as the sounds
    } else
        _mixer->play(s, audio::SoundType::kEffects, kClueSoundVolume);
}

void Play::startVideo(const std::string &file) {
    stopVideo();
    const std::filesystem::path *p = _data.find(file);
    auto player = std::make_unique<Movie>();
    if (!p || player->open(readFile(*p)))
        return;
    if (_mixer && player->hasAudio()) {
        auto s = std::make_shared<audio::Sound>();
        if (!player->audio(*s))
            _videoSound = _mixer->play(s, audio::SoundType::kSpeech);
    }
    _video = std::move(player);
    _videoStart = _now;
}

void Play::stopVideo() {
    if (_mixer && _videoSound != audio::kNoSound)
        _mixer->stop(_videoSound);
    _videoSound = audio::kNoSound;
    _video.reset();
}

void Play::stopSounds() {
    stopVideo();
    if (_mixer && _gust != audio::kNoSound)
        _mixer->stop(_gust);
    _gust = audio::kNoSound;
    for (audio::SoundHandle &h : _ambient) {
        if (_mixer && h != audio::kNoSound)
            _mixer->stop(h);
        h = audio::kNoSound;
    }
    if (_mixer && _voice != audio::kNoSound)
        _mixer->stop(_voice);
    _voice = audio::kNoSound;
}

void Play::centreOn(int suspect) {
    const Walker &w = _walkers[size_t(suspect)];
    _camX = (w.x - w.y) - kW / 2;
    _camY = floorDiv(w.x + w.y, 2) - 60 - kBarTop / 2;
}

// ---- Drawing -------------------------------------------------------------------

void Play::drawAnimation(const std::string &name, int x, int y, int frame) {
    const Animation *a = _data.animation(name);
    if (!a || a->layers.empty() || a->frameCount() == 0)
        return;
    const size_t f = size_t(std::clamp(frame, 0, int(a->frameCount()) - 1));
    for (const auto &layer : a->layers) {
        const Animation::Part &part = layer[f];
        if (part.image >= a->images.size())
            continue;
        if (const Image *img = _data.image(a->images[part.image]))
            image::blit(_screen, img->pixels, x + part.x + img->offsetX, y + part.y + img->offsetY);
    }
}

std::vector<Mansion::Actor> Play::actors() {
    std::vector<Mansion::Actor> out;
    // The suspects: standing (states 1-8) or walking (9-16), by facing.
    for (int s = 0; s < kSuspectCount; ++s) {
        const Walker &w = _walkers[size_t(s)];
        const ObjectDef *o = _data.object(kCharacter[s]);
        const ObjectDef::State *st = o ? o->state(uint16_t((w.walking ? 9 : 1) + w.facing)) : nullptr;
        const Animation *a = st ? _data.animation(st->animation) : nullptr;
        if (!a || a->frameCount() == 0)
            continue;
        const auto *frames = _mansion.frames(_data, *a);
        Mansion::Actor actor;
        const size_t f = a->frameAt(_now + uint64_t(s) * 337, true) % frames->size();
        actor.frame = &(*frames)[f];
        actor.box = {w.x, w.y, a->frames[f].sizeX, a->frames[f].sizeY, a->frames[f].height};
        // Only the suspect whose turn it is shows what is in front of it, as
        // Clue.exe's (its flag at 0xd08, set for the turn); in a room, only
        // the walls.
        if (s == _game.turn() && _game.phase() != Game::Phase::kOver) {
            actor.reveal = true;
            const Square sq = Board::squareAt(w.x, w.y);
            actor.inRoom = _board.roomAt(sq) != kNoRoom;
            actor.inCorridor = _board.isCorridor(sq);
        }
        out.push_back(actor);
    }
    // A move being planned: its squares numbered as they are clicked (only
    // they, as the original).
    if (localTurn() && _game.canMove() && !busy()) {
        const auto number = [&](const Square &sq, int n) {
            if (n < 1 || n > 9)
                return;
            const Animation *a = _data.animation(std::string("tilenum:At_") + char('a' + n - 1));
            if (!a || a->frameCount() == 0)
                return;
            Mansion::Actor actor;
            actor.frame = &(*_mansion.frames(_data, *a))[0];
            actor.box = {Board::kGridOrigin + sq.x * Board::kSquareSize, Board::kGridOrigin + sq.y * Board::kSquareSize,
                         Board::kSquareSize, Board::kSquareSize, 1};
            actor.flat = true;
            actor.reveal = true; // the numbers are on the corridors
            actor.inCorridor = true;
            out.push_back(actor);
        };
        for (size_t i = 0; i < _plan.size(); ++i)
            if (!_plan[i].inRoom())
                number(_plan[i].square, int(i) + 1);
    }
    return out;
}

void Play::draw() {
    const ScreenRect &b = _mansion.bounds();
    _camX = std::clamp(_camX, b.x - kW / 2, b.x + b.w - kW / 2);
    _camY = std::clamp(_camY, b.y - kBarTop / 2, b.y + b.h - kBarTop / 2);
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {0, 0, 0});
    MansionLook look = _look.mansion;
    look.characters = false;
    _mansion.draw(_screen, _camX, _camY, look, _now, actors());
    drawMap();
    drawPanel();
    // The solution's window is the whole screen; the others leave the bar.
    if (_window == Window::kSolution) {
        drawBar();
        drawWindows();
        return;
    }
    drawWindows();
    drawBar();
}

void Play::drawBar() {
    drawAnimation("UI:Atopframe", 0, 0);
    // Placed as Clue.exe lays the game's screen out (0x435630): the frames
    // below the top one, from 5 down to the bar.
    drawAnimation("UI:Aleftframe", 0, 5);
    drawAnimation("UI:Artframe", 635, 5);
    drawAnimation("UI:Aleftorn", 0, kBarTop);
    drawAnimation("UI:Anotecardbtn", 39, kBarTop);
    drawAnimation("UI:Agamebdbtn", 84, kBarTop);
    for (int k = 0; k < kSuspectCount; ++k) {
        // Frames: 0 as it is, 1 whose turn it is, 2 not playing (or out).
        const bool away = _players[size_t(k)].kind == Player::kNone || _game.seat(k).out;
        const int frame = k == _game.turn() && _game.phase() != Game::Phase::kOver ? 1 : away ? 2 : 0;
        drawAnimation(std::string("UI:A") + kKey[k] + "bottom", 141 + 60 * k, kBarTop, frame);
    }
    drawAnimation("UI:Alogobtn", 501, kBarTop);
    drawAnimation("UI:Arightorn", 594, kBarTop);
    if (localTurn() && _game.phase() != Game::Phase::kOver)
        drawActions();
    drawAnimation("UI:Aticker", 0, kTickerTop);
    drawAnimation("UI:Artbtmornnorm", 599, kTickerTop + 1);
    drawTicker();
}

void Play::say(const std::string &sentence) {
    if (sentence != _ticker)
        _tickerSince = _now;
    _ticker = sentence;
}

void Play::drawTicker() {
    // The ticker's font, packed as the original draws it.
    if (!_tickerFont)
        if (const ResiFont *rf = _resi.font(2))
            _tickerFont = std::make_unique<BitmapFont>(_data, *rf, true);
    const BitmapFont *f = _tickerFont ? _tickerFont.get() : _fonts.get(2);
    if (!f || _ticker.empty())
        return;
    // Drawn into a strip the ticker's width, so as to come round.
    const int w = kTickerRight - kTickerLeft;
    const int h = 18;
    if (_tickerStrip.w != w)
        _tickerStrip.create(w, h, PixelFormat::kRGBA32);
    std::fill(_tickerStrip.pixels.begin(), _tickerStrip.pixels.end(), 0);
    const int textW = f->width(_ticker);
    const uint64_t moving = _now - _tickerSince > uint64_t(kTickerStillMs) ? _now - _tickerSince - kTickerStillMs : 0;
    const int start = (kW - textW) / 2 - kTickerLeft;
    int x = start - int(moving * kTickerSpeed / 10000);
    x = ((x % w) + w) % w;
    const int lineY = (h - f->lineHeight()) / 2;
    f->draw(_tickerStrip, x, lineY, _ticker);
    f->draw(_tickerStrip, x - w, lineY, _ticker);
    image::Image strip{w, h, _tickerStrip.pixels};
    image::blit(_screen, strip, kTickerLeft, kTickerTop);
}

void Play::drawActions() {
    drawAnimation("UI:Aicondrawer", kDrawerX, kBarTop);
    const int hover = actionAt(_mouseX, _mouseY);
    const auto frameOf = [&](int slot, bool enabled) {
        if (!enabled)
            return 3;
        if (slot == _pressedAction && hover == slot)
            return 2;
        return hover == slot ? 1 : 0;
    };
    const int s = _game.turn();
    const std::string end = std::string("UI:A") + kKey[s] + "end";
    // The first place: the die, rolling, its number; without the die the
    // moves left; once moved, the suspect's pawn to end the turn.
    if (_now - _rollStart < 800 && _rollStart != 0)
        drawAnimation("UI:Arolled", kSlotX[0], kSlotY, int(_now / 70 % 6));
    else if (_game.canEndTurn())
        drawAnimation(end, kSlotX[0], kSlotY, hover == 0 ? 1 : 0);
    else if (_game.noDieRoll())
        drawAnimation("UI:Anoroll", kSlotX[0], kSlotY, _game.moves());
    else if (_game.phase() == Game::Phase::kMoving)
        drawAnimation("UI:Arolled", kSlotX[0], kSlotY, _game.roll() - 1);
    else
        drawAnimation("UI:Arolldiebtn", kSlotX[0], kSlotY, frameOf(0, _game.canRoll()));
    drawAnimation("UI:Asuggestbtn", kSlotX[1], kSlotY, frameOf(1, _game.canSuggest()));
    drawAnimation("UI:Aaccusebtn", kSlotX[2], kSlotY, frameOf(2, _game.canAccuse()));
}

void Play::drawPanel() {
    if (_panel == Panel::kNone || _panelOwner < 0)
        return;
    const int who = _panelOwner;
    const Game::Seat &seat = _game.seat(who);
    if (_panel == Panel::kCards) {
        drawAnimation(std::string("UI:A") + kKey[who] + "cardwin", kLeftX, kLeftY);
        for (size_t i = 0; i < seat.hand.size() && i < 6; ++i)
            drawAnimation(std::string("UI:A") + kCardKey[seat.hand[i]] + "card", kLeftX + kHandX[i],
                          kLeftY + kHandY[i]);
        drawAnimation("UI:Anotesbtn", kLeftX + 3, kLeftY + 352);
        return;
    }
    drawAnimation(std::string("UI:A") + kKey[who] + "notepad", kLeftX, kLeftY);
    const BitmapFont *font = _fonts.get(1);
    static const int kFirst[3] = {suspectCard(0), weaponCard(0), roomCard(0)};
    static const int kCount[3] = {kSuspectCount, kWeaponCount, kRoomCount};
    for (int k = 0; k < 3; ++k) {
        if (font)
            drawText(_screen, *font, _kindNames[size_t(k)], kLeftX + 10, kLeftY + kNoteHeadY[k], 150, kNoteRowH, false,
                     true);
        for (int r = 0; r < kCount[k]; ++r) {
            const int card = kFirst[k] + r;
            const int y = kLeftY + noteRowY(k, r);
            if (font)
                drawText(_screen, *font, _cardNames[size_t(card)], kLeftX + kNoteTextX, y, 115, kNoteRowH, false, true);
            const NoteRow &row = _notes[size_t(who)][size_t(card)];
            if (row.colour >= 0)
                drawAnimation(std::string("UI:A") + kKey[row.colour] + "noteclr", kLeftX + kNoteColourX, y);
            for (int m = 0; m < 2; ++m)
                if (row.marks[size_t(m)] > 0)
                    drawAnimation(kMark[row.marks[size_t(m)] - 1], kLeftX + kNoteMarkX[m], y);
            // The triangle: pointing right while its note is open; marked
            // when there is one.
            const char *tri = _editing == card ? "notesrt" : row.text.empty() ? "notesup" : "notemark";
            drawAnimation(std::string("UI:A") + kKey[who] + tri, kLeftX + kNoteTriangleX, y);
            // The note, beside the notepad: while it is open, or the
            // pointer is on its triangle.
            const bool hover =
                _mouseX >= kLeftX + kNoteTriangleX && _mouseX < kLeftX + 205 && _mouseY >= y && _mouseY < y + kNoteRowH;
            if (const BitmapFont *f = _fonts.get(2); f && (_editing == card || (hover && !row.text.empty())))
                f->draw(_screen, kLeftX + 212, y + (kNoteRowH - f->lineHeight()) / 2,
                        row.text + (_editing == card && _now / 500 % 2 ? "|" : ""));
        }
    }
    drawAnimation("UI:Acardsbtn", kLeftX + 3, kLeftY + 375);
}

void Play::drawMap() {
    if (!_map)
        return;
    drawAnimation("UI:Aoverhead", kMapX, kMapY);
    const auto at = [&](double col, double row, int &x, int &y) {
        x = kMapX + kMapCol0X + int(std::lround((col - kMapFirst) * kMapSquare));
        y = kMapY + kMapRow0Y + int(std::lround((row - kMapFirst) * kMapSquare));
    };
    // A move being planned: its squares numbered in the map's small digits.
    if (localTurn() && _game.canMove() && !busy())
        for (size_t i = 0; i < _plan.size() && i < 9; ++i)
            if (!_plan[i].inRoom()) {
                int x;
                int y;
                at(_plan[i].square.x, _plan[i].square.y, x, y);
                drawAnimation(std::string("tilenum:Aoverhead_") + char('a' + i), x + 1, y + 1);
            }
    // The suspects' pawns (their state 19), where they are, walking or not:
    // a square's middle is 40 units on from where they stand on it.
    for (int s = 0; s < kSuspectCount; ++s) {
        const ObjectDef *o = _data.object(kCharacter[s]);
        const ObjectDef::State *st = o ? o->state(19) : nullptr;
        if (!st)
            continue;
        const Walker &w = _walkers[size_t(s)];
        int x;
        int y;
        at((w.x - 40) / 64.0, (w.y - 40) / 64.0, x, y);
        drawAnimation(st->animation, x, y);
    }
}

void Play::drawWindows() {
    if (_told)
        _told->draw(_screen);
    if (_video) {
        // In its frame (UI:Isuggframe: black, edged in white), against the
        // screen's right-hand frame, as the original shows its videos.
        drawAnimation("UI:Asuggframe", kVideoFrameX, kVideoFrameY);
        const image::Image &pic = _video->picture();
        image::blit(_screen, pic, kVideoFrameX + (kVideoFrameW - pic.w) / 2, kVideoFrameY + (kVideoFrameH - pic.h) / 2);
    }
    if (!_dialog || _window == Window::kNone)
        return;
    _dialog->drawBackground(_screen);
    if (_window == Window::kChoose && _choosePage == 3) {
        // The summary: what has been chosen, under the frame with its holes.
        const auto put = [&](int card, const UiRect &r) {
            const std::string name = std::string("dialog:A") + kCardKey[card] + "SugAcs";
            if (const Animation *a = _data.animation(name); a && a->frameCount() > 0)
                drawAnimation(name, kRightX + r.x + (r.w - a->width) / 2, kRightY + r.y + (r.h - a->height) / 2);
        };
        put(suspectCard(_chosen.suspect), kSummarySuspect);
        put(weaponCard(_chosen.weapon), kSummaryWeapon);
        put(roomCard(_chosen.room), kSummaryRoom);
    }
    _dialog->drawControls(_screen);
    switch (_window) {
    case Window::kPick:
        for (size_t i = 0; i < _pickCards.size() && i < 3; ++i)
            if (const ResiControl *c = _dialog->info().control(pickSlot(i)))
                drawAnimation(std::string("UI:A") + kCardKey[_pickCards[i]] + "card", kRightX + c->left,
                              kRightY + c->top);
        break;
    case Window::kShown:
        drawAnimation("UI:Algcard", kShownX + 23, kShownY + 2, _game.shown());
        break;
    case Window::kSolution: {
        const Accusation &s = _game.solution();
        const int cards[3] = {suspectCard(s.suspect), weaponCard(s.weapon), roomCard(s.room)};
        for (int k = 0; k < 3; ++k)
            if (const ResiControl *c = _dialog->info().control(k))
                drawAnimation("UI:Algcard", c->left, c->top, cards[k]);
        break;
    }
    default:
        break;
    }
}

} // namespace reluva::clue
