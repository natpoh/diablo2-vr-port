"""Which of the game's passes draw into its full-screen targets - from Ctrl+F10's trace.

    python tools/trace_passes.py [d2r_vr_uitrace.txt] [--format R11G11B10_FLOAT] [--depth] [--names <hashes.txt>]

Lists every counted run of draws into a full-size target of that format (the lighting by
default) with its pipeline ("pipe", this run's handle - for [debug] skip_pipeline) and its
pixel shader's hash ("ps", the same every run - for [render] skip_shaders). --depth keeps
only the runs drawn with a depth buffer bound: full-screen post passes that read the depth
(fog, veils) - the usual suspects for a straight line across the picture. See
docs/DEVELOPER_NOTES.md, "Finding a game pass".

--names: a "<hash> <shader name> ..." line per shader (the game's own data/hd/shaders/*.bin,
each DXBC container hashed as uitrace.cpp does - FNV-1a 64 of its bytes) puts each pass's
shader name beside its hash. Made on your own machine from your copy of the game: the names
and the shaders are Blizzard's, never in this repository.
"""
import re
import sys

DEFAULT = r"D:\SteamLibrary\steamapps\common\Diablo II Resurrected\d2rloader\plugins\d2r_vr_uitrace.txt"


def main():
    args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith("--") and sys.argv[i - 1] not in ("--format", "--names")]
    path = args[0] if args else DEFAULT
    fmt = "R11G11B10_FLOAT"
    if "--format" in sys.argv:
        fmt = sys.argv[sys.argv.index("--format") + 1]
    depth_only = "--depth" in sys.argv
    names = {}
    if "--names" in sys.argv:
        for l in open(sys.argv[sys.argv.index("--names") + 1], encoding="utf-8", errors="ignore"):
            p = l.split()
            if len(p) >= 2:
                names[p[0]] = " ".join(p[1:3])
    lines = open(path, encoding="utf-8", errors="ignore").read().split("\n")
    big = set()
    size = None
    for l in lines:
        for m in re.finditer(r"#([0-9a-f]+) (\d+)x(\d+) " + re.escape(fmt), l):
            w, h = int(m.group(2)), int(m.group(3))
            if size is None or w * h > size[0] * size[1]:
                size = (w, h)
    for l in lines:
        for m in re.finditer(r"#([0-9a-f]+) (\d+)x(\d+) " + re.escape(fmt), l):
            if size and (int(m.group(2)), int(m.group(3))) == size:
                big.add(m.group(1))
    print(f"full-size {fmt} targets {size}: {', '.join(sorted(big))}")
    for l in lines:
        if "draws" not in l or "last pipe" not in l or "->" not in l:
            continue
        target = l.split("->", 1)[1]
        ids = re.findall(r"#([0-9a-f]+)", target)
        if not ids or ids[0] not in big:
            continue
        if depth_only and "depth" not in target:
            continue
        m = re.search(r"^\s*(\d+).*?(\d+) draws.*last pipe ([0-9a-f]+)(?: ps ([0-9a-f]+))?", l)
        if m:
            name = names.get(m.group(4) or "", "")
            print(f"#{m.group(1):>5}  {m.group(2):>5} draws  pipe {m.group(3):>12}  ps {m.group(4) or '?':>16} {name:<28} -> {target.strip()[:70]}")


if __name__ == "__main__":
    main()
