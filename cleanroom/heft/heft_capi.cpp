// heft_bench.dll: the model and the bench's weapons and paths, for the Unity
// bench (tools/heft_bench_unity) to run side by side with its PhysX reference.
// Doubles throughout; poses are pos[3] then rot[4] as x, y, z, w.

#include "bench.h"
#include "heft.h"

#include <algorithm>

using namespace heft;
using namespace heft::bench;

namespace {

struct Sim {
	const Weapon* weapon = nullptr;
	Body          body;
	Grip          grips[2];
	int           n = 1;
	State         state;
	World         world;
	Controller    last[2];
	bool          hasLast = false;
};

V3 ToV3(const double* p) { return { p[0], p[1], p[2] }; }
Q  ToQ(const double* p) { return { p[0], p[1], p[2], p[3] }; }
void Out(V3 v, double* p) { p[0] = v.x; p[1] = v.y; p[2] = v.z; }
void Out(Q q, double* p) { p[0] = q.x; p[1] = q.y; p[2] = q.z; p[3] = q.w; }

}   // namespace

#define HEFT_API extern "C" __declspec(dllexport)

HEFT_API int heft_weapon_count() { return WeaponCount(); }
HEFT_API const char* heft_weapon_name(int w) { return WeaponAt(w).name; }
HEFT_API int heft_weapon_two_handed(int w) { return WeaponAt(w).twoHanded ? 1 : 0; }

// The steel alone: mass, centre of mass (grip frame), principal moments and the
// rotation of their axes (grip frame) - what a Rigidbody's inertiaTensor and
// inertiaTensorRotation take.
HEFT_API void heft_weapon_steel(int w, double* mass, double* com3, double* moments3, double* rot4) {
	const Body b = WeaponOnly(WeaponAt(w));
	V3 d;
	Q  r;
	Principal(b.inertia, &d, &r);
	*mass = b.mass;
	Out(b.com, com3);
	Out(d, moments3);
	Out(r, rot4);
}
HEFT_API void heft_weapon_points(int w, double* tip3, double* second3) {
	Out(WeaponAt(w).tip, tip3);
	Out(WeaponAt(w).second, second3);
}

HEFT_API int heft_path_count() { return PathCount(); }
HEFT_API const char* heft_path_name(int p) { return PathAt(p).name; }
HEFT_API double heft_path_duration(int p) { return PathAt(p).duration; }
HEFT_API int heft_path_two_hands(int p) { return PathAt(p).twoHands ? 1 : 0; }
HEFT_API void heft_controller(int p, int w, int hand, double t, double* pos3, double* rot4) {
	const Pose c = ControllerAt(p, WeaponAt(w), hand, t);
	Out(c.pos, pos3);
	Out(c.rot, rot4);
}

// six: spring, damper, maxForce, torqueSpring, torqueDamper, maxTorque.
HEFT_API void heft_muscle(int twoHands, double* six) {
	const Muscle m = twoHands ? TwoHandEach() : OneHand();
	const double v[6] = { m.spring, m.damper, m.maxForce, m.torqueSpring, m.torqueDamper, m.maxTorque };
	std::copy(v, v + 6, six);
}

HEFT_API void* heft_sim_new(int w, int twoHands, double substep) {
	Sim* s    = new Sim;
	s->weapon = &WeaponAt(w);
	s->n      = twoHands ? 2 : 1;
	s->body   = Held(*s->weapon, twoHands != 0);
	for (int i = 0; i < 2; ++i) s->grips[i].muscle = twoHands ? TwoHandEach() : OneHand();
	s->grips[1].at = s->weapon->second;
	if (substep > 0) s->world.substep = substep;
	s->world.glide = false;   // the reference sees its targets teleported at each step
	return s;
}

// poses: n * 7 (pos, rot); vels: n * 6 (vel, angVel) or null.
HEFT_API void heft_sim_step(void* sim, const double* poses, const double* vels, double dt) {
	Sim* s = static_cast<Sim*>(sim);
	Controller now[2];
	for (int i = 0; i < s->n; ++i) {
		now[i].pose = { ToV3(poses + i * 7), ToQ(poses + i * 7 + 3) };
		if (vels) {
			now[i].vel    = ToV3(vels + i * 6);
			now[i].angVel = ToV3(vels + i * 6 + 3);
		}
	}
	if (!s->hasLast) {
		for (int i = 0; i < s->n; ++i) s->last[i] = now[i];
		s->hasLast = true;
	}
	Step(s->state, s->body, s->grips, s->last, now, s->n, dt, s->world);
	for (int i = 0; i < s->n; ++i) s->last[i] = now[i];
}

HEFT_API void heft_sim_grip(void* sim, double* pos3, double* rot4) {
	const Sim* s = static_cast<const Sim*>(sim);
	const Pose g = GripPose(s->state, s->body);
	Out(g.pos, pos3);
	Out(g.rot, rot4);
}

HEFT_API void heft_sim_free(void* sim) { delete static_cast<Sim*>(sim); }
