"""Fixtures for installer\\tests\\mod_layout_test.py: an "earlier install" the way it really is, not built by hand.

1. install_0169 + run_0169: a game folder as D2R VR 0.169's own Setup leaves it. The file layout is DERIVED from the 0.169 Setup script
   (tests\\fixtures\\D2R_VR_Setup_0.169.iss = master d919f8b's installer\\D2R_VR_Setup.iss): every [Files] line is replayed onto a scratch folder with the real
   0.169 payload files (flags and checks as Inno reads them: onlyifdoesntexist, skipifsourcedoesntexist, wildcards, Excludes, recursesubdirs; the lines for
   %LOCALAPPDATA% are BodyWalk's and not in the game folder). What the program wrote while running is added the way 0.169 writes it (the player's
   ini and its backups, ReShade from the Install ReShade button with its ini and presets, the dumps, the logs).
2. snapshot_real: the small files of a real game folder copied into a scratch folder (read only on the source side), for a migration of what a
   real machine looks like after weeks of test builds.
Neither runs anything of the machine: files are copied, nothing is executed.
"""
import re
import shutil
from pathlib import Path

_FIELD = re.compile(r'\s*(\w+)\s*:\s*(?:"([^"]*)"|([^;"]*))\s*(?:;|$)')


def parse_files_section(text):
    """The lines of [Files] of an Inno script as dicts of their parameters (Source, DestDir, DestName, Flags, Check, Excludes)."""
    out, inside = [], False
    for raw in text.replace("\r\n", "\n").split("\n"):
        line = raw.strip()
        if line.startswith("["):
            inside = line.lower() == "[files]"
            continue
        if not inside or not line.startswith("Source:"):
            continue
        fields, pos = {}, 0
        while pos < len(line):
            m = _FIELD.match(line, pos)
            if not m or m.end() == pos:
                raise ValueError("cannot read: " + line)
            fields[m.group(1)] = m.group(2) if m.group(2) is not None else m.group(3).strip()
            pos = m.end()
        out.append(fields)
    return out


def _put_file(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)


def install_0169(game: Path, payload: Path, iss_text: str, license_file: Path, size_cap=12 << 20):
    """Replays the [Files] lines of the 0.169 Setup (a VR install on a PC with no BodyWalk: IsVR, UsePortable and PortableFresh are true).
    Returns the list of game-relative paths it made. Files over size_cap are left out of the BodyWalk Portable copy (DirectML and onnxruntime, 35 MB:
    the move of the folder does not care what is inside)."""
    made = []
    for f in parse_files_section(iss_text):
        dd = f["DestDir"]
        if "{localappdata}" in dd:
            continue                                            # BodyWalk's own folder: not in the game's
        check = f.get("Check", "")
        if check not in ("", "IsVR", "UsePortable", "PortableFresh"):
            raise ValueError("a check the fixture does not know: " + check)
        flags = f.get("Flags", "").split()
        rest = dd.replace("{#PortableDir}", "{app}\\BodyWalkVR").replace("{app}", "").lstrip("\\")
        dest_dir = game / rest if rest else game
        src = f["Source"]
        if src.startswith("payload\\"):
            sp = payload / src[len("payload\\"):].replace("\\", "/")
        elif src == "..\\LICENSE":
            sp = license_file
        else:
            raise ValueError("a source the fixture does not know: " + src)
        excludes = {x.strip().lower() for x in f.get("Excludes", "").split(",") if x.strip()}
        if "*" in sp.name:
            if "recursesubdirs" in flags:
                files = [p for p in sorted(sp.parent.rglob("*")) if p.is_file()]
            else:
                files = [p for p in sorted(sp.parent.glob(sp.name)) if p.is_file()]
        else:
            if not sp.is_file():
                if "skipifsourcedoesntexist" in flags:
                    continue
                raise FileNotFoundError(sp)
            files = [sp]
        for p in files:
            if p.name.lower() in excludes:
                continue
            if "recursesubdirs" in flags and p.stat().st_size > size_cap:
                continue
            rel = p.relative_to(sp.parent) if "*" in sp.name else Path(f.get("DestName") or p.name)
            dst = dest_dir / rel
            if "onlyifdoesntexist" in flags and dst.exists():
                continue
            _put_file(p, dst)
            made.append(dst.relative_to(game).as_posix())
        if "createallsubdirs" in flags:                         # (empty folders of the portable copy come with it)
            for d in sorted(sp.parent.rglob("*")):
                if d.is_dir():
                    (dest_dir / d.relative_to(sp.parent)).mkdir(parents=True, exist_ok=True)
    return made


