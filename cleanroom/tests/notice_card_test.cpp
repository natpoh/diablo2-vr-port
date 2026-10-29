// [hud] notice_card (vr/notice_card.h): where the game's quest / level-up notice is in the interface layer, and when the card in the room
// comes, stays, fades and goes.
//
//   notice_card_test        exit code 0 = every case passed

#include "../../vr/notice_card.h"
#include "../../vr/hud_rects.h"

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
	using namespace noticecard;

	// ---- the box ----
	{
		// A 2160-high layer at 100 %: one pixel a unit, the area the whole layer. The layout's rect: 1200 x 244, 500 to 256 units over the bottom,
		// its middle on the layer's - with the margin and the steps.
		Box b;
		bool fromWidget = true;
		CHECK(BoxPx(nullptr, 1.0f, 3840, 2160, &b, &fromWidget) && !fromWidget);
		CHECK(b.left <= 1920 - 600 - 8 && b.right >= 1920 + 600 + 8);
		CHECK(b.top <= 2160 - 500 - 8 && b.bottom >= 2160 - 256 + 8);
		CHECK((b.right - b.left) % kStepPx == 0 && (b.bottom - b.top) % kStepPx == 0);
		CHECK(Near(0.5f * (b.left + b.right), 1920.0f, 1.0f));
		CHECK(b.right - b.left - (1200 + 16) < (int)kStepPx && b.bottom - b.top - (244 + 16) < (int)kStepPx);

		// Why it was lost: the toolbar's strip, emptied with [hud] hide = 1, takes the box but its top rim (vr/hud_rects.h).
		const hudrects::Box strip = hudrects::Toolbar(3840, 2160, 1.0f);
		CHECK(strip.top < 2160 - 256 - 8 - 150);    // the text's middle (378 units up) is inside the strip
		CHECK(strip.top > 2160 - 500);              // and only a rim of the box is above it

		// The user's square picture at 80 %: the interface scale 0.569 on 1536 (vr/durability_figure.h's shot) - the area 80 % of the layer, in the middle.
		Box q;
		CHECK(BoxPx(nullptr, 0.569f, 1536, 1536, &q));
		const float s = 0.569f, bottom = 1536.0f * (0.5f + 0.5f * 0.8f);
		CHECK(Near(0.5f * (q.left + q.right), 768.0f, 1.0f));
		CHECK(q.top <= bottom - (500 + 8) * s + 1.0f && q.bottom >= bottom - (256 - 8) * s - 1.0f);
		CHECK(q.left >= 0 && q.right <= 1536 && q.top >= 0 && q.bottom <= 1536);

		// No scale known: the layer's own (h / 2160) - the same as 100 %.
		Box z;
		CHECK(BoxPx(nullptr, 0.0f, 3840, 2160, &z) && z.left == b.left && z.top == b.top && z.right == b.right && z.bottom == b.bottom);

		// A rect from the widget service: used when it is the layout's own, a little moved; the layout's when it is something else.
		const int32_t moved[4] = {40, -520, 1200, 244};
		Box m;
		CHECK(BoxPx(moved, 1.0f, 3840, 2160, &m, &fromWidget) && fromWidget);
		CHECK(Near(0.5f * (m.left + m.right), 1960.0f, 1.0f));
		const int32_t empty[4] = {0, 0, 0, 0}, far[4] = {0, -1500, 1200, 244}, huge[4] = {0, -500, 3000, 244};
		for (const int32_t* r : {empty, far, huge}) {
			Box o;
			CHECK(BoxPx(r, 1.0f, 3840, 2160, &o, &fromWidget) && !fromWidget && o.left == b.left && o.top == b.top);
		}

		// No layer: no box.
		Box n;
		CHECK(!BoxPx(nullptr, 1.0f, 0, 2160, &n) && !BoxPx(nullptr, 1.0f, 3840, 0, &n) && !BoxPx(nullptr, 1.0f, 3840, 2160, nullptr));
		// A tiny layer: the box kept inside it.
		CHECK(BoxPx(nullptr, 0.1f, 400, 300, &n) && n.left >= 0 && n.right <= 400 && n.top >= 0 && n.bottom <= 300);
	}

	// ---- the seconds ----
	{
		CHECK(ShowMs(4.0f) == 4000 && ShowMs(0.0f) == 1000 && ShowMs(99.0f) == 10000 && ShowMs(NAN) == 4000);
		CHECK(LeanDeg() > 10.0f && LeanDeg() < 20.0f);
	}

	// ---- which ui messages count ----
	{
		CHECK(IsPoke("PanelManager", "OpenPanel", "NotificationPanel"));
		CHECK(IsPoke("PanelManager", "OpenExclusivePanel", "NotificationPanel"));
		CHECK(IsPoke("NotificationMessage", "QuestUpdate", ""));
		CHECK(IsPoke("NotificationMessage", "AnythingElse", nullptr));
		CHECK(!IsPoke("PanelManager", "ClosePanel", "NotificationPanel"));
		CHECK(!IsPoke("PanelManager", "OpenPanel", "NotificationPanelX"));
		CHECK(!IsPoke("PanelManager", "OpenPanel", "HUDPanel"));
		CHECK(!IsPoke("QuestLogPanelMessage", "SetQuestAlert", ""));
		CHECK(!IsPoke(nullptr, "OpenPanel", "NotificationPanel") && !IsPoke("PanelManager", nullptr, "NotificationPanel"));
	}

	// ---- the card ----
	const uint64_t show = 4000;
	{
		// What was counted before the card was first asked is not a card.
		Card c;
		Frame f = c.Step(1000, 5, true, true, show);
		CHECK(!f.show && !c.On());

		// A quest update: the message, then the panel 30 ms later (one card), shown at once, placed, full for 4 s, faded over 0.8 s, gone.
		f = c.Step(2000, 6, false, true, show);
		CHECK(c.On() && !f.show && f.place);   // (the panel not open yet: nothing to draw, the card waits)
		f = c.Step(2030, 7, true, true, show);
		CHECK(f.show && f.place && Near(f.alpha, 1.0f) && f.seq == 1);
		c.Shown(2030);
		f = c.Step(2050, 7, true, true, show);
		CHECK(f.show && !f.place && Near(f.alpha, 1.0f));
		f = c.Step(2030 + show, 7, true, true, show);
		CHECK(f.show && Near(f.alpha, 1.0f));
		f = c.Step(2030 + show + kFadeMs / 2, 7, true, true, show);
		CHECK(f.show && Near(f.alpha, 0.5f, 0.01f));
		f = c.Step(2030 + show + kFadeMs, 7, true, true, show);
		CHECK(!f.show && !c.On());
		// The game's panel is still open (its own 10 s): no card comes back.
		f = c.Step(9000, 7, true, true, show);
		CHECK(!f.show && !c.On());
	}
	{
		// The game's panel closes early (it was answered): the card goes with it.
		Card c;
		c.Step(0, 0, false, true, show);
		Frame f = c.Step(100, 1, true, true, show);
		c.Shown(100);
		f = c.Step(2000, 1, false, true, show);
		CHECK(!f.show && !c.On());
	}
	{
		// Not in force (a menu, another view): not drawn, its time not started; in force again within the game's time: shown then, for its full time.
		Card c;
		c.Step(0, 0, false, false, show);
		Frame f = c.Step(100, 1, true, false, show);
		CHECK(!f.show && c.On());
		f = c.Step(3000, 1, true, true, show);
		CHECK(f.show && Near(f.alpha, 1.0f) && f.place);
		c.Shown(3000);
		f = c.Step(3000 + show - 1, 1, true, true, show);
		CHECK(f.show && Near(f.alpha, 1.0f));
	}
	{
		// Never shown within the game's own time: dropped, not shown late.
		Card c;
		c.Step(0, 0, true, false, show);
		c.Step(100, 1, true, false, show);
		Frame f = c.Step(100 + kWaitMs + 1, 1, true, true, show);
		CHECK(!f.show && !c.On());
	}
	{
		// The level-up's second message while the first card is up: a new card in the same place, its own full time.
		Card c;
		c.Step(0, 0, false, true, show);
		Frame f = c.Step(100, 1, true, true, show);
		CHECK(f.place && f.seq == 1);
		c.Shown(100);
		f = c.Step(3000, 2, true, true, show);
		CHECK(f.show && !f.place && f.seq == 2 && Near(f.alpha, 1.0f));
		c.Shown(3000);
		f = c.Step(3000 + show - 1, 2, true, true, show);
		CHECK(f.show && Near(f.alpha, 1.0f));
		// One right after the last one went: its place kept too; one a while later: placed anew.
		f = c.Step(3000 + show + kFadeMs, 2, true, true, show);
		CHECK(!c.On());
		f = c.Step(3000 + show + kFadeMs + kFollowMs / 2, 3, true, true, show);
		CHECK(f.show && !f.place && f.seq == 3);
		Card d;
		d.Step(0, 0, false, true, show);
		d.Step(100, 1, true, true, show);
		d.Shown(100);
		d.Step(100 + show + kFadeMs, 1, true, true, show);
		f = d.Step(100 + show + kFadeMs + kFollowMs + 1000, 2, true, true, show);
		CHECK(f.show && f.place);
	}
	{
		// Pokes within the merge window are the same card (the panel's opening and the message), not a new one each.
		Card c;
		c.Step(0, 0, false, true, show);
		c.Step(100, 1, true, true, show);
		c.Shown(100);
		Frame f = c.Step(400, 2, true, true, show);
		CHECK(f.seq == 1);
		f = c.Step(100 + show + kFadeMs, 2, true, true, show);
		CHECK(!c.On());
	}
	{
		// The state the parts share.
		Configure(true, 6.0f);
		CHECK(Wanted() && ShowMsNow() == 6000);
		Configure(false, 4.0f);
		CHECK(!Wanted() && ShowMsNow() == 4000);
		const uint32_t before = Pokes();
		Poke();
		CHECK(Pokes() == before + 1);
		SetOpen(true);
		CHECK(PanelOpen());
		SetOpen(false);
		SetActive(true);
		CHECK(Active());
		SetActive(false);
	}

	std::printf("notice_card_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
