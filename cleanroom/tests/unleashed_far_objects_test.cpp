// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 9e9c2410e8df57f72547c691de9971c88b0b014a, its steamvr/far_objects_test.cpp, for D2R VR Authentic's port
// (shared/unleashed_far_objects.h): the VR panel's stepper checks left out (the Settings slider steps 0 - 1000 by 100 here, checked
// instead). Stays MPL 2.0.
//
// Big objects drawn farther: a model's reach grows with its size up to the setting and small ones keep the object draw distance; with
// the setting off the test is the game's own; the setting is Off or 300 ... 1000. The hooked test's pattern (cleanroom/sigscan/
// game_sigs.h ModelInRange, cleanroom/camera/game_layout.h) against D2R 3.3.93787's code at its site, unhooked: it matches there,
// resolves to the test's address, and the hook's bytes are its first bytes.
#include "../../shared/unleashed_far_objects.h"
#include "../camera/game_layout.h"
#include "../sigscan/game_sigs.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int failures=0;
static void Check(bool ok,const char* what){if(!ok){std::printf("FAILED: %s\n",what);++failures;}}

// A box: min x y z, max x y z (w unused, as the game's)
struct Box {float v[8];};
static Box At(float x,float z,float width,float height,float depth){return {{x,0,z,1,x+width,height,z+depth,1}};}
// The game's own test (0x15723B0): the box's nearest point to the hero across the ground, squared, under radiusSq
static bool Game(const Box& b,const float* hero,float radiusSq){
    const float dx=std::max(b.v[0],std::min(hero[0],b.v[4]))-hero[0],dz=std::max(b.v[2],std::min(hero[2],b.v[6]))-hero[2];
    return radiusSq>dx*dx+dz*dz;
}

// "4C ?? 8B" -> bytes, -1 for ??
static std::vector<int> Parse(const char* p){
    std::vector<int> v;
    for(const char* c=p;*c;){
        while(*c==' ')++c;
        if(!*c)break;
        if(c[0]=='?'){v.push_back(-1);c+=2;continue;}
        v.push_back(int(std::strtol(std::string(c,2).c_str(),nullptr,16)));c+=2;
    }
    return v;
}
static bool Matches(const uint8_t* at,size_t size,const std::vector<int>& p){
    if(p.size()>size)return false;
    for(size_t i=0;i<p.size();++i)if(p[i]>=0&&at[i]!=p[i])return false;
    return true;
}

