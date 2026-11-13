// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/classic/cube_renderer.h"
#include "engines/boggle/classic/layout.h"
#include "engines/boggle/classic/game.h"
#include "engines/boggle/classic/highscores.h"
#include "tests/engines/boggle/fixtures.h"
#include "tests/engines/boggle/system.h"
#include "tests/support/test.h"
#include <algorithm>
using namespace reluva;
using namespace reluva::boggle;
namespace {
CubeGeometry geometry() {
    Mesh mesh;CHECK(!readMesh(fixtures::cubeMesh(),mesh));CubeGeometry g;CHECK(!g.prepare(mesh,1));return g;
}
image::Image atlas(uint8_t r,uint8_t g,uint8_t b) {
    image::Image image;image.w=image.h=512;image.rgba.resize(512*512*4);
    for(size_t i=0;i<image.rgba.size();i+=4){image.rgba[i]=r;image.rgba[i+1]=g;image.rgba[i+2]=b;image.rgba[i+3]=255;}
    return image;
}
void topology() {
    auto g=geometry();CubeRenderer renderer;CHECK(!renderer.prepare(g) && renderer.active() && renderer.triangleCount()==12);
    auto polygon=[&](const std::vector<std::array<double,3>>& points) {
        auto next=g;const size_t first=next.mesh.positions.size();next.mesh.faces[2].clear();next.mesh.normalFaces[2].clear();
        for(size_t i=0;i<points.size();++i){next.mesh.positions.push_back(points[i]);next.mesh.texcoords.push_back({.5,.5});next.mesh.faces[2].push_back(uint32_t(first+i));next.mesh.normalFaces[2].push_back(2);}
        return next;
    };
    auto concave=polygon({{-.5,.5,-.5},{-.5,.5,.5},{.5,.5,.5},{0,.5,0},{.5,.5,-.5}});
    CHECK(!renderer.prepare(concave) && renderer.triangleCount()==13);
    auto collinear=polygon({{-.5,.5,-.5},{-.5,.5,0},{-.5,.5,.5},{.5,.5,.5},{.5,.5,-.5}});
    CHECK(!renderer.prepare(collinear) && renderer.triangleCount()>=12 && renderer.triangleCount()<=13);
    auto folded=g;folded.mesh.positions[folded.mesh.faces[0][0]][0]+=.05;
    CHECK(!renderer.prepare(folded) && renderer.triangleCount()==12);
    auto crossed=polygon({{-.5,.5,-.5},{-.5,.5,.5},{.5,.5,-.5},{.5,.5,.5},{0,.5,.25}});
    CHECK(bool(renderer.prepare(crossed)) && !renderer.active());
    auto invalid=g;invalid.mesh.faces[0][0]=999;CHECK(bool(renderer.prepare(invalid)) && !renderer.active());
    CHECK(!renderer.prepare(g));CHECK(bool(renderer.prepare({})) && !renderer.active());
}
void raster() {
    auto g=geometry();CubeRenderer renderer;CHECK(!renderer.prepare(g));
    auto letters=atlas(200,100,50),qu=atlas(40,200,80);
    auto reversed=g;std::reverse(reversed.mesh.faces.begin(),reversed.mesh.faces.end());
    std::reverse(reversed.mesh.normalFaces.begin(),reversed.mesh.normalFaces.end());std::reverse(reversed.mesh.faceMaterials.begin(),reversed.mesh.faceMaterials.end());
    CubeRenderer backwards;CHECK(!backwards.prepare(reversed));
    for(bool master:{false,true}) {
        const size_t side=master?5:4;CameraProjection camera(classicLidView(master,43).camera);
        Board board;auto faces=std::vector<std::string>(side*side,"A");faces.back()="QU";CHECK(!board.set(side,faces));
        Surface screen,other;screen.create(800,600,PixelFormat::RGBA32);other=screen;
        renderer.draw(screen,board,camera,letters,qu,{},1,true);backwards.draw(other,board,camera,letters,qu,{},1,true);
        for(size_t i=0;i<side*side;++i) {
            const auto p=renderer.cellCenter(camera,i,side);CHECK(renderer.cellAt(camera,p.x,p.y,side)==i);
            const auto* a=screen.row(p.y)+p.x*4;const auto* b=other.row(p.y)+p.x*4;
            CHECK(std::equal(a,a+4,b)); // hidden back/bottom faces never overwrite the upward face
            CHECK(a[0]==(i+1==side*side?38:192) && a[1]==(i+1==side*side?167:83) && a[2]==(i+1==side*side?57:36) && a[3]==255);
        }
        CHECK(!renderer.cellAt(camera,0,0,side) && !renderer.cellAt(camera,799,599,side));
        CHECK(!renderer.cellAt(camera,-1,100,side) && !renderer.cellAt(camera,800,100,side));
        CHECK(!renderer.cellAt(camera,100,100,0));CHECK(renderer.cellCenter(camera,99,side).x==-1);
        const auto before=screen.pixels;renderer.draw(screen,board,camera,letters,qu,{0},1,true);CHECK(screen.pixels!=before);
        const auto selected=renderer.cellCenter(camera,0,side);CHECK(renderer.cellAt(camera,selected.x,selected.y,side)==0);
        CameraParameters near;near.position={0,0,0};Surface hidden;hidden.create(800,600,PixelFormat::RGBA32);const auto empty=hidden.pixels;
        renderer.draw(hidden,board,CameraProjection(near),letters,qu,{},1,true);CHECK(hidden.pixels==empty);
        auto bad=letters;bad.rgba.clear();renderer.draw(hidden,board,camera,bad,qu,{},1,true);CHECK(hidden.pixels==empty);
        Surface small;small.create(32,32,PixelFormat::RGBA32);renderer.draw(small,board,camera,letters,qu,{},1,true);
        CameraParameters extreme;extreme.position={0,0,-1.5};extreme.fov=10;
        renderer.draw(small,board,CameraProjection(extreme),letters,qu,{},1,false); // bounded near-plane intermediates under UBSan
    }
    Board board;CHECK(!board.set(4,std::vector<std::string>(16,"A")));
    for(int y=0;y<512;++y)for(int x=0;x<512;++x){auto* p=letters.rgba.data()+(y*512+x)*4;p[0]=uint8_t(x/2);p[1]=uint8_t(y/2);p[2]=0;}
    Surface a,b;a.create(800,600,PixelFormat::RGBA32);b=a;CameraProjection camera(classicLidView(false,43).camera);
    renderer.draw(a,board,camera,letters,qu,{},0x1997,true);renderer.draw(b,board,camera,letters,qu,{},0x1997,false);CHECK(a.pixels!=b.pixels);
    a=b;renderer.draw(b,board,camera,letters,qu,{},0x1997,false);CHECK(a.pixels==b.pixels);
}
void layout(const std::filesystem::path& tmp) {
    for(bool master:{false,true})for(bool rounded:{false,true}) {
        const auto root=tmp/(std::to_string(master)+std::to_string(rounded));fixtures::artworkInstall(root);
        ClassicOptions options;options.variant=master?ClassicVariant::Master:ClassicVariant::Original;
        Resources resources;CHECK(!resources.open(root));ClassicArtwork old,art;CHECK(!old.open(resources,root,options,{}));CHECK(!old.usesCubeMesh());
        const auto path=rounded?"Scripts/Rounded.x":"Scripts/Die.x";fixtures::write(root/path,fixtures::cubeMesh(rounded?7.2:1));
        CHECK(!resources.open(root));
        if(auto error=art.open(resources,root,options,{})){test::fail(error.message);continue;}
        CHECK(art.usesCubeMesh());
        image::Image letters,qu;Dictionary dictionary;CHECK(!resources.image("Textures/Letters.bmp",letters));CHECK(!resources.image("Textures/LettersQu.bmp",qu));
        CHECK(!resources.dictionary("Dicts/English.dic",dictionary));Board board;CHECK(!board.set(options.side(),std::vector<std::string>(options.side()*options.side(),"A")));
        ClassicRound round(dictionary);CHECK(!round.board(board));CHECK(!round.start(0));Surface screen,control;screen.create(800,600,PixelFormat::RGBA32);control=screen;
        old.draw(control,round,0,180000,letters,qu,{},0);art.draw(screen,round,0,180000,letters,qu,{},0);CHECK(screen.pixels!=control.pixels);
        for(int y=0;y<600;++y)CHECK(std::equal(screen.row(y)+621*4,screen.row(y)+800*4,control.row(y)+621*4));
        for(size_t i=0;i<options.side()*options.side();++i){const auto p=art.cellCenter(i,options.side());CHECK(art.cellAt(p.x,p.y,options.side())==i);}
        for(size_t frame:{34u,37u,43u,44u,51u,52u}) {
            std::fill(screen.pixels.begin(),screen.pixels.end(),uint8_t(17));art.drawTransition(screen,board,frame,letters,qu,1);
            const auto view=classicLidView(master,frame);size_t changed=0;
            for(int y=0;y<600;++y)for(int x=0;x<800;++x)if(screen.row(y)[x*4+3]!=17) {
                ++changed;CHECK(view.visible && x*4/5>=view.clip[0] && x*4/5<view.clip[2] && y*4/5>=view.clip[1] && y*4/5<view.clip[3]);
            }
            if(frame==43)CHECK(changed>0);
            if(frame==52)CHECK(!changed);
        }
        GameTarget target;target.path=root;target.savePath=tmp/("save-"+std::to_string(master)+std::to_string(rounded));target.settings={{"classic_seed","6551"},{"classic_variant",master?"master":"original"},{"classic_animations","0"},{"classic_sound","0"}};
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1){std::vector<Event> events;for(size_t i=0;i<4;++i){const auto p=art.cellCenter(i,options.side());events.push_back(mouseEvent(EventType::MouseDown,p.x,p.y));}return events;}
            if(f==2)return {mouseEvent(EventType::MouseDown,700,300)};
            if(f==3)return {keyEvent(key::F1)};
            if(f==4)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,target);if(auto error=game.run()){test::fail(error.message);continue;}HighScores scores;CHECK(!scores.load(target.savePath,target.path));CHECK(scores.table().size()==1 && scores.table()[0].score==1);
        fixtures::write(root/"Scripts/Rounded.x","malformed mesh");CHECK(bool(art.open(resources,root,options,{})) && !art.active());
        options.artwork=false;CHECK(!art.open(resources,root,options,{}) && !art.usesCubeMesh());
    }
}
}
static const test::Suite suite("boggle.unit.cube_renderer",{
    {"convex/concave/folded polygons, collinear corners and invalid input",[](const auto&){topology();}},
    {"depth, Gouraud materials, Qu, rotation, centers, hit testing and clipping",[](const auto&){raster();}},
    {"both meshes and variants, lid masks, word entry, scoring and opt-out",layout},
});
