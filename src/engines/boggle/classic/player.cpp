// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/player.h"
#include <fstream>
#include <algorithm>
namespace reluva::boggle {
namespace {
Error regular(const std::filesystem::path& path) {
    std::error_code ec;const auto status=std::filesystem::symlink_status(path,ec);
    if(status.type()==std::filesystem::file_type::not_found)return Error::success();
    if(ec || !std::filesystem::is_regular_file(status))return Error::fail("Player profile path must be a regular file");
    return Error::success();
}
bool inside(const std::filesystem::path& parent,std::filesystem::path child) {
    while(!child.empty()) {
        std::error_code ec;if(child==parent || std::filesystem::equivalent(parent,child,ec))return true;
        const auto next=child.parent_path();if(next==child)break;child=next;
    }
    return false;
}
Error failed(){return Error::fail("Cannot save player name. Check the save directory, retry, or cancel the name change.");}
}
bool playerName(std::string_view input,std::string& normalized) {
    normalized.clear();
    if(input.size()>18)return false;
    for(unsigned char c:input)if(c<32 || c>126)return false;
    const auto first=input.find_first_not_of(' '),last=input.find_last_not_of(' ');
    if(first==std::string_view::npos)return false;
    normalized=std::string(input.substr(first,last-first+1));return true;
}
Error PlayerProfile::load(const std::filesystem::path& savePath,const std::filesystem::path& gamePath) {
    name_="PLAYER 1";history_={name_};file_.clear();loaded_=false;
    if(savePath.empty()){loaded_=true;return Error::success();}
    std::error_code ec;const auto save=std::filesystem::weakly_canonical(savePath,ec);
    if(ec)return Error::fail("Cannot resolve player save directory");
    if(!gamePath.empty()) {
        const auto game=std::filesystem::weakly_canonical(gamePath,ec);
        if(ec || inside(game,save))return Error::fail("Player save directory must be outside the original game folder");
    }
    const auto file=save / "Player.txt",backup=save / "Player.txt.bak";
    if(auto error=regular(file))return error;
    if(auto error=regular(backup))return error;
    auto source=file;bool present=std::filesystem::exists(file,ec);
    if(ec)return Error::fail("Cannot inspect player profile");
    if(!present){source=backup;present=std::filesystem::exists(backup,ec);}
    if(ec)return Error::fail("Cannot inspect player profile backup");
    std::vector<std::string> names{"PLAYER 1"};
    if(present) {
        std::ifstream in(source,std::ios::binary|std::ios::ate);
        if(!in || in.tellg()<0 || in.tellg()>1024)return Error::fail("Cannot read bounded player profile");
        in.seekg(0);std::string header,line;
        auto nextLine=[&](std::string& text){if(!std::getline(in,text))return false;if(!text.empty() && text.back()=='\r')text.pop_back();return true;};
        if(!nextLine(header) || (header!="RELUVA BOGGLE PLAYER 1" && header!="RELUVA BOGGLE PLAYER 2"))return Error::fail("Invalid player profile");
        names.clear();
        while(nextLine(line)) {
            std::string name;
            if(!playerName(line,name) || names.size()==maximumNames ||
               (header=="RELUVA BOGGLE PLAYER 1" && !names.empty()) ||
               (header=="RELUVA BOGGLE PLAYER 2" && name!=line) ||
               std::find(names.begin(),names.end(),name)!=names.end())return Error::fail("Invalid player profile");
            names.push_back(std::move(name));
        }
        if(in.bad() || names.empty())return Error::fail("Invalid player profile");
    }
    file_=file;history_=std::move(names);name_=history_.front();loaded_=true;return Error::success();
}

Error PlayerProfile::rename(std::string_view input) {
    if(!loaded_)return Error::fail("Player profile has not loaded");
    std::string next;
    if(!playerName(input,next))return Error::fail("Use 1 to 18 printable English characters for the player name.");
    if(next==name_)return Error::success();
    auto names=history_;
    names.erase(std::remove(names.begin(),names.end(),next),names.end());
    names.insert(names.begin(),next);if(names.size()>maximumNames)names.resize(maximumNames);
    if(file_.empty()){name_=std::move(next);history_=std::move(names);return Error::success();}
    const auto temp=file_.parent_path() / "Player.txt.tmp",backup=file_.parent_path() / "Player.txt.bak";
    for(const auto& path:{file_,temp,backup})if(auto error=regular(path))return error;
    std::error_code ec;std::filesystem::create_directories(file_.parent_path(),ec);if(ec)return failed();
    {
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        out<<"RELUVA BOGGLE PLAYER 2\n";
        for(const auto& name:names)out<<name<<'\n';
        out.flush();if(!out)return failed();
        out.close();if(!out)return failed();
    }
    const bool present=std::filesystem::exists(file_,ec);if(ec)return failed();
    if(present) {
        std::filesystem::remove(backup,ec);if(ec)return failed();
        std::filesystem::rename(file_,backup,ec);if(ec)return failed();
    }
    std::filesystem::rename(temp,file_,ec);
    if(ec) {
        if(present){std::error_code restore;std::filesystem::rename(backup,file_,restore);}
        return failed();
    }
    name_=std::move(next);history_=std::move(names);std::filesystem::remove(backup,ec);return Error::success();
}
} // namespace reluva::boggle
