// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/cube_geometry.h"
#include "engines/boggle/classic/cube_renderer.h"
#include "common/fs.h"
#include "common/md5.h"
#include "tools/tool.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace {
int check(int argc,char** argv) {
    using namespace reluva;using namespace reluva::boggle;
    if(argc!=3){std::fprintf(stderr,"usage: reluva-tool boggle cube-check <game folder>\n");return 2;}
    Resources resources;if(auto error=resources.open(argv[2])){std::fprintf(stderr,"%s\n",error.message.c_str());return 1;}
    Md5 digest;size_t meshes=0,vertices=0,polygons=0,triangles=0;
    auto number=[&](double value) {
        const uint64_t n=uint64_t(int64_t(std::llround(value*1000000)));
        uint8_t bytes[8];for(unsigned i=0;i<8;++i)bytes[i]=uint8_t(n>>(i*8));digest.update(bytes,8);
    };
    for(const auto* path:classicCubeMeshPaths)if(fs::findFile(argv[2],path)) {
        CubeGeometry geometry;if(auto error=readClassicCubeGeometry(resources,path,geometry)) {
            std::fprintf(stderr,"%s: %s\n",path,error.message.c_str());return 1;
        }
        CubeRenderer renderer;if(auto error=renderer.prepare(geometry)) {
            std::fprintf(stderr,"%s: %s\n",path,error.message.c_str());return 1;
        }
        triangles+=renderer.triangleCount();
        const auto& mesh=geometry.mesh;++meshes;vertices+=mesh.positions.size();polygons+=mesh.faces.size();
        digest.update(path,std::char_traits<char>::length(path));const uint8_t separator=0;digest.update(&separator,1);
        number(geometry.appliedScale);number(double(mesh.positions.size()));number(double(mesh.normals.size()));
        number(double(mesh.texcoords.size()));number(double(mesh.faces.size()));number(double(mesh.materials.size()));
        for(const auto& p:mesh.positions)for(auto v:p)number(v);
        for(const auto& p:mesh.normals)for(auto v:p)number(v);
        for(const auto& p:mesh.texcoords)for(auto v:p)number(v);
        for(size_t f=0;f<mesh.faces.size();++f) {
            number(double(mesh.faceMaterials[f]));number(double(mesh.faces[f].size()));
            for(auto i:mesh.faces[f])number(double(i));
            for(auto i:mesh.normalFaces[f])number(double(i));
        }
        for(const auto& m:mesh.materials){for(auto v:m.diffuse)number(v);number(m.power);for(auto v:m.specular)number(v);for(auto v:m.emissive)number(v);}
        for(auto group:geometry.letters)number(double(group));
    }
    std::printf("Classic cube meshes %zu, vertices %zu, polygons %zu, triangles %zu\n",meshes,vertices,polygons,triangles);
    std::printf("decoded cubes: %s\n",digest.hexDigest().c_str());return 0;
}
const reluva::tool::Commands commands("boggle",{{"cube-check","<game folder>","validate complete cube geometry and face groups; report metadata only",check}});
}
