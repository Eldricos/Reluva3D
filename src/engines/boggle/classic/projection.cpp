// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/projection.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
namespace reluva::boggle {
CameraProjection::CameraProjection(const CameraParameters& p) {
    if(!std::isfinite(p.tilt) || !std::isfinite(p.pan) || !std::isfinite(p.fov) ||
       std::abs(p.tilt)>180 || std::abs(p.pan)>180 || p.fov<10 || p.fov>120 ||
       p.width<16 || p.width>4096 || p.height<16 || p.height>4096)return;
    for(auto coordinate:p.position)if(!std::isfinite(coordinate) || std::abs(coordinate)>64)return;
    constexpr double radians=3.14159265358979323846/180.0;
    const double ct=std::cos(p.tilt*radians),st=std::sin(p.tilt*radians),cp=std::cos(p.pan*radians),sp=std::sin(p.pan*radians);
    const std::array<double,9> inverse={cp,sp*st,-sp*ct,0,ct,st,sp,-cp*st,cp*ct};
    constexpr int64_t basisScale=int64_t(1)<<30;
    for(size_t i=0;i<9;++i)basis_[i]=std::llround(inverse[i]*basisScale);
    for(size_t i=0;i<3;++i)position_[i]=std::llround(p.position[i]*basisScale);
    focal_=std::llround((p.width*.5/std::tan(p.fov*radians*.5))*65536);
    width_=p.width;height_=p.height;
}
ProjectedVertex CameraProjection::project(int x,int y,int z,int units,int numerator,int denominator) const {
    if(!focal_ || units<1 || units>1000000 || numerator<1 || numerator>8 || denominator<1 || denominator>8 ||
       std::abs(int64_t(x))>int64_t(units)*64 || std::abs(int64_t(y))>int64_t(units)*64 ||
       std::abs(int64_t(z))>int64_t(units)*64)return {-1,-1,0,0,0};
    const std::array<int,3> world={x,y,z};std::array<int64_t,3> view{};
    for(size_t row=0;row<3;++row) {
        int64_t sum=-position_[row]*units;
        for(size_t column=0;column<3;++column)sum+=basis_[row*3+column]*world[column];
        view[row]=sum/(int64_t(units)*(int64_t(1)<<14)); // world units in Q16
    }
    if(view[2]<65536)return {-1,-1,0,0,0}; // front clip plane at one unit
    const auto px=(int64_t(width_)*32768+focal_*view[0]/view[2])*numerator/denominator;
    const auto py=(int64_t(height_)*32768-focal_*view[1]/view[2])*numerator/denominator;
    return {int(px/65536),int(py/65536),0,0,(int64_t(1)<<46)/view[2]};
}
PlaneProjection::PlaneProjection(const std::array<ClassicPoint,4>& p) {
    // Bound intermediate products even for invalid caller-supplied corners.
    for(const auto& point:p)if(std::abs(int64_t(point.x))>2048 || std::abs(int64_t(point.y))>2048)return;
    int64_t winding=0;
    for(size_t i=0;i<4;++i) {
        const auto a=p[i],b=p[(i+1)%4],c=p[(i+2)%4];
        const int64_t cross=int64_t(b.x-a.x)*(c.y-b.y)-int64_t(b.y-a.y)*(c.x-b.x);
        if(!cross || (winding && (cross>0)!=(winding>0)))return;
        winding=cross;
    }
    const int64_t dx1=p[1].x-p[2].x,dx2=p[3].x-p[2].x,dx3=p[0].x-p[1].x+p[2].x-p[3].x;
    const int64_t dy1=p[1].y-p[2].y,dy2=p[3].y-p[2].y,dy3=p[0].y-p[1].y+p[2].y-p[3].y;
    const int64_t det=dx1*dy2-dx2*dy1;
    if(!det)return;
    const int64_t g=dx3*dy2-dx2*dy3,h=dx1*dy3-dx3*dy1;
    coefficients_={int64_t(p[1].x-p[0].x)*det+g*p[1].x,
                   int64_t(p[3].x-p[0].x)*det+h*p[3].x,int64_t(p[0].x)*det,
                   int64_t(p[1].y-p[0].y)*det+g*p[1].y,
                   int64_t(p[3].y-p[0].y)*det+h*p[3].y,int64_t(p[0].y)*det,g,h};
    determinant_=det;
}
ProjectedVertex PlaneProjection::project(int u,int v,int units,int numerator,int denominator) const {
    if(!determinant_ || units<1 || units>1024 || u<0 || v<0 || u>units || v>units ||
       numerator<1 || numerator>8 || denominator<1 || denominator>8)return {-1,-1,0,0,0};
    const auto& c=coefficients_;
    const int64_t depth=c[6]*u+c[7]*v+determinant_*units;
    if(!depth)return {-1,-1,0,0,0};
    const int x=int((c[0]*u+c[1]*v+c[2]*units)*numerator/(depth*denominator));
    const int y=int((c[3]*u+c[4]*v+c[5]*units)*numerator/(depth*denominator));
    const int64_t reciprocal=std::min<int64_t>(int64_t(1)<<30,std::abs(determinant_)*(int64_t(1)<<30)/std::abs(depth));
    return {x,y,0,0,reciprocal};
}
} // namespace reluva::boggle
