// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/cursor.h"
#include "engines/boggle/classic/competition.h"
#include "engines/boggle/classic/turn_clock.h"
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/orientation.h"
#include "engines/boggle/classic/history.h"
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/player.h"
#include "engines/boggle/classic/results.h"
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/seed.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/presentation.h"
#include "engines/boggle/classic/user_dictionary.h"
#include "engines/boggle/resources.h"
#include "common/font.h"
#include "common/fs.h"
#include <algorithm>
#include <cstdio>
#include <memory>

namespace reluva::boggle {
namespace {
constexpr int width = 800, height = 600, boardX = 24, boardY = 100, boardSize = 368;
struct Ink { uint8_t r, g, b; };
constexpr HistoryButton scoresArtwork{216,448,158,29}, scoresPrototype{618,76,158,24};
constexpr HistoryButton scoresClose{448,450,184,30}, scoresRetry{160,450,184,30};
constexpr HistoryButton resumeButton{300,340,200,44}, noticeOkay{320,350,160,40};
constexpr Ink white{246, 249, 255}, muted{164, 184, 205}, dark{18, 32, 48};
void rect(Surface& screen, int x, int y, int w, int h, Ink color) {
    for (int row = std::max(0, y); row < std::min(screen.h, y + h); ++row)
        for (int col = std::max(0, x); col < std::min(screen.w, x + w); ++col) {
            auto* p = screen.row(row) + col * 4;
            p[0] = color.r; p[1] = color.g; p[2] = color.b; p[3] = 255;
        }
}
void label(Surface& screen, int x, int y, std::string_view text, Ink ink = white, int scale = 1) {
    font::drawText(screen, x, y, text, {ink.r, ink.g, ink.b}, scale);
}
bool hit(int x, int y, int left, int top, int w, int h) {
    return x >= left && y >= top && x < left + w && y < top + h;
}
void scoreTable(Surface& screen,const HighScores& scores,const std::string& error) {
    rect(screen,128,94,544,404,{22,40,60});
    label(screen,160,116,"HIGH SCORES",white,2);
    label(screen,160,150,"CLASSIC / ALL VERSIONS AND ROUND LENGTHS",muted);
    label(screen,160,174,"RANK");label(screen,210,174,"NAME");label(screen,400,174,"VERSION");label(screen,550,174,"SCORE");
    const auto rows=scores.table();
    for(size_t i=0;i<rows.size();++i) {
        const int y=198+int(i)*19;
        label(screen,160,y,std::to_string(i+1));label(screen,210,y,rows[i].name);label(screen,400,y,rows[i].variant==ClassicVariant::Master?"MASTER":"ORIGINAL");label(screen,550,y,std::to_string(rows[i].score));
    }
    if(rows.empty())label(screen,160,218,error.empty()?"No scores yet.":"Table unavailable.",muted);
    if(!error.empty())label(screen,160,392,error.substr(0,80),{255,116,116});
    label(screen,160,410,"Top ten Classic scores; player names may repeat.",muted);
    label(screen,160,426,!error.empty()?"Scores unavailable until a successful retry.":scores.persistent()?"Saved in your launcher profile.":"Session only; no save directory configured.",muted);
    rect(screen,scoresClose.x,scoresClose.y,scoresClose.w,scoresClose.h,{48,88,120});label(screen,460,460,"CLOSE (ENTER / H)");
    if(!error.empty()){rect(screen,scoresRetry.x,scoresRetry.y,scoresRetry.w,scoresRetry.h,{48,88,120});label(screen,172,460,"RETRY (R)");}
}
void matchTable(Surface& screen,const std::vector<std::string>& names,const ClassicCompetition& competition,bool chess,bool background) {
    rect(screen,128,94,544,404,{22,40,60});label(screen,160,116,"CLASSIC RESULTS",white,2);
    label(screen,160,150,std::string(chess?"CHESS TIMER":"TRADITIONAL")+(background?" / BACKGROUND CPU":"")+" / SHARED WORDS SCORE ZERO",muted);
    for(size_t i=0;i<names.size();++i) {
        label(screen,160,190+int(i)*40,std::to_string(i+1)+"  "+names[i],white,2);
        label(screen,550,190+int(i)*40,std::to_string(competition.players()[i].score),white,2);
    }
    label(screen,160,380,"1-4 or click a name: review that player's words",muted);
    label(screen,160,410,"Grey words in review were shared / worth zero.",muted);
    rect(screen,448,450,184,30,{48,88,120});label(screen,460,460,"CLOSE (ENTER / V)");
}
void liveScores(Surface& screen,const std::vector<std::string>& names,
                const std::vector<std::unique_ptr<ClassicRound>>& rounds,const ClassicCompetition& competition) {
    rect(screen,0,590,width,10,dark);
    for(size_t i=0;i<names.size();++i) {
        const int score=competition.ready()?competition.players()[i].score:rounds[i]->score();
        label(screen,12+int(i)*196,591,std::to_string(i+1)+" "+names[i]+" "+std::to_string(score));
    }
}
void handoffScreen(Surface& screen,std::string_view next,bool chess,uint64_t remaining,bool timed,const std::vector<std::string>& finished) {
    rect(screen,0,0,width,height,dark);
    if(chess && !finished.empty()) {
        label(screen,80,100,"PLAYER FINISHED",white,2);
        for(size_t i=0;i<finished.size();++i)label(screen,80,140+int(i)*22,finished[i]+"'s game is over.",muted);
    } else label(screen,80,140,"NEXT PLAYER",white,3);
    label(screen,80,220,chess && !finished.empty()?std::string(next)+" is up.":std::string(next),white,3);
    if(chess) {
        const auto seconds=(remaining+999)/1000;
        label(screen,80,310,timed?"Your paused timer: "+std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60):"Chess Timer / Not Timed",muted);
    } else label(screen,80,310,"The same board / a full round for each player.",muted);
    rect(screen,80,380,640,60,{48,88,120});label(screen,100,402,"START TURN (ENTER)",white,2);
    label(screen,80,480,"Pass the controls, then start when ready.",muted);
}
void glyph(Surface& screen, int x, int y, const std::string& face,
           const image::Image& letters, const image::Image& qu, int cellSize,unsigned turns) {
    if (face.size() != 1 && face != "QU") { label(screen, x + 14, y + 25, face, {0, 0, 220}, 2); return; }
    const unsigned index = unsigned(face[0] - 'A');
    const auto& atlas = face == "QU" ? qu : letters;
    // The observed atlas has 6x6 cells, five alphabet entries per row.
    const int sx = int(index % 5) * 512 / 6, sy = int(index / 5) * 512 / 6;
    const int sw = int(index % 5 + 1) * 512 / 6 - sx, sh = int(index / 5 + 1) * 512 / 6 - sy;
    for (int dy = 0; dy < cellSize - 12; ++dy)
        for (int dx = 0; dx < cellSize - 12; ++dx) {
            const auto uv=cubeTexturePoint(dx,dy,cellSize-13,turns);
            const auto* p = atlas.pixel(sx + uv[0] * sw / (cellSize - 12), sy + uv[1] * sh / (cellSize - 12));
            auto* out = screen.row(y + 6 + dy) + (x + 6 + dx) * 4;
            std::copy_n(p, 4, out);
        }
}
std::string feedback(Submission result, size_t minimum) {
    switch (result) {
    case Submission::Accepted: return "Counted!";
    case Submission::NotPlaying: return "The round has ended.";
    case Submission::InvalidLetters: return "Enter a word using A-Z.";
    case Submission::TooShort: return "Use at least " + std::to_string(minimum) + " letters.";
    case Submission::UnknownWord: return "Not in dictionary. Review after this round.";
    case Submission::NotOnBoard: return "No matching path on this board.";
    case Submission::Duplicate: return "";
    case Submission::SelectPath: return "Please select this word on the board.";
    }
    return "";
}
void draw(Surface& screen, const ClassicRound& round, uint64_t now,
          const image::Image& letters, const image::Image& qu, const Palette& palette,
          const std::string& input, const std::vector<size_t>& path, const std::string& message, size_t scroll, bool fromMenu,
          const ClassicArtwork& artwork,uint64_t duration,uint32_t boardSeed,bool autoOrient,std::string_view player,const RoundResults& results,bool chess) {
    const bool reviewing = round.candidate() != nullptr;
    const bool ready = round.phase() == RoundPhase::Ready, playing = round.phase() == RoundPhase::Playing;
    if(artwork.active()) artwork.draw(screen,round,now,duration,letters,qu,path,scroll,boardSeed,player,&results);
    else {
    rect(screen, 0, 0, width, height, dark);
    rect(screen, 0, 0, width, 76, {29, 49, 70});
    const auto& accent = palette[1]; rect(screen, 24, 76, 752, 2, {accent.r, accent.g, accent.b});
    const size_t side = round.board().side();
    const int cellSize = boardSize / int(side);
    const bool master = side == 5;
    label(screen, 24, 18, master ? "BOGGLE / CLASSIC MASTER" : "BOGGLE / CLASSIC ORIGINAL", white, 2);
    label(screen, 24, 50, master ? "U.S. English  |  5 x 5  |  4-letter minimum" : "U.S. English  |  4 x 4  |  3-letter minimum", muted);
    const auto seconds = (round.remaining(now) + 999) / 1000;
    char timer[48]; std::snprintf(timer, sizeof(timer), "%llu:%02llu", (unsigned long long)(seconds / 60), (unsigned long long)(seconds % 60));
    if (round.timed()) label(screen, 670, 25, timer, white, 3);
    else label(screen, 652, 32, "NOT TIMED", white, 2);

    if(player!="PLAYER 1")label(screen,430,125,std::string(player),muted);

    for (size_t i = 0; i < round.board().faces().size(); ++i) {
        const int x = boardX + int(i % side) * cellSize, y = boardY + int(i / side) * cellSize;
        const bool selected = std::find(path.begin(), path.end(), i) != path.end();
        rect(screen, x, y, cellSize - 2, cellSize - 2, selected ? Ink{57, 180, 221} : Ink{86, 107, 130});
        rect(screen, x + 3, y + 3, cellSize - 8, cellSize - 8, white);
        if (!ready) glyph(screen, x, y, round.board().faces()[i], letters, qu, cellSize,cubeQuarterTurns(boardSeed,i,autoOrient));
        else label(screen, x + (cellSize - 32) / 2, y + (cellSize - 38) / 2, "?", {76, 95, 120}, 3);
        if(round.phase()==RoundPhase::Results && selected) {
            const auto at=std::find(path.begin(),path.end(),i);
            const auto number=std::to_string(size_t(at-path.begin())+1);
            rect(screen,x+7,y+7,int(number.size())*6+2,11,{20,48,60});
            label(screen,x+8,y+8,number);
        }
    }
    label(screen, 430, 102, ready ? "READY" : playing ? "ROUND IN PROGRESS" : "ROUND COMPLETE", white, 2);
    label(screen, 430, 140, "SCORE  " + std::to_string(results.ready()?results.score():round.score()), {110, 220, 200}, 2);
    std::vector<const RoundWord*> words;
    if(results.ready())for(const auto& word:results.words())words.push_back(&word);
    else for (const auto& word : round.words()) if (word.state != WordState::Declined) words.push_back(&word);
    label(screen, 430, 179, std::string(results.allWords()?"ALL WORDS  ":"WORDS  ") + std::to_string(words.size()), muted);
    rect(screen, 430, 204, 346, 260, {24, 42, 62});
    const auto view=wordHistoryView(words.size(),14,scroll);
    const size_t begin=view.begin;
    for (size_t i = begin; i < view.end; ++i) {
        const auto& entry = words[i]->entry;
        const bool rejected = words[i]->state == WordState::Pending || words[i]->state == WordState::Declined;
        const Ink ink = rejected ? Ink{255, 116, 116} : results.shared(entry.word)?Ink{156,156,156}:results.missed(entry.word)?Ink{130,180,255}:white;
        std::string shown = entry.word; if (shown.size() > 23) shown = shown.substr(0, 20) + "...";
        if(round.phase()==RoundPhase::Results && !path.empty() && entry.path==path)
            rect(screen,438,217+int(i-begin)*17,336,17,{48,88,120});
        label(screen, 442, 217 + int(i - begin) * 17, shown, ink);
        label(screen, 739, 217 + int(i - begin) * 17, words[i]->state == WordState::Declined ? "-" : std::to_string(entry.points), rejected || results.missed(entry.word) ? ink : Ink{110, 220, 200});
    }
    if (words.size() > 14) label(screen, 430, 470, "PgUp/PgDn: word history", muted);
    }
    rect(screen, 24, 485, artwork.active()?580:752, 43, {35, 57, 81});
    const std::string finalSummary="FINAL SCORE "+std::to_string(results.score())+"  /  "+std::to_string(results.countedCount())+" WORDS";
    const auto shownInput=results.ready()?input:input.substr(input.size()>50?input.size()-50:0);
    label(screen, 35, 497, input.empty() ? (reviewing ? "Review pending words before the next round." : playing ? "Type or select a word..." : ready ? "Press Enter to start or replay" : finalSummary) : shownInput, white, input.size() > 29 ? 1 : 2);
    if(results.ready()) {
        const auto b=artwork.active()?allWordsArtwork:allWordsPrototype;
        rect(screen,b.x,b.y,b.w,b.h,{48,88,120});
        label(screen,b.x+8,b.y+8,results.allWords()?"COUNTED WORDS (A)":"ALL WORDS (A)");
        const auto h=artwork.active()?scoresArtwork:scoresPrototype;
        rect(screen,h.x,h.y,h.w,h.h,{48,88,120});label(screen,h.x+8,h.y+8,"HIGH SCORES (H)");
    }
    const auto detail=message.empty() && results.allWords()?"Blue: missed words / potential points. Final score unchanged.":message;
    label(screen, 24, 535, reviewing ? "Choose one of the four dictionary options." : detail.empty() ? (ready ? "Press Enter when ready. The timer starts with the board." : playing ? "Enter: submit   Backspace: undo   Right click: clear" : results.words().empty()?"No words counted. Enter: replay   F2: new board":"Click a word or use Up/Down to review its path and points.") : detail, muted);
    if (!reviewing) { rect(screen, 24, 557, 180, 29, {48, 88, 120}); label(screen, 35, 566, playing ? (chess?"END PLAYER (F1)":"END ROUND (F1)") : ready ? "START / REPLAY (ENTER)" : "REPLAY (ENTER)"); }
    if (!playing && !reviewing) { rect(screen, 216, 557, 158, 29, {48, 88, 120}); label(screen, 227, 566, "NEW BOARD (F2)"); }
    label(screen, artwork.active()?450:610, 566, fromMenu ? "MENU (F3)" : "ESC: LAUNCHER", muted);
    if (const auto* word = round.candidate()) {
        rect(screen, 24, 182, 752, 270, {50, 78, 108});
        rect(screen, 27, 185, 746, 264, {22, 40, 60});
        label(screen, 48, 206, "ADD TO YOUR DICTIONARY?", white, 2);
        label(screen, 48, 244, word->entry.word, {255, 116, 116}, word->entry.word.size() > 40 ? 1 : 2);
        label(screen, 48, 282, "Yes keeps this word's points. No removes them.", muted);
        label(screen, 48, 306, "All applies to the remaining words in this round.", muted);
        for (int i = 0; i < 4; ++i) rect(screen, 40 + i * 184, 352, 168, 40, {48, 88, 120});
        label(screen, 54, 368, "YES (1)"); label(screen, 238, 368, "NO (2)");
        label(screen, 422, 368, "YES TO ALL (3)"); label(screen, 606, 368, "NO TO ALL (4)");
        if (!message.empty()) label(screen, 48, 416, message.substr(0, 88), muted);
    }

}
}
Error ClassicGame::run() {
    returnToMenu_ = false;
    ClassicOptions options;
    if (auto err = readClassicOptions(target_.settings, options)) return err;
    const bool chess=options.turns==ClassicTurnMode::Chess;
    PlayerProfile profile;
    if(auto error=profile.load(target_.savePath,target_.path))return error;
    std::string player=profile.name();
    if(const auto it=target_.settings.find("classic_player_name");it!=target_.settings.end())
        if(!playerName(it->second,player))return Error::fail("Invalid session player name");
    const auto names=players_.empty()?std::vector<std::string>{player}:players_;
    if(names.empty() || names.size()>4)return Error::fail("Classic needs one to four players");
    for(const auto& name:names){std::string normalized;if(!playerName(name,normalized) || normalized!=name)return Error::fail("Invalid Classic player name");}
    const auto kinds=computers_.empty()?std::vector<ComputerLevel>(names.size(),ComputerLevel::Human):computers_;
    if(kinds.size()!=names.size() || !std::all_of(kinds.begin(),kinds.end(),validComputerLevel))
        return Error::fail("Invalid Classic computer roster");
    const bool hasComputer=std::any_of(kinds.begin(),kinds.end(),[](auto kind){return kind!=ComputerLevel::Human;});
    const size_t humans=size_t(std::count(kinds.begin(),kinds.end(),ComputerLevel::Human));
    // As in the observed all-computer setup, background play needs a human.
    const bool background=options.background && hasComputer && humans;
    const bool computerCursor=options.cursor && hasComputer && !background;
    const size_t foregroundCount=background?humans:names.size();
    auto foreground=[&](size_t i){return !background || kinds[i]==ComputerLevel::Human;};
    Resources resources;
    if (auto err = resources.open(target_.path)) return err;
    Dictionary dictionary,computerDictionary; DiceSet dice; Palette palette;
    image::Image letters, qu;
    if (auto err = resources.dictionary("Dicts/English.dic", dictionary)) return err;
    if(std::any_of(kinds.begin(),kinds.end(),usesCommonVocabulary)) {
        if(!fs::findFile(target_.path,"Dicts/AI.dic"))return Error::fail("Beginner, Intermediate and Advanced need Dicts/AI.dic. Re-import the game from your CD or installation.");
        if(auto error=resources.dictionary("Dicts/AI.dic",computerDictionary))return error;
    }
    if (auto err = resources.diceSet(options.dice(), dice)) return err;
    if (auto err = resources.palette(options.palette(), palette)) return err;
    if (auto err = resources.image("Textures/Letters.bmp", letters)) return err;
    if (auto err = resources.image("Textures/LettersQu.bmp", qu)) return err;
    if (letters.w != 512 || letters.h != 512 || qu.w != 512 || qu.h != 512 || dice.side != options.side())
        return Error::fail("Unsupported Classic letter atlas or dice layout");
    video::AviVideo1 clip;
    if (options.animations) {
        const char* path = options.side() == 5 ? "Anims/ClsM1All.avi" : "Anims/ClsO1All.avi";
        if (auto err = resources.animation(path,clip)) return err;
        if (clip.frames.size() != 55) return Error::fail("Unsupported Classic round animation length");
    }
    ClassicArtwork artwork;
    if(auto err=artwork.open(resources,target_.path,options,clip))return err;
    ClassicCursorArtwork cursorArtwork;
    if(computerCursor && fs::findFile(target_.path,"Language/English.dll")) {
        Language language;if(auto error=resources.language("Language/English.dll",language))return error;
        if(auto error=cursorArtwork.read(language.resources))return error;
    }
    ClassicAudio sound(sys_);
    ClassicTransition transition;
    UserDictionary user;
    if (auto err = user.load(target_.savePath, target_.path)) return err;
    HighScores highScores;
    auto scoreError=highScores.load(target_.savePath,target_.path);
    bool scoreAttempted=false,scoreOpen=false,matchOpen=false,handoff=false,handoffVisible=false;
    std::vector<std::string> finishedNotices;
    RoundResults results;Board board;uint32_t seed=options.seed?*options.seed:freshClassicSeed();
    std::vector<std::unique_ptr<ClassicRound>> rounds;size_t turn=0,reviewPlayer=0,nextTurn=0;ClassicCompetition competition;PlayerClocks clocks,backgroundClock;
    bool backgroundStarted=false,backgroundFinished=false;
    std::vector<ComputerPlan> computers(names.size());std::vector<uint64_t> started(names.size());
    ComputerGeometry computerGeometry;computerGeometry.scale=1250;
    auto& centers=computerGeometry.centers;
    for(size_t i=0;i<options.side()*options.side();++i) {
        const int cell=boardSize/int(options.side());
        centers.push_back(artwork.active()?artwork.cellCenter(i,options.side()):
            ClassicPoint{boardX+int(i%options.side())*cell+cell/2,boardY+int(i/options.side())*cell+cell/2});
    }
    const HistoryButton submitArea=artwork.active()?HistoryButton{621,52,179,396}:HistoryButton{430,204,346,260};
    const ClassicPoint submitPoint{submitArea.x+submitArea.w/2,submitArea.y+submitArea.h/2};
    const ClassicPoint cursorRest{(centers.front().x+centers.back().x)/2,(centers.front().y+centers.back().y)/2};
    computerGeometry.origin=cursorRest;computerGeometry.submit=submitPoint;
    auto resetRounds=[&](const Board& cubes)->Error {
        std::vector<std::unique_ptr<ClassicRound>> next;
        std::vector<ComputerPlan> plans(names.size());
        for(size_t i=0;i<names.size();++i) {
            if(kinds[i]!=ComputerLevel::Human)
                if(auto error=plans[i].build(cubes,computerDictionary,dictionary,kinds[i],seed,i,500000,&computerGeometry))return error;
            auto current=std::make_unique<ClassicRound>(dictionary,&user);
            if(auto error=current->board(cubes,options.duration))return error;
            if(!foreground(i))if(auto error=current->start(0,options.duration))return error;
            next.push_back(std::move(current));
        }
        if(auto error=clocks.reset(names.size()))return error;
        if(auto error=backgroundClock.reset(1))return error;
        backgroundStarted=backgroundFinished=false;
        rounds=std::move(next);computers=std::move(plans);std::fill(started.begin(),started.end(),0);
        turn=0;while(!foreground(turn))++turn;
        reviewPlayer=nextTurn=turn;competition.clear();finishedNotices.clear();return Error::success();
    };
    auto round=[&]()->ClassicRound& {return *rounds[competition.ready()?reviewPlayer:turn];};
    ClassicClock gameplayClock;
    auto nowMillis=[&](){return gameplayClock.now(sys_.getMillis());};
    auto gameTime=[&](){return chess?clocks.elapsed(competition.ready()?reviewPlayer:turn,nowMillis()):nowMillis();};
    auto nextChessPlayer=[&]()->std::optional<size_t> {
        for(size_t step=1;step<=rounds.size();++step) {
            const auto index=(turn+step)%rounds.size();
            if(foreground(index) && rounds[index]->phase()!=RoundPhase::Results)return index;
        }
        return {};
    };
    if(auto error=Board::generate(dice,seed,board))return error;
    if(auto error=resetRounds(board))return error;
    if (auto err = sys_.initGraphics(width, height, PixelFormat::RGBA32)) return err;
    sys_.setWindowTitle(options.variant == ClassicVariant::Master ? "Reluva - Boggle Classic Master" : "Reluva - Boggle Classic Original");
    // Start output after the window and framebuffer are ready.
    if (auto err = sound.open(resources,options.sound,options.music,uint32_t(nowMillis()))) return err;
    sys_.setMouseCursorVisible(true); sys_.setTextInput(true);
    struct TextInputCleanup { System& system; ~TextInputCleanup() { system.setTextInput(false);system.setMouseCursorVisible(true); } } cleanup{sys_};
    Surface screen; screen.create(width, height, PixelFormat::RGBA32);
    std::string input, message; std::vector<size_t> selected; size_t scroll = 0;
    bool followingU=false,minimumNotice=false,noticeVisible=false,pauseVisible=false;
    const HistoryButton pauseButton{artwork.active()?610:384,557,artwork.active()?166:110,29};
    bool running = true, dragging = false, resultsVisible = false, promptVisible = false;
    int pointerX=-1,pointerY=-1;
    bool softwareCursor=false,computerPressed=false;
    std::optional<ClassicPoint> computerPointer;ClassicPoint traceOrigin;
    const AcceptedWord* tracing=nullptr;
    std::optional<size_t> reviewSelection;
    auto browse=[&](int64_t rows) {
        const size_t count=results.ready()?results.words().size():size_t(std::count_if(round().words().begin(),round().words().end(),[](const auto& word){return word.state!=WordState::Declined;}));
        scroll=scrollWordHistory(scroll,count,artwork.active()?ClassicArtwork::visibleWords:14,rows);
    };
    auto clear = [&] { followingU=false;input.clear(); selected.clear(); scroll = 0; dragging = false; reviewSelection.reset();computerPointer.reset();computerPressed=false;tracing=nullptr; };
    auto toggleWords=[&]() {
        if(!resultsVisible || !results.ready())return;
        if(auto error=results.showAll(!results.allWords(),dictionary,&user)){message=error.message;return;}
        clear();message.clear();
        if(results.allWords())scroll=wordHistoryView(results.words().size(),artwork.active()?ClassicArtwork::visibleWords:14,0).maximum;
    };
    auto saveScore=[&]() {
        scoreAttempted=true;
        if(!scoreError) {
            std::vector<HighScore> scores;
            if(competition.ready()) {
                for(size_t i=0;i<names.size();++i)if(kinds[i]==ComputerLevel::Human)
                    scores.push_back({options.variant,options.duration,competition.players()[i].score,names[i]});
            } else if(kinds[0]==ComputerLevel::Human)scores.push_back({options.variant,options.duration,results.score(),names[0]});
            if(!scores.empty())scoreError=highScores.recordBatch(scores);
        }
    };
    auto updateInput = [&] { followingU=false;input.clear(); for (size_t i : selected) input += round().board().faces()[i]; };
    auto startBackground=[&](uint64_t now)->Error {
        if(!background || backgroundStarted)return Error::success();
        if(auto error=backgroundClock.activate(0,now))return error;
        backgroundStarted=true;return Error::success();
    };
    auto settleBackground=[&](uint64_t now) {
        if(!background || !backgroundStarted || backgroundFinished)return;
        for(size_t i=0;i<names.size();++i)if(foreground(i) && rounds[i]->phase()!=RoundPhase::Results)return;
        backgroundClock.stop(now);backgroundFinished=true;
    };
    auto startOrFinish = [&]() -> Error {
        if (round().phase() == RoundPhase::Results && !resultsVisible) return Error::success();
        if(competition.ready() || (names.size()==1 && round().phase()==RoundPhase::Results)) {
            const auto cubes=round().board();if(auto error=resetRounds(cubes))return error;
        }
        resultsVisible = false; results.clear(); scoreAttempted=false; clear(); message.clear();
        if (round().phase() == RoundPhase::Playing) {
            round().finish(); sound.stopShake();if(chess)clocks.stop(nowMillis());
            settleBackground(nowMillis());
            if (options.animations && (!chess || !nextChessPlayer()))transition.begin(Transition::Closing,nowMillis());
        } else {
            sound.beginRound(options.animations);
            if (options.animations) transition.begin(Transition::Opening,nowMillis());
            else {
                started[turn]=nowMillis();
                if(chess)if(auto error=clocks.activate(turn,started[turn]))return error;
                if(auto error=round().start(chess?0:started[turn],options.duration))return error;
                if(auto error=startBackground(started[turn]))return error;
            }
        }
        return Error::success();
    };
    auto newBoard = [&]() -> Error {
        if (round().phase() == RoundPhase::Results && !resultsVisible) return Error::success();
        if (round().phase() == RoundPhase::Playing) return Error::success();
        seed=options.seed?seed+1:freshClassicSeed();
        if (auto err = Board::generate(dice, seed, board)) return err;
        if(auto err=resetRounds(board))return err;
        resultsVisible = false; results.clear(); scoreAttempted=false; clear(); message.clear(); return Error::success();
    };
    auto reviewWord=[&](size_t index) {
        if(!results.ready() || index>=results.words().size())return;
        const auto& word=results.words()[index];reviewSelection=index;
        selected=word.entry.path;input=word.entry.word;
        if(results.shared(word.entry.word)){message="Shared word / 0 points | Right click: summary";scroll=results.scrollFor(index,artwork.active(),scroll);dragging=false;return;}
        message=std::to_string(word.entry.points)+(results.missed(word.entry.word)?(word.entry.points==1?" potential point / not submitted":" potential points / not submitted"):word.entry.points==1?" point / ":" points / ");
        if(!results.missed(word.entry.word))message+=word.state==WordState::Added?"added to your dictionary":"dictionary word";
        message+=" | Right click: summary";
        scroll=results.scrollFor(index,artwork.active(),scroll);dragging=false;
    };
    auto queueChessHandoff=[&](size_t target,bool endedCurrent) {
        finishedNotices.clear();
        if(endedCurrent)finishedNotices.push_back(names[turn]);
        for(size_t i=(turn+1)%names.size();i!=target;i=(i+1)%names.size())
            if(foreground(i) && rounds[i]->phase()==RoundPhase::Results)finishedNotices.push_back(names[i]);
        nextTurn=target;handoff=true;handoffVisible=false;resultsVisible=false;
    };
    auto advanceBackground=[&](bool finish)->Error {
        if(!background)return Error::success();
        const uint64_t now=nowMillis();
        settleBackground(now);
        // The owner confirmed one continuous budget across player switches.
        // Timed computers complete the full budget when all humans end early;
        // untimed computers stop at the last human's end, before dictionary review.
        const uint64_t elapsed=options.duration && backgroundFinished?
            options.duration:backgroundClock.elapsed(0,now);
        // Words at the exact deadline cannot enter a timed Classic round.
        const uint64_t horizon=options.duration?std::min(elapsed,options.duration-1):elapsed;
        for(size_t i=0;i<names.size();++i)if(!foreground(i)) {
            uint64_t submittedAt=0;
            while(const auto* word=computers[i].takeScheduled(horizon,submittedAt))
                if(rounds[i]->submit(word->word,submittedAt,word->path)!=Submission::Accepted)
                    return Error::fail("Background computer produced an invalid Classic submission");
            if(finish)rounds[i]->finish();
        }
        return Error::success();
    };
    auto publish=[&]()->Error {
        if(names.size()==1)return round().candidate()?Error::success():results.build(round());
        if(!competition.ready()) {
            if(chess) {
                if(const auto next=nextChessPlayer()){queueChessHandoff(*next,true);return Error::success();}
            } else {
                for(size_t i=turn+1;i<rounds.size();++i)if(foreground(i) && rounds[i]->phase()!=RoundPhase::Results) {
                    nextTurn=i;handoff=true;handoffVisible=false;return Error::success();
                }
            }
            if(auto error=advanceBackground(true))return error;
            for(auto& current:rounds)current->acceptKnownPending();
            for(size_t i=0;i<rounds.size();++i)if(rounds[i]->candidate()) {
                if(turn!=i){turn=i;clear();message.clear();promptVisible=false;}
                return Error::success();
            }
            std::vector<const ClassicRound*> input;for(const auto& current:rounds)input.push_back(current.get());
            if(auto error=competition.build(input))return error;
            reviewPlayer=0;matchOpen=true;
        }
        return results.build(round(),competition.players()[reviewPlayer]);
    };
    auto chooseReview=[&](size_t index)->Error {
        if(index>=names.size())return Error::success();
        reviewPlayer=index;clear();message.clear();return results.build(round(),competition.players()[index]);
    };
    auto timeout=[&](uint64_t now) {
        clear();dragging=false;resultsVisible=false;message="Time is up.";
        sound.stopShake();if(chess)clocks.stop(now);settleBackground(now);sound.countdown(0,chess?turn:0);
        if(options.animations && (!chess || !nextChessPlayer()))transition.begin(Transition::Closing,now);
    };
    auto passWord=[&]()->Error {
        if(!chess || foregroundCount==1)return Error::success();
        const auto now=nowMillis();clocks.stop(now);
        round().tick(clocks.elapsed(turn,now));
        if(round().phase()==RoundPhase::Results){timeout(now);return Error::success();}
        const auto next=nextChessPlayer();
        if(!next)return Error::fail("Chess Timer has no next player after a recorded word");
        queueChessHandoff(*next,false);return Error::success();
    };
    auto submitInput=[&]()->Error {
        const auto result=round().submit(input,gameTime(),selected);
        message=feedback(result,round().minimum());
        if(result==Submission::Accepted || result==Submission::UnknownWord)sound.recorded(round().words().back().entry.word.size());
        if(result!=Submission::SelectPath)clear();
        if(result==Submission::TooShort) {
            minimumNotice=true;noticeVisible=false;gameplayClock.notice(true,sys_.getMillis());
        }
        if(result==Submission::NotPlaying)timeout(nowMillis());
        if(result==Submission::Accepted || result==Submission::UnknownWord || (chess && result==Submission::Duplicate))return passWord();
        return Error::success();
    };
    auto select = [&](int x, int y) {
        const size_t side = round().board().side(); const int cellSize = boardSize / int(side);
        if(round().phase()!=RoundPhase::Playing)return;
        size_t cell=0;
        if(artwork.active()) {
            const auto found=artwork.cellAt(x,y,side);if(!found)return;cell=*found;
        } else {
            if(!hit(x,y,boardX,boardY,cellSize*int(side),cellSize*int(side)))return;
            cell=size_t((y-boardY)/cellSize)*side+size_t((x-boardX)/cellSize);
        }
        if (!selected.empty() && cell == selected.back()) return;
        if (std::find(selected.begin(), selected.end(), cell) != selected.end()) { message = "That cube is already used."; return; }
        if (!selected.empty() && !round().board().adjacent(selected.back(), cell)) { message = "Choose a touching cube."; return; }
        if (selected.empty()) input.clear();
        selected.push_back(size_t(cell)); updateInput(); message.clear(); sound.play(Cue::ClickDown);
    };
    auto advance = [&]() -> Error {
        if(gameplayClock.paused())return Error::success();
        const uint64_t now = nowMillis();
        const auto type = transition.type();
        if (type == Transition::Opening && transition.frame(now) >= 31) sound.stopShake();
        if (transition.finish(now) && type == Transition::Opening) {
            sound.stopShake();
            if(chess)if(auto error=clocks.activate(turn,now))return error;
            started[turn]=now;
            if (auto err = round().start(chess?0:now,options.duration)) return err;
            if(auto error=startBackground(now))return error;
        }
        if (transition.type() != Transition::None) return Error::success();
        const bool playing = round().phase() == RoundPhase::Playing;
        round().tick(gameTime());
        if (playing && round().phase() == RoundPhase::Results) {
            timeout(now);
        } else if (playing && round().timed() && !handoff) sound.countdown(round().remaining(gameTime()),chess?turn:0);
        return Error::success();
    };
    auto acceptHandoff=[&]()->Error {
        turn=nextTurn;handoff=false;handoffVisible=false;resultsVisible=false;
        if(chess) {
            clear();message.clear();
            if(auto error=clocks.activate(turn,nowMillis()))return error;
            if(round().phase()==RoundPhase::Ready)if(auto error=round().start(0,options.duration))return error;
        } else if(auto error=startOrFinish())return error;
        return Error::success();
    };
    if (fromMenu_) { if (auto err = startOrFinish()) return err; }
    while (running) {
        // Render each handoff once before an automatic computer turn starts.
        if(!gameplayClock.paused() && handoff && handoffVisible && kinds[nextTurn]!=ComputerLevel::Human)
            if(auto error=acceptHandoff())return error;
        if (auto err = advance()) return err;
        Event event;bool scoreDismissed=false;
        while (sys_.pollEvent(event)) {
            if (auto err = advance()) return err;
            if (event.type == EventType::Quit || event.type == EventType::ReturnToLauncher ||
                (event.type == EventType::KeyDown && event.key == key::Escape && !event.repeat && !fromMenu_ && !minimumNotice && !scoreOpen && !matchOpen && !scoreDismissed)) {
                running = false; break;
            }
            // Escape is inactive in the original game. Provide an explicit
            // native menu action without changing that observed key behavior.
            if (!scoreOpen && !matchOpen && !scoreDismissed && fromMenu_ && ((event.type == EventType::KeyDown && !event.repeat && event.key == key::F1+2) ||
                (transition.type() == Transition::None && event.type == EventType::MouseDown && event.button == MouseButton::Left &&
                 hit(event.mouseX,event.mouseY,artwork.active()?432:610,557,artwork.active()?172:166,29)))) {
                returnToMenu_ = true; running = false; break;
            }
            if(event.type==EventType::MouseMove || event.type==EventType::MouseDown || event.type==EventType::MouseUp) {
                pointerX=event.mouseX;pointerY=event.mouseY;
            }
            if(event.type==EventType::FocusLost || event.type==EventType::FocusGained) {
                gameplayClock.focus(event.type==EventType::FocusGained,sys_.getMillis());
                dragging=false;scoreDismissed=true;continue;
            }
            if(scoreDismissed)continue;
            if(minimumNotice) {
                const bool okay=(event.type==EventType::KeyDown && !event.repeat &&
                    (event.key==key::Return || event.key==key::KpEnter || event.key==key::Escape || event.key==key::Space)) ||
                    (event.type==EventType::MouseDown && event.button==MouseButton::Left && noticeOkay.contains(event.mouseX,event.mouseY));
                if(gameplayClock.focused() && noticeVisible && okay) {
                    minimumNotice=false;gameplayClock.notice(false,sys_.getMillis());
                    message.clear();scoreDismissed=true;
                }
                continue;
            }
            if(gameplayClock.manual()) {
                if(gameplayClock.focused() && pauseVisible && event.type==EventType::MouseDown &&
                   event.button==MouseButton::Left && resumeButton.contains(event.mouseX,event.mouseY)) {
                    gameplayClock.manual(false,sys_.getMillis());scoreDismissed=true;
                }
                continue;
            }
            if(gameplayClock.paused())continue;
            if(!handoff && !matchOpen && !scoreOpen && transition.type()==Transition::None &&
               round().phase()==RoundPhase::Playing && event.type==EventType::MouseDown &&
               event.button==MouseButton::Left && pauseButton.contains(event.mouseX,event.mouseY)) {
                gameplayClock.manual(true,sys_.getMillis());pauseVisible=false;dragging=false;continue;
            }
            if(handoff) {
                if(handoffVisible && ((event.type==EventType::KeyDown && !event.repeat && (event.key==key::Return || event.key==key::KpEnter)) ||
                   (event.type==EventType::MouseDown && event.button==MouseButton::Left && hit(event.mouseX,event.mouseY,80,380,640,60)))) {
                    if(auto error=acceptHandoff())return error;
                    scoreDismissed=true;
                }
                continue;
            }
            if(matchOpen) {
                const bool keyDown=event.type==EventType::KeyDown && !event.repeat;
                const bool click=event.type==EventType::MouseDown && event.button==MouseButton::Left;
                std::optional<size_t> selectedPlayer;
                if(keyDown && event.key>='1' && event.key<'1'+names.size())selectedPlayer=size_t(event.key-'1');
                if(click && hit(event.mouseX,event.mouseY,150,180,480,int(names.size())*40))selectedPlayer=size_t((event.mouseY-180)/40);
                if(selectedPlayer){if(auto error=chooseReview(*selectedPlayer))return error;matchOpen=false;scoreDismissed=true;}
                else if((keyDown && (event.key==key::Return || event.key==key::KpEnter || event.key==key::Escape || event.key=='v')) ||
                        (click && scoresClose.contains(event.mouseX,event.mouseY))) {matchOpen=false;scoreDismissed=true;}
                continue;
            }
            if(scoreOpen) {
                const bool key=event.type==EventType::KeyDown && !event.repeat;
                const bool click=event.type==EventType::MouseDown && event.button==MouseButton::Left;
                if((key && (event.key==key::Return || event.key==key::KpEnter || event.key==key::Escape || event.key=='h')) ||
                   (click && scoresClose.contains(event.mouseX,event.mouseY))) {scoreOpen=false;scoreDismissed=true;}
                else if(scoreError && ((key && event.key=='r') || (click && scoresRetry.contains(event.mouseX,event.mouseY)))) {
                    scoreError=highScores.load(target_.savePath,target_.path);saveScore();
                }
                continue;
            }
            if (transition.type() != Transition::None) continue;
            if (round().candidate()) {
                int choice = -1;
                if (promptVisible && event.type == EventType::KeyDown && !event.repeat && event.key >= '1' && event.key <= '4')
                    choice = int(event.key - '1');
                if (promptVisible && event.type == EventType::MouseDown && event.button == MouseButton::Left)
                    for (int i = 0; i < 4; ++i) if (hit(event.mouseX, event.mouseY, 40 + i * 184, 352, 168, 40)) choice = i;
                if (choice >= 0) {
                    const DictionaryChoice choices[] = {DictionaryChoice::Yes, DictionaryChoice::No, DictionaryChoice::YesAll, DictionaryChoice::NoAll};
                    if (auto err = round().decide(choices[choice])) message = err.message;
                    else { message.clear(); promptVisible = false; resultsVisible = false; }
                }
                continue;
            }
            if(round().phase()==RoundPhase::Results && !results.ready()) {
                if(names.size()>1 && !competition.ready())continue;
                if(auto error=publish())return error;
            }
            if(resultsVisible && competition.ready() && event.type==EventType::KeyDown && !event.repeat) {
                if(event.key=='v'){matchOpen=true;continue;}
                if(event.key>='1' && event.key<'1'+names.size()){if(auto error=chooseReview(size_t(event.key-'1')))return error;continue;}
            }
            if(resultsVisible && results.ready() && ((event.type==EventType::KeyDown && !event.repeat && event.key=='h') ||
               (event.type==EventType::MouseDown && event.button==MouseButton::Left &&
                (artwork.active()?scoresArtwork:scoresPrototype).contains(event.mouseX,event.mouseY)))) {
                scoreOpen=true;dragging=false;continue;
            }
            if(resultsVisible && results.ready() && ((event.type==EventType::KeyDown && !event.repeat && event.key=='a') ||
               (event.type==EventType::MouseDown && event.button==MouseButton::Left &&
                (artwork.active()?allWordsArtwork:allWordsPrototype).contains(event.mouseX,event.mouseY)))) {
                toggleWords();continue;
            }
            if(resultsVisible && results.ready() && event.type==EventType::MouseDown && event.button==MouseButton::Left) {
                if(const auto row=results.rowAt(event.mouseX,event.mouseY,artwork.active(),scroll)) {
                    reviewWord(*row);continue;
                }
            }
            if(kinds[turn]!=ComputerLevel::Human && round().phase()==RoundPhase::Playing) {
                const bool end=(event.type==EventType::KeyDown && !event.repeat && event.key==key::F1) ||
                    (event.type==EventType::MouseDown && event.button==MouseButton::Left && hit(event.mouseX,event.mouseY,24,557,180,29));
                if(end)if(auto error=startOrFinish())return error;
                continue; // human typing and cube input cannot submit for a computer
            }
            if (event.type == EventType::KeyDown) {
                if(resultsVisible && results.ready() && (event.key==key::Up || event.key==key::Down)) {
                    if(!results.words().empty()) {
                        const size_t last=results.words().size()-1;
                        const size_t index=reviewSelection ? (event.key==key::Up ? *reviewSelection-std::min(*reviewSelection,size_t(1)) : std::min(last,*reviewSelection+1)) : (event.key==key::Up?last:0);
                        reviewWord(index);
                    }
                } else if ((event.key == key::Return || event.key == key::KpEnter) && !event.repeat) {
                    if (round().phase() != RoundPhase::Playing) { if (auto err = startOrFinish()) return err; }
                    else if(auto error=submitInput())return error;
                } else if (event.key == key::PageUp || event.key == key::PageDown) {
                    const int64_t page=artwork.active()?ClassicArtwork::visibleWords:14;
                    browse(event.key==key::PageUp?page:-page);
                } else if (event.key == key::Backspace && round().phase() == RoundPhase::Playing) {
                    if (!selected.empty()) { selected.pop_back(); updateInput(); }
                    else eraseEnglishCube(input);
                    followingU=false;
                    message.clear();
                } else if (event.key == key::F1 && !event.repeat && round().phase() == RoundPhase::Playing) {
                    if (auto err = startOrFinish()) return err;
                } else if (event.key == key::F1 + 1 && !event.repeat) { if (auto err = newBoard()) return err; }
            } else if (event.type == EventType::TextInput && round().phase() == RoundPhase::Playing) {
                const auto end = std::find(std::begin(event.text), std::end(event.text), '\0');
                const auto result=enterBoardText(round().board(),std::string_view(event.text,size_t(end-event.text)),input,selected,followingU);
                message=result==KeyboardResult::InvalidLetters?"Use A-Z for this English round.":
                    result==KeyboardResult::NotOnBoard?"Choose a touching, unused cube.":
                    result==KeyboardResult::Limit?"Please select this word on the board.":"";
            } else if(event.type==EventType::MouseWheel) {
                const bool over=artwork.active()?hit(pointerX,pointerY,621,52,179,448):hit(pointerX,pointerY,430,204,346,260);
                if(over){browse(int64_t(event.wheelY)*3);dragging=false;}
            } else if (event.type == EventType::MouseDown && event.button == MouseButton::Right) {
                const size_t previousScroll=scroll;clear();if(results.ready())scroll=previousScroll;
                message.clear(); dragging = false;
            } else if (event.type == EventType::MouseDown && event.button == MouseButton::Left) {
                if(artwork.active() && (historyOlder.contains(event.mouseX,event.mouseY) || historyNewer.contains(event.mouseX,event.mouseY))) {
                    browse(historyOlder.contains(event.mouseX,event.mouseY)?1:-1);dragging=false;
                } else if(round().phase()==RoundPhase::Playing && submitArea.contains(event.mouseX,event.mouseY)) {
                    dragging=false;if(!input.empty())if(auto error=submitInput())return error;
                } else if (hit(event.mouseX, event.mouseY, 24, 557, 180, 29)) { if (auto err = startOrFinish()) return err; }
                else if (hit(event.mouseX, event.mouseY, 216, 557, 158, 29)) { if (auto err = newBoard()) return err; }
                else { dragging = true; select(event.mouseX, event.mouseY); }
            } else if (event.type == EventType::MouseMove && dragging) select(event.mouseX, event.mouseY);
            else if (event.type == EventType::MouseUp && event.button == MouseButton::Left) {
                if (dragging && !selected.empty()) sound.play(Cue::ClickUp);
                dragging = false;
            }
        }
        if (!running) break;
        if(!gameplayClock.paused())if(auto error=advanceBackground(false))return error;
        if(!gameplayClock.paused() && !handoff && !competition.ready() && transition.type()==Transition::None &&
           kinds[turn]!=ComputerLevel::Human && round().phase()==RoundPhase::Playing) {
            auto& plan=computers[turn];
            const uint64_t now=gameTime();
            const uint64_t elapsed=chess?now:(now>=started[turn]?now-started[turn]:0);
            if(computerCursor) {
                computerPressed=false;input.clear();selected.clear();
                if(const auto* word=plan.nextWord()) {
                    if(tracing!=word){tracing=word;traceOrigin=computerPointer.value_or(word==plan.words().data()?cursorRest:submitPoint);}
                    const auto frame=computerCursorFrame(*word,centers,traceOrigin,elapsed,plan.due(),submitPoint,plan.nextMotion());
                    computerPointer=frame.active?frame.point:traceOrigin;computerPressed=frame.pressed;
                    if(frame.active) {
                        selected.assign(word->path.begin(),word->path.begin()+frame.cubes);
                        for(size_t cube:selected)input+=round().board().faces()[cube];
                    }
                }
            }
            if(plan.exhausted()) {
                if(plan.finished(elapsed)){if(auto error=startOrFinish())return error;}
                else message="Computer looking for more words / F1: end player";
            } else if(const auto* word=plan.take(elapsed)) {
                const auto outcome=round().submit(word->word,now,word->path);
                if(outcome==Submission::Accepted) {
                    input=word->word;selected.clear();message="Computer playing / F1: end player";
                    if(computerCursor)computerPointer=submitPoint;
                    sound.recorded(word->word.size());
                    if(plan.finished(elapsed)) {
                        if(auto error=startOrFinish())return error;
                    } else if(auto error=passWord())return error;
                } else if(outcome==Submission::NotPlaying)timeout(nowMillis());
                else return Error::fail("Computer plan produced an invalid Classic submission");
            } else message="Computer playing / F1: end player";
        }
        if(transition.type()==Transition::None && round().phase()==RoundPhase::Results && !results.ready() && !handoff)
            if(auto error=publish())return error;
        if(results.ready() && !scoreAttempted)saveScore();
        if(handoff)handoffScreen(screen,names[nextTurn],chess,rounds[nextTurn]->remaining(clocks.elapsed(nextTurn,nowMillis())),rounds[nextTurn]->timed(),finishedNotices);
        else if (transition.type() != Transition::None) {
            const size_t frameIndex=transition.frame(nowMillis());
            const auto& frame = clip.frames[frameIndex];
            for (int y=0;y<height;++y) for (int x=0;x<width;++x)
                std::copy_n(frame.pixel(x*frame.w/width,y*frame.h/height),4,screen.row(y)+x*4);
            artwork.drawTransition(screen,round().board(),frameIndex,letters,qu,seed);
            rect(screen,0,height-40,width,40,dark);
            label(screen,24,height-25,transition.type() == Transition::Opening ? "Rolling dice and opening the lid..." : "Closing the lid...");
        } else draw(screen, round(), gameTime(), letters, qu, palette, input, selected, results.ready() && scoreError && message.empty()?"High scores unavailable. H: details / retry.":competition.ready() && message.empty()?"1-4: player review / V: match scores / Up-Down: word path":message, scroll, fromMenu_,artwork,options.duration,seed,options.autoOrient,names[competition.ready()?reviewPlayer:turn],results,chess);
        const bool showComputerCursor=!gameplayClock.paused() && computerCursor && !handoff && !competition.ready() &&
            transition.type()==Transition::None && round().phase()==RoundPhase::Playing && kinds[turn]!=ComputerLevel::Human;
        if(showComputerCursor)cursorArtwork.draw(screen,computerPointer.value_or(cursorRest),computerPressed);
        if(showComputerCursor!=softwareCursor){softwareCursor=showComputerCursor;sys_.setMouseCursorVisible(!softwareCursor);}
        if(background)liveScores(screen,names,rounds,competition);
        if(matchOpen)matchTable(screen,names,competition,chess,background);
        if(scoreOpen)scoreTable(screen,highScores,scoreError.message);
        if(!handoff && !matchOpen && !scoreOpen && transition.type()==Transition::None && round().phase()==RoundPhase::Playing) {
            rect(screen,pauseButton.x,pauseButton.y,pauseButton.w,pauseButton.h,{48,88,120});
            label(screen,pauseButton.x+12,pauseButton.y+9,"PAUSE");
        }
        if(gameplayClock.manual() || !gameplayClock.focused()) {
            rect(screen,0,0,width,height,dark);
            label(screen,260,280,gameplayClock.focused()?"Game paused":"Paused while window is inactive");
            if(gameplayClock.manual() && gameplayClock.focused()) {
                rect(screen,resumeButton.x,resumeButton.y,resumeButton.w,resumeButton.h,{48,88,120});
                label(screen,350,357,"RESUME");
            }
        } else if(minimumNotice) {
            rect(screen,100,210,600,210,{22,40,60});
            label(screen,160,260,"Words must be at least "+std::to_string(round().minimum())+" characters long.");
            rect(screen,noticeOkay.x,noticeOkay.y,noticeOkay.w,noticeOkay.h,{48,88,120});
            label(screen,390,365,"OK");
        }
        sys_.updateScreen(screen);
        if(handoff)handoffVisible=true;
        noticeVisible=minimumNotice && gameplayClock.focused();
        pauseVisible=gameplayClock.manual() && gameplayClock.focused();
        if (transition.type() == Transition::None && round().phase() == RoundPhase::Results) resultsVisible = true;
        promptVisible = !handoff && !gameplayClock.paused() && transition.type() == Transition::None && round().candidate() != nullptr;
        sys_.delayMillis(16);
    }
    return Error::success();
}
} // namespace reluva::boggle
