"""The settings program's icon, drawn by OpenAI (2026-10-05).

  python gen_icon.py [--redo]

tools/settings/d2r_vr.ico (16..256 px) from extracted/raw_icon/icon.png. No game
logo or lettering: our own emblem in the game's dark mood, readable at 16 px.
"""
import argparse
import base64
import pathlib
import sys

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gen_sky   # noqa: E402  (MODEL, load_key)

RAW = HERE.parent / "extracted" / "raw_icon"
ICO = HERE / "settings" / "d2r_vr.ico"

PROMPT = (
    "A square app icon: a virtual-reality headset seen from the front, forged from dark blackened iron with "
    "riveted gothic edges and small curved demon horns on top, its two lenses glowing deep ember red and orange "
    "like hellfire. Centered, bold simple silhouette that stays readable at 16 pixels, strong contrast, a dark "
    "charcoal-to-black rounded-square background with a faint red glow behind the headset. Dark fantasy, painterly "
    "but clean, like a classic action-RPG's emblem. No text, no letters, no logo, no frame beyond the rounded square."
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--redo", action="store_true")
    args = ap.parse_args()
    RAW.mkdir(parents=True, exist_ok=True)
    raw = RAW / "icon.png"
    if args.redo or not raw.exists():
        if not gen_sky.load_key():
            sys.exit("no OpenAI key (OPENAI_API_KEY or fractal_game\\key.conf)")
        from openai import OpenAI
        r = OpenAI().images.generate(model=gen_sky.MODEL, prompt=PROMPT, n=1, size="1024x1024", quality="high")
        raw.write_bytes(base64.b64decode(r.data[0].b64_json))
        print("drawn:", raw)
    im = Image.open(raw).convert("RGBA")
    # the drawing's own rounded square, its corners made clear
    from PIL import ImageDraw
    w = im.size[0]
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([int(w * 0.02), int(w * 0.02), int(w * 0.98), int(w * 0.98)], radius=int(w * 0.16), fill=255)
    im.putalpha(mask)
    im.save(ICO, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("icon:", ICO)


if __name__ == "__main__":
    main()
