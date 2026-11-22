#pragma once
// The town portal drawn once for both eyes: which particle asset is the portal, its instance and origin, its centre offsets.
// From bearstonem/d2r-pcvr vr/portal_billboard.h at 9c3e88e (2026-10-10), MIT, Copyright (c) 2026 bearstonem (the notice: LICENSE).
// Taken as it is; used by vr/billboards.cpp.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace billboards::portal {
struct Vec {float x=0,y=0,z=0;};
inline bool Finite(Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
// view.direction is camera-to-scene, not the surface normal. Keep a standing
// portal upright. Looking/rolling in place cannot change this direction.
inline bool Direction(Vec head,Vec origin,Vec& result) {
    if(!Finite(head)||!Finite(origin))return false;
    const float x=origin.x-head.x,z=origin.z-head.z,length=std::hypot(x,z);
    if(!std::isfinite(length)||length<.01f)return false;
    result={x/length,0,z/length};return true;
}
inline bool Asset(std::string_view path) {
    constexpr std::string_view prefix="data/hd/vfx/particles/objects/vfx_only/town_portal/";
    constexpr std::string_view suffix=".particles";
    if(path.size()<prefix.size()+suffix.size()||path.size()>159)return false;
    auto lower=[](char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):(c=='\\'?'/':c);};
    for(size_t i=0;i<prefix.size();++i)if(lower(path[i])!=prefix[i])return false;
    for(size_t i=0;i<suffix.size();++i)if(lower(path[path.size()-suffix.size()+i])!=suffix[i])return false;
    const auto file=path.substr(prefix.size());
    return file.find('/')==file.npos&&file.find('\\')==file.npos&&file.find("..") == file.npos;
}
// NewStuff's center-position scripts displace their mesh centers three units
// from the authored anchor (0,4,0). The return portal can retain a pitched
// offset: live input (-2.324593,3.434088,1.808851) has radius 2.999322 around
// that anchor, not horizontal radius 3 at height 4. Recognize the full 3D
// shell, including native small animation noise, then use one shared anchor.
// Keeping input Y would retain the very vertical separation being removed.
// Scope remains the observed asset and local-point transform; this is not a
// rule for arbitrary particles, other portal variants or ground effects.
inline bool CenterOffset(Vec local,Vec& corrected) {
    if(!Finite(local)||std::fabs(std::hypot(local.x,local.y-4.f,local.z)-3.f)>.025f)return false;
    corrected={0,4,0};return true;
}
inline bool CenterAsset(std::string_view path) {
    if(!Asset(path))return false;
    constexpr std::string_view file="vfx_town_portal_newstuff.particles";
    const auto tail=path.substr(path.size()-file.size());
    for(size_t i=0;i<file.size();++i) {
        char c=tail[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');
        if(c!=file[i])return false;
    }
    return true;
}
struct Context {uint64_t worker=0,scene=0,particles=0,indices=0;uint32_t count=0,unused=0;int32_t instance=-1;uint32_t padding=0;};
static_assert(sizeof(Context)==48);
struct Instance {uint64_t address=0,definition=0,transform=0;};
// Same segmented instance lookup as native effect.position (a91150), with
// bounded reads. SceneContext selects a constant instance or a per-lane ID.
template<class Read> bool Resolve(Read read,const Context& c,uint32_t lane,uint64_t base,Instance& out) {
    int32_t id=c.instance;
    if(id<0&&(lane>=c.count||!read(&id,c.indices+4ull*lane,sizeof id)))return false;
    if(id<0||id>=131072||!c.scene)return false;
    struct Pool {uint64_t pages,reserved;uint32_t count,capacity;} pool{};
    if(!read(&pool,c.scene+0x230,sizeof pool)||!pool.pages||!pool.count||pool.count>256||uint32_t(id)/512>=pool.count)return false;
    uint64_t page=0,instance=0;
    if(!read(&page,pool.pages+8ull*(uint32_t(id)/512),8)||!page||
       !read(&instance,page+8ull*(uint32_t(id)%512),8)||!instance)return false;
    uint64_t header[10]{};
    if(!read(header,instance,sizeof header)||header[0]!=base+0x1da1118||!header[8]||!header[9])return false;
    out={instance,header[8],header[9]};return true;
}
// One bounded per-batch cache, never a single classification for a mixed batch.
// Only lanes in the verified center shell need instance/asset resolution.
template<class Read,class Classify> bool CorrectCenters(Vec* points,uint32_t count,const Context& c,uint64_t base,Read read,Classify classify) {
    struct Entry {int32_t id=-1;bool offsets=false;};Entry cache[8];bool changed=false;
    for(uint32_t i=0;i<count;++i) {
        Vec corrected;if(!CenterOffset(points[i],corrected))continue;
        int32_t id=c.instance;
        if(id<0&&(i>=c.count||!read(&id,c.indices+4ull*i,4)))continue;
        if(id<0)continue;
        auto& entry=cache[uint32_t(id)%8];
        if(entry.id!=id) {
            Instance instance{};entry.id=id;
            entry.offsets=Resolve(read,c,i,base,instance)&&classify(instance.definition);
        }
        if(entry.offsets){points[i]=corrected;changed=true;}
    }
    return changed;
}
template<class Read> bool IsPortal(Read read,uint64_t definition,uint64_t base,bool centerOffsets=false) {
    uint64_t header[10]{},compiled[3]{};
    if(!read(header,definition,sizeof header)||header[0]!=base+0x1da0ff0||!header[9]||
       !read(compiled,header[9],sizeof compiled)||compiled[0]!=base+0x1da67f0||!compiled[2])return false;
    uint64_t stringType=0;char path[160]{};
    if(!read(&stringType,compiled[2],8)||stringType!=base+0x1d9d258||
       !read(path,compiled[2]+0x20,sizeof path))return false;
    const auto end=static_cast<const char*>(std::memchr(path,0,sizeof path));
    if(!end)return false;
    const std::string_view name{path,size_t(end-path)};
    return centerOffsets?CenterAsset(name):Asset(name);
}
template<class Read> bool Origin(Read read,const Instance& instance,Vec& out) {
    float m[16]{};
    if(!read(m,instance.transform,sizeof m))return false;
    for(float f:m)if(!std::isfinite(f))return false;
    if(std::fabs(m[3])>1e-5f||std::fabs(m[7])>1e-5f||std::fabs(m[11])>1e-5f||std::fabs(m[15]-1)>1e-5f)return false;
    out={m[12],m[13],m[14]};return true;
}
}
