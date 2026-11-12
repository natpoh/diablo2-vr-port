// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed: steamvr/vr_keyboard.h (Kudlatytrue df1cf3a, 29d29b0, ee20d25; PR #486) taken as it is - the name box (NameBox),
// the keys, the board under the menu (Place), the key at the laser (KeyAt, KeySpot), the labels and the typist; its picture's key face
// and label stamp (steamvr/respec_button.h PaintFace / Stamp, steamvr/stats_button.h StoneNoise) and the GDI label
// (steamvr/transport.cpp RespecLabel) beside it, and the key sent as a real keyboard's (transport.cpp SendKeyboard). math:: replaced by
// the few lines below. Used by vr/xr.cpp ([openxr] name_keyboard). The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
//
// The VR keyboard: letters for the game's text boxes - a new character's name first - without taking the headset off. D2R shows no
// on-screen keyboard of its own there (tested 2026-11-05: the name box does nothing under native controller navigation in VR). Its own
// quad hinged under the menu, tilted toward the player like a keyboard on a desk (Board): one picture of every key, small letters or,
// with Shift, capitals; a lit copy cropped to the key under the laser, which hits the board itself. Its letters go in through SendInput
// with no tag of ours (KeyboardKeyTag), so the plugin's typing rule ([openxr] laser_typing) lets the keyboard layout in and the box takes
// them like letters from a real keyboard.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace framevr::keyboard {

