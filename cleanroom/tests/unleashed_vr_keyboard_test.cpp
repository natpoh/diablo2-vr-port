// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed: steamvr/vr_keyboard_test.cpp (Kudlatytrue df1cf3a / 29d29b0, PR #486) as it is, on vr/vr_keyboard_unleashed.h;
// the few frame_math.h calls it makes are given below. Run: unleashed_vr_keyboard_test.exe
#include "vr_keyboard_unleashed.h"
namespace framevr::math {
using Vec = keyboard::Vec;
using Quat = keyboard::Quat;
using keyboard::Rotate;
using keyboard::Multiply;
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec Normalized(Vec v) { const float l = std::sqrt(Dot(v, v)); return l > 0 ? v * (1.0f / l) : v; }
struct PanelHit { float u = 0, v = 0, distance = 0; Vec point{}; };
// The ray against a w x h panel at `centre` turned by `q` (its front +z): u, v from its top left (0-1), the distance, the point.
inline bool IntersectPanel(Vec from, Vec dir, Vec centre, Quat q, float w, float h, PanelHit& out) {
    const Vec n = Rotate(q, {0, 0, 1});
    const float dn = Dot(dir, n);
    if (std::fabs(dn) < 1e-6f) return false;
    const float t = Dot(centre - from, n) / dn;
    if (!(t > 0)) return false;
    const Vec p = from + dir * t;
    const Quat back{-q.x, -q.y, -q.z, q.w};
    const Vec l = Rotate(back, p - centre);
    out.u = l.x / w + 0.5f; out.v = 0.5f - l.y / h; out.distance = t; out.point = p;
    return out.u >= 0 && out.u <= 1 && out.v >= 0 && out.v <= 1;
}
}  // namespace framevr::math
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
using namespace framevr;
static void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
// Where a ray from `eye` through key units (col, row) of the board would hit it.
static math::Vec OnBoard(const keyboard::Board& b,float col,float row) {
    const float unit=b.height/keyboard::Rows;
    return b.position+math::Rotate(b.orientation,{col*unit-b.width*.5f,b.height*.5f-row*unit,0});
}
static bool Near(math::Vec a,math::Vec b){const auto d=a-b;return math::Dot(d,d)<1e-8f;}
static int Find(char ch){for(int i=0;i<keyboard::KeyCount;++i)if(keyboard::Keys[i].action==keyboard::Action::Char&&keyboard::Keys[i].ch==ch)return i;return -1;}
static int Find(keyboard::Action a){for(int i=0;i<keyboard::KeyCount;++i)if(keyboard::Keys[i].action==a)return i;return -1;}
int main(){
    using namespace keyboard;
    // Every key a name needs, once each, and no key reaching past the board or over another.
    for(char c='a';c<='z';++c)Check(Find(c)>=0,"every letter has a key");
    Check(Find('-')>=0&&Find('_')>=0,"the two marks a name may hold have keys");
    for(auto a:{Action::Backspace,Action::Enter,Action::Shift,Action::Close})Check(Find(a)>=0,"Backspace, Enter, Shift and Close have keys");
    for(int i=0;i<KeyCount;++i) {
        const Key& k=Keys[i];
        Check(k.col>=0&&k.col+k.span<=Columns+1e-4f&&k.row>=0&&k.row+1<=Rows,"every key inside the board");
        for(int j=i+1;j<KeyCount;++j)
            if(Keys[j].row==k.row)Check(Keys[j].col>=k.col+k.span-1e-4f||k.col>=Keys[j].col+Keys[j].span-1e-4f,"no two keys overlap");
        const Rect r=KeyTexelRect(i);
        Check(r.w>0&&r.h>0&&r.x>=0&&r.y>=0&&r.x+r.w<=TexelWidth&&r.y+r.h<=TexelHeight,"every key's picture inside the keyboard's");
    }

    // Under the menu, out of the game's picture: tilted toward the player, facing up to the eyes.
    const math::Vec menu{0,1.6f,-1.5f};const math::Quat facing{0,0,0,1};   // a panel straight ahead, facing the player
    for(auto size:{std::pair{2.0f,1.125f},std::pair{1.125f,1.125f}}) {    // 16:9, and a square eye target's panel
        const float width=size.first,height=size.second;
        const Board b=Place(menu,facing,width,height);
        Check(b.width>0&&b.height>0&&std::fabs(b.width/b.height-Columns/Rows)<1e-4f,"the quad has the picture's aspect");
        Check(std::fabs(b.width-WidthOfWorkspace*2.0f)<1e-4f,"the same size on a wide or a square panel (the 16:9 workspace's width)");
        const auto top=OnBoard(b,Columns*.5f,0),bottom=OnBoard(b,Columns*.5f,Rows);
        Check(top.y<menu.y-height*.5f&&top.y>menu.y-height*.5f-.1f,"its top edge just under the panel's bottom edge");
        Check(std::fabs(top.z-menu.z)<1e-4f,"hinged in the panel's plane");
        Check(bottom.y<top.y&&bottom.z>top.z,"its lower edge lower and nearer the player: a keyboard on a desk");
        const auto normal=math::Rotate(b.orientation,{0,0,1});
        Check(normal.y>0&&normal.z>0&&std::fabs(std::acos(normal.z)*180/3.14159265f-TiltDegrees)<.01f,"facing up toward the eyes, tilted Tilt");
        Check(std::fabs(OnBoard(b,0,0).x+b.width*.5f)<1e-4f,"centred under the menu");
        // The laser, through the board's own intersection: each key's middle hits it.
        const math::Vec eye{0,1.7f,0};
        for(int i=0;i<KeyCount;++i) {
            const auto at=OnBoard(b,Keys[i].col+Keys[i].span*.5f,Keys[i].row+.5f);
            math::PanelHit hit{};
            Check(math::IntersectPanel(eye,math::Normalized(at-eye),b.position,b.orientation,b.width,b.height,hit),"the laser reaches the board from the eyes");
            Check(KeyAt(hit.u,hit.v)==i,"a key's middle hits that key");
            Check(Near(hit.point,at),"where the laser says it hit");
            const Spot k=KeySpot(b,i);
            Check(Near(b.position+math::Rotate(b.orientation,{k.x,k.y,0}),at),"the lit copy lies over the key it lights");
            Check(k.width>0&&k.height>0&&k.height<b.height/Rows,"the lit copy is the key's size, not a whole row");
        }
        Check(!Place(menu,facing,0,1).width&&!Place(menu,facing,1,0).width,"no panel, no keyboard");
    }
    Check(KeyAt(.1f,.5f/Rows)==-1,"the gap between Q and W hits nothing (no press on the wrong letter)");
    Check(KeyAt(.2f/Columns,1.5f/Rows)==-1,"the empty half key left of A hits nothing");
    Check(KeyAt(.05f,.125f)==Find('q')&&KeyAt(.95f,.875f)==Find(Action::Enter),"the corners: Q and Enter");
    Check(std::fabs(float(TexelWidth)/TexelHeight-Columns/Rows)<1e-4f,"the picture has the board's aspect");
    // Labels: small letters, capitals while Shift waits; other keys their names.
    Check(Label(Find('k'),false)==L"k"&&Label(Find('k'),true)==L"K","a letter's key shows it small, a capital with Shift");
    Check(Label(Find('-'),true)==L"-"&&Label(Find('_'),false)==L"_","marks the same either way");
    Check(Label(Find(Action::Enter),true)==L"Enter"&&Label(-1,false).empty(),"other keys their name");

    // Typing: lower case, Shift once for a capital, Backspace, Enter, Close.
    Typist t;
    auto out=t.Press(Find('k'));
    Check(out.send==Send::Char&&out.ch==L'k'&&!out.close,"a letter types its lower case");
    Check(t.Press(Find(Action::Shift)).send==Send::None&&t.Shifted(),"Shift sends nothing and waits for a letter");
    out=t.Press(Find('k'));
    Check(out.send==Send::Char&&out.ch==L'K'&&!t.Shifted(),"Shift makes one capital, then letters are small again");
    Check(t.Press(Find('k')).ch==L'k',"the next letter is small");
    t.Press(Find(Action::Shift));t.Press(Find(Action::Shift));
    Check(!t.Shifted(),"Shift twice takes it back");
    t.Press(Find(Action::Shift));
    out=t.Press(Find('-'));
    Check(out.send==Send::Char&&out.ch==L'-'&&!t.Shifted(),"Shift leaves '-' as it is, and is used up");
    Check(t.Press(Find('_')).ch==L'_',"'_' types itself");
    Check(t.Press(Find(Action::Backspace)).send==Send::Backspace,"Backspace sends Backspace");
    t.Press(Find(Action::Shift));
    Check(t.Press(Find(Action::Enter)).send==Send::Enter&&!t.Shifted(),"Enter sends Enter and drops a waiting Shift");
    t.Press(Find(Action::Shift));
    out=t.Press(Find(Action::Close));
    Check(out.close&&out.send==Send::None&&!t.Shifted(),"Close hides the keyboard, sends nothing");
    Check(t.Press(-1).send==Send::None&&t.Press(KeyCount).send==Send::None,"no key: nothing");

    // The picture: every key painted, the gaps between them clear.
    const auto px=PaintKeys(false),lit=PaintKeys(true),caps=PaintKeys(false,true);
    {
        const Rect r=KeyTexelRect(Find(Action::Shift)),q=KeyTexelRect(Find('q'));
        const size_t shift=(size_t(r.y+2)*TexelWidth+r.x+r.w/2)*4,letter=(size_t(q.y+2)*TexelWidth+q.x+q.w/2)*4;   // their rims
        Check(caps[shift]>px[shift]&&caps[letter]==px[letter],"with capitals the waiting Shift is lit, nothing else");
    }
    Check(px.size()==size_t(TexelWidth)*TexelHeight*4&&lit.size()==px.size(),"one picture the keyboard's size, lit or not");
    for(int i=0;i<KeyCount;++i) {
        const Rect r=KeyTexelRect(i);
        const size_t middle=(size_t(r.y+r.h/2)*TexelWidth+r.x+r.w/2)*4;
        Check(px[middle+3]==255&&lit[middle+3]==255,"each key's middle is solid stone");
        Check(lit[middle]+lit[middle+1]+lit[middle+2]>=px[middle]+px[middle+1]+px[middle+2],"the lit copy is no darker");
    }
    Check(px[3]==0&&px[(size_t(KeyTexels)*TexelWidth+KeyTexels/4)*4+3]==0,"the gaps and the empty half key are clear");
    // The name box, from the game's UI messages as the 2026-11-06 log has them.
    {
        NameBox box;
        Check(!box.Shown(),"no character creation: no keyboard");
        box.Message("PanelManager","OpenPanel","CharacterSelectPanel");
        box.Message("CharacterCreate","Select","Barbarian");
        Check(!box.Shown(),"the character list, or a stray Select outside creation: no keyboard");
        box.Message("PanelManager","OpenExclusivePanel","CharacterCreatePanel");
        box.Message("PanelManager","OpenPanel","CandidateListPanel");
        Check(!box.Shown(),"at the campfire before a hero is picked: no name box, no keyboard");
        box.Message("CharacterCreate","Hover","Assassin");box.Message("CharacterCreate","Unhover","Assassin");
        Check(!box.Shown(),"a hero only hovered: no keyboard");
        const auto e0=box.Epoch();
        box.Message("CharacterCreate","Select","Necromancer");
        Check(box.Shown()&&box.Epoch()!=e0,"a hero picked: the name box is up, the keyboard shows");
        const auto e1=box.Epoch();
        box.Message("InputMessage","MouseMove","");box.Message("CharacterCreate","Hover","Paladin");
        Check(box.Shown()&&box.Epoch()==e1,"pointing about keeps it, and a closed keyboard stays closed");
        box.Message("CharacterCreate","Select","Barbarian");box.Message("CharacterCreate","Unselect","Necromancer");
        Check(box.Shown()&&box.Epoch()!=e1,"another hero (Select before the old one's Unselect): still shown, a closed keyboard back");
        box.Message("CharacterCreate","Unselect","Barbarian");
        Check(!box.Shown(),"the hero put back: the name box goes, the keyboard with it");
        box.Message("CharacterCreate","Select","Sorceress");
        Check(box.Shown(),"picked again");
        box.Message("FrontEndNavigation","ToCharacterSelect","");box.Message("PanelManager","UnloadPanel","CharacterCreatePanel");
        Check(!box.Shown(),"back to the list: gone");
        box.Message("CharacterCreate","Select","Sorceress");
        Check(!box.Shown(),"a late Select after the panel went: still gone");
        box.Message("PanelManager","OpenExclusivePanel","CharacterCreatePanel");
        Check(!box.Shown(),"creation opened again starts with no hero picked");
        box.Message("CharacterCreate","Select","Amazon");box.Message("PanelManager","UnloadAll","");
        Check(!box.Shown(),"every panel unloaded (a game starting): gone");
    }
    std::puts("VR keyboard layout, laser hits, typing and picture passed");
}
