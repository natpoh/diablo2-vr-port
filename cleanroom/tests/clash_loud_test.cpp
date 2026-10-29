// [weight] clash_sound's rule (vr/clash_loud.h): how loud a knock of steel on steel is and how hard its pulse, by how fast the two met.
//
//   clash_loud_test

#include "clash_loud.h"

#include <cmath>
#include <cstdio>

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

}  // namespace

int main() {
	using clashloud::Of;
	// under the threshold: neither heard nor felt (laid on the other, slid along it, a touch)
	for (float v : {0.0f, 0.1f, 0.3f, 0.49f, -1.0f, NAN, INFINITY * -1.0f}) {
		const clashloud::Clash c = Of(v);
		EXPECT(!c.heard && c.gameVolume == 0 && c.volume == 0.0f && c.pulse == 0.0f && c.pulseMs == 0, "%.2f m/s: heard %d volume %u pulse %.2f", v, c.heard, c.gameVolume, c.pulse);
	}
	// at the threshold: heard, quiet - never silent once heard
	{
		const clashloud::Clash c = Of(clashloud::kQuietMps);
		EXPECT(c.heard && c.strength == 0.0f && c.gameVolume == 38 && std::fabs(c.pulse - 0.6f) < 1e-6f && c.pulseMs == 35, "the threshold: volume %u pulse %.2f %d ms", c.gameVolume,
		       c.pulse, c.pulseMs);
	}
	// a hard blow and over: the game's full loudness, the hardest pulse
	for (float v : {4.0f, 6.0f, 30.0f, INFINITY}) {
		const clashloud::Clash c = Of(v);
		if (!std::isfinite(v)) { EXPECT(!c.heard, "an endless speed is no knock"); continue; }
		EXPECT(c.heard && c.strength == 1.0f && c.gameVolume == 255 && c.pulse == 1.0f && c.pulseMs == 70, "%.1f m/s: volume %u pulse %.2f %d ms", v, c.gameVolume, c.pulse, c.pulseMs);
	}
	// the harder, the louder: rises evenly, never falls
	{
		uint32_t was = 0;
		float pulseWas = 0.0f;
		int msWas = 0;
		for (float v = 0.5f; v <= 4.5f; v += 0.25f) {
			const clashloud::Clash c = Of(v);
			EXPECT(c.gameVolume >= was && c.pulse >= pulseWas && c.pulseMs >= msWas, "%.2f m/s falls: %u < %u", v, c.gameVolume, was);
			was = c.gameVolume;
			pulseWas = c.pulse;
			msWas = c.pulseMs;
		}
		const clashloud::Clash mid = Of(2.25f);   // halfway: (2.25 - 0.5) / 3.5
		EXPECT(std::fabs(mid.strength - 0.5f) < 1e-5f && mid.gameVolume == 147 && std::fabs(mid.pulse - 0.8f) < 1e-5f, "halfway: strength %.3f volume %u pulse %.2f", mid.strength,
		       mid.gameVolume, mid.pulse);
		const clashloud::Clash tap = Of(1.0f), blow = Of(3.0f);
		EXPECT(tap.gameVolume < blow.gameVolume / 2, "a tap %u against a blow %u", tap.gameVolume, blow.gameVolume);
		std::printf("  a light tap 1 m/s: %u/255 (%.0f %%), pulse %.2f %d ms; a blow 3 m/s: %u/255 (%.0f %%), pulse %.2f %d ms\n", tap.gameVolume, tap.volume * 100.0f, tap.pulse, tap.pulseMs,
		            blow.gameVolume, blow.volume * 100.0f, blow.pulse, blow.pulseMs);
	}
	std::printf(g_fails ? "clash_loud_test: %d FAILED\n" : "clash_loud_test: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