// (Unleashed's frame_math.h, the little of it used here)
struct Vec { float x = 0, y = 0, z = 0; };
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
inline Vec Rotate(Quat q, Vec v) {
    const Vec u{q.x, q.y, q.z};
    const Vec t{2 * (u.y * v.z - u.z * v.y), 2 * (u.z * v.x - u.x * v.z), 2 * (u.x * v.y - u.y * v.x)};
    return {v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z), v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
}
inline Quat Multiply(Quat a, Quat b) {   // b first, then a
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
// The KeyboardKeyTag of Unleashed: the keyboard's keys are the player's typing, not the plugin's own keys (vr/typed_keys.h kOwnKeyTag).
inline constexpr uintptr_t KeyboardKeyTag = 0x4b45594230415244ull;   // "KEYB0ARD"

inline float StoneNoise(float x,float y) {   // value noise, 0-1, repeatable (stats_button.h)
    const auto hash=[](int i,int j){uint32_t h=uint32_t(i)*374761393u+uint32_t(j)*668265263u;h=(h^(h>>13))*1274126177u;return float((h^(h>>16))&0xffff)/65535.f;};
    const int i=int(std::floor(x)),j=int(std::floor(y));
    const float fx=x-i,fy=y-j,ux=fx*fx*(3-2*fx),uy=fy*fy*(3-2*fy);
    const float a=hash(i,j),b=hash(i+1,j),c=hash(i,j+1),d=hash(i+1,j+1);
    return a+(b-a)*ux+(c-a)*uy+(a-b-c+d)*ux*uy;
}
// The face behind a key's label: the stone plaque with a gold rim of Unleashed's Reset Skills & Stats button (respec_button.h);
// `lit` brightens it. Premultiplied RGBA.
inline std::vector<uint8_t> PaintFace(int w,int h,bool lit) {
    std::vector<uint8_t> px(size_t(std::max(w,0))*size_t(std::max(h,0))*4,0);
    if(w<=0||h<=0)return px;
    const float ax=float(w)/float(h);   // x in units of the height
    const auto box=[](float x,float y,float cx,float cy,float hx,float hy,float r) {
        const float qx=std::fabs(x-cx)-hx+r,qy=std::fabs(y-cy)-hy+r;
        return std::hypot(std::max(qx,0.f),std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-r;
    };
    const float gold=lit?1.f:.8f;
    const float rimGold[3]={236*gold/255.f,196*gold/255.f,112*gold/255.f};
    constexpr int S=4;
    for(int py=0;py<h;++py)for(int qx=0;qx<w;++qx) {
        float acc[4]{};
        for(int sy=0;sy<S;++sy)for(int sx=0;sx<S;++sx) {
            const float x=(qx+(sx+.5f)/S)/h,y=(py+(sy+.5f)/S)/h;   // both in heights
            const float plaque=box(x,y,ax*.5f,.5f,ax*.5f-.04f,.46f,.10f);
            if(plaque>0)continue;
            const float grain=.5f*StoneNoise(x*7,y*7)+.3f*StoneNoise(x*19+3,y*19+5)+.2f*StoneNoise(x*53+7,y*53+1);
            float g=.30f+.16f*(grain-.5f)*2.f;
            const bool rim=plaque>-.07f,lip=!rim&&plaque>-.085f;
            if(rim)g+=.10f+.10f*(.5f-y);
            if(lip)g*=.45f;
            if(!rim)g*=.78f+.22f*(1-y);
            float c[3]={g,g*.99f,g*.96f};
            if(rim)for(int i=0;i<3;++i)c[i]=rimGold[i]*(1.f-.2f*y);
            for(int i=0;i<3;++i)acc[i]+=std::clamp(c[i],0.f,1.f);
            acc[3]+=1;
        }
        auto* p=&px[(size_t(py)*w+qx)*4];
        const float n=float(S*S);
        for(int i=0;i<3;++i)p[i]=uint8_t(std::clamp(acc[i]/n,0.f,1.f)*255.f+.5f);
        p[3]=uint8_t(acc[3]/n*255.f+.5f);
    }
    return px;
}

// Lays label coverage over a premultiplied face in white, a little brighter lit (respec_button.h).
inline void Stamp(std::vector<uint8_t>& face,const std::vector<uint8_t>& coverage,int w,int h,bool lit) {
    if(w<=0||h<=0||face.size()!=size_t(w)*h*4||coverage.size()!=size_t(w)*h)return;
    const float white=lit?1.f:.9f;
    const float c[3]={255*white,252*white,246*white};
    for(size_t i=0;i<coverage.size();++i) {
        const float a=coverage[i]/255.f;if(a<=0)continue;
        auto* p=&face[i*4];const float under=p[3]/255.f;
        if(under<=0)continue;   // text never spills off the plaque
        for(int k=0;k<3;++k)p[k]=uint8_t(std::clamp(p[k]*(1-a)+c[k]*a*under,0.f,255.f)+.5f);
    }
}

// A label's coverage (0-255 a texel, w x h): white bold Georgia, as large as fits with an inset (transport.cpp RespecLabel).
inline std::vector<uint8_t> LabelCoverage(const wchar_t* text,int w,int h) {
    std::vector<uint8_t> coverage;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return coverage;}
    auto old=SelectObject(dc,bitmap);std::memset(bits,0,size_t(w)*h*4);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    bool fits=false;
    const int fontScaleHeight=h;
    for(int px=h*30/64;px>=fontScaleHeight/4&&!fits;px-=2) {
        HFONT font=CreateFontW(px,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
        auto before=SelectObject(dc,font);RECT measured{0,0,0,0};
        DrawTextW(dc,text,-1,&measured,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
        fits=measured.right<=w-fontScaleHeight*7/16||px-2<fontScaleHeight/4;
        if(fits){RECT bounds{0,0,w,h};DrawTextW(dc,text,-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}
        SelectObject(dc,before);DeleteObject(font);
    }
    GdiFlush();
    coverage.resize(size_t(w)*h);const auto* b=static_cast<const uint8_t*>(bits);
    for(size_t i=0;i<coverage.size();++i)coverage[i]=std::max({b[4*i],b[4*i+1],b[4*i+2]});
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    return coverage;
}

class NameBox {
    bool panel=false;std::string hero;uint32_t shownAt=0;
public:
    void Message(std::string_view target,std::string_view command,std::string_view text) {
        const bool before=Shown();
        if(target=="PanelManager") {
            if(text=="CharacterCreatePanel") {
                if(command=="OpenPanel"||command=="OpenExclusivePanel")panel=true;
                else if(command=="ClosePanel"||command=="UnloadPanel")panel=false;
                hero.clear();
            } else if(command=="UnloadAll"||command=="CloseAll"){panel=false;hero.clear();}
        } else if(target=="CharacterCreate"&&panel) {
            if(command=="Select"&&!text.empty()){if(!hero.empty()&&hero!=text)++shownAt;hero=text;}   // another hero: anew
            else if(command=="Unselect"&&text==hero)hero.clear();
        }
        if(Shown()&&!before)++shownAt;
    }
    bool Shown() const{return panel&&!hero.empty();}
    bool PanelOpen() const{return panel;}   // (D2R VR Authentic: the creation screen up, a hero picked or not - the 3D scene's keyboard)
    uint32_t Epoch() const{return shownAt;}
};

// What a key does. Char types its letter; Shift makes the next letter a
// capital (once); Close hides the keyboard until the text box goes away.
enum class Action : uint8_t {Char,Backspace,Enter,Shift,Close};
struct Key {
    float col,row,span;   // in key units from the keyboard's top left
    Action action;
    char ch;              // Action::Char: the lower-case letter, '-' or '_'
    const wchar_t* label;
};
// Character names take letters and one '-' or '_' (the game checks the rest),
// so no digits and no space. Ten keys a row.
inline constexpr Key Keys[]={
    {0,0,1,Action::Char,'q',L"Q"},{1,0,1,Action::Char,'w',L"W"},{2,0,1,Action::Char,'e',L"E"},
    {3,0,1,Action::Char,'r',L"R"},{4,0,1,Action::Char,'t',L"T"},{5,0,1,Action::Char,'y',L"Y"},
    {6,0,1,Action::Char,'u',L"U"},{7,0,1,Action::Char,'i',L"I"},{8,0,1,Action::Char,'o',L"O"},
    {9,0,1,Action::Char,'p',L"P"},
    {.5f,1,1,Action::Char,'a',L"A"},{1.5f,1,1,Action::Char,'s',L"S"},{2.5f,1,1,Action::Char,'d',L"D"},
    {3.5f,1,1,Action::Char,'f',L"F"},{4.5f,1,1,Action::Char,'g',L"G"},{5.5f,1,1,Action::Char,'h',L"H"},
    {6.5f,1,1,Action::Char,'j',L"J"},{7.5f,1,1,Action::Char,'k',L"K"},{8.5f,1,1,Action::Char,'l',L"L"},
    {0,2,1.5f,Action::Shift,0,L"Shift"},
    {1.5f,2,1,Action::Char,'z',L"Z"},{2.5f,2,1,Action::Char,'x',L"X"},{3.5f,2,1,Action::Char,'c',L"C"},
    {4.5f,2,1,Action::Char,'v',L"V"},{5.5f,2,1,Action::Char,'b',L"B"},{6.5f,2,1,Action::Char,'n',L"N"},
    {7.5f,2,1,Action::Char,'m',L"M"},
    {8.5f,2,1.5f,Action::Backspace,0,L"Back"},
    {0,3,2,Action::Close,0,L"Close"},{2,3,1.5f,Action::Char,'-',L"-"},{3.5f,3,1.5f,Action::Char,'_',L"_"},
    {5,3,5,Action::Enter,0,L"Enter"},
};
constexpr int KeyCount=int(sizeof Keys/sizeof Keys[0]);
constexpr float Columns=10,Rows=4;

// The picture: KeyTexels a key unit, Pad texels of stone-free gap round each
// key, so the lit copy's crop never shows a neighbour's edge.
constexpr int KeyTexels=64,Pad=3;
constexpr int TexelWidth=int(Columns)*KeyTexels,TexelHeight=int(Rows)*KeyTexels;
struct Rect {int x=0,y=0,w=0,h=0;};
inline Rect KeyTexelRect(int i) {
    if(i<0||i>=KeyCount)return {};
    const Key& k=Keys[i];
    return {int(std::lround(k.col*KeyTexels))+Pad,int(std::lround(k.row*KeyTexels))+Pad,
            int(std::lround(k.span*KeyTexels))-2*Pad,KeyTexels-2*Pad};
}

// Where it sits: under the menu, out of the game's picture (owner 2026-11-06:
// over the heroes it hid their names, and the picture's sides are cut at some
// aspect ratios, so a spot on it is not the same for every headset). Its top
// edge is Gap below the panel's bottom edge, the board tilted Tilt toward the
// player, its lower edge nearer, facing up to the eyes. Sizes are fractions of
// the 16:9 workspace's width (panels::Fit): the same on a wide or a square
// panel, and growing with the menu distance, so a key stays as easy to hit.
constexpr float WidthOfWorkspace=.5f,GapOfWorkspace=.02f,TiltDegrees=30;
struct Board {Vec position{};Quat orientation{0,0,0,1};float width=0,height=0;};
inline Board Place(Vec surface,Quat facing,float width,float height) {
    if(!(width>0)||!(height>0))return {};
    const float workspace=std::max(width,height*16.f/9.f);
    const float w=WidthOfWorkspace*workspace,h=w*Rows/Columns,t=TiltDegrees*3.14159265f/180;
    const Vec up{0,std::cos(t),-std::sin(t)};   // the board's up: its top edge away from the player
    const Vec top{0,-height*.5f-GapOfWorkspace*workspace,0};
    const Quat tilt{-std::sin(t*.5f),0,0,std::cos(t*.5f)};   // about the panel's x axis
    return {surface+Rotate(facing,top-up*(h*.5f)),Multiply(facing,tilt),w,h};
}
// The key at the laser's hit on the board (u, v from its top left, 0-1: the
// board's own a ray against the board), or -1: in the gap between two keys.
inline int KeyAt(float u,float v) {
    const float col=u*Columns,row=v*Rows,pad=float(Pad)/KeyTexels;
    for(int i=0;i<KeyCount;++i) {
        const Key& k=Keys[i];
        if(col>=k.col+pad&&col<k.col+k.span-pad&&row>=k.row+pad&&row<k.row+1-pad)return i;
    }
    return -1;
}
// A key's lit copy on the board: its centre from the board's centre (x right,
// y up, metres) and its size.
struct Spot {float x=0,y=0,width=0,height=0;};
inline Spot KeySpot(const Board& b,int i) {
    if(i<0||i>=KeyCount||!(b.height>0))return {};
    const Key& k=Keys[i];
    const float unit=b.height/Rows,pad=float(Pad)/KeyTexels*unit;
    return {(k.col+k.span*.5f)*unit-b.width*.5f,b.height*.5f-(k.row+.5f)*unit,k.span*unit-2*pad,unit-2*pad};
}
// What a key shows: a letter small, or a capital while Shift waits (Typist);
// the other keys their name.
inline std::wstring Label(int i,bool capitals) {
    if(i<0||i>=KeyCount)return {};
    const Key& k=Keys[i];
    if(k.action!=Action::Char)return k.label;
    return std::wstring(1,wchar_t(capitals&&k.ch>='a'&&k.ch<='z'?k.ch-'a'+'A':k.ch));
}

// What a press sends: a character (ch), Backspace or Enter (key), or nothing
// (Shift, Close). close: hide the keyboard.
enum class Send : uint8_t {None,Char,Backspace,Enter};
struct Output {Send send=Send::None;wchar_t ch=0;bool close=false;};
class Typist {
    bool shift=false;
public:
    bool Shifted() const{return shift;}
    void Reset(){shift=false;}
    Output Press(int i) {
        Output out;
        if(i<0||i>=KeyCount)return out;
        const Key& k=Keys[i];
        switch(k.action) {
            case Action::Char:
                out.send=Send::Char;
                out.ch=wchar_t(shift&&k.ch>='a'&&k.ch<='z'?k.ch-'a'+'A':k.ch);
                shift=false;break;
            case Action::Shift:shift=!shift;break;
            case Action::Backspace:out.send=Send::Backspace;break;
            case Action::Enter:out.send=Send::Enter;shift=false;break;
            case Action::Close:out.close=true;shift=false;break;
        }
        return out;
    }
};

// The picture: one stone plaque per key (Reset Skills & Stats' face), lit or
// not, premultiplied RGBA, TexelWidth x TexelHeight. Labels (Label) are stamped
// over each key on Windows (transport.cpp, GDI), as on that button.
// With capitals the Shift key is lit in both, so the waiting Shift shows.
inline std::vector<uint8_t> PaintKeys(bool lit,bool capitals=false) {
    std::vector<uint8_t> px(size_t(TexelWidth)*TexelHeight*4,0);
    for(int i=0;i<KeyCount;++i) {
        const Rect r=KeyTexelRect(i);
        const auto face=PaintFace(r.w,r.h,lit||(capitals&&Keys[i].action==Action::Shift));
        for(int y=0;y<r.h;++y)
            std::copy_n(&face[size_t(y)*r.w*4],size_t(r.w)*4,&px[(size_t(r.y+y)*TexelWidth+r.x)*4]);
    }
    return px;
}
}

namespace framevr::keyboard {
// The four pictures (small letters, capitals, each plain and lit) stacked in one atlas, TexelWidth x 4*TexelHeight: picture p (lit = p & 1,
// capitals = p >= 2) at rows p*TexelHeight.. - every key on its stone with its label (transport.cpp SetupStatic's keyboard). Empty: GDI failed.
inline std::vector<uint8_t> PaintAtlas() {
    std::vector<uint8_t> atlas(size_t(TexelWidth)*TexelHeight*4*4,0);
    for(int picture=0;picture<4;++picture) {
        const bool lit=picture&1,capitals=picture>=2;
        auto board=PaintKeys(lit,capitals);
        for(int i=0;i<KeyCount;++i) {
            const auto r=KeyTexelRect(i);
            std::vector<uint8_t> face(size_t(r.w)*r.h*4);
            for(int y=0;y<r.h;++y)std::memcpy(&face[size_t(y)*r.w*4],&board[(size_t(r.y+y)*TexelWidth+r.x)*4],size_t(r.w)*4);
            const auto text=LabelCoverage(Label(i,capitals).c_str(),r.w,r.h);
            if(text.empty())return {};
            Stamp(face,text,r.w,r.h,lit||(capitals&&Keys[i].action==Action::Shift));
            for(int y=0;y<r.h;++y)std::memcpy(&board[(size_t(r.y+y)*TexelWidth+r.x)*4],&face[size_t(y)*r.w*4],size_t(r.w)*4);
        }
        std::memcpy(&atlas[size_t(picture)*TexelWidth*TexelHeight*4],board.data(),board.size());
    }
    return atlas;
}
// A key to the game as a real keyboard's would arrive: the key the game window's own Windows layout puts the character on, its scan
// code, Shift round it when the layout wants it, tagged KeyboardKeyTag - typing (transport.cpp SendKeyboard).
inline bool SendKey(const Output& out,HWND gameWindow) {
    const HKL layout=GetKeyboardLayout(gameWindow?GetWindowThreadProcessId(gameWindow,nullptr):0);
    WORD vk=0;bool shift=false;
    if(out.send==Send::Char) {
        const SHORT k=VkKeyScanExW(out.ch,layout);
        if(k==-1||(HIBYTE(k)&6))return false;   // not on this layout, or only with Ctrl / Alt
        vk=LOBYTE(k);shift=(HIBYTE(k)&1)!=0;
    } else if(out.send==Send::Backspace)vk=VK_BACK;
    else if(out.send==Send::Enter)vk=VK_RETURN;
    else return false;
    const WORD scan=WORD(MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC,layout)),shiftScan=WORD(MapVirtualKeyExW(VK_LSHIFT,MAPVK_VK_TO_VSC,layout));
    if(!scan||(shift&&!shiftScan))return false;
    INPUT in[4]{};UINT n=0;
    const auto key=[&](WORD code,bool up) {
        INPUT& i=in[n++];i.type=INPUT_KEYBOARD;i.ki.wScan=code;
        i.ki.dwFlags=KEYEVENTF_SCANCODE|(up?KEYEVENTF_KEYUP:0);i.ki.dwExtraInfo=KeyboardKeyTag;
    };
    if(shift)key(shiftScan,false);
    key(scan,false);key(scan,true);
    if(shift)key(shiftScan,true);
    return SendInput(n,in,sizeof(INPUT))==n;
}
}  // namespace framevr::keyboard
