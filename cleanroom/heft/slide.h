#pragma once
// slide.h - the second hand sliding along a long shaft. Pure.
//
// Two hands on a long shaft (a staff, a spear, a polearm, a two-handed sword): the first hand holds it where it took it and the second hand, while
// its grip stays squeezed, slides along the shaft with the controller - its grasp point is the controller's fist projected on the shaft's axis, clamped to
// the shaft, with a little smoothing. The old staff_free_left feel ("the left hand takes the staff and slides along it"), inside the weight model.
// A fist that goes off an end of the shaft, or is pulled away from its axis, lets go. All lengths are metres along / from the shaft's axis.
//
// Hold::Step (hold.cpp) feeds it the second hand's fist in the body's frame and moves the grasp point to the answer.

#include <algorithm>
#include <cmath>

namespace heft::slide {

// A shaft shorter than this (a dagger, a sword in one hand) is not slid on.
constexpr double kMinShaft = 1.1;

struct In {
	double t    = 0;      // where the hand holds the shaft now, along it from the line's origin
	double raw  = 0;      // the controller's fist projected on the axis, the same measure (not clamped)
	double perp = 0;      // the fist's distance from the axis
	double butt = -0.1, tip = 0.9;   // the shaft's ends, the same measure
	double dt   = 0;      // s
	double tau  = 0.05;   // s: the smoothing; 0 = no smoothing
	double margin = 0.12; // a fist this far past an end has slid off it
	double letGo  = 0.25; // a fist this far from the axis has been pulled away
};

struct Out {
	double t       = 0;       // the new grasp point
	bool   release = false;   // the hand lets go: it slid off an end, or was pulled away
};

inline Out Step(const In& in) {
	Out out;
	out.t = in.t;
	if (!std::isfinite(in.t + in.raw + in.perp + in.butt + in.tip + in.dt) || !(in.tip > in.butt)) return out;   // not numbers to act on: it stays
	if (in.perp > in.letGo || in.raw < in.butt - in.margin || in.raw > in.tip + in.margin) {
		out.release = true;
		return out;
	}
	const double target = std::clamp(in.raw, in.butt, in.tip);
	const double k = in.tau > 1e-6 && in.dt > 0 ? 1.0 - std::exp(-in.dt / in.tau) : 1.0;
	out.t = in.t + (target - in.t) * k;
	return out;
}

}   // namespace heft::slide
