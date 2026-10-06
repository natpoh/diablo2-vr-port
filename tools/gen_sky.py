"""Sky panoramas for D2R_DepthFog.fx, drawn by OpenAI.

    python gen_sky.py act1            # generate raw/act1.png (if missing) and build the texture
    python gen_sky.py act1 --redo     # draw it again
    python gen_sky.py act1 --install  # and copy into the game's reshade-shaders\\Textures
    python gen_sky.py act1 --edit norain   # redraw the raw picture with a fix from EDITS (old one kept as act1_vN.png)

The picture is a band around the viewer: its bottom edge is a little under the
horizon, its top about 70 degrees up; the shader fades from its top rows into
their average colour toward the zenith and wraps it twice around the circle
(1536 px for 360 degrees would be too soft in a headset). Left and right edges
are blended so the wrap has no seam.

The key comes the way fractal_game's bestiary scripts take it: OPENAI_API_KEY,
else C:\\wsl\\unity_add_walk\\fractal_game\\key.conf.
"""
import argparse
import base64
import pathlib
import shutil
import sys
import time

import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "reshade" / "sky"
RAW = OUT / "raw"
# Our own skies in a folder of their own, apart from the ones taken out of the game.
GAME_TEX = pathlib.Path(r"D:\SteamLibrary\steamapps\common\Diablo II Resurrected\reshade-shaders\Textures\D2R_Sky_ours")
BESTIARY = pathlib.Path(r"C:\wsl\unity_add_walk\fractal_game\bestiary")
MODEL = "gpt-image-2"
SIZE = "1536x1024"

FRAME = (
    "A wide panoramic view of the SKY ONLY, looking straight at the horizon and up: the bottom edge "
    "of the picture is just below the horizon line, the top edge is high overhead. No ground, no "
    "landscape, no trees, no buildings, no mountains, no people, no text, no frame, no vignette. "
    "Only sky, clouds and weather. Painterly but realistic, like the sky of a dark fantasy video "
    "game's skybox, in the gloomy gothic style of Diablo II: Resurrected. The left and right edges "
    "of the picture must continue each other, the clouds flow across them evenly; no single object "
    "in the middle of the frame. Evenly lit across the width, no bright spot near an edge. "
    "NO visible particles of any kind - no rain streaks, no raindrops, no snowflakes, no embers, no "
    "sparks, no birds: in the game the picture stands still and they would hang frozen in the air; "
    "rain, snow or dust only as soft blurred veils and haze."
)

SKIES = {
    "act1": (
        "Act 1, the Rogue Encampment at night in a storm: a heavy, low, brooding sky of dark "
        "blue-grey rain clouds with torn edges, faint cold moonlight leaking through a gap high up, "
        "veils and curtains of rain falling from the clouds toward the distant horizon, a slight "
        "warm-brown haze right at the horizon. Dark overall, but the clouds clearly readable, rich "
        "soft gradients, no flat black areas. Mood: cold, wet, ominous, beautiful."
    ),
    # 2026-10-02, from the acts' descriptions: 2 Lut Gholein desert, 3 Kurast jungle (Mayan),
    # 4 Hell (rock and lava), 5 Mount Arreat under siege (ruins) and its snowy heights.
    "act2": (
        "Act 2, the deserts around Lut Gholein at dusk: a vast desert sky after sunset, a hot "
        "ochre-orange and dusty amber band along the horizon fading up into deep dusky violet and "
        "dark indigo, long thin streaks of high clouds lit from below by the last light, a "
        "sandstorm's brown dust haze hanging low over the horizon. Warm, dry, oppressive and "
        "mysterious, darker than daylight, rich colour, no flat areas."
    ),
    "act3": (
        "Act 3, the jungles of Kurast at dusk: a humid, heavy, overcast tropical sky of dark "
        "grey-green and slate clouds, thick steamy mist and haze rising at the horizon tinted sickly "
        "green and yellow, a faint dim glow of a hidden sun behind the clouds low on one side, soft "
        "distant rain veils as blurred haze. Moody, oppressive, damp and dangerous, dark overall but "
        "readable, rich soft gradients."
    ),
    "act4": (
        "Act 4, the Burning Hells: a sky of churning, billowing smoke and ash clouds lit from below "
        "by an unseen sea of lava, deep blood-red and black above turning to fiery orange and molten "
        "glow at the horizon, ominous dark red light, hellish and apocalyptic, heavy and suffocating. "
        "No sun, no moon. Dramatic but not too bright."
    ),
    "act5": (
        "Act 5, the foothills of Mount Arreat under siege at dusk: a cold northern evening sky of "
        "heavy grey-blue clouds, tall dark columns of smoke from burning fortifications rising from "
        "the horizon in a few places and spreading into the clouds, a dim orange glow of distant "
        "fires low at the horizon, cold pale light high up. War-torn, cold, grim and epic."
    ),
    "snow": (
        "Act 5, the frozen heights of Mount Arreat: a cold overcast winter sky in a blizzard, low "
        "heavy pale grey-blue and white snow clouds, soft veils of blowing snow as blurred haze near "
        "the horizon, a faint cold silver light from a hidden sun, icy, desolate and harsh. Not too "
        "bright, muted cold colours, rich soft gradients."
    ),
}


# Fixes asked of the model on an already drawn sky: the picture changes as little as possible.
EDITS = {
    # 2026-10-02: thin rain streaks painted on the sky stand still in the game and look wrong
    "norain": (
        "Remove every thin rain streak and individual raindrop from this sky picture, everywhere, "
        "including the lines inside the dark clouds. Keep everything else exactly as it is: the same "
        "clouds in the same places, the same moonlit gap, the same colours and brightness, the same "
        "soft hazy rain veils near the horizon as blurred mist without any visible lines. Same framing, "
        "no ground, no text."
    ),
}