int main(){
    using namespace unleashed::farobjects;
    const float radius=210,radiusSq=radius*radius;
    const float hero[4]={0,7,0,1};
    // A tree 20 high, 2 across, its near side 549 away: kept out to 600 (20 x 30), lost at 210 alone
    const Box tree=At(549,-1,2,20,2);
    Check(Size(tree.v)==20&&Reach(tree.v,radius,1000)==600,"a 20-unit tree reaches 600");
    Check(InRange(tree.v,hero,radiusSq,600),"the tree 549 away kept with Big 600");
    Check(!InRange(tree.v,hero,radiusSq,0)&&!Game(tree,hero,radiusSq),"the tree 549 away lost without it, as the game");
    Check(!InRange(At(601,-1,2,20,2).v,hero,radiusSq,1000),"the tree 601 away lost: its own reach is 600");
    Check(InRange(tree.v,hero,radiusSq,1000)&&!InRange(tree.v,hero,radiusSq,500),"the setting caps the reach");
    // A stone 2 high: its reach 60 is under the object draw distance, which it keeps
    const Box stone=At(200,0,1,2,1),farStone=At(300,0,1,2,1);
    Check(Reach(stone.v,radius,1000)==radius,"a stone keeps the object draw distance");
    Check(InRange(stone.v,hero,radiusSq,1000)&&!InRange(farStone.v,hero,radiusSq,1000),"stones: 200 kept, 300 lost, Big or not");
    // A wall 20 long and 4 high: half its length, 10, by 30 - 300
    const Box wall=At(0,280,20,4,1);
    Check(Size(wall.v)==10&&InRange(wall.v,hero,radiusSq,1000)&&!InRange(At(0,310,20,4,1).v,hero,radiusSq,1000),"a 20-unit wall reaches 300");
    // Big under the object draw distance changes nothing
    Check(Reach(tree.v,radius,200)==radius&&!InRange(tree.v,hero,radiusSq,200),"Big under the object distance does nothing");
    // Height above the ground is not distance: the hero over the box is in it
    const float above[4]={550,400,0,1};
    Check(InRange(tree.v,above,radiusSq,0)&&Game(tree,above,radiusSq),"across the ground only");
    // Off: the game's own comparison, for boxes all round
    bool same=true;
    for(int i=0;i<2000;++i){
        const float x=std::fmod(i*37.3f,900.f)-450,z=std::fmod(i*91.7f,900.f)-450,w=std::fmod(i*3.1f,30.f),h=std::fmod(i*7.9f,40.f);
        const Box b=At(x,z,w,h,w*.5f);
        same=same&&InRange(b.v,hero,radiusSq,0)==Game(b,hero,radiusSq);
    }
    Check(same,"off: the game's own result for every box");
    // The setting: Off, 300 ... 1000 in steps of 100
    Check(Normalize(0)==0&&Normalize(-5)==0&&Normalize(NAN)==0&&Normalize(250)==300&&Normalize(550)==600&&Normalize(1234)==1000,"the setting normalized");
    // The Settings slider (0 - 1000 by 100, tools/settings/settings.cpp): 0 off, 100 and 200 read as 300, the rest as they are
    Check(Normalize(100)==300&&Normalize(200)==300&&Normalize(300)==300&&Normalize(700)==700&&Normalize(1000)==1000,"the Settings slider's stops");

    // The hooked test at its site, unhooked (3.3.93787, also 3.3.94995)
    static const uint8_t window[32]={0x0f,0x10,0x49,0x10,0x0f,0x57,0xc0,0x0f,0x10,0x21,0x0f,0x28,0xea,0x0f,0x10,0x12,
                                     0x0f,0xc6,0xe4,0xe1,0xf3,0x0f,0x10,0xe0,0x0f,0xc6,0xc9,0xe1,0xf3,0x0f,0x10,0xc8};
    const d2rsig::Sig* sig=nullptr;size_t at=0;
    const size_t count=sizeof(d2rsig::kSigs)/sizeof(d2rsig::kSigs[0]);
    for(size_t i=0;i<count;++i)if(std::strcmp(d2rsig::kSigs[i].name,"ModelInRange")==0){sig=&d2rsig::kSigs[i];at=i;}
    Check(sig!=nullptr,"ModelInRange in the table");
    if(sig){
        const auto pattern=Parse(sig->pattern);
        Check(pattern.size()>=24&&Matches(window,sizeof window,pattern),"its pattern matches at its site");
        Check(sig->kind==d2rsig::Kind::Code&&sig->site==sig->rva&&sig->offset==0&&sig->rva==d2rcam::layout::ModelInRange.rva,"it resolves to the test's address");
        Check(sig->body&&Matches(window,sizeof window,Parse(sig->body)),"its hook bytes are its first bytes");
        Check(std::strcmp(sig->area,"Rooms and models")==0&&at>0&&std::strcmp(d2rsig::kSigs[at-1].area,"Rooms and models")==0,"in the Rooms and models group, with its neighbours");
    }
    const auto& site=d2rcam::layout::ModelInRange;
    Check(site.size==16&&std::memcmp(site.bytes,window,site.size)==0,"the layout's expected bytes: five whole instructions, no branch, no RIP-relative operand");
    if(failures){std::printf("unleashed_far_objects_test: %d FAILED\n",failures);return 1;}
    std::printf("unleashed_far_objects_test: big objects - all passed\n");
    return 0;
}
