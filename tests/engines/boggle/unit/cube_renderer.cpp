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
void physicalDice() {
    const DieAxis up={0,0,-1};
    for(bool rounded:{false,true})for(size_t face=0;face<6;++face)for(unsigned spin=0;spin<4;++spin) {
        const auto pose=settledDiePose(face,spin,rounded);CHECK(pose.source(dieFaceAxes[face])==up);
        for(size_t a=0;a<3;++a)for(size_t b=0;b<3;++b){int dot=0;for(size_t k=0;k<3;++k)dot+=pose.rows[a][k]*pose.rows[b][k];CHECK(dot==(a==b?1:0));}
        auto cycle=pose;for(int n=0;n<4;++n)cycle=spinDie(cycle,1);CHECK(cycle.rows==pose.rows);
    }
    for(size_t face=0;face<6;++face)CHECK((settledDiePose(face,0,false).rows!=settledDiePose(face,0,true).rows)==(face==2 || face==4));
    auto letters=atlas(200,100,50),qu=atlas(40,200,80);
    for(bool rounded:{false,true})for(bool master:{false,true}) {
        const size_t side=master?5:4;auto g=geometry();if(rounded)g.appliedScale=classicCubeMeshScale(classicCubeMeshPaths[0]);
        CubeRenderer renderer;CHECK(!renderer.prepare(g));CameraProjection camera(classicLidView(master,43).camera);
        DiceSet dice;dice.side=side;std::array<std::string,6> faces={"A","B","C","D","E","QU"};dice.dice.resize(side*side,faces);
        for(uint32_t seed:{1u,6551u}) {
            Board board;CHECK(!Board::generate(dice,seed,board));Board manual;CHECK(!manual.set(side,board.faces()));
            Surface a,b;a.create(800,600,PixelFormat::RGBA32);b=a;
            renderer.draw(a,board,camera,letters,qu,{},seed,true);renderer.draw(b,manual,camera,letters,qu,{},seed,true);
            CHECK(a.pixels!=b.pixels); // generated dice have real side lettering
            for(bool orient:{false,true}) {
                for(size_t i=0;i<side*side;++i) {
                    const auto p=renderer.cellCenter(camera,board,i,seed,orient);CHECK(renderer.cellAt(camera,board,p.x,p.y,seed,orient)==i);
                    if(orient){const auto* color=a.row(p.y)+p.x*4;CHECK(color[0]==(board.faces()[i]=="QU"?38:192));}
                }
            }
            b=a;renderer.draw(b,board,camera,letters,qu,{},seed,false);CHECK(a.pixels!=b.pixels);
            auto gradient=letters;
            for(int y=0;y<512;++y)for(int x=0;x<512;++x) {
                auto* pixel=gradient.rgba.data()+(y*512+x)*4;
                pixel[0]=uint8_t((x-(x*6/512)*512/6)*2);pixel[1]=uint8_t((y-(y*6/512)*512/6)*2);
            }
            renderer.draw(b,board,camera,gradient,gradient,{},seed,true);
            for(size_t i=0;i<side*side;++i) {
                auto sample=[&](int dx,int dy,int channel){const auto p=camera.project((int(i%side)*2+1-int(side))*55000+dx,(int(side)-int(i/side)*2-1)*55000+dy,-50000,100000,5,4);return b.row(p.y)[p.x*4+channel];};
                CHECK(sample(-20000,0,0)<sample(20000,0,0));
                CHECK(sample(0,20000,1)<sample(0,-20000,1)); // all six upward groups remain upright
            }
        }
    }
}
void sideTexture() {
    // Both styles share an invented geometric cube. An asymmetric atlas makes
    // a horizontal mirror visible independently of letter shape and material.
    auto gradient=atlas(0,100,50);
    for(int y=0;y<512;++y)for(int x=0;x<512;++x)gradient.rgba[(y*512+x)*4]=uint8_t((x-(x*6/512)*512/6)*2);
    DiceSet dice;dice.side=4;std::array<std::string,6> faces;faces.fill("A");dice.dice.resize(16,faces);
    Board board;uint32_t seed=0;
    do {CHECK(!Board::generate(dice,seed++,board));}while(board.rolledDice()[12].upward!=5 && seed<100);
    CHECK(board.rolledDice()[12].upward==5);
    CameraProjection camera(classicLidView(false,43).camera);
    std::array<std::array<int,2>,2> red{};
    for(size_t style=0;style<2;++style) {
        auto g=geometry();if(style)g.appliedScale=classicCubeMeshScale(classicCubeMeshPaths[0]);
        CubeRenderer renderer;CHECK(!renderer.prepare(g));Surface screen;screen.create(800,600,PixelFormat::RGBA32);
        renderer.draw(screen,board,camera,gradient,gradient,{},seed-1,true);
        for(size_t k=0;k<2;++k) {
            // Bottom-row cell 12: its visible front is source +Z (input 2).
            const auto p=camera.project(-165000+(k?20000:-20000),-215000,0,100000,5,4);
            CHECK(renderer.cellAt(camera,board,p.x,p.y,seed-1,true)==12);
            red[style][k]=screen.row(p.y)[p.x*4];
        }
    }
    CHECK(red[0][0]>red[0][1]);CHECK(red[1][0]<red[1][1]);
}
void styleChoice(const std::filesystem::path& tmp) {
    for(bool master:{false,true})for(bool rounded:{false,true}) {
        const auto root=tmp/(std::to_string(master)+std::to_string(rounded));fixtures::artworkInstall(root);
        const auto chosen=rounded?"Scripts/Rounded.x":"Scripts/Die.x",other=rounded?"Scripts/Die.x":"Scripts/Rounded.x";
        fixtures::write(root/chosen,fixtures::cubeMesh(rounded?7.2:1));fixtures::write(root/other,"malformed unselected mesh");
        Resources resources;CHECK(!resources.open(root));ClassicOptions options;
        options.variant=master?ClassicVariant::Master:ClassicVariant::Original;options.autoOrient=false;
        options.cubeStyle=rounded?ClassicCubeStyle::Rounded:ClassicCubeStyle::Angular;
        ClassicArtwork art;CHECK(!art.open(resources,root,options,{}) && art.usesCubeMesh());
        DiceSet dice;Board board;CHECK(!resources.diceSet(options.dice(),dice));CHECK(!Board::generate(dice,6551,board));
        for(size_t i=0;i<board.faces().size();++i){const auto p=art.cellCenter(i,board,6551);CHECK(art.cellAt(p.x,p.y,board,6551)==i);}
        GameTarget target;target.path=root;target.savePath=tmp/("save-"+std::to_string(master)+std::to_string(rounded));
        target.settings={{"classic_seed","6551"},{"classic_variant",master?"master":"original"},{"classic_cube_style",rounded?"rounded":"angular"},{"classic_auto_orient","0"},{"classic_animations","0"},{"classic_sound","0"}};
        fixtures::ScriptedSystem sys;sys.events=[&](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1){std::vector<Event> events;for(size_t i=0;i<4;++i){const auto p=art.cellCenter(i,board,6551);events.push_back(mouseEvent(EventType::MouseDown,p.x,p.y));}return events;}
            if(f==2)return {mouseEvent(EventType::MouseDown,700,300)};
            if(f==3)return {keyEvent(key::F1)};
            if(f==4)return {keyEvent(key::Escape)};
            return {};
        };
        ClassicGame game(sys,target);CHECK(!game.run());HighScores scores;CHECK(!scores.load(target.savePath,target.path));CHECK(scores.table().size()==1 && scores.table()[0].score==1);
        options.cubeStyle=ClassicCubeStyle::Automatic;
        const auto automatic=art.open(resources,root,options,{});CHECK(bool(automatic)==!rounded);
        options.cubeStyle=rounded?ClassicCubeStyle::Angular:ClassicCubeStyle::Rounded;
        CHECK(bool(art.open(resources,root,options,{})) && !art.active());
        std::filesystem::remove(root/chosen);options.cubeStyle=rounded?ClassicCubeStyle::Rounded:ClassicCubeStyle::Angular;
        const auto missing=art.open(resources,root,options,{});CHECK(bool(missing) && missing.message.find("missing")!=std::string::npos && !art.active());
        options.artwork=false;CHECK(!art.open(resources,root,options,{}) && !art.active());
        std::filesystem::remove(root/other);options.artwork=true;options.cubeStyle=ClassicCubeStyle::Automatic;
        CHECK(!art.open(resources,root,options,{}) && art.active() && !art.usesCubeMesh());
    }
}
void pausedCover(const std::filesystem::path& tmp) {
    for(bool master:{false,true})for(int style=0;style<3;++style) {
        const auto root=tmp/(std::to_string(master)+std::to_string(style));fixtures::artworkInstall(root);
        if(style)fixtures::write(root/(style==1?"Scripts/Rounded.x":"Scripts/Die.x"),fixtures::cubeMesh(style==1?7.2:1));
        Resources resources;CHECK(!resources.open(root));ClassicOptions options;options.variant=master?ClassicVariant::Master:ClassicVariant::Original;options.autoOrient=false;
        ClassicArtwork art;CHECK(!art.open(resources,root,options,{}));
        Dictionary dictionary;DiceSet dice;Board board;image::Image letters,qu;
        CHECK(!resources.dictionary("Dicts/English.dic",dictionary));CHECK(!resources.diceSet(options.dice(),dice));CHECK(!Board::generate(dice,6551,board));
        CHECK(!resources.image("Textures/Letters.bmp",letters));CHECK(!resources.image("Textures/LettersQu.bmp",qu));
        ClassicRound round(dictionary);CHECK(!round.board(board));
        Surface ready,playing,covered,resumed;ready.create(800,600,PixelFormat::RGBA32);playing=covered=resumed=ready;
        art.draw(ready,round,0,180000,letters,qu,{},0,6551);CHECK(!round.start(0));
        art.draw(playing,round,0,180000,letters,qu,{0},0,6551);
        art.draw(covered,round,0,180000,letters,qu,{0},0,6551,"PLAYER 1",nullptr,true);
        CHECK(covered.pixels==ready.pixels && covered.pixels!=playing.pixels);
        art.draw(resumed,round,0,180000,letters,qu,{0},0,6551);CHECK(resumed.pixels==playing.pixels);
        CHECK(round.phase()==RoundPhase::Playing && round.board().faces()==board.faces());
        GameTarget target;target.path=root;target.savePath=tmp/("save-"+std::to_string(master)+std::to_string(style));
        target.settings={{"classic_seed","6551"},{"classic_variant",master?"master":"original"},{"classic_auto_orient","0"},{"classic_animations","0"},{"classic_sound","0"}};
        fixtures::ScriptedSystem sys;std::array<uint8_t,4> before{},paused{},after{};
        sys.events=[](size_t f)->std::vector<Event>{using namespace fixtures;
            if(f==0)return {keyEvent(key::Return)};
            if(f==1)return {textEvent("AAAA")};
            if(f==2)return {mouseEvent(EventType::MouseDown,700,570)};
            if(f==4)return {mouseEvent(EventType::MouseDown,400,360)};
            if(f==5)return {keyEvent(key::Return)};
            if(f==6)return {keyEvent(key::F1)};
            if(f==7)return {keyEvent(key::Escape)};
            return {};
        };
        sys.present=[&](const Surface& frame,size_t f){auto* pixel=frame.row(440)+200*4;if(f==1)std::copy_n(pixel,4,before.begin());if(f==2)std::copy_n(pixel,4,paused.begin());if(f==5)std::copy_n(pixel,4,after.begin());};
        ClassicGame game(sys,target);CHECK(!game.run());CHECK(before==after && before!=paused);
        CHECK(std::equal(paused.begin(),paused.end(),covered.row(440)+200*4));
        HighScores scores;CHECK(!scores.load(target.savePath,target.path));CHECK(scores.table().size()==1 && scores.table()[0].score==1);
    }
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
    {"physical die faces, rigid poses, side lettering and rotated picking",[](const auto&){physicalDice();}},
    {"rounded side bitmap mirror differs from angular template",[](const auto&){sideTexture();}},
    {"explicit styles, missing assets, ignored alternatives and mouse scoring",styleChoice},
    {"manual pause covers installed boards and resumes their existing pose",pausedCover},
    {"both meshes and variants, lid masks, word entry, scoring and opt-out",layout},
});
