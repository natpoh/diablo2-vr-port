"""uitex_*.raw (vr/uitrace.cpp, Ctrl+F10 in the game) -> PNG, colour and alpha apart.

    python uitex_to_png.py <folder with the .raw files> [out folder]

Each texture gives <name>.png (as stored, alpha kept) and <name>_alpha.png
(the alpha channel as grey: where a layer such as the interface is opaque).
"""
import glob
import os
import struct
import sys

from PIL import Image

BGRA = {86, 87, 90, 91}          # B8G8R8A8 / X8 variants
RGBA = {27, 28, 29}              # R8G8B8A8 variants


def half_rgba(data, w, h, pitch):
    """R16G16B16A16_FLOAT -> 8-bit RGBA: colour through x/(1+x) (HDR), alpha clamped."""
    import numpy as np
    a = np.frombuffer(data, dtype=np.uint8)[:h * pitch].reshape(h, pitch)[:, :w * 8].copy().view(np.float16).reshape(h, w, 4).astype(np.float32)
    rgb = np.nan_to_num(a[..., :3], nan=0.0, posinf=0.0, neginf=0.0).clip(0, None)
    rgb = (rgb / (1.0 + rgb)) ** (1 / 2.2)
    alpha = np.nan_to_num(a[..., 3:]).clip(0, 1)
    out = np.concatenate([rgb, alpha], axis=2)
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8), 'RGBA')


def other(data, w, h, pitch, fmt):
    """The lighting's and masks' formats -> 8-bit RGBA (HDR through x/(1+x), one channel as grey)."""
    import numpy as np
    raw = np.frombuffer(data, dtype=np.uint8)[:h * pitch].reshape(h, pitch)
    if fmt == 26:   # R11G11B10_FLOAT: 6e5 / 6e5 / 5e5, no sign
        v = raw[:, :w * 4].copy().view(np.uint32).reshape(h, w).astype(np.uint64)
        def f(bits, mbits):
            e = (bits >> mbits).astype(np.int64); m = (bits & ((1 << mbits) - 1)).astype(np.float64)
            return np.where(e == 0, m / (1 << mbits) * 2.0 ** -14, (1 + m / (1 << mbits)) * 2.0 ** (e - 15))
        rgb = np.stack([f(v & 0x7FF, 6), f((v >> 11) & 0x7FF, 6), f((v >> 22) & 0x3FF, 5)], axis=2)
        rgb = (rgb / (1.0 + rgb)) ** (1 / 2.2)
    elif fmt == 61:
        g = raw[:, :w].astype(np.float64) / 255.0
        rgb = np.stack([g, g, g], axis=2)
    elif fmt == 54:
        g = np.nan_to_num(raw[:, :w * 2].copy().view(np.float16).reshape(h, w).astype(np.float64)).clip(0, None)
        g = g / (1.0 + g)
        rgb = np.stack([g, g, g], axis=2)
    elif fmt == 56:
        g = raw[:, :w * 2].copy().view(np.uint16).reshape(h, w).astype(np.float64) / 65535.0
        rgb = np.stack([g, g, g], axis=2)
    else:           # 34 R16G16_FLOAT
        a = np.nan_to_num(raw[:, :w * 4].copy().view(np.float16).reshape(h, w, 2).astype(np.float64))
        a = a * 0.5 + 0.5
        rgb = np.concatenate([a, np.zeros((h, w, 1))], axis=2)
    out = np.concatenate([rgb.clip(0, 1), np.ones((h, w, 1))], axis=2)
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8), 'RGBA')


def convert(path, out_dir):
    with open(path, 'rb') as f:
        w, h, fmt, pitch = struct.unpack('<4I', f.read(16))
        data = f.read()
    if fmt == 10:
        img = half_rgba(data, w, h, pitch)
    elif fmt in (26, 61, 54, 56, 34):
        img = other(data, w, h, pitch, fmt)
    else:
        rows = b''.join(data[y * pitch:y * pitch + w * 4] for y in range(h))
        mode = 'BGRA' if fmt in BGRA else 'RGBA'
        img = Image.frombytes('RGBA', (w, h), rows, 'raw', mode)
    base = os.path.join(out_dir, os.path.splitext(os.path.basename(path))[0])
    img.save(base + '.png')
    img.getchannel('A').save(base + '_alpha.png')
    a = img.getchannel('A')
    lo, hi = a.getextrema()
    print(f'{os.path.basename(path)}: {w}x{h} fmt {fmt}, alpha {lo}..{hi}')


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else src
    os.makedirs(out, exist_ok=True)
    for p in sorted(glob.glob(os.path.join(src, 'uitex_*.raw'))):
        convert(p, out)


if __name__ == '__main__':
    main()
