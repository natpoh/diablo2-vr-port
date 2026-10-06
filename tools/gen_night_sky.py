"""Night skies drawn by OpenAI from the approved day skies (2026-10-05).

The game's light goes from day to night (vrcam reads it, see env:: in vrcam.cpp);
the shader blends each act's day picture into its night one by it. The night
picture is the day one edited - the same clouds in the same places - so the blend
does not move anything, it only darkens and turns to moonlight.

  python gen_night_sky.py [act ...] [--redo] [--install]

reshade/sky/D2R_SkyNight_<act>.png and D2R_SkyCapNight_<act>.png (our pictures,
shipped by the installer); --install copies them into the game's
reshade-shaders/Textures/D2R_Sky_ours. The raw drawings stay in extracted/raw_night.
"""
import argparse
import base64
import pathlib
import shutil
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gen_sky   # noqa: E402  (MODEL, SIZE, load_key, seamless_x, CAP_PROMPT, BESTIARY, GAME_TEX)

SKY = HERE.parent / "reshade" / "sky"
RAW = HERE.parent / "extracted" / "raw_night"

RULES = (
    " Keep the composition exactly: the same clouds in the same places with the same shapes, the same horizon "
    "line and haze, the same framing - only the time of day changes. It stays a SKYBOX: no ground, no trees, no "
    "buildings, no figures. The left and right edges must still continue each other. No moon disc, no sun, NO "
    "visible particles (no rain streaks, no snowflakes, no embers, no birds, no shooting stars), no text, no frame, "
    "no vignette. Much darker overall than the day picture, but the cloud shapes stay readable. Painterly but "
    "realistic, in the gloomy style of Diablo II: Resurrected."
)
NIGHT = {
    "act1": "Turn this stormy sky into the same sky deep in the night: a blue-black sky, the storm clouds dark "
            "grey-blue, their edges lit faintly by cold pale moonlight from behind them, no warm light left.",
    "act2": "Turn this sky into the same sky on a clear desert night: deep dark blue, the clouds dim blue-grey "
            "with moonlit silver edges, many fine faint stars in every clear part of the sky.",
    "act3": "Turn this jungle sky into the same sky on a humid tropical night: dark teal-blue, the storm clouds "
            "near-black with faint moonlit edges, the mist along the horizon a dim silvery grey, a few faint stars "
            "only where the clouds part.",
    "act4": "Turn this sky of the Burning Hells into the same sky at night: the smoke above near-black with dull "
            "deep-red under-lighting, the glow at the horizon much dimmer - a low smouldering dark orange-red. "
            "No stars, no moonlight: hell has no moon.",
    "act5": "Turn this cold northern sky into the same sky on a cold clear night: dark navy blue, the cloud banks "
            "dark slate with cold moonlit tops, faint stars in the gaps between the clouds.",
    "snow": "Turn this frozen mountain sky into the same sky on a freezing night: dark blue-grey overcast above, "
            "the bright cloud band now pale moonlit blue-grey, the clear strip a deep cold navy with faint stars, "
            "a dim bluish haze along the horizon.",
}


def edit(src, prompt, size, dest):
    from openai import OpenAI
    t = time.time()
    with open(src, "rb") as f:
        r = OpenAI().images.edit(model=gen_sky.MODEL, image=f, prompt=prompt, n=1, size=size, quality="high")
    dest.write_bytes(base64.b64decode(r.data[0].b64_json))
    return time.time() - t


def build_cap(cap_raw, band, out):
    """Seamless both ways, then the colours of the band's top 15% rows (as gen_sky.build_cap)."""
    sys.path.insert(0, str(gen_sky.BESTIARY))
    from gen_flora_tex import seamless   # noqa: E402  (fractal_game: half-roll + blend)
    a = np.asarray(seamless(Image.open(cap_raw)), np.float32)
    b = np.asarray(Image.open(band).convert("RGB"), np.float32)
    ref = b[: b.shape[0] * 15 // 100].reshape(-1, 3)
    flat = a.reshape(-1, 3)
    a = (a - flat.mean(0)) / (flat.std(0) + 1e-3) * ref.std(0) + ref.mean(0)
    Image.fromarray(a.clip(0, 255).astype(np.uint8)).save(out)


# The model turns the sky blue but leaves the clouds' tops lit as by day
# ("in the clouds there is still daylight, the last acts most", 2026-10-05): the
# highlights are pressed down and turned to cold moonlight; hell keeps its red.
LUM = np.array([0.3, 0.59, 0.11], np.float32)


def grade(path, act):
    a = np.asarray(Image.open(path).convert("RGB"), np.float32) / 255.0
    lum = (a * LUM).sum(2, keepdims=True) + 1e-4
    top, knee = (0.24, 0.25) if act == "act4" else (0.20, 0.22)
    nl = top * (1.0 - np.exp(-lum / knee))
    keep = a / lum * nl
    if act != "act4":
        tint = np.array([0.62, 0.74, 1.0], np.float32)
        tint /= float(tint @ LUM)
        keep = keep * 0.35 + tint * nl * 0.65
    Image.fromarray((keep.clip(0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)).save(path)


def one(act, redo):
    day = SKY / f"D2R_Sky_{act}.png"
    if not day.exists():
        return f"{act}: no day sky {day}"
    band_raw, cap_raw = RAW / f"{act}_band.png", RAW / f"{act}_cap.png"
    band = SKY / f"D2R_SkyNight_{act}.png"
    if redo or not band_raw.exists():
        s = edit(day, NIGHT[act] + RULES, gen_sky.SIZE, band_raw)
        print(f"{act}: night band drawn in {s:.0f} s", flush=True)
    a = gen_sky.seamless_x(np.asarray(Image.open(band_raw).convert("RGB"), np.float32))
    Image.fromarray(a.clip(0, 255).astype(np.uint8)).save(band)
    if redo or not cap_raw.exists():   # drawn from the band before its grading
        s = edit(band, gen_sky.CAP_PROMPT, "1024x1024", cap_raw)
        print(f"{act}: night zenith drawn in {s:.0f} s", flush=True)
    cap = SKY / f"D2R_SkyCapNight_{act}.png"
    build_cap(cap_raw, band, cap)
    grade(band, act)
    grade(cap, act)
    return f"{act}: {band.name} + D2R_SkyCapNight_{act}.png"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("acts", nargs="*", default=list(NIGHT))
    ap.add_argument("--redo", action="store_true")
    ap.add_argument("--install", action="store_true")
    args = ap.parse_args()
    if not gen_sky.load_key():
        sys.exit("no OpenAI key (OPENAI_API_KEY or fractal_game\\key.conf)")
    RAW.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=6) as ex:
        for line in ex.map(lambda a: one(a, args.redo), args.acts):
            print(line, flush=True)
    if args.install:
        for act in args.acts:
            for name in (f"D2R_SkyNight_{act}.png", f"D2R_SkyCapNight_{act}.png"):
                if (SKY / name).exists():
                    shutil.copy2(SKY / name, gen_sky.GAME_TEX / name)
        print("installed into", gen_sky.GAME_TEX)


if __name__ == "__main__":
    main()
