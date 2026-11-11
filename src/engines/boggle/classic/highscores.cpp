// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/highscores.h"
#include "engines/boggle/classic/player.h"
#include <algorithm>
#include <charconv>
#include <fstream>
namespace reluva::boggle {
namespace {
Error regular(const std::filesystem::path& path) {
    std::error_code ec;const auto status=std::filesystem::symlink_status(path,ec);
    if(status.type()==std::filesystem::file_type::not_found)return Error::success();
    if(ec || !std::filesystem::is_regular_file(status))return Error::fail("High-score path must be a regular file");
    return Error::success();
}
bool inside(const std::filesystem::path& parent,std::filesystem::path child) {
    while(!child.empty()) {
        std::error_code ec;if(child==parent || std::filesystem::equivalent(parent,child,ec))return true;
        const auto next=child.parent_path();if(next==child)break;child=next;
    }
    return false;
}
bool category(ClassicVariant variant,uint64_t duration) {
    return (variant==ClassicVariant::Original || variant==ClassicVariant::Master) &&
           (!duration || (duration>=60000 && duration<=300000 && duration%30000==0));
}
void order(std::vector<HighScore>& rows) {
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){
        return a.score>b.score;
    });
}
bool number(std::string_view text,uint64_t& n) {
    const auto result=std::from_chars(text.data(),text.data()+text.size(),n);
    return result.ec==std::errc{} && result.ptr==text.data()+text.size() && std::to_string(n)==text;
}
Error failure(){return Error::fail("Cannot save high scores. Check the save directory and retry.");}
}
Error HighScores::load(const std::filesystem::path& savePath,const std::filesystem::path& gamePath) {
    rows_.clear();file_.clear();loaded_=false;
    if(savePath.empty()){loaded_=true;return Error::success();}
    std::error_code ec;const auto save=std::filesystem::weakly_canonical(savePath,ec);
    if(ec)return Error::fail("Cannot resolve high-score save directory");
    if(!gamePath.empty()) {
        const auto game=std::filesystem::weakly_canonical(gamePath,ec);
        if(ec || inside(game,save))return Error::fail("High scores must be saved outside the original game folder");
    }
    const auto file=save / "HighScores.txt",backup=save / "HighScores.txt.bak";
    for(const auto& path:{file,backup})if(auto error=regular(path))return error;
    auto source=file;bool present=std::filesystem::exists(file,ec);
    if(ec)return Error::fail("Cannot inspect high scores");
    if(!present){source=backup;present=std::filesystem::exists(backup,ec);}
    if(ec)return Error::fail("Cannot inspect high-score backup");
    std::vector<HighScore> rows;
    if(present) {
        std::ifstream in(source,std::ios::binary|std::ios::ate);
        if(!in || in.tellg()<0 || in.tellg()>4096)return Error::fail("Cannot read bounded high-score file");
        in.seekg(0);std::string line;
        auto nextLine=[&]{if(!std::getline(in,line))return false;if(!line.empty() && line.back()=='\r')line.pop_back();return true;};
        if(!nextLine() || (line!="RELUVA BOGGLE HIGHSCORES 1" && line!="RELUVA BOGGLE HIGHSCORES 2"))return Error::fail("Invalid native high-score header");
        const bool legacy=line=="RELUVA BOGGLE HIGHSCORES 1";
        while(nextLine()) {
            const auto a=line.find('\t'),b=a==std::string::npos?a:line.find('\t',a+1),c=b==std::string::npos?b:line.find('\t',b+1);
            if(c==std::string::npos || rows.size()==10)return Error::fail("Invalid native high-score row");
            const auto variant=line.substr(0,a);uint64_t seconds=0,score=0;std::string name;
            const auto mode=variant=="original"?ClassicVariant::Original:ClassicVariant::Master;
            if((variant!="original" && variant!="master") || !number(std::string_view(line).substr(a+1,b-a-1),seconds) || seconds>300 ||
               !category(mode,seconds*1000) || !number(std::string_view(line).substr(b+1,c-b-1),score) || (legacy && !score) || score>INT32_MAX ||
               !playerName(std::string_view(line).substr(c+1),name) || name!=line.substr(c+1))
                return Error::fail("Invalid native high-score row");
            rows.push_back({mode,seconds*1000,int(score),name});
        }
        if(in.bad())return Error::fail("Cannot read high scores");

    }
    order(rows);rows_=std::move(rows);file_=file;loaded_=true;return Error::success();
}
std::vector<HighScore> HighScores::table() const { return rows_; }

Error HighScores::record(ClassicVariant variant,uint64_t duration,int score,std::string_view input) {
    return recordBatch({HighScore{variant,duration,score,std::string(input)}});
}
Error HighScores::recordBatch(const std::vector<HighScore>& scores) {
    if(!loaded_)return Error::fail("High scores are unavailable; reload the table before retrying");
    if(scores.empty() || scores.size()>4)return Error::fail("A Classic score batch needs one to four players");
    auto rows=rows_;
    for(const auto& score:scores) {
        std::string name;
        if(!category(score.variant,score.duration) || score.score<0 || !playerName(score.name,name))return Error::fail("Invalid native high-score entry");
        const auto at=std::lower_bound(rows.begin(),rows.end(),score.score,[](const auto& row,int value){return row.score>value;});
        rows.insert(at,{score.variant,score.duration,score.score,name});
    }
    order(rows);if(rows.size()>10)rows.resize(10);
    if(rows==rows_)return Error::success();
    if(!file_.empty()) {
        const auto temp=file_.parent_path() / "HighScores.txt.tmp",backup=file_.parent_path() / "HighScores.txt.bak";
        for(const auto& path:{file_,temp,backup})if(auto error=regular(path))return error;
        std::error_code ec;std::filesystem::create_directories(file_.parent_path(),ec);if(ec)return failure();
        {
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<"RELUVA BOGGLE HIGHSCORES 2\n";
            for(const auto& row:rows)out<<(row.variant==ClassicVariant::Original?"original":"master")<<'\t'<<row.duration/1000<<'\t'<<row.score<<'\t'<<row.name<<'\n';
            out.flush();if(!out)return failure();out.close();if(!out)return failure();
        }
        const bool present=std::filesystem::exists(file_,ec);if(ec)return failure();
        if(present){std::filesystem::remove(backup,ec);if(ec)return failure();std::filesystem::rename(file_,backup,ec);if(ec)return failure();}
        std::filesystem::rename(temp,file_,ec);
        if(ec){if(present){std::error_code restore;std::filesystem::rename(backup,file_,restore);}return failure();}
        std::filesystem::remove(backup,ec);
    }
    rows_=std::move(rows);return Error::success();
}
} // namespace reluva::boggle
