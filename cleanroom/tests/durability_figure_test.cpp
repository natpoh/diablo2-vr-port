// The item-condition figure (vr/durability_figure.h): where it is, and what the effect does with it when it is moved, sized or hidden.
//
//   durability_figure_test        exit code 0 = every case passed

#include "../../vr/durability_figure.h"

#include <cmath>
#include <cstdio>

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

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
}  // namespace

int main() {
	using namespace durab;

	// ---- what the player asked for ----
	{
		Look l;
		CHECK(!Moved(l));   // the defaults: nothing is done
		l.x = 0.001f;
		CHECK(Moved(l));
		l = Look{};
		l.y = -0.2f;
		CHECK(Moved(l));
		l = Look{};
		l.scale = 1.5f;
		CHECK(Moved(l));
		l = Look{};
		l.scale = 1.003f;
		CHECK(!Moved(l));   // (a rounding is not a setting)
		l = Look{};
		l.hide = true;
		CHECK(Moved(l));
	}

	// ---- the interface area ----
	{
		Area a;
		CHECK(AreaOf(0.569f, 1536, 1536, &a));   // Serg's picture: a square 1536, the game's interface scale 0.569 = Safe Screen Percent 80
		CHECK(Near(a.right, 1382.4f, 0.5f) && Near(a.top, 153.6f, 0.5f));
		CHECK(AreaOf(1080.0f / 2160.0f, 1920, 1080, &a) && Near(a.right, 1920.0f, 0.5f) && Near(a.top, 0.0f, 0.5f));   // a wide screen at 100 %: the corner
		CHECK(AreaOf(0.0f, 1536, 1536, &a));     // not a scale: the picture's own (height / 2160)
		CHECK(!AreaOf(0.569f, 0, 1536, &a) && !AreaOf(0.569f, 1536, 0, &a));
	}

	// ---- the box ----
	{
		const int32_t none[4] = {0, 0, 0, 0};
		float box[4];
		bool fromWidget = true;
		// the widget cannot be read: where the figure was seen on that shot - x 1228 - 1350, y 203 - 355 of a 1536 x 1536 picture - with a margin
		CHECK(BoxUv(false, none, none, 0.569f, 1536, 1536, box, &fromWidget) && !fromWidget);
		const float px[4] = {box[0] * 1536.0f, box[1] * 1536.0f, box[2] * 1536.0f, box[3] * 1536.0f};
		CHECK(px[0] <= 1228.0f && px[1] <= 203.0f && px[2] >= 1350.0f && px[3] >= 355.0f);   // it holds the figure ...
		CHECK(1228.0f - px[0] < 15.0f && 203.0f - px[1] < 15.0f && px[2] - 1350.0f < 15.0f && px[3] - 355.0f < 15.0f);   // ... and not much more
		// the same units on another picture: the figure's place does not depend on the size of the picture, only on the interface scale
		float big[4];
		CHECK(BoxUv(false, none, none, 1.138f, 3072, 3072, big, nullptr));
		CHECK(Near(big[0], box[0], 0.002f) && Near(big[1], box[1], 0.002f) && Near(big[2], box[2], 0.002f) && Near(big[3], box[3], 0.002f));

		// the widget, read: its rect off the panel's place (the panel -40, 12; the widget as the layout's units give it) - a rect where the figure was seen is used
		const int32_t panel[4] = {-40, 12, 0, 0};
		const int32_t widget[4] = {-231, 75, 214, 267};   // the figure's own size, 56 units off the corner
		float w[4];
		CHECK(BoxUv(true, panel, widget, 0.569f, 1536, 1536, w, &fromWidget) && fromWidget);
		CHECK(w[0] < box[0] + 0.01f && Near(w[0] * 1536.0f, 1382.4f + (-40 - 231 - 8) * 0.569f, 0.5f) && Near(w[3] * 1536.0f, 153.6f + (12 + 75 + 267 + 8) * 0.569f, 0.5f));
		// a rect that is not a figure's: far from the corner, or not its size - the calibrated box
		const int32_t farOff[4] = {500, 600, 214, 267}, tiny[4] = {-231, 75, 10, 10}, huge[4] = {-231, 75, 2000, 2000};
		for (const int32_t* bad : {farOff, tiny, huge}) {
			float f[4];
			fromWidget = true;
			CHECK(BoxUv(true, panel, bad, 0.569f, 1536, 1536, f, &fromWidget) && !fromWidget);
			CHECK(Near(f[0], box[0]) && Near(f[1], box[1]) && Near(f[2], box[2]) && Near(f[3], box[3]));
		}
		// nothing worn out: the widget is there and has no size - no figure, no box
		const int32_t empty[4] = {-231, 75, 0, 0};
		CHECK(!BoxUv(true, panel, empty, 0.569f, 1536, 1536, w, &fromWidget));
		CHECK(!BoxUv(false, none, none, 0.569f, 0, 1536, w, &fromWidget));
		CHECK(BoxUv(false, none, none, 0.569f, 1536, 1536, w, nullptr));   // (no place to say where it came from: fine)
	}

	// ---- the effect's figure branch ----
	{
		const float box[4] = {0.80f, 0.13f, 0.88f, 0.24f};   // the figure's box, uv
		const float cu = 0.5f * (box[0] + box[2]), cv = 0.5f * (box[1] + box[3]);
		const float none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		Look moved;
		moved.x = -0.40f;     // far to the left
		moved.y = 0.30f;      // and down (uv, + down)
		// at its defaults nothing is the figure's business, anywhere
		bool untouched = true;
		for (float v = 0.0f; v < 1.0f; v += 0.01f)
			for (float u = 0.0f; u < 1.0f; u += 0.01f) {
				if (MovedFigure(u, v, Look{}, box).kind != Kind::Other || OldPlaceEmpty(u, v, Look{}, box)) untouched = false;
				if (MovedFigure(u, v, moved, none).kind != Kind::Other || OldPlaceEmpty(u, v, moved, none)) untouched = false;   // no box (nothing worn out): nothing
			}
		CHECK(untouched);
		// moved: the figure's own pixel is shown where the offset puts it, and its old place is left empty
		Pick p = MovedFigure(cu + moved.x, cv + moved.y, moved, box);
		CHECK(p.kind == Kind::Layer && Near(p.u, cu) && Near(p.v, cv));   // the middle of the new place shows the figure's middle
		p = MovedFigure(box[0] + moved.x + 0.001f, box[1] + moved.y + 0.001f, moved, box);
		CHECK(p.kind == Kind::Layer && Near(p.u, box[0] + 0.001f) && Near(p.v, box[1] + 0.001f));   // its corner, its corner
		CHECK(MovedFigure(cu, cv, moved, box).kind == Kind::Other);        // the old place is not the figure's any more ...
		CHECK(OldPlaceEmpty(cu, cv, moved, box));                          // ... it is left empty
		CHECK(!OldPlaceEmpty(0.1f, 0.1f, moved, box) && !OldPlaceEmpty(cu + moved.x, cv + moved.y, moved, box));   // (outside the box: the layer's own)
		// sized about its middle
		Look big;
		big.scale = 2.0f;
		p = MovedFigure(cu + 0.04f, cv, big, box);   // the figure is twice as big: 4 % of the picture from the middle is 2 % of the figure
		CHECK(p.kind == Kind::Layer && Near(p.u, cu + 0.02f) && Near(p.v, cv));
		CHECK(MovedFigure(box[2] + 0.01f, cv, big, box).kind == Kind::Layer);   // it grows past its old box
		Look small;
		small.scale = 0.5f;
		CHECK(MovedFigure(box[2] - 0.001f, cv, small, box).kind == Kind::Other);   // smaller: the old edge is no longer the figure's, but the old place is empty
		CHECK(OldPlaceEmpty(box[2] - 0.001f, cv, small, box));
		// hidden: nothing of it shown, its place empty
		Look hid;
		hid.hide = true;
		CHECK(MovedFigure(cu, cv, hid, box).kind == Kind::Other && OldPlaceEmpty(cu, cv, hid, box));
		// the offset and the size do not change which place is the old one: that is the box, whatever the look
		bool same = true;
		for (float v = 0.0f; v < 1.0f; v += 0.01f)
			for (float u = 0.0f; u < 1.0f; u += 0.01f) {
				const bool inBox = InBox(u, v, box);
				if (OldPlaceEmpty(u, v, moved, box) != inBox || OldPlaceEmpty(u, v, big, box) != inBox || OldPlaceEmpty(u, v, hid, box) != inBox) same = false;
			}
		CHECK(same);
	}

	std::printf("durability_figure_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
