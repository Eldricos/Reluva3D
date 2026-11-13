// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/cube_renderer.h"
#include "engines/boggle/classic/orientation.h"
#include "common/font.h"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace reluva::boggle {
namespace {
using Point=std::array<double,2>;
double cross(Point a,Point b,Point c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);}
bool triangulate(const Mesh& mesh,const Mesh::Face& face,std::vector<std::array<size_t,3>>& out) {
    std::array<double,3> normal{};
    for(size_t i=0;i<face.size();++i){const auto& a=mesh.positions[face[i]];const auto& b=mesh.positions[face[(i+1)%face.size()]];
        for(size_t d=0;d<3;++d)normal[d]+=(a[(d+1)%3]-b[(d+1)%3])*(a[(d+2)%3]+b[(d+2)%3]);}
    if(face.size()==4) {
        double length=0;for(double v:normal)length+=v*v;length=std::sqrt(length);
        const auto& origin=mesh.positions[face[0]];double deviation=0;
        if(length>1e-12)for(auto i:face) {
            double distance=0;for(size_t d=0;d<3;++d)distance+=(mesh.positions[i][d]-origin[d])*normal[d];
            deviation=std::max(deviation,std::abs(distance)/length);
        }
        // Some supplied shoulder quads are genuinely folded in 3D. Preserve
        // their original-order diagonal rather than projecting them flat.
        if(deviation>1e-5){out={{{0,1,2}},{{0,2,3}}};return true;}
    }
    size_t drop=0;for(size_t d=1;d<3;++d)if(std::abs(normal[d])>std::abs(normal[drop]))drop=d;
    std::vector<Point> p;for(auto i:face)p.push_back({mesh.positions[i][(drop+1)%3],mesh.positions[i][(drop+2)%3]});
    constexpr double epsilon=1e-10;
    std::vector<size_t> remaining;
    for(size_t i=0;i<p.size();++i) {
        const auto a=p[i],b=p[(i+1)%p.size()];
        if(std::abs(a[0]-b[0])+std::abs(a[1]-b[1])>epsilon)remaining.push_back(i);
    }
    if(remaining.size()<3)return false;
    double area=0;for(size_t i=0;i<remaining.size();++i){const auto a=p[remaining[i]],b=p[remaining[(i+1)%remaining.size()]];area+=a[0]*b[1]-b[0]*a[1];}
    if(std::abs(area)<epsilon)return false;
    const double sign=area>0?1:-1;
    // Reject crossed boundaries rather than filling an ambiguous polygon.
    for(size_t i=0;i<remaining.size();++i)for(size_t j=i+2;j<remaining.size();++j) {
        if(i==0 && j+1==remaining.size())continue;
        const auto a=p[remaining[i]],b=p[remaining[(i+1)%remaining.size()]],c=p[remaining[j]],d=p[remaining[(j+1)%remaining.size()]];
        if(cross(a,b,c)*cross(a,b,d)<-epsilon && cross(c,d,a)*cross(c,d,b)<-epsilon)return false;
    }
    while(remaining.size()>3) {
        bool removed=false;
        for(size_t i=0;i<remaining.size();++i) {
            const size_t a=remaining[(i+remaining.size()-1)%remaining.size()],b=remaining[i],c=remaining[(i+1)%remaining.size()];
            const double turn=cross(p[a],p[b],p[c])*sign;
            if(std::abs(turn)<=epsilon){remaining.erase(remaining.begin()+i);removed=true;break;}
            if(turn<0)continue;
            bool occupied=false;
            for(auto n:remaining)if(n!=a && n!=b && n!=c && cross(p[a],p[b],p[n])*sign>=-epsilon &&
                cross(p[b],p[c],p[n])*sign>=-epsilon && cross(p[c],p[a],p[n])*sign>=-epsilon){occupied=true;break;}
            if(occupied)continue;
            out.push_back({a,b,c});remaining.erase(remaining.begin()+i);removed=true;break;
        }
        if(!removed || remaining.size()<3)return false;
    }
    if(std::abs(cross(p[remaining[0]],p[remaining[1]],p[remaining[2]]))>epsilon)out.push_back({remaining[0],remaining[1],remaining[2]});
    return !out.empty();
}
ProjectedVertex project(const CameraProjection& camera,const std::array<int,3>& p,size_t index,size_t side) {
    return camera.project(p[0]+(int(index%side)*2+1-int(side))*55000,
        p[1]+(int(side)-int(index/side)*2-1)*55000,p[2],100000,5,4);
}
int64_t edge(ProjectedVertex a,ProjectedVertex b,int x,int y){return int64_t(b.x-a.x)*(y-a.y)-int64_t(b.y-a.y)*(x-a.x);}
bool valid(ProjectedVertex p){return p.reciprocalDepth>0 && p.reciprocalDepth<=(int64_t(1)<<30) && std::abs(int64_t(p.x))<=4096 && std::abs(int64_t(p.y))<=4096;}
// Divide barycentric depth terms before interpolating attributes. This bounds
// every intermediate to portable int64_t even near the front clip plane.
std::array<int64_t,3> weights(const std::array<ProjectedVertex,3>& p,int x,int y) {
    const int64_t area=edge(p[0],p[1],p[2].x,p[2].y);if(!area)return {};
    const int64_t a=edge(p[1],p[2],x,y),b=edge(p[2],p[0],x,y),c=edge(p[0],p[1],x,y);
    if(area>0?(a<0 || b<0 || c<0):(a>0 || b>0 || c>0))return {};
    return {a*p[0].reciprocalDepth/area,b*p[1].reciprocalDepth/area,c*p[2].reciprocalDepth/area};
}
}
Error CubeRenderer::prepare(const CubeGeometry& geometry) {
    *this={};CubeGeometry checked;
    // Revalidate public input before dereferencing any resource-owned indices.
    if(auto error=checked.prepare(geometry.mesh,1))return error;
    CubeRenderer prepared;const auto& mesh=checked.mesh;const size_t top=checked.letters[2];
    size_t topVertices=0;std::array<double,3> center{};
    for(size_t f=0;f<mesh.faces.size();++f) {
        const auto& face=mesh.faces[f];std::vector<std::array<size_t,3>> tessellation;
        if(!triangulate(mesh,face,tessellation) || prepared.triangles_.size()+tessellation.size()>16384)
            return Error::fail("Unsupported Classic cube polygon topology at face "+std::to_string(f));
        const bool letter=mesh.faceMaterials[f]==top;const auto& material=mesh.materials[mesh.faceMaterials[f]];
        if(letter)for(auto i:face){++topVertices;for(size_t a=0;a<3;++a)center[a]+=mesh.positions[i][a];}
        for(const auto& t:tessellation) {
            Triangle triangle;triangle.letter=letter;
            for(size_t k=0;k<3;++k) {
                const size_t corner=t[k],i=face[corner];const auto& p=mesh.positions[i];const auto& n=mesh.normals[mesh.normalFaces[f][corner]];
                auto& out=triangle.corners[k];
                // Canonical settled orientation: source +Y is the upward face
                // toward the camera (-Z), source +Z is the back of the tray.
                out.position={int(std::lround(p[0]*100000)),int(std::lround(p[2]*100000)),int(std::lround(-p[1]*100000))};
                out.u=int(std::lround(mesh.texcoords[i][0]*65536));out.v=65536-int(std::lround(mesh.texcoords[i][1]*65536));
                const double intensity=.7+std::max(0.,n[0]*.5+n[2]*(-.7071067811865475)+(-n[1])*(-.5));
                out.light={uint8_t(std::lround(std::clamp(material.diffuse[0]*intensity+material.emissive[0],0.,1.)*255)),
                           uint8_t(std::lround(std::clamp(material.diffuse[1]*intensity+material.emissive[1],0.,1.)*255)),
                           uint8_t(std::lround(std::clamp(material.diffuse[2]*intensity+material.emissive[2],0.,1.)*255))};
            }
            prepared.triangles_.push_back(triangle);
        }
    }
    if(!topVertices)return Error::fail("Classic cube has no upward letter surface");
    prepared.topCenter_={int(std::lround(center[0]/topVertices*100000)),int(std::lround(center[2]/topVertices*100000)),int(std::lround(-center[1]/topVertices*100000))};
    *this=std::move(prepared);return Error::success();
}
ClassicPoint CubeRenderer::cellCenter(const CameraProjection& camera,size_t cell,size_t side) const {
    if(!active() || (side!=4 && side!=5) || cell>=side*side)return {-1,-1};
    const auto p=project(camera,topCenter_,cell,side);return valid(p)?ClassicPoint{p.x,p.y}:ClassicPoint{-1,-1};
}
std::optional<size_t> CubeRenderer::cellAt(const CameraProjection& camera,int x,int y,size_t side) const {
    if(!active() || (side!=4 && side!=5) || x<0 || y<0 || x>=800 || y>=600)return {};
    int64_t nearest=0;std::optional<size_t> result;
    for(size_t cell=0;cell<side*side;++cell)for(const auto& triangle:triangles_) {
        std::array<ProjectedVertex,3> p;bool clipped=false;
        for(size_t k=0;k<3;++k){p[k]=project(camera,triangle.corners[k].position,cell,side);clipped|=!valid(p[k]);}
        if(clipped)continue;
        const auto w=weights(p,x,y);const int64_t q=w[0]+w[1]+w[2];
        if(q>nearest){nearest=q;result=cell;}
    }
    return result;
}
void CubeRenderer::draw(Surface& screen,const Board& board,const CameraProjection& camera,const image::Image& letters,
                        const image::Image& qu,const std::vector<size_t>& selected,uint32_t seed,bool autoOrient) const {
    const size_t side=board.side();
    if(!active() || (side!=4 && side!=5) || screen.format!=PixelFormat::RGBA32 || screen.w<1 || screen.h<1 || screen.w>4096 || screen.h>4096 ||
       letters.w!=512 || letters.h!=512 || qu.w!=512 || qu.h!=512 ||
       letters.rgba.size()!=512*512*4 || qu.rgba.size()!=512*512*4 || screen.pitch<screen.w*4 ||
       screen.pixels.size()<size_t(screen.pitch)*screen.h)return;
    std::vector<int64_t> depth(size_t(screen.w)*screen.h);
    for(size_t cell=0;cell<side*side;++cell) {
        const auto& word=board.faces()[cell];const bool textured=word=="QU" || (word.size()==1 && word[0]>='A' && word[0]<='Z');
        const bool marked=std::find(selected.begin(),selected.end(),cell)!=selected.end();
        const auto& texture=word=="QU"?qu:letters;const int index=textured?word[0]-'A':0;
        const int left=index%5*512/6,top=index/5*512/6,right=(index%5+1)*512/6-1,bottom=(index/5+1)*512/6-1;
        for(const auto& triangle:triangles_) {
            std::array<ProjectedVertex,3> p;bool clipped=false;
            for(size_t k=0;k<3;++k) {
                const auto& c=triangle.corners[k];p[k]=project(camera,c.position,cell,side);clipped|=!valid(p[k]);
                const auto uv=cubeTexturePoint(c.u,c.v,65536,cubeQuarterTurns(seed,cell,autoOrient));
                p[k].u=left+uv[0]*(right-left)/65536;p[k].v=top+uv[1]*(bottom-top)/65536;
            }
            if(clipped)continue;
            const int minX=std::max(0,std::min({p[0].x,p[1].x,p[2].x})),maxX=std::min(screen.w-1,std::max({p[0].x,p[1].x,p[2].x}));
            const int minY=std::max(0,std::min({p[0].y,p[1].y,p[2].y})),maxY=std::min(screen.h-1,std::max({p[0].y,p[1].y,p[2].y}));
            for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x) {
                const auto w=weights(p,x,y);const int64_t q=w[0]+w[1]+w[2];const size_t pixel=size_t(y)*screen.w+x;
                if(q<=depth[pixel])continue;
                auto mix=[&](int a,int b,int c){return int((w[0]*a+w[1]*b+w[2]*c)/q);};
                const auto& a=triangle.corners[0].light;const auto& b=triangle.corners[1].light;const auto& c=triangle.corners[2].light;
                Color light{uint8_t(mix(a.r,b.r,c.r)),uint8_t(mix(a.g,b.g,c.g)),uint8_t(mix(a.b,b.b,c.b))};
                const uint8_t* sample=nullptr;if(textured && triangle.letter)sample=texture.pixel(mix(p[0].u,p[1].u,p[2].u),mix(p[0].v,p[1].v,p[2].v));
                auto* out=screen.row(y)+x*4;out[0]=sample?uint8_t(unsigned(sample[0])*light.r/255):light.r;
                out[1]=sample?uint8_t(unsigned(sample[1])*light.g/255):light.g;out[2]=sample?uint8_t(unsigned(sample[2])*light.b/255):light.b;
                if(marked){out[0]=uint8_t(unsigned(out[0])*3/4);out[1]=uint8_t((unsigned(out[1])*3+220)/4);out[2]=uint8_t((unsigned(out[2])*3+255)/4);}
                out[3]=255;depth[pixel]=q;
            }
        }
        if(!textured){const auto p=cellCenter(camera,cell,side);font::drawText(screen,p.x-int(word.size())*3,p.y-5,word,{0,0,190});}
    }
}
} // namespace reluva::boggle
