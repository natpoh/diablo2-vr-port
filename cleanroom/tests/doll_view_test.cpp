// [weight] monster_ragdoll: the mod's side of the monsters' weight (vr/doll_view.h) - what is handed to the doll library and what is drawn of it.
//
// Checks: the head frame -> render world mapping is the inverse of monsterbodies::RenderToHead; the snapshot's bodies go to the library as the weapon meets
// them (a limb as thick as its mesh, a game capsule less the touch's margin, the humanoid's mark) and come back moved, nothing else changed; the debug view
// draws the push's arrow (as long as the push, only while fresh), each humanoid's weight bar (as long as its weight) and its balance ball in the colour of
// its balance, leaning with its sway, nothing for a monster that is no humanoid, and no more than it has room for.

#include "../../vr/doll_view.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int g_fails = 0;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

using monsterbodies::Body;
using monsterbodies::Snapshot;

float Len(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

void Limb(Snapshot& s, uint32_t id, int bone, float x, float y0, float y1, float r, bool doll) {
	Body& b = s.b[s.n++];
	b.id = id;
	b.bone = bone;
	b.rig = true;
	b.doll = doll;
	b.bottom[0] = b.top[0] = x;
	b.bottom[1] = y0;
	b.top[1] = y1;
	b.radius = r;
}

int Count(float (*b)[4], int n, int kind) {
	int c = 0;
	for (int i = 0; i < n; ++i) c += static_cast<int>(b[i][3] + 0.5f) == kind;
	return c;
}

void TestFrames() {
	monsterbodies::View v;
	v.eye[0] = 40.0f; v.eye[1] = 8.0f; v.eye[2] = -12.0f;
	v.upm = 4.0f;
	const float a = 0.7f;
	v.camR[0] = std::cos(a); v.camR[1] = 0.0f; v.camR[2] = std::sin(a);
	v.camF[0] = -std::sin(a); v.camF[1] = 0.0f; v.camF[2] = std::cos(a);
	const float head[3] = {0.3f, -0.4f, -0.9f};
	float render[3], back[3];
	dollview::HeadToRender(v, head, render);
	EXPECT(monsterbodies::RenderToHead(v, render, back), "RenderToHead failed");
	for (int k = 0; k < 3; ++k) EXPECT(std::fabs(back[k] - head[k]) < 1e-4f, "axis %d: %f -> %f", k, head[k], back[k]);
	float d[3];
	dollview::HeadDirToRender(v, head, d);
	EXPECT(std::fabs(Len(d) - Len(head)) < 1e-4f, "a direction keeps its length");
}

void TestToDoll() {
	Snapshot s;
	Limb(s, 1, 4, 0.0f, 0.9f, 1.5f, 0.15f, true);
	Body& own = s.b[s.n++];
	own.id = 2;
	own.rig = false;
	own.radius = 0.4f;
	own.top[1] = 1.2f;
	D2rdBody d[monsterbodies::kMax];
	const int n = dollview::ToDoll(s, d);
	EXPECT(n == 2 && d[0].id == 1 && d[0].bone == 4 && d[0].rig == 1 && d[0].doll == 1 && d[0].radius == 0.15f, "a humanoid's limb");
	EXPECT(d[1].rig == 0 && d[1].doll == 0 && d[1].bone == -1 && std::fabs(d[1].radius - monsterbodies::CoreRadius(0.4f)) < 1e-6f, "a game capsule, less the margin");
	d[0].a[2] = 0.05f;
	d[0].b[2] = 0.1f;
	Snapshot back = s;
	dollview::FromDoll(d, n, &back);
	EXPECT(back.b[0].bottom[2] == 0.05f && back.b[0].top[2] == 0.1f && back.b[0].radius == 0.15f && back.b[0].doll, "moved back, the rest kept");
	EXPECT(back.b[1].top[1] == 1.2f, "the other as it was");
	// [weight] npc_bodies: a town NPC (not hostile) is handed to the doll exactly as a monster is - it is pushed and bends the same (the author: «надо чтобы
	// удар и искажение модели под весом работало так же на нпс в городе»). Nothing on the doll's way may tell them apart.
	Snapshot npc = s;
	for (int i = 0; i < npc.n; ++i) npc.b[i].hostile = false;
	D2rdBody dn[monsterbodies::kMax];
	const int nn = dollview::ToDoll(npc, dn);
	D2rdBody dm[monsterbodies::kMax];
	dollview::ToDoll(s, dm);
	bool same = nn == n;
	for (int i = 0; same && i < n; ++i)
		same = dn[i].id == dm[i].id && dn[i].bone == dm[i].bone && dn[i].rig == dm[i].rig && dn[i].doll == dm[i].doll && dn[i].radius == dm[i].radius &&
		       dn[i].a[0] == dm[i].a[0] && dn[i].a[1] == dm[i].a[1] && dn[i].a[2] == dm[i].a[2] && dn[i].b[0] == dm[i].b[0] && dn[i].b[1] == dm[i].b[1] && dn[i].b[2] == dm[i].b[2];
	EXPECT(same, "an NPC's bodies go to the doll exactly as a monster's");
}

void TestView() {
	using meleeboxes::TouchKind;
	Snapshot s;
	s.upm = 4.0f;
	Limb(s, 1, 0, 0.0f, 0.9f, 1.5f, 0.15f, true);
	Limb(s, 1, 2, 0.0f, 0.0f, 0.85f, 0.07f, true);
	Limb(s, 2, 0, 3.0f, 0.0f, 0.5f, 0.2f, false);   // a beast
	dollview::Arrow arrows[2];
	arrows[0].at[1] = 1.4f;
	arrows[0].imp[2] = -4.0f;   // 4 N s: 20 cm
	arrows[0].ms = 1000;
	arrows[1] = arrows[0];
	arrows[1].ms = 100;         // too old
	const float eye[3] = {};
	float a[16][4], b[16][4];
	float balance = 0.1f, lean = 0.0f;
	const auto mass = [](uint32_t id) { return id == 1 ? 60.0f : 500.0f; };
	const auto bal = [&](uint32_t, float off[3]) { off[0] = lean; off[1] = off[2] = 0.0f; return balance; };
	int n = dollview::PackDebug(s, arrows, 2, 1200, eye, mass, bal, a, b, 16);
	const int tube = TouchKind(meleeboxes::kCAmber, meleeboxes::kTube), bar = TouchKind(meleeboxes::kCGrey, meleeboxes::kTube);
	EXPECT(n == 4, "an arrow and its ball, a bar and a ball for the humanoid, nothing for the beast: %d", n);
	EXPECT(Count(b, n, tube) == 1 && Count(b, n, bar) == 1 && Count(b, n, TouchKind(meleeboxes::kCGreen, meleeboxes::kBall)) == 1, "the kinds");
	for (int i = 0; i < n; ++i) {
		const int kind = static_cast<int>(b[i][3] + 0.5f);
		if (kind == tube) EXPECT(std::fabs((a[i][2] - b[i][2]) / 4.0f - 0.2f) < 1e-4f, "the arrow 20 cm long: %f", (a[i][2] - b[i][2]) / 4.0f);
		if (kind == bar) {
			EXPECT(std::fabs((b[i][1] - a[i][1]) / 4.0f - 60.0f * dollview::kBarMPerKg) < 1e-4f, "the bar as long as its weight");
			EXPECT(std::fabs(a[i][0]) < 1e-4f && a[i][1] / 4.0f > 1.5f, "over the humanoid's head");
		}
		EXPECT(a[i][3] > 0.0f && std::fabs(a[i][0] - b[i][0]) + std::fabs(a[i][1] - b[i][1]) + std::fabs(a[i][2] - b[i][2]) > 0.0f, "every capsule has a length (the effect's ray test)");
	}
	balance = 0.5f;
	n = dollview::PackDebug(s, arrows, 0, 1200, eye, mass, bal, a, b, 16);
	EXPECT(n == 2 && Count(b, n, TouchKind(meleeboxes::kCAmber, meleeboxes::kBall)) == 1, "swaying: amber");
	balance = 0.9f;
	lean = 0.1f;
	n = dollview::PackDebug(s, arrows, 0, 1200, eye, mass, bal, a, b, 16);
	bool leaning = false;
	for (int i = 0; i < n; ++i)
		if (static_cast<int>(b[i][3] + 0.5f) == TouchKind(meleeboxes::kCRed, meleeboxes::kBall)) leaning = std::fabs(a[i][0] / 4.0f - 0.3f) < 1e-4f;
	EXPECT(leaning, "at the edge: red, leaning three times its sway");
	EXPECT(dollview::PackDebug(s, arrows, 2, 1200, eye, mass, bal, a, b, 1) == 1, "no more than room for");
	Snapshot none;
	EXPECT(dollview::PackDebug(none, arrows, 2, 1200, eye, mass, bal, a, b, 16) == 0, "no upm: nothing");
}

}  // namespace

int main() {
	TestFrames();
	TestToDoll();
	TestView();
	std::printf("doll_view_test: %s (%d failed)\n", g_fails ? "FAIL" : "ok", g_fails);
	return g_fails ? 1 : 0;
}
