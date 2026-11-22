// [stereo] portal_once / portal_centers / particles_head_centre - see vr/billboards.h.
// From bearstonem/d2r-pcvr vr/billboards.cpp at 9c3e88e (2026-10-10), MIT, Copyright (c) 2026 bearstonem (the notice: LICENSE).
// Ported to D2R VR Authentic: their native-OpenXR memory reads and hook metrics left out (guarded reads here), the three parts switched
// by our ini keys, a log line when the game's bytes do not match. The logic is theirs, unchanged.
#include "billboards.h"
#include "billboard_signatures.h"
#include "portal_billboard.h"
#include "sigscan.h"
#include "aura_view.h"   // the one hook of view.direction, shared with [render] aura_centre
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace billboards {
namespace {
// PopcornFX Screen/View Quad::Align share (this, view context, strided batch).
// The view-position implementation uses context+0x30 and the engine's world
// up axis. It retains native size, rotation, UVs, normals and tangent generation.
using Align = void (*)(void*, const void*, void*);
Align original=nullptr,viewAligned=nullptr;
void (*logger)(const char*)=nullptr;
bool (*wanted)()=nullptr;
bool installed=false;
using ScriptView=void (*)(void*,const void*,const void*);
ScriptView originalDirection=nullptr,originalPosition=nullptr;
using LocalPosition=void (*)(void*,const void*,uint32_t,const void*);
LocalPosition originalLocalPosition=nullptr;
std::atomic<bool> portalReady{false},portalReported{false};
std::atomic<bool> portalEnabled{true},centerEnabled{true},particlesEnabled{true},centerReported{false};
bool directionShared=false;   // view.direction hooked by vr/aura_view.cpp, which runs our correction (AfterDirection)
bool portalMissTold=false,centerMissTold=false,quadMissTold=false;
uintptr_t gameBase=0;
SRWLOCK centerLock=SRWLOCK_INIT;
float center[3]{};ULONGLONG centerAt=0;
std::atomic<bool> reported{false};
bool Read(void* to,const void* from,size_t bytes) {
    __try { std::memcpy(to,from,bytes);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Write(void* to,const void* from,size_t bytes) {
    __try { std::memcpy(to,from,bytes);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadAt(void* to,uint64_t address,size_t bytes) {
    return address>=0x10000&&address<0x0000800000000000ull&&
        bytes<0x10000&&Read(to,reinterpret_cast<const void*>(address),bytes);
}
bool Head(portal::Vec& eye) {
    ULONGLONG at;
    AcquireSRWLockShared(&centerLock);std::memcpy(&eye,center,sizeof eye);at=centerAt;ReleaseSRWLockShared(&centerLock);
    return at&&GetTickCount64()-at<=250&&portal::Finite(eye);
}
bool PortalDefinition(uint64_t definition,bool offsets=false) {
    // Script callbacks run on worker threads. Keep a small, bounded cache of
    // classifications; no allocations or asset reads in the rendering path.
    struct Entry {uint64_t definition=0;ULONGLONG at=0;bool portal=false,offsets=false;};
    thread_local Entry cache[32];
    auto& e=cache[(definition>>6)%32];const auto now=GetTickCount64();
    if(e.definition!=definition||now-e.at>1000) {
        const bool portal=portal::IsPortal(ReadAt,definition,gameBase);
        e={definition,now,portal,portal&&portal::IsPortal(ReadAt,definition,gameBase,true)};
    }
    return offsets?e.offsets:e.portal;
}
void CenterPosition(void* output,const void* input,uint32_t mask,const void* worker) {
    portal::Vec head;
    // 19 is the observed native local-point -> world-point path. Keep all
    // other conversions and non-center particle positions exactly native.
    if(mask!=19||!portalReady.load()||!portalEnabled.load()||!centerEnabled.load()||!wanted||!wanted()||!Head(head)) {
        originalLocalPosition(output,input,mask,worker);return;
    }
    struct Stream {uint64_t data;uint32_t count,stride;};
    Stream out{},in{};portal::Context c{};portal::Vec points[64];
    if(!Read(&out,output,sizeof out)||!Read(&in,input,sizeof in)||
       !ReadAt(&c,reinterpret_cast<uint64_t>(worker)+0x58,sizeof c)||
       c.worker!=reinterpret_cast<uint64_t>(worker)||!out.data||!in.data||
       !out.count||out.count>64||out.stride!=12||
       (in.stride!=0&&in.stride!=12)||!in.count||
       (in.stride&&in.count<out.count)) {
        originalLocalPosition(output,input,mask,worker);return;
    }
    // Read a constant stream once, or one contiguous batch. Mixed instances
    // still require per-instance classification; never assume the whole batch
    // belongs to a portal just because one lane does.
    if(!ReadAt(points,in.data,in.stride?out.count*sizeof points[0]:sizeof points[0])) {
        originalLocalPosition(output,input,mask,worker);return;
    }
    if(!in.stride)for(uint32_t i=1;i<out.count;++i)points[i]=points[0];
    const bool changed=portal::CorrectCenters(points,out.count,c,gameBase,ReadAt,
        [](uint64_t definition){return PortalDefinition(definition,true);});
    if(!changed){originalLocalPosition(output,input,mask,worker);return;}
    // A private input stream keeps the native transform/interpolation path and
    // never edits shared VM inputs. Resolve separately for mixed-instance lanes.
    const Stream local{reinterpret_cast<uint64_t>(points),out.count,12};
    originalLocalPosition(output,&local,mask,worker);
    if(!centerReported.exchange(true)&&logger)
        logger("vrcam: town portal - its centre offsets put on one anchor for both eyes (the game's local-to-world transform kept) [stereo] portal_centers; after bearstonem/d2r-pcvr, MIT");
}
void CorrectScriptView(void* output,const void* input,const void* context,bool direction) {
    if(!portalReady.load()||!portalEnabled.load()||!wanted||!wanted())return;
    portal::Vec head;
    if(!Head(head))return;
    struct Stream {uint64_t data;uint32_t count,stride;};
    Stream out{},in{};portal::Context c{};
    if(!Read(&out,output,sizeof out)||!Read(&in,input,sizeof in)||!Read(&c,context,sizeof c)||
       !out.data||!in.data||!out.count||out.count>1024||out.stride!=12||
       in.count!=out.count||(in.stride!=0&&in.stride!=4))return;
    struct Views {uint64_t data;uint32_t count,capacity;} views{};
    if(!ReadAt(&views,c.scene+0x310,sizeof views)||!views.data||!views.count||views.count>8)return;
    // Native callbacks use tightly packed float3 output, including constant
    // inputs (stride zero). Resolve each effect independently in mixed batches.
    int32_t constantView=-1;
    if(!in.stride&&!ReadAt(&constantView,in.data,4))return;
    int32_t lastView=-1;bool cameraMatches=false;
    int32_t lastInstance=-1;portal::Instance instance{};portal::Vec value{};bool valid=false;
    for(uint32_t i=0;i<out.count;++i) {
        int32_t view=constantView,id=c.instance;
        if((in.stride&&!ReadAt(&view,in.data+uint64_t(i)*in.stride,4))||view<0||uint32_t(view)>=views.count)continue;
        if(view!=lastView) {
            portal::Vec nativeEye{};lastView=view;cameraMatches=false;
            if(ReadAt(&nativeEye,views.data+uint64_t(view)*0x1f8+0x70,sizeof nativeEye)) {
                const float x=nativeEye.x-head.x,y=nativeEye.y-head.y,z=nativeEye.z-head.z;
                const float distance=x*x+y*y+z*z;
                cameraMatches=std::isfinite(distance)&&distance<=4.f;
            }
        }
        if(!cameraMatches)continue;
        if(id<0&&(i>=c.count||!ReadAt(&id,c.indices+4ull*i,4)))continue;
        if(id<0)continue;
        if(id!=lastInstance) {
            lastInstance=id;valid=false;
            portal::Vec origin{};
            if(portal::Resolve(ReadAt,c,i,gameBase,instance)&&PortalDefinition(instance.definition)&&
               portal::Origin(ReadAt,instance,origin)&&portal::Direction(head,origin,value)) {
                if(!direction)value=head;
                valid=true;
            }
        }
        if(!valid)continue;
        if(!Write(reinterpret_cast<void*>(out.data+12ull*i),&value,sizeof value))continue;
        if(direction&&!portalReported.exchange(true)&&logger)
            logger("vrcam: town portal - its meshes face the head's centre, upright toward the portal's origin, in both eyes [stereo] portal_once; after bearstonem/d2r-pcvr, MIT");
    }
}
// One inline hook of view.direction serves both this and [render] aura_centre (vr/aura_view.cpp): the game's answer, then the aura rings' correction
// (the Paladin's overlay assets), then the portal's (town-portal assets) - disjoint lanes. 2026-11-22, test 0.221.8: this hook went in first and the
// aura's could not, so the rings were back beside the hero, one per eye.
void ViewDirection(void* out,const void* in,const void* context) {
    originalDirection(out,in,context);
    auraview::AfterDirection(out,in,context);
    CorrectScriptView(out,in,context,true);
}
void ViewPosition(void* out,const void* in,const void* context) {
    originalPosition(out,in,context);CorrectScriptView(out,in,context,false);
}
void ScreenAlign(void* self,const void* view,void* batch) {
    if(!particlesEnabled.load()||!wanted||!wanted()||!viewAligned||!view||!batch){original(self,view,batch);return;}
    alignas(16) float local[16];float eye[3];ULONGLONG at;
    AcquireSRWLockShared(&centerLock);std::memcpy(eye,center,sizeof eye);at=centerAt;ReleaseSRWLockShared(&centerLock);
    if(!at||GetTickCount64()-at>250||!Read(local,view,sizeof local)){original(self,view,batch);return;}
    float distance=0;
    for(int i=0;i<3;++i){const float d=local[12+i]-eye[i];distance+=d*d;}
    // Verify the native context is this VR camera before changing its position;
    // other views (shadow/UI/etc.) keep their native screen-aligned behavior.
    if(!std::isfinite(distance)||distance>4.f){original(self,view,batch);return;}
    std::memcpy(local+12,eye,sizeof eye);
    viewAligned(self,local,batch);
    if(!reported.exchange(true)&&logger)logger("vrcam: particles - screen-facing quads face the head's centre in both eyes (the game's world up and animation kept) [stereo] particles_head_centre; after bearstonem/d2r-pcvr, MIT");
}
}
void SetCenter(const float position[3]) {
    AcquireSRWLockExclusive(&centerLock);
    std::memcpy(center,position,sizeof center);centerAt=GetTickCount64();
    ReleaseSRWLockExclusive(&centerLock);
}
bool DirectionHooked() {return originalDirection!=nullptr;}
void AfterDirection(void* out,const void* in,const void* context) {
    if(!directionShared||!out||!context)return;
    CorrectScriptView(out,in,context,true);
}
void ConfigurePortal(bool enabled) {portalEnabled.store(enabled);}
void ConfigurePortalCenters(bool enabled) {centerEnabled.store(enabled);}
void ConfigureParticles(bool enabled) {particlesEnabled.store(enabled);}
void Install(void (*log)(const char*),bool (*active)()) {
    if(installed&&portalReady.load()&&originalLocalPosition)return;logger=log;wanted=active;
    using namespace signatures;
    // asked every tick: a miss is told only after 30 s (the game's code pages may still be decrypting at the start)
    static const ULONGLONG firstAt=GetTickCount64();
    const bool tellMiss=GetTickCount64()-firstAt>30000;
    if(!portalReady.load()) {
        if((originalDirection||directionShared||d2rsig::Check(DirectionRva,DirectionSig,sizeof DirectionSig))&&
           (originalPosition||d2rsig::Check(PositionRva,PositionSig,sizeof PositionSig))&&
           d2rsig::Check(DirectionViewsRva,DirectionViewsSig,sizeof DirectionViewsSig)&&
           d2rsig::Check(InstanceLookupRva,InstanceLookupSig,sizeof InstanceLookupSig)&&
           d2rsig::Check(InstancePositionRva,InstancePositionSig,sizeof InstancePositionSig)) {
            gameBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if(!originalDirection&&!directionShared&&
               !d2rsig::Hook(DirectionRva,DirectionSig,sizeof DirectionSig,reinterpret_cast<void*>(&ViewDirection),reinterpret_cast<void**>(&originalDirection))&&
               auraview::DirectionHooked()) {
                directionShared=true;   // the aura's hook has it and calls AfterDirection
                if(logger)logger("vrcam: town portal - view.direction is hooked by the aura code ([render] aura_centre), which runs the portal's correction too");
            }
            if(!originalPosition)d2rsig::Hook(PositionRva,PositionSig,sizeof PositionSig,reinterpret_cast<void*>(&ViewPosition),reinterpret_cast<void**>(&originalPosition));
            if((originalDirection||directionShared)&&originalPosition) {
                portalReady.store(true);
                if(logger)logger("vrcam: town portal - the particle scripts' view hooks in (town-portal assets only) [stereo] portal_once");
            }
        } else if(tellMiss&&!portalMissTold) {
            portalMissTold=true;
            if(logger)logger("vrcam: town portal - the particle scripts' view code NOT found (another build?) - the portal may stand apart in the two eyes");
        }
    }
    if(portalReady.load()&&!originalLocalPosition) {
        if(d2rsig::Check(LocalPositionContextRva,LocalPositionContextSig,sizeof LocalPositionContextSig)&&
           d2rsig::Hook(LocalPositionRva,LocalPositionSig,sizeof LocalPositionSig,reinterpret_cast<void*>(&CenterPosition),reinterpret_cast<void**>(&originalLocalPosition))) {
            if(logger)logger("vrcam: town portal - the centre-position hook in (its offset centres only) [stereo] portal_centers");
        } else if(tellMiss&&!centerMissTold) {
            centerMissTold=true;
            if(logger)logger("vrcam: town portal - the centre-position code NOT found (another build?) - its centre offsets stay the game's");
        }
    }
    if(installed)return;
    if(!d2rsig::Check(ScreenRva,ScreenSig,sizeof ScreenSig)||
       !d2rsig::Check(ViewRva,ViewSig,sizeof ViewSig)||
       !d2rsig::Check(ViewPositionRva,ViewPositionSig,sizeof ViewPositionSig)||
       !d2rsig::Check(ViewRotateRva,ViewRotateSig,sizeof ViewRotateSig)||
       !d2rsig::Check(ScreenBatchRva,ScreenBatchSig,sizeof ScreenBatchSig)) {
        if(tellMiss&&!quadMissTold){quadMissTold=true;if(logger)logger("vrcam: particles - the screen-facing quads' code NOT found (another build?) - they face each eye as the game has them");}
        return;
    }
    viewAligned=reinterpret_cast<Align>(d2rsig::Addr(ViewRva));
    installed=d2rsig::Hook(ScreenRva,ScreenSig,sizeof ScreenSig,reinterpret_cast<void*>(&ScreenAlign),reinterpret_cast<void**>(&original));
    if(installed&&logger)logger("vrcam: particles - the screen-facing quads' hook in [stereo] particles_head_centre");
}
}
