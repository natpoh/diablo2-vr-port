// What this build of the mod holds.
//
// D2RVR_FIRST_PERSON: first person - the hero's arms and hands on the controllers,
// the weapons held in them, the body that follows yours (VR F4) and the flat mouse-
// and-keyboard first person (flat F3). Our own build has it; the public one is cut
// by tools/make_public.py, which drops every "#if D2RVR_FIRST_PERSON" block, and the
// settings program shows F4 as coming soon.
#pragma once
#ifndef D2RVR_FIRST_PERSON
#define D2RVR_FIRST_PERSON 1
#endif
