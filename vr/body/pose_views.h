// pose_views.h - in which views our writers may touch a pose the game drew (header only, no game code; cleanroom/tests/pose_views_test.cpp).
//
// VR F1 (from above) and F3 (the floor) are the game's own, whole: no pose of ours on the hero, his gear or anyone (Serg, 0.192.35: "in F1 and F3 don't
// freeze the doll at all"; 0.192.41: "in F1 and F3 the doll still freezes"). Every writer asks here - the update thread decides it each tick from the view
// the game shows now (ViewNow: VR the F-key's view, flat the flat view) and hands it to the pose thread (skel::SetPoseView) and the skinning's hook
// (headshadow::SetPoseView). F2 and F4 as before; flat as before, but for the legs (legs_rear: off in view 1 and 3 on a monitor too, since 0.192.36).
#pragma once

namespace poseviews {

enum Writer {
    kHeroBody,      // PoseBody: the arms on the controllers, the body turned to the look, the head hidden, the trunk held, the crouch, doll_feet, the gait legs
    kHeroLegs,      // [body] legs_rear and all it carries: the hold, the play-back, the side steps, legs_turn
    kOurPoseCopy,   // headshadow::PublishOurPose: the skinning handed our finished pose
    kHeroGear,      // the hero's bow rig, flail chain, hanging head (PoseBowRig, PoseFlail, PoseHead)
    kMonsterDoll,   // [weight] monster_ragdoll_pose / knock_down: a struck monster's pose, written or handed to the skinning
    kRemoteHero,    // [multiplayer] pose_share: the other heroes posed from their poses, their legs stepped
    kWriters
};

// VR F1 / F3: nothing of ours on any pose
inline bool Vanilla(int platform, int view) { return platform == 1 && (view == 1 || view == 3); }

inline bool Allowed(Writer w, int platform, int view) {
    if (Vanilla(platform, view)) return false;
    if (w == kHeroLegs) return view != 1 && view != 3;
    return w >= 0 && w < kWriters;
}

// Any writer at all in this view (the pose thread's and the skinning's gate: all but the legs, which keep their own on a monitor)
inline bool AnyAllowed(int platform, int view) { return !Vanilla(platform, view); }

}  // namespace poseviews