# The zenith: the band ends 65 degrees up and its top rows converge into a point,
# so the top of the dome is a separate square picture of the clouds seen straight
# up, laid flat over the viewer (tiled) and blended in above ~40 degrees.
CAP_PROMPT = (
    "Using the attached sky as the style and colour reference: a square picture of the SAME "
    "clouds (or smoke) seen from directly below, the camera pointing straight up at the zenith. Only "
    "clouds, filling the whole frame evenly, no horizon, no ground, no sun or moon disc, no rain "
    "streaks, no snowflakes, no embers, no particles, no light rays, no vignette, no text. Even "
    "overall brightness with no dark or bright corner, the same colours and lighting as the UPPER "
    "part of the reference. The texture must tile seamlessly on all four edges."
)


def draw_cap(name, raw, cap_raw):
    if not load_key():
        sys.exit("no OpenAI key (OPENAI_API_KEY or fractal_game\\key.conf)")
    from openai import OpenAI
    t = time.time()
    with open(raw, "rb") as f:
        r = OpenAI().images.edit(model=MODEL, image=f, prompt=CAP_PROMPT, n=1, size="1024x1024", quality="high")
    cap_raw.write_bytes(base64.b64decode(r.data[0].b64_json))
    print(f"{name}: zenith drawn in {time.time() - t:.0f} s -> {cap_raw}")


def build_cap(name, band, cap_raw):
    """Seamless both ways, then the colour statistics of the band's top 15% rows."""
    sys.path.insert(0, str(BESTIARY))
    from gen_flora_tex import seamless   # noqa: E402  (fractal_game: half-roll + blend)
    a = np.asarray(seamless(Image.open(cap_raw)), np.float32)
    ref = band[: band.shape[0] * 15 // 100].reshape(-1, 3)
    flat = a.reshape(-1, 3)
    a = (a - flat.mean(0)) / (flat.std(0) + 1e-3) * ref.std(0) + ref.mean(0)
    out = OUT / f"D2R_SkyCap_{name}.png"
    Image.fromarray(a.clip(0, 255).astype(np.uint8)).save(out)
    print(f"{name}: zenith texture {out}")
    return out


def edit(name, raw, fix):
    if not load_key():
        sys.exit("no OpenAI key (OPENAI_API_KEY or fractal_game\\key.conf)")
    from openai import OpenAI
    n = 1
    while (raw.parent / f"{name}_v{n}.png").exists():
        n += 1
    keep = raw.parent / f"{name}_v{n}.png"
    shutil.copy2(raw, keep)
    t = time.time()
    with open(keep, "rb") as f:
        r = OpenAI().images.edit(model=MODEL, image=f, prompt=EDITS[fix], n=1, size=SIZE, quality="high")
    raw.write_bytes(base64.b64decode(r.data[0].b64_json))
    print(f"{name}: '{fix}' applied in {time.time() - t:.0f} s -> {raw} (previous kept as {keep.name})")


def load_key():
    sys.path.insert(0, str(BESTIARY))
    import gen_sheets   # noqa: E402  (fractal_game's own key loader)
    return gen_sheets.load_key()


def draw(name, dest):
    if not load_key():
        sys.exit("no OpenAI key (OPENAI_API_KEY or fractal_game\\key.conf)")
    from openai import OpenAI
    t = time.time()
    r = OpenAI().images.generate(model=MODEL, prompt=SKIES[name] + " " + FRAME, n=1, size=SIZE, quality="high")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(base64.b64decode(r.data[0].b64_json))
    print(f"{name}: drawn in {time.time() - t:.0f} s -> {dest}")


def seamless_x(a):
    """Left and right edges meet: the picture rolled by half fills a band round the old edges."""
    w = a.shape[1]
    b = np.roll(a, w // 2, 1)
    x = np.abs(np.linspace(-1, 1, w))[None, :, None]
    k = np.clip((x - 0.7) / 0.3, 0, 1)
    k = k * k * (3 - 2 * k)
    return a * (1 - k) + b * k


def build(name, raw):
    a = np.asarray(Image.open(raw).convert("RGB"), np.float32)
    a = seamless_x(a)
    out = OUT / f"D2R_Sky_{name}.png"
    Image.fromarray(a.clip(0, 255).astype(np.uint8)).save(out)
    top = a[:24].reshape(-1, 3).mean(0) / 255.0
    print(f"{name}: texture {out} ({a.shape[1]}x{a.shape[0]}), top rows average {top.round(3).tolist()}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name", choices=sorted(SKIES))
    ap.add_argument("--redo", action="store_true")
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--edit", choices=sorted(EDITS))
    ap.add_argument("--redo-cap", action="store_true", help="draw the zenith picture again")
    args = ap.parse_args()
    raw = RAW / f"{args.name}.png"
    if args.redo or not raw.exists():
        draw(args.name, raw)
    if args.edit:
        edit(args.name, raw, args.edit)
    out = build(args.name, raw)
    cap_raw = RAW / f"{args.name}_cap.png"
    if args.redo_cap or not cap_raw.exists():
        draw_cap(args.name, raw, cap_raw)
    cap = build_cap(args.name, np.asarray(Image.open(out).convert("RGB"), np.float32), cap_raw)
    if args.install:
        for f in (out, cap):
            shutil.copy2(f, GAME_TEX / f.name)
            print(f"installed -> {GAME_TEX / f.name}")


if __name__ == "__main__":
    main()
