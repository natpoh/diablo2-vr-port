// The monster's plate and the item labels on the ground, kept apart in the interface layer's effect (vr/plate_look.h).
// Serg, test-56, 2026-10-17: "the settings of the items on the ground change the bars over the monsters the same way, the Name over a monster ones do not".
// Both directions: no item-label setting moves, fades or sizes the plate; no plate setting moves, fades or sizes an item label.
//
//   plate_look_test        exit code 0 = every case passed

#include "../../vr/plate_look.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int g_fails = 0;
int g_checks = 0;
#define CHECK(c)                                               \
	do {                                                       \
		++g_checks;                                            \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
bool Same(const platelook::Pick& a, const platelook::Pick& b) {
	return a.kind == b.kind && a.plate == b.plate && (a.kind == platelook::Kind::Empty || (Near(a.u, b.u) && Near(a.v, b.v)));
}
}  // namespace

int main() {
	using namespace platelook;

	// ---- the settings each side reads ----
	{
		CHECK(Near(Shift(1.6f), 0.008f) && Near(Shift(-20.0f), -0.1f) && Near(Shift(0.0f), 0.0f));
		// the plate: its own look from its own settings; first person only has an own depth
		PlateIn pin;
		pin.firstPerson = true; pin.depthPercent = 1.6f; pin.alpha = 0.25f; pin.size = 0.8f;
		const PlateUniforms f4 = Plate(pin);
		CHECK(f4.own && Near(f4.shift, 0.008f) && Near(f4.alpha, 0.25f) && Near(f4.scale, 0.8f));
		PlateIn flat;
		flat.depthPercent = 1.6f;
		const PlateUniforms other = Plate(flat);
		CHECK(!other.own && Near(other.shift, 0.0f) && Near(other.scale, 1.0f));   // elsewhere it stays on the labels' plane
		// the labels: as vrcam has always set them (depth when tilted, the tilt on the tilted plane, the floor's fade while the game does not fade the boxes)
		LabelUniforms l = Labels(true, 1.6f, false, 3.0f, false, false, 0.5f);
		CHECK(Near(l.base, 0.008f) && Near(l.tilt, 0.0f) && Near(l.alpha, 1.0f));          // F4: the depth, no tilt, never faded as a picture
		l = Labels(true, 2.0f, true, 3.0f, false, false, 0.5f);
		CHECK(Near(l.base, 0.01f) && Near(l.tilt, 0.015f) && Near(l.alpha, 1.0f));          // from above: the depth and the tilt
		l = Labels(false, 2.0f, true, 3.0f, true, false, 0.5f);
		CHECK(Near(l.base, 0.0f) && Near(l.tilt, 0.0f) && Near(l.alpha, 0.5f));             // the floor: only faded
		l = Labels(false, 2.0f, true, 3.0f, true, true, 0.5f);
		CHECK(Near(l.alpha, 1.0f));                                                         // ... unless the game's own code fades the boxes

		// the one place the two meet: does the effect need the plate's box. Its look (alpha, scale, shift) is the plate's alone either way.
		CHECK(!PlateNeedsBox(true, 1.6f, 1.6f, 1.0f, 1.0f));      // same depth, nothing of its own: the plate goes with the layer, no box needed
		CHECK(PlateNeedsBox(true, 0.0f, 1.6f, 1.0f, 1.0f));       // its own depth in first person
		CHECK(!PlateNeedsBox(false, 0.0f, 1.6f, 1.0f, 1.0f));     // elsewhere the depth is not its own
		CHECK(PlateNeedsBox(false, 1.6f, 1.6f, 0.10f, 1.0f));     // faded
		CHECK(PlateNeedsBox(false, 1.6f, 1.6f, 1.0f, 0.8f));      // sized
		CHECK(!PlateNeedsBox(true, 1.6f, 1.6f, 0.996f, 1.003f));  // (a rounding is not a setting)
	}

	// ---- the item labels' other two rules, for the monster's plate (Serg: "where are the same sliders - near, far - for the monsters' bar?") ----
	{
		// size far away: the labels_far rule as it always was - 1.5 m and nearer all of the size, 10 m and farther this share, a straight line between
		auto oldRule = [](float m, float far) { const float t = std::fmin(std::fmax((m - 1.5f) / (10.0f - 1.5f), 0.0f), 1.0f); return 1.0f + (far - 1.0f) * t; };
		bool same = true;
		for (float m = 0.0f; m < 30.0f; m += 0.25f)
			for (float far : {1.0f, 0.8f, 0.4f, 0.2f}) if (!Near(DistanceK(m, far), oldRule(m, far))) same = false;
		CHECK(same);
		CHECK(Near(DistanceK(0.0f, 0.4f), 1.0f) && Near(DistanceK(1.5f, 0.4f), 1.0f) && Near(DistanceK(10.0f, 0.4f), 0.4f) && Near(DistanceK(50.0f, 0.4f), 0.4f));
		CHECK(Near(DistanceK(5.75f, 0.4f), 0.7f) && Near(DistanceK(7.0f, 1.0f), 1.0f));   // halfway, and 100 % = no change at any distance

		// at its monster: the labels_depth_body formula as it always was, k * 0.5 * M0 * half * (1/z - 1/conv), z = max(1, hypot(ground, eye height))
		auto oldShift = [](float ground, float eyeH, float m0, float half, float conv, float k) {
			const float z = std::fmax(1.0f, std::sqrt(ground * ground + eyeH * eyeH));
			return k * 0.5f * m0 * half * (1.0f / z - (conv > 0.0f ? 1.0f / conv : 0.0f));
		};
		bool sameShift = true;
		for (float g : {0.0f, 50.0f, 300.0f, 1200.0f})
			for (float conv : {0.0f, 900.0f})
				for (float k : {0.0f, 0.5f, 1.0f, 1.5f}) if (!Near(DepthShift(g, 270.0f, 1.2f, 0.06f, conv, k), oldShift(g, 270.0f, 1.2f, 0.06f, conv, k), 1e-7f)) sameShift = false;
		CHECK(sameShift);
		CHECK(Near(DepthShift(500.0f, 270.0f, 1.2f, 0.06f, 0.0f, 0.0f), 0.0f));       // 0 % = where the game leaves it
		CHECK(DepthShift(300.0f, 270.0f, 1.2f, 0.06f, 0.0f, 1.0f) > DepthShift(900.0f, 270.0f, 1.2f, 0.06f, 0.0f, 1.0f));   // nearer = more shift

		// the plate: with no target seen nothing of the two applies; with one, its distance sets the size and, in stereo in first person, its depth
		PlateIn in;
		in.firstPerson = true; in.size = 1.0f; in.farFraction = 0.4f; in.atMonster = 1.0f;
		in.stereo = true; in.eyeHeightUnits = 270.0f; in.m0 = 1.2f; in.halfGap = 0.06f; in.conv = 0.0f;
		PlateUniforms u = Plate(in);
		CHECK(Near(u.scale, 1.0f) && Near(u.shift, 0.0f));                                  // nobody targeted (groundM -1): the plate as it was
		in.groundM = 12.0f; in.groundUnits = 36.0f * 3.0f;
		u = Plate(in);
		CHECK(Near(u.scale, 0.4f) && u.shift > 0.0f && Near(u.shift, DepthShift(108.0f, 270.0f, 1.2f, 0.06f, 0.0f, 1.0f)));   // 12 m off: 40 % of the size, set at its depth
		in.size = 0.8f;
		CHECK(Near(Plate(in).scale, 0.32f));                                                // the size above times the far rule
		in.groundM = 1.0f;
		CHECK(Near(Plate(in).scale, 0.8f));                                                 // near: the size above
		in.farFraction = 1.0f; in.atMonster = 0.0f; in.groundM = 12.0f; in.depthPercent = 1.6f;
		u = Plate(in);
		CHECK(Near(u.scale, 0.8f) && Near(u.shift, 0.008f));                                // defaults (100 %, 0 %): today's look exactly
		in.atMonster = 1.0f; in.stereo = false;
		CHECK(Near(Plate(in).shift, 0.008f));                                               // not a stereo picture: no depth rule
		in.stereo = true; in.firstPerson = false;
		CHECK(Near(Plate(in).shift, 0.0f) && !Plate(in).own);                               // other views: the plate goes with the labels' plane

		// the box is needed for them too (the plate's pixels are told from the labels')
		CHECK(PlateNeedsBox(true, 1.6f, 1.6f, 1.0f, 1.0f, 0.4f, 0.0f));
		CHECK(PlateNeedsBox(true, 1.6f, 1.6f, 1.0f, 1.0f, 1.0f, 1.0f));
		CHECK(!PlateNeedsBox(true, 1.6f, 1.6f, 1.0f, 1.0f, 1.0f, 0.0f));
		CHECK(!PlateNeedsBox(false, 1.6f, 1.6f, 1.0f, 1.0f, 1.0f, 1.0f));                   // the depth is first person's
	}

	// ---- the effect's pixel, both directions ----
	{
		const float box[4] = {0.40f, 0.20f, 0.60f, 0.26f};   // the plate's box, uv
		const float empty[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		const float eyes[2] = {+1.0f, -1.0f};
		const float labelBases[] = {0.0f, 0.008f, 0.03f, -0.05f, 0.1f};
		const float labelTilts[] = {0.0f, 0.02f};
		const float plateShifts[] = {0.0f, 0.008f, 0.04f, -0.06f};
		const float plateScales[] = {0.5f, 1.0f, 1.25f, 2.0f};

		// 1. THE PLATE does not follow the item labels: each pixel of the plate on screen reads the same layer pixel whatever the labels' depth and tilt are
		bool plateMoved = false, plateSeen = false;
		for (const float eye : eyes)
			for (const float shift : plateShifts)
				for (const float scale : plateScales) {
					// scan the screen for the pixels that show the plate with the first label setting, and hold them against every other
					for (float v = 0.0f; v < 1.0f; v += 1.0f / 256.0f)
						for (float u = 0.0f; u < 1.0f; u += 1.0f / 256.0f) {
							const Pick ref = LayerPixel(u, v, eye, labelBases[0], labelTilts[0], true, shift, scale, box);
							if (!ref.plate) continue;
							plateSeen = true;
							for (const float b : labelBases)
								for (const float t : labelTilts)
									if (!Same(ref, LayerPixel(u, v, eye, b, t, true, shift, scale, box))) plateMoved = true;
						}
				}
		CHECK(plateSeen);
		CHECK(!plateMoved);

		// 2. THE ITEM LABELS do not follow the plate's settings: a pixel that is not the plate's reads the label layer where the labels' shift puts it
		//    - the plate's depth, size and fade do not change that (only whether the plate's old place is left empty, which is the box's, not a setting's)
		bool labelMoved = false, labelSeen = false;
		for (const float eye : eyes)
			for (const float b : labelBases)
				for (const float t : labelTilts)
					for (float v = 0.0f; v < 1.0f; v += 1.0f / 128.0f)
						for (float u = 0.0f; u < 1.0f; u += 1.0f / 128.0f) {
							const Pick ref = LayerPixel(u, v, eye, b, t, true, plateShifts[0], plateScales[1], empty);   // no plate on screen: the labels alone
							if (ref.kind != Kind::Layer || ref.plate) continue;
							labelSeen = true;
							// the labels' pixel is where GameLabels' src puts it: u - eye * (base + tilt * v)
							if (!Near(ref.u, u - eye * (b + t * v)) || !Near(ref.v, v)) labelMoved = true;
							for (const float shift : plateShifts)
								for (const float scale : plateScales) {
									const Pick p = LayerPixel(u, v, eye, b, t, true, shift, scale, box);
									if (p.plate) continue;   // (this pixel shows the plate; its own)
									if (p.kind == Kind::Layer && !Same(ref, p)) labelMoved = true;   // a label pixel never moves with the plate's settings
								}
						}
		CHECK(labelSeen);
		CHECK(!labelMoved);

		// 3. with no depth of its own the plate goes with the layer (the other views), as it always did
		{
			bool same = true;
			for (const float eye : eyes)
				for (float v = 0.1f; v < 0.4f; v += 0.01f)
					for (float u = 0.3f; u < 0.7f; u += 0.01f) {
						const Pick a = LayerPixel(u, v, eye, 0.03f, 0.0f, false, 0.2f, 1.0f, box);
						if (!Near(a.u, u - eye * 0.03f) || a.plate) same = false;
					}
			CHECK(same);
		}
		// 4. no jump when the depths are equal and the plate is not sized: the plate's own depth equal to the labels' shows exactly what the layer shows
		{
			bool same = true;
			for (const float eye : eyes)
				for (float v = 0.1f; v < 0.4f; v += 0.005f)
					for (float u = 0.3f; u < 0.7f; u += 0.005f) {
						const Pick own = LayerPixel(u, v, eye, 0.008f, 0.0f, true, 0.008f, 1.0f, box);
						const Pick shared = LayerPixel(u, v, eye, 0.008f, 0.0f, false, 0.008f, 1.0f, box);
						if (own.kind == Kind::Layer && shared.kind == Kind::Layer && !(Near(own.u, shared.u) && Near(own.v, shared.v))) same = false;
					}
			CHECK(same);
		}
		// 5. the plate's old place on the labels' plane is left empty once the plate stands elsewhere (own depth differs): no ghost of it with the labels
		{
			const float eye = 1.0f;
			const float mid_v = 0.5f * (box[1] + box[3]);
			// the label-plane pixel that falls in the box: u such that u - eye * base is the box's middle
			const float u = 0.5f * (box[0] + box[2]) + 0.03f;
			const Pick ghost = LayerPixel(u, mid_v, eye, 0.03f, 0.0f, true, 0.2f, 1.0f, box);   // the plate stands 0.2 away from there (its own depth)
			CHECK(ghost.kind == Kind::Empty);
		}
		// 6. a size about the plate's middle: the plate's centre pixel stays put
		{
			const float cu = 0.5f * (box[0] + box[2]), cv = 0.5f * (box[1] + box[3]);
			const Pick c = LayerPixel(cu, cv, 1.0f, 0.03f, 0.0f, true, 0.0f, 2.0f, box);
			CHECK(c.plate && Near(c.u, cu) && Near(c.v, cv));
			const Pick edge = LayerPixel(cu + 0.15f, cv, 1.0f, 0.0f, 0.0f, true, 0.0f, 2.0f, box);   // twice as big: this pixel is the plate's edge-half
			CHECK(edge.plate && Near(edge.u, cu + 0.075f));
		}
	}

	// ---- the item-label hooks are for items ----
	{
		struct Unit { unsigned type; };
		Unit item{4}, monster{1}, object{2}, player{0}, missile{3};
		auto read = [](const unsigned char* at, unsigned& v) { std::memcpy(&v, at, sizeof v); return true; };
		CHECK(IsLabelOfItem(&item, read));
		CHECK(!IsLabelOfItem(&monster, read) && !IsLabelOfItem(&object, read) && !IsLabelOfItem(&player, read) && !IsLabelOfItem(&missile, read));
		CHECK(IsLabelOfItem(nullptr, read));                                                        // no unit: as it always was
		auto bad = [](const unsigned char*, unsigned&) { return false; };
		CHECK(IsLabelOfItem(&monster, bad));                                                        // not readable: counts as an item, as it always did
	}

	// ---- eyes by turns: the target's plate the same in each eye every frame (the doubled plate over a near NPC, 2026-10-26) ----
	// The game lays the layer out for the eye drawn: the plate's middle at mid + h for the left eye, mid - h for the right (h its half-disparity).
	{
		const float h = platelook::TurnsHalf(300.0f, 120.0f, 0.9f, 32.0f, 0.0f);   // a target about 3 m off (game units), native (no convergence)
		CHECK(h > 0.0f && Near(h, platelook::DepthShift(300.0f, 120.0f, 0.9f, 32.0f, 0.0f, 1.0f)));
		CHECK(platelook::TurnsHalf(300.0f, 120.0f, 0.0f, 32.0f, 0.0f) == 0.0f);   // no stereo: nothing
		const float mid = 0.5f, v = 0.3f;
		for (float S : {h, 0.0f, h + 0.01f}) {   // the plate's shift: at its target's depth, where the game leaves it, nearer still
			float shown[2][2], plain[2][2];       // [eye drawn: 0 left, 1 right][eye shown]: where its middle is shown
			for (int d = 0; d < 2; ++d) {
				const float sd = d == 0 ? 1.0f : -1.0f;
				const float c = mid + sd * h;   // this frame's layout: the drawn eye's own view of the target
				const float box[4] = {c - 0.05f, v - 0.02f, c + 0.05f, v + 0.02f};
				for (int e = 0; e < 2; ++e) {
					const float se = e == 0 ? 1.0f : -1.0f;
					const float u = c + (se * S - sd * h);   // the effect, eyes by turns (layerEye = the eye drawn)
					const platelook::Pick p = platelook::LayerPixel(u, v, se, 0.0f, 0.0f, true, S, 1.0f, box, sd, h);
					CHECK(p.kind == platelook::Kind::Layer && p.plate && Near(p.u, c) && Near(p.v, v));   // that pixel shows the plate's middle
					shown[d][e] = u;
					plain[d][e] = c + se * S;   // before: the same shifts whichever eye laid the layer out
				}
			}
			CHECK(Near(shown[0][0], shown[1][0]) && Near(shown[0][1], shown[1][1]));   // each eye: the same place every frame
			CHECK(Near(shown[0][0] - shown[0][1], 2.0f * S));                          // the disparity the shift asks for
			CHECK(!Near(plain[0][0], plain[1][0], 1e-3f));                             // before: it jumped by its whole disparity - doubled
			if (S == h) CHECK(Near(shown[0][0], mid + h) && Near(shown[1][1], mid - h));   // at its depth: the eye drawn exactly where the game put it
		}
		// not by turns (layerEye 0): as before, whatever turnH says
		const float box[4] = {0.45f, 0.28f, 0.55f, 0.32f};
		const platelook::Pick a = platelook::LayerPixel(0.52f, 0.3f, 1.0f, 0.0f, 0.0f, true, 0.02f, 1.0f, box);
		const platelook::Pick b = platelook::LayerPixel(0.52f, 0.3f, 1.0f, 0.0f, 0.0f, true, 0.02f, 1.0f, box, 0.0f, h);
		CHECK(Same(a, b));
	}

	// ---- the plate's stereo depth by its monster's distance ([hud] monster_stereo_by_distance; Serg, 2026-10-31: "2 near, 1 when the monster is far") ----
	{
		platelook::DepthByDistance d;   // the defaults: 2 near, 1 far, the plate's own 1.5 m and 10 m
		CHECK(Near(d.nearPercent, 2.0f) && Near(d.farPercent, 1.0f) && Near(d.nearM, platelook::kNearM) && Near(d.farM, platelook::kFarM));
		CHECK(Near(platelook::DepthAt(0.5f, d), 2.0f) && Near(platelook::DepthAt(1.5f, d), 2.0f));     // near and nearer: the near depth
		CHECK(Near(platelook::DepthAt(10.0f, d), 1.0f) && Near(platelook::DepthAt(40.0f, d), 1.0f));   // far and farther: the far depth
		CHECK(Near(platelook::DepthAt(5.75f, d), 1.5f, 1e-4f));                                         // halfway: halfway
		float prev = 3.0f;
		bool falls = true;
		for (float m = 0.0f; m <= 12.0f; m += 0.25f) {   // never back up on the way out
			const float v = platelook::DepthAt(m, d);
			if (v > prev + 1e-6f) falls = false;
			prev = v;
		}
		CHECK(falls);
		CHECK(std::fabs(platelook::DepthAt(1.6f, d) - 2.0f) < 0.01f && std::fabs(platelook::DepthAt(9.9f, d) - 1.0f) < 0.01f);   // smooth at both ends
		// the eyes' distance: the ground distance and the eyes' height
		CHECK(Near(platelook::EyeDistanceM(3.0f, 4.0f), 5.0f) && Near(platelook::EyeDistanceM(0.0f, 1.7f), 1.7f) && Near(platelook::EyeDistanceM(2.0f, std::nanf("")), 2.0f));
		// kept sane: far past near, never past the drawn world's edge
		platelook::DepthByDistance w = d;
		w.nearM = 8.0f;
		w.farM = 3.0f;
		const platelook::DepthByDistance wb = platelook::Bounded(w, 0.0f);
		CHECK(wb.farM > wb.nearM && Near(wb.nearM, 8.0f));
		const platelook::DepthByDistance eb = platelook::Bounded(d, 6.0f);   // the world drawn out to 6 m
		CHECK(Near(eb.farM, 6.0f) && Near(eb.nearM, 1.5f) && Near(platelook::DepthAt(6.0f, eb), 1.0f));
		CHECK(Near(platelook::Bounded(d, 50.0f).farM, 10.0f) && Near(platelook::Bounded(d, std::nanf("")).farM, 10.0f));
		// the ease: half the way in kEaseHalfS, no further than the target, nothing at no time, at once from nothing
		CHECK(Near(platelook::EaseDepth(2.0f, 1.0f, platelook::kEaseHalfS), 1.5f, 1e-4f));
		CHECK(Near(platelook::EaseDepth(2.0f, 1.0f, 0.0f), 2.0f) && Near(platelook::EaseDepth(std::nanf(""), 1.0f, 0.0f), 1.0f));
		CHECK(Near(platelook::EaseDepth(2.0f, 1.0f, 100.0f), 1.0f, 1e-3f));
		// a new target, far after near: no jump - each frame (90 Hz) a small step, and there within a second
		platelook::DepthEase e;
		float shown = platelook::DepthNow(&e, true, 1.4f, d, true, 1.0f, 0.0f);
		CHECK(Near(shown, 2.0f));   // the first target: at once
		float biggest = 0.0f;
		for (int f = 0; f < 90; ++f) {
			const float v = platelook::DepthNow(&e, true, 1.4f, d, true, 20.0f, 1.0f / 90.0f);
			biggest = std::max(biggest, std::fabs(v - shown));
			shown = v;
		}
		CHECK(biggest < 0.1f && std::fabs(shown - 1.0f) < 0.01f);
		// no target for a while: the last depth kept; the next target eases from it
		CHECK(Near(platelook::DepthNow(&e, true, 1.4f, d, false, std::nanf(""), 0.5f), shown));
		const float next = platelook::DepthNow(&e, true, 1.4f, d, true, 1.0f, 1.0f / 90.0f);
		CHECK(next > shown && next < 1.1f);
		// off: the single depth (monster_near_body), and nothing kept for the next time it is on
		CHECK(Near(platelook::DepthNow(&e, false, 1.4f, d, true, 20.0f, 0.1f), 1.4f));
		CHECK(Near(platelook::DepthNow(&e, true, 1.4f, d, true, 20.0f, 0.1f), 1.0f));   // on again: the depth at once
		platelook::DepthEase fresh;
		CHECK(Near(platelook::DepthNow(&fresh, true, 1.4f, d, false, std::nanf(""), 0.1f), 2.0f));   // on, nothing targeted yet: the near depth
		// the box: by distance, the depth farther from the labels' decides
		CHECK(Near(platelook::DepthForBox(false, 1.4f, d, 1.6f), 1.4f));
		CHECK(Near(platelook::DepthForBox(true, 1.4f, d, 1.6f), 1.0f) && Near(platelook::DepthForBox(true, 1.4f, d, 0.5f), 2.0f));
		CHECK(platelook::PlateNeedsBox(true, platelook::DepthForBox(true, 1.6f, d, 1.6f), 1.6f, 1.0f, 1.0f));   // one of the two off the labels': the box
		platelook::DepthByDistance same = d;
		same.nearPercent = same.farPercent = 1.6f;
		CHECK(!platelook::PlateNeedsBox(true, platelook::DepthForBox(true, 1.0f, same, 1.6f), 1.6f, 1.0f, 1.0f));
	}

	// ---- the other VR views: F1, F2, F3 (OtherView; martin039383's doubled bar in F3, 2026-11-09) ----
	{
		// DepthShift is DepthShiftZ at the eyes' distance (no change for F4)
		CHECK(Near(platelook::DepthShift(300.0f, 400.0f, 1.2f, 3.0f, 0.0f, 1.0f), platelook::DepthShiftZ(500.0f, 1.2f, 3.0f, 0.0f, 1.0f)));
		CHECK(Near(platelook::DepthShift(30.0f, 40.0f, 1.2f, 3.0f, 900.0f, 0.7f), platelook::DepthShiftZ(50.0f, 1.2f, 3.0f, 900.0f, 0.7f)));
		// k = 1 is the real disparity: two parallel eyes h either side, a point z ahead, projected with m0 - the two pixels (uv) differ by twice the shift
		const float m0 = 1.1f, h = 2.5f, z = 180.0f;
		const float uL = 0.5f + 0.5f * m0 * (0.0f + h) / z, uR = 0.5f + 0.5f * m0 * (0.0f - h) / z;   // (the point straight ahead of the eyes' middle)
		CHECK(Near(uL - uR, 2.0f * platelook::DepthShiftZ(z, m0, h, 0.0f, 1.0f)));
		// off: nothing apart, nothing moved - the old picture
		platelook::OtherViewIn in;
		in.stereo = true; in.m0 = m0; in.halfGap = h; in.zUnits = z;
		platelook::OtherViewOut o = platelook::OtherView(in);
		CHECK(!o.own && o.shift == 0.0f && o.turnH == 0.0f);
		// the view's monster_depth: apart, at its monster's depth by k
		in.atMonster = 1.0f;
		o = platelook::OtherView(in);
		CHECK(o.own && Near(o.shift, platelook::DepthShiftZ(z, m0, h, 0.0f, 1.0f)) && o.turnH == 0.0f);
		in.atMonster = 0.5f;
		CHECK(Near(platelook::OtherView(in).shift, 0.5f * platelook::DepthShiftZ(z, m0, h, 0.0f, 1.0f)));
		// a nearer monster stands nearer (a larger shift)
		platelook::OtherViewIn nearer = in;
		nearer.zUnits = 60.0f;
		CHECK(platelook::OtherView(nearer).shift > platelook::OtherView(in).shift);
		// each eye its own layout: the game's plate is already where that eye sees it - its half-disparity taken off (at k = 1 the two cancel)
		in.atMonster = 1.0f; in.turns = true;
		o = platelook::OtherView(in);
		CHECK(Near(o.turnH, o.shift));
		// no point over the head (no target, its body not read): apart only if ours is wanted, never moved
		in.zUnits = -1.0f;
		o = platelook::OtherView(in);
		CHECK(o.own && o.shift == 0.0f && o.turnH == 0.0f);
		// our plate alone: apart (its box: the game's not drawn under ours), the game's not moved
		platelook::OtherViewIn ours;
		ours.stereo = true; ours.ours = true; ours.m0 = m0; ours.halfGap = h; ours.zUnits = z; ours.turns = true;
		o = platelook::OtherView(ours);
		CHECK(o.own && o.shift == 0.0f && o.turnH == 0.0f);
		// not a stereo pair of our camera (the game's own flat F1, F4 has its own rule): nothing
		ours.stereo = false; ours.atMonster = 1.0f;
		o = platelook::OtherView(ours);
		CHECK(!o.own && o.shift == 0.0f);
		// a broken projection: never a shift
		in.zUnits = z; in.m0 = 0.0f;
		CHECK(platelook::OtherView(in).shift == 0.0f);
		in.m0 = m0; in.zUnits = std::nanf("");
		CHECK(platelook::OtherView(in).shift == 0.0f);
		// the depth along the view: a point to the side at the same depth has the same depth
		const float eyes[3] = {10.0f, 20.0f, 30.0f}, fwd[3] = {0.0f, 0.0f, -1.0f};
		const float ahead[3] = {10.0f, 20.0f, -70.0f}, side[3] = {60.0f, 5.0f, -70.0f}, behind[3] = {10.0f, 20.0f, 40.0f};
		CHECK(Near(platelook::AheadOf(ahead, eyes, fwd), 100.0f) && Near(platelook::AheadOf(side, eyes, fwd), 100.0f) && platelook::AheadOf(behind, eyes, fwd) < 0.0f);
	}

	std::printf("plate_look_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