def run_0169(game: Path, put, reshade_files: dict, toml_text: str, tuned_ini_extra: str):
    """What 0.169 and its user wrote into the game folder AFTER Setup: the player's own d2r_vr.ini (tuned) and its backups, Settings' window file, the headset
    and depth inis, the game-code dump, the skill-icon cache, ReShade as the Install ReShade button leaves it (beside D2R.exe: its dll, ini, presets and our mark),
    the add-on's log, ReShade's log, D2RLoader's logs, plus the game's and D2RLoader's own files. `put(path, data)` writes a file; reshade_files:
    {"ReShade64.dll": bytes, "ReShade.ini": bytes, "ReShadePreset.ini": bytes}."""
    p = game / "d2rloader" / "plugins"
    ini = p / "d2r_vr.ini"
    base = ini.read_text(encoding="utf-8", errors="replace")
    tuned = base.replace("platform=1", "platform=1") + tuned_ini_extra
    shutil.copyfile(ini, p / "d2r_vr.ini.before-0.168")             # what Setup's question "keep or take the new defaults" leaves behind
    put(p / "d2r_vr.ini.before_my_own_try", base[: len(base) // 2])    # the player's own backups, named as he likes
    put(ini, tuned.encode("utf-8"))
    put(p / "d2r_vr_settings.ini", "[window]\r\nzoom=1.3\r\nwidth=1400\r\nheight=900\r\n")
    put(p / "d2r_vr_headset.ini", "[headset]\r\nrec_w=2064\r\nrec_h=2208\r\n")
    put(p / "d2r_vr_depth.ini", "[depth]\r\nsize=1920x1080\r\n")
    put(p / "d2r_vr_game_code.txt", "game code dump of this machine\n" * 50)
    put(p / "d2r_vr_skill_icons" / "manifest.tsv", "s36\tab12.png\tskillname36\n")
    put(p / "d2r_vr_skill_icons" / "trees.tsv", "0\ta\tb\tc\n")
    put(p / "d2r_vr_skill_icons" / "ab12.png", b"\x89PNG\r\n\x1a\n" + b"icon" * 100)
    put(p / "d2r_vr_reshade_ours.txt", "ReShade was installed by D2R VR Settings.")
    for name, data in reshade_files.items():
        put(game / name, data)
    put(game / "ReShade.log", "ReShade 6.x log of the last run\n" * 20)
    put(game / "FlatVR_DepthProvider.log", "FlatVR Depth Provider, 64-bit\n" * 10)
    put(game / "d2rloader" / "logs" / "d2rloader.log", "D2RLoader log\n" * 100)
    put(game / "d2rloader" / "logs" / "d2r-vr-vrcam.log", "vrcam log\n" * 100)
    put(game / "d2rloader" / "logs" / "d2r_vr_start.txt", "start\n")
    put(game / "d2rloader" / "logs" / "d2r_vr_launch.txt", "start: D2R VR in the game\n")
    put(game / "d2rloader" / "config" / "d2rloader.toml", toml_text)


def snapshot_real(real: Path, dest: Path):
    """Copies the SMALL files of a real game folder that D2R VR touches into `dest` (the real folder is only read): d2rloader\\plugins (the plugin, the inis and
    all their backups, the skill icons; a sample of the plugin's big backups and dumps, cut to 1 MB), d2rloader\\config, ReShade's files and logs, the add-on, the
    shaders and sky pictures, the Settings program with a sample of its backups, and a stand-in of BodyWalk Portable (the big libraries left out). The game's
    own files and D2RLoader are stand-ins, not copies. Returns the list of what was copied (game-relative)."""
    copied = []

    def cp(src: Path, rel: str, cut=None):
        d = dest / rel
        d.parent.mkdir(parents=True, exist_ok=True)
        if cut is None:
            shutil.copyfile(src, d)
        else:
            d.write_bytes(src.read_bytes()[:cut])
        copied.append(rel.replace("\\", "/"))

    plugins = real / "d2rloader" / "plugins"
    big_backups = sorted(p for p in plugins.glob("d2rl-vrcam.dll.*") if p.is_file())[:3]
    for p in sorted(plugins.iterdir()):
        if p.is_dir():
            if p.name == "d2r_vr_skill_icons":
                for q in sorted(p.rglob("*")):
                    if q.is_file():
                        cp(q, "d2rloader\\plugins\\" + str(q.relative_to(plugins)))
            else:
                (dest / "d2rloader" / "plugins" / p.name).mkdir(parents=True, exist_ok=True)      # d2r_vr_icons, d2r_vr_potions, the backup folders: kept as empty folders
            continue
        if p.name == "d2rl-vrcam.dll" or p.stat().st_size <= 1 << 20:
            cp(p, "d2rloader\\plugins\\" + p.name)
        elif p in big_backups or p.name == "d2r_vr_frames.csv":
            cp(p, "d2rloader\\plugins\\" + p.name, cut=1 << 20)          # (a sample of the big backups and dumps, cut to 1 MB; the rest is not copied)
    for sub in ("config", "data"):
        for p in sorted((real / "d2rloader" / sub).glob("*")):
            if p.is_file() and p.stat().st_size < 1 << 20:
                cp(p, f"d2rloader\\{sub}\\{p.name}")
    for p in sorted((real / "d2rloader" / "logs").glob("*")):
        if p.is_file() and p.stat().st_size < 1 << 20:
            cp(p, "d2rloader\\logs\\" + p.name)
    roots = ["ReShade.ini", "ReShadePreset.ini", "ReShade.log", "ReShade.log1", "ReShade64.dll", "FlatVR_DepthProvider.log", "FlatVR_Keepalive.addonfx",
             "D2R_VR_LICENSE.txt", "D2R_VR_Settings.exe"]
    roots += [p.name for p in real.glob("ReShade.ini.*")] + [p.name for p in real.glob("FlatVR_DepthProvider.addon64*")]
    roots += [p.name for p in sorted(real.glob("D2R_VR_Settings.exe.*"))[:3]]
    for name in roots:
        if (real / name).is_file():
            cp(real / name, name)
    for p in sorted((real / "reshade-shaders").rglob("*")):
        if p.is_file():
            cp(p, "reshade-shaders\\" + str(p.relative_to(real / "reshade-shaders")))
    bw = real / "BodyWalkVR"
    if bw.is_dir():
        for p in sorted(bw.iterdir()):
            if p.is_dir():
                (dest / "BodyWalkVR" / p.name).mkdir(parents=True, exist_ok=True)
            elif p.stat().st_size < 7 << 20:
                cp(p, "BodyWalkVR\\" + p.name)
    for d in ("D2R_Sky_ours", "mods"):
        if (real / d).is_dir():
            (dest / d).mkdir(parents=True, exist_ok=True)
    # the game's and D2RLoader's own files: stand-ins (not copied, not run)
    for name in ("D2R.exe", "D2RLoader.exe", "D2RCore.dll", "d2rloader.mpq", "steam_api64.dll"):
        d = dest / name
        d.write_bytes(("stand-in for " + name).encode())
    return copied
