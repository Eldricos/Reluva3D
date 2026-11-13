// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/cube_geometry.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace reluva::boggle {
double classicCubeMeshScale(std::string_view path) {
    if(path==classicCubeMeshPaths[0])return double(0.13888895511627197f);
    if(path==classicCubeMeshPaths[1])return 1;
    return 0;
}
Error readClassicCubeGeometry(const Resources& resources,std::string_view path,CubeGeometry& out) {
    out={};const double scale=classicCubeMeshScale(path);
    if(!scale)return Error::fail("Unsupported Classic cube mesh path");
    Mesh mesh;if(auto error=resources.mesh(path,mesh))return error;
    return out.prepare(mesh,scale);
}
Error CubeGeometry::prepare(const Mesh& source,double scale) {
    *this={};
    auto fail=[] {return Error::fail("Unsupported Classic cube mesh geometry");};
    const size_t count=source.positions.size(),groups=source.materials.size();
    if(!std::isfinite(scale) || scale<=0 || scale>1 || !count || count>4096 || source.faces.empty() || source.faces.size()>1024 ||
       source.normals.empty() || source.normals.size()>4096 || source.normalFaces.size()!=source.faces.size() ||
       source.texcoords.size()!=count || source.faceMaterials.size()!=source.faces.size() || groups<6 || groups>32)return fail();
    for(const auto& p:source.positions)for(double v:p)if(!std::isfinite(v) || std::abs(v)>16 || std::abs(v*scale)>1)return fail();
    for(const auto& uv:source.texcoords)for(double v:uv)if(!std::isfinite(v) || v<0 || v>1)return fail();
    for(const auto& normal:source.normals) {
        double length=0;for(double v:normal){if(!std::isfinite(v) || std::abs(v)>1.001)return fail();length+=v*v;}
        if(length<.9 || length>1.1)return fail();
    }
    for(const auto& m:source.materials) {
        for(double v:m.diffuse)if(!std::isfinite(v) || v<0 || v>1)return fail();
        if(m.diffuse[3]!=1 || !std::isfinite(m.power) || m.power<0 || m.power>10000)return fail();
        for(const auto& color:{m.specular,m.emissive})for(double v:color)if(!std::isfinite(v) || v<0 || v>1)return fail();
    }
    std::vector<std::array<double,3>> normals(groups);std::vector<double> areas(groups);
    for(size_t f=0;f<source.faces.size();++f) {
        const auto& face=source.faces[f];const auto& normalFace=source.normalFaces[f];const auto group=source.faceMaterials[f];
        if(face.size()<3 || face.size()>256 || normalFace.size()!=face.size() || group>=groups)return fail();
        for(auto i:face)if(i>=count)return fail();
        for(auto i:normalFace)if(i>=source.normals.size())return fail();
        // Newell's area vector handles the retained polygons without assuming
        // that their first three vertices span the face.
        std::array<double,3> normal{};
        for(size_t k=0;k<face.size();++k) {
            const auto& a=source.positions[face[k]];const auto& b=source.positions[face[(k+1)%face.size()]];
            for(size_t axis=0;axis<3;++axis) {
                const size_t u=(axis+1)%3,v=(axis+2)%3;
                normal[axis]+=(a[u]-b[u])*(a[v]+b[v]);
            }
        }
        double length=0;for(double v:normal)length+=v*v;length=std::sqrt(length);
        if(length<1e-12)return fail();
        for(size_t axis=0;axis<3;++axis)normals[group][axis]+=normal[axis];
        areas[group]+=length;
    }
    std::array<size_t,6> letterGroups;letterGroups.fill(std::numeric_limits<size_t>::max());
    for(size_t group=0;group<groups;++group)if(areas[group]>0) {
        for(size_t axis=0;axis<3;++axis)if(std::abs(normals[group][axis])/areas[group]>.99) {
            const size_t direction=axis*2+(normals[group][axis]<0?1:0);
            if(letterGroups[direction]!=std::numeric_limits<size_t>::max())return fail();
            letterGroups[direction]=group;
        }
    }
    for(auto group:letterGroups)if(group==std::numeric_limits<size_t>::max())return fail();
    CubeGeometry prepared;prepared.mesh=source;prepared.letters=letterGroups;prepared.appliedScale=scale;
    for(auto& p:prepared.mesh.positions)for(auto& v:p)v*=scale;
    *this=std::move(prepared);return Error::success();
}
} // namespace reluva::boggle
