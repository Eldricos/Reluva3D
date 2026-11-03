// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/registry.h"
#include "common/fs.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <cstdio>
namespace {
int check(int argc,char** argv) {
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle import-check <source folder>\n");return 2;}
    const auto* engine=reluva::findEngine("boggle");
    const auto plans=engine->planImport(argv[2]);
    if(plans.size()!=1){std::fprintf(stderr,"No complete supported Classic import found\n");return 1;}
    reluva::Md5 digest;
    for(const auto& file:plans.front().files) {
        const auto hash=reluva::fs::md5File(file.from,0);
        if(!hash){std::fprintf(stderr,"Cannot read import file: %s\n",file.to.c_str());return 1;}
        digest.update(file.to.data(),file.to.size()); const char separator=0; digest.update(&separator,1);
        digest.update(hash->data(),hash->size());
    }
    std::printf("Classic import files %zu, bytes %llu\n",plans.front().files.size(),
                static_cast<unsigned long long>(plans.front().totalBytes()));
    std::printf("decoded import: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{
    {"import-check","<source folder>","validate the Classic copy plan and checksum its selected files without copying",check},
});
}
