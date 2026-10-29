#!/usr/bin/env python3
"""Tests installer\\mod_layout.inc, installer\\ini_choice.inc and the [Files] lines of installer\\mod_files.inc by running them: Setup and the
uninstaller of a stand-in for D2R VR Authentic (installer\\tests\\mod_layout_test.iss) in a scratch folder.

    python installer\\tests\\mod_layout_test.py <scratch folder> --standin <fake_launcher_setup.exe> --standin-fail <fake_launcher_setup_fail.exe> [--iscc ISCC.exe]

Nothing outside the scratch folder is touched: CreateUninstallRegKey=no, no BodyWalk, no network, not the player's game folder; the registry only
for section 8, in a throw-away key HKCU\\Software\\D2R VR Launcher Test <pid> that is deleted again (the launcher's own key is only read in 7).
The real launcher's Setup is never run: a stand-in (installer\\tests\\fake_launcher_setup.cpp) takes its place. The scratch path should be short.

Cases:
 1. a clean game (VR, then flat): the mod's files, modinfo.json, the launcher's manifest + icon, default_mod, nothing outside the mod but Settings.exe;
    the uninstaller takes away exactly that.
 2. an earlier global install (0.167 and older): moved into the mod, the player's d2r_vr.ini byte for byte, ReShade only when it was ours, what is
    there wins, a dxgi.dll ReShade of the player's.
 3. d2r-pcvr as mods\\D2R-PCVR next to a clean game and next to an earlier D2R VR install: not one byte of it changes by Setup or the uninstaller;
    its default_mod stays.
 4. bearstonem's legacy global install (d2rloader\\plugins\\d2rl-vrcam.dll + pcvr-install.json): left alone by Setup and the uninstaller.
 5. an update over an existing mods\\D2R-VR: the new plugin and manifest, the player's ini kept (the new keys added), an unchanged ini takes the new defaults.
 6. the uninstaller: only mods\\D2R-VR and the default_mod it set; a default that is not ours stays.
 7. the launcher lookup for the finish page.
 8. the launcher carried inside Setup: installed silently as the original user when missing or older (the stand-in is started with exactly
    /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS=), not when the same or newer is installed (dotted numbers, up to four parts), a failing or
    unstartable one never stops the mod's install, the uninstaller leaves the launcher and its registry key.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import winreg
from pathlib import Path

import argparse
import ctypes

ap = argparse.ArgumentParser()
ap.add_argument("scratch")
ap.add_argument("--iscc", default=r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe")
ap.add_argument("--standin", required=True, help="fake_launcher_setup.exe: the carried launcher Setup's stand-in (exit 0)")
ap.add_argument("--standin-fail", required=True, help="fake_launcher_setup_fail.exe: the same, exit code 5")
ap.add_argument("--payload169", help="D2R VR 0.169's staged payload (installer\\payload of the build that made it): the real files of the earlier-install fixture")
ap.add_argument("--settings-exe", help="the real D2R_VR_Settings.exe: Setup and the uninstaller run it for the player's dxgi.dll ReShade (--reshade-paths add / remove); without it section 2d is skipped")
ap.add_argument("--real-game", help="a real game folder (read only): its small files are copied into a scratch folder for the second migration fixture")
args = ap.parse_args()
work = Path(args.scratch).resolve()
iscc = args.iscc
here = Path(__file__).resolve().parent
LAUNCHER_KEY = r"Software\D2R VR Launcher Test " + str(os.getpid())    # a throw-away key: never the launcher's own
LAUNCHER_VERSION = "0.2.0"
LAUNCHER_FILE = f"D2R_VR_Launcher_Setup_{LAUNCHER_VERSION}.exe"
SWITCHES = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS="
repo = here.parent.parent
# the version the manifest tool accepts as a base: vr/vrcam.cpp's g_info_version (a label such as "<it>-launcher-test" must start with it)
BASE_VERSION = re.search(r'g_info_version\[\] = "([0-9.]+)"', (repo / "vr" / "vrcam.cpp").read_text(encoding="utf-8", errors="replace")).group(1)
game, payload, out = work / "game", work / "payload", work / "out"
failures = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    print(("  ok    " if cond else "  FAIL  ") + what)
    if not cond:
        failures.append(what)


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def snap(root):
    return {p.relative_to(root).as_posix(): sha(p) for p in sorted(root.rglob("*")) if p.is_file()}


def sub(s, prefix):
    return {k: v for k, v in s.items() if k.startswith(prefix)}


def put(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))


def rd(p):
    return Path(p).read_text(encoding="utf-8", errors="replace")


def ini_keys(text):
    sec, d = "", {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line[0] in ";#":
            continue
        if line[0] == "[":
            sec = line[1:line.index("]")]
        elif "=" in line:
            k, v = line.split("=", 1)
            d[(sec.lower(), k.strip().lower())] = v.split(";")[0].strip()
    return d


NOISE = bytes((i * 131 + 7) % 251 for i in range(1 << 16)) * 16        # 1 MB
DLL_V1 = b"MZ" + b"\0" * 62 + b"D2R VR vrcam 0.167 (test payload v1)\0" + NOISE[:300000]
DLL_V2 = b"MZ" + b"\0" * 62 + b"D2R VR vrcam 0.168 (test payload v2)\0" + NOISE[:300000]
DLL_OLD = b"MZ" + b"\0" * 62 + b"D2R VR vrcam 0.160 (an earlier install, global plugin)\0" + NOISE[:200000]
OLD_WEIGHT = b"MZ weight library of an earlier install (global)\0" + NOISE[:7000]
OLD_GRIP = b"MZ grip library of an earlier install (global)\0" + NOISE[7000:12000]
NEW_WEIGHT = b"MZ weight library of 0.169 (d2r_vr_libs)\0" + NOISE[12000:19000]
NEW_GRIP = b"MZ grip library of 0.169 (d2r_vr_libs)\0" + NOISE[19000:24000]
BEAR_DLL = b"MZ" + b"\0" * 62 + NOISE[:500000] + "d2r_vr_icons".encode("utf-16-le") + b"\0d2r-vr-vrcam\0" + NOISE[500000:]
DEFAULTS_V1 = ("[mode]\nplatform=1\n[openxr]\non=1\ndirect_input=1\n[hands]\nbow_draw=1\nweight=1\n[render]\nfog=30\n").replace("\n", "\r\n")
DEFAULTS_V2 = DEFAULTS_V1 + "[new]\nthing=7\n".replace("\n", "\r\n")
PLAYERS_RESHADE_INI = ("[ADDON]\nDisabledAddons=\nOverlayCollapsed=Effect Runtime Sync,FlatVR Depth Provider v2.19@FlatVR_DepthProvider.addon64\n\n[DEPTH]\nDepthCopyBeforeClears=2\n\n"
                       "[GENERAL]\nEffectSearchPaths=.\\reshade-shaders\\Shaders\\**\nIntermediateCachePath=C:\\Temp\\ReShade\nNoDebugInfo=1\nPresetPath=.\\ReShadePreset.ini\n"
                       "TextureSearchPaths=.\\reshade-shaders\\Textures\\**\n\n[OVERLAY]\nDocking=[Docking][Data],DockSpace   ID=0xB0DF600F Pos=8,,8 Size=1008,,1008 Split=X\nShowFPS=2\n"
                       ).replace("\n", "\r\n").encode("utf-8")   # (the shape of a real ReShade 6.8 ini: the player's own, in the game's folder)
TOML = ('[d2rloader]\r\nallow_global_extensions = false\r\nallow_mod_extensions = true\r\ndefault_mod = ""\r\n'
        '[logging]\r\nlevel = "info"\r\n')


def make_payload(tag, dll, defaults, version, libs=True, settings_exe=None):
    """What build_installer.ps1 stages (installer\\payload), with the REAL manifest tool making the launcher's files.
    libs: the weight and grip libraries are staged (the private weight_physics build); False = this build ships none.
    settings_exe: the real D2R_VR_Settings.exe instead of a stand-in file (Setup runs it for the player's dxgi.dll ReShade)."""
    if payload.exists():
        shutil.rmtree(payload)
    put(payload / "plugins/d2rl-vrcam.dll", dll)
    if libs:
        put(payload / "plugins/d2r_vr_weight.dll", b"MZ weight library " + tag.encode() + NOISE[:5000])
        put(payload / "plugins/d2r_vr_grip.dll", b"MZ grip library " + tag.encode() + NOISE[5000:9000])
    put(payload / "plugins/d2r_vr.ini", defaults)
    put(payload / "plugins/D2R_VR_Settings.exe", Path(settings_exe).read_bytes() if settings_exe else b"MZ SETTINGS " + tag.encode())
    put(payload / "shaders/D2R_DepthFog.fx", "// D2R VR shader " + tag + "\n")
    put(payload / "license/D2R_VR_LICENSE.txt", (repo / "LICENSE").read_bytes())
    put(payload / "lang/en.txt", "language.name = English\n# " + tag + "\n")
    put(payload / "lang/ru.txt", "language.name = Русский\n# " + tag + "\n")
    put(payload / "sky/D2R_Sky_act1.png", "SKY-A1 " + tag)
    put(payload / "sky/D2R_Ceiling_cave.png", "CEILING " + tag)
    put(payload / "game/FlatVR_DepthProvider.addon64", b"MZ ADDON " + tag.encode())
    put(payload / "game/FlatVR_Keepalive.addonfx", "// keepalive " + tag + "\n")
    r = subprocess.run([sys.executable, "-I", str(repo / "tools" / "make_vrmod_manifest.py"), "--out", str(payload / "launcher"),
                        "--payload", str(payload), "--version-label", version], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        sys.exit("the manifest tool failed")


def compile_setup(tag, launcher=False):
    """launcher: the installer carries the launcher Setup staged in payload/launcher_setup (the stand-in, or a file that is not a program)."""
    env = dict(os.environ, MOD_LAYOUT_TEST_DIR=str(work), MOD_LAYOUT_TEST_LAUNCHER_KEY=LAUNCHER_KEY)
    env.pop("MOD_LAYOUT_TEST_LAUNCHER_FILE", None)
    if launcher:
        env.update(MOD_LAYOUT_TEST_LAUNCHER_FILE=LAUNCHER_FILE, MOD_LAYOUT_TEST_LAUNCHER_VERSION=LAUNCHER_VERSION)
    r = subprocess.run([iscc, "/Q", str(here / "mod_layout_test.iss")], capture_output=True, text=True, env=env)
    print((r.stdout + r.stderr).strip()[-2500:])
    ok = r.returncode == 0 and (out / "mod_layout_test_setup.exe").exists()
    if ok:
        shutil.copyfile(out / "mod_layout_test_setup.exe", out / f"setup_{tag}.exe")
    return ok


def run_setup(tag, which, vr=True, env_extra=None):
    log = work / f"{tag}.log"
    for f in ("ready.txt", "moved.txt", "launcher.txt", "launcher_result.txt", "launcher_record.txt"):
        (work / f).unlink(missing_ok=True)
    env = dict(os.environ, MOD_LAYOUT_TEST_VR="1" if vr else "0")
    env.update(env_extra or {})
    r = subprocess.run([str(out / f"setup_{which}.exe"), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", f"/LOG={log}", f"/DIR={game}"],
                       capture_output=True, text=True, timeout=300, env=env)
    return r.returncode, (log.read_text(encoding="utf-8", errors="replace") if log.exists() else "")


def run_uninstall(tag):
    log = work / f"{tag}.log"
    exe = game / "unins000.exe"
    r = subprocess.run([str(exe), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", f"/LOG={log}"], capture_output=True, text=True, timeout=300)
    t0 = time.time()
    while time.time() - t0 < 90:       # the uninstaller hands over to a copy of itself: it is done when its log is closed
        if log.exists() and "Log closed." in log.read_text(encoding="utf-8", errors="replace"):
            break
        time.sleep(0.2)
    time.sleep(0.4)
    return r.returncode, (log.read_text(encoding="utf-8", errors="replace") if log.exists() else "")


# ------------------------------------------------------------------ the game folders
def fresh_game(*kinds):
    if game.exists():
        shutil.rmtree(game)
    put(game / "D2R.exe", "GAME")
    put(game / "D2RLoader.exe", "D2RLOADER")
    put(game / "d2rloader/config/d2rloader.toml", TOML)
    put(game / "Settings.json", "{}")
    for k in kinds:
        {"bear_mod": bears_mod, "bear_legacy": bears_legacy, "old_ours": old_ours, "old_ours_players_reshade": lambda g: old_ours(g, ours_reshade=False),
         "dxgi": lambda g: put(g / "dxgi.dll", "PLAYERS RESHADE AS DXGI"),
         "dxgi_ini": lambda g: (put(g / "dxgi.dll", b"MZ PLAYERS RESHADE AS DXGI" + NOISE[:3000]), put(g / "ReShade.ini", PLAYERS_RESHADE_INI),
                                put(g / "ReShadePreset.ini", "Techniques=Players@Own.fx\r\n"), put(g / "reshade-shaders/Shaders/Own.fx", "// his own effect\r\n"))}[k](game)


def bears_mod(g):
    """bearstonem's d2r-pcvr as the D2RLoader mod D2R-PCVR: all in its own folder."""
    m = g / "mods/D2R-PCVR"
    put(m / "D2R-PCVR.mpq/modinfo.json", '{ "name": "D2R-PCVR", "savepath": "../" }')
    put(m / "vrmod-manifest.json", '{ "schema": "vrmod-manifest/1", "id": "d2r-pcvr" }')
    put(m / "launcher_icon.png", "BEAR-ICON")
    put(m / "d2rloader/plugins/d2rl-pcvr.dll", BEAR_DLL)
    put(m / "d2rloader/plugins/d2r_pcvr.ini", "[calibration]\nbear=1\n")
    put(m / "d2rloader/plugins/d2r_vr_icons/a.png", "ICON")
    put(m / "reshade/ReShade64.dll", "BEARS RESHADE")
    put(m / "reshade/ReShade.ini", "[GENERAL]\nbear=1\n")
    put(m / "reshade/reshade-shaders/Shaders/D2R_DepthFog.fx", "// BEAR shader (same name as ours)\n")
    put(m / "reshade/reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_act1.png", "BEAR-SKY")
    put(m / "openxr_loader.dll", "OPENXR")


def bears_legacy(g):
    """d2r-pcvr up to 0.3.15: GLOBAL, with the same file names as ours; its install record says whose they are."""
    put(g / "d2rloader/plugins/d2rl-vrcam.dll", BEAR_DLL)
    put(g / "d2rloader/plugins/d2r_vr.ini", "[calibration]\nbear=1\n")
    put(g / "d2rloader/plugins/d2r_vr_game_code.txt", "bear game code")
    put(g / "d2rloader/plugins/d2r_vr_icons/a.png", "ICON")
    put(g / "d2rloader/plugins/d2r_vr_reshade_ours.txt", "left by an earlier D2R VR Setup")
    put(g / "d2rloader/pcvr-install.json", '{"snapshots": []}')
    put(g / "reshade-shaders/Shaders/D2R_DepthFog.fx", "// BEAR shader\n")
    put(g / "reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_act1.png", "SKY-A1")
    put(g / "D2R_Sky_ours/D2R_Sky_act1.png", "SKY-A1")
    put(g / "ReShade64.dll", "RESHADE")
    put(g / "ReShade.ini", "[GENERAL]\nbear=1\n")
    put(g / "D2R_Frame_ReShade.ini", "Techniques=D2R_DepthFog@D2R_DepthFog.fx\n")
    put(g / "openxr_loader.dll", "OPENXR")


PLAYERS_INI = ("[mode]\r\nplatform=0\r\n[openxr]\r\non=1\r\ndirect_input=1\r\n[hands]\r\nbow_draw=0 ; the player's choice\r\nweight=1\r\n"
               "[render]\r\nfog=55\r\n[calibration]\r\nheight=1.77\r\n")


def old_ours(g, ours_reshade=True, players_ini=PLAYERS_INI):
    """D2R VR up to 0.167: the plugin in d2rloader\\plugins, ReShade, the shader, the sky and FlatVR's add-on beside D2R.exe."""
    p = g / "d2rloader/plugins"
    put(p / "d2rl-vrcam.dll", DLL_OLD)
    put(p / "d2r_vr.ini", players_ini)
    put(p / "d2r_vr.default.ini", DEFAULTS_V1)
    put(p / "d2r_vr_headset.ini", "[headset]\nipd=0.063\n")
    put(p / "d2r_vr_settings.ini", "[window]\nzoom=1.2\n")
    put(p / "d2r_vr_depth.ini", "[depth]\nx=1\n")
    put(p / "d2r_vr_skill_icons/s1.png", "ICON1")
    put(p / "d2r_vr_skill_icons/sub/s2.png", "ICON2")
    put(p / "d2r_vr_game_code.txt", "dump")
    put(p / "d2rl-vrcam.dll.prev", "OLDER BACKUP")
    put(p / "other_plugin.dll", "SOMEONE ELSES PLUGIN")
    put(p / "d2r_vr_weight.dll", OLD_WEIGHT)             # 0.168 put the optional libraries beside the global plugin
    put(p / "d2r_vr_grip.dll", OLD_GRIP)
    if ours_reshade:
        put(p / "d2r_vr_reshade_ours.txt", "ReShade was installed by D2R VR Setup.")
        put(g / "ReShade64.dll", "OUR RESHADE")
        put(g / "ReShade.ini", "[GENERAL]\r\nEffectSearchPaths=.\\reshade-shaders\\Shaders\\**\\**\r\n")
        put(g / "ReShadePreset.ini", "Techniques=D2R_DepthFog@D2R_DepthFog.fx\n")
    else:                                                   # a ReShade of the player's own beside the game
        put(g / "ReShade64.dll", "PLAYERS OWN RESHADE")
        put(g / "ReShade.ini", "[GENERAL]\nplayers=1\n")
    put(g / "FlatVR_DepthProvider.addon64", "OLD ADDON")
    put(g / "FlatVR_Keepalive.addonfx", "// old keepalive\n")
    put(g / "reshade-shaders/Shaders/D2R_DepthFog.fx", "// old shader\n")
    put(g / "reshade-shaders/Shaders/FlatVR_Keepalive.addonfx", "// old keepalive in shaders\n")
    put(g / "reshade-shaders/Shaders/Players_Own.fx", "// the player's own effect\n")
    put(g / "reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_act1.png", "OLD-SKY-A1")
    put(g / "reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_old_only.png", "OLD-ONLY")
    # the Arcane Sanctuary's pictures a test build of claude/arcane-sky made in the game's own folder (stars.png, clouds.png, the receipt)
    put(g / "reshade-shaders/Textures/D2R_Arcane/stars.png", b"\x89PNG" + b"STARS" * 20)
    put(g / "reshade-shaders/Textures/D2R_Arcane/clouds.png", b"\x89PNG" + b"CLOUDS" * 20)
    put(g / "reshade-shaders/Textures/D2R_Arcane/made.json", '{ "game_build": "21741" }')
    put(g / "FlatVR_DepthProvider.log", "log")


MOD = "mods/D2R-VR"
SETTINGS_FILES = ("D2R_VR_Settings.exe",)


# ------------------------------------------------------------------ the launcher carried inside Setup (section 8)
def reg_clear():
    try:
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, LAUNCHER_KEY)
    except OSError:
        pass


def reg_set(version=None, install_dir=None):
    """The throw-away key as the launcher's Setup leaves it (Version, InstallDir); a value left out is not there."""
    reg_clear()
    k = winreg.CreateKey(winreg.HKEY_CURRENT_USER, LAUNCHER_KEY)
    if version is not None:
        winreg.SetValueEx(k, "Version", 0, winreg.REG_SZ, version)
    if install_dir is not None:
        winreg.SetValueEx(k, "InstallDir", 0, winreg.REG_SZ, str(install_dir))
    winreg.CloseKey(k)


def reg_get():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, LAUNCHER_KEY) as k:
            out = {}
            for name in ("Version", "InstallDir"):
                try:
                    out[name] = winreg.QueryValueEx(k, name)[0]
                except OSError:
                    pass
            return out
    except OSError:
        return None


def lb_result():
    t = rd(work / "launcher_result.txt") if (work / "launcher_result.txt").exists() else ""
    m = re.search(r"decision=(.*?) ran=(\d) exit=(-?\d+)", t)
    rec = rd(work / "launcher_record.txt") if (work / "launcher_record.txt").exists() else None
    return (m.group(1), m.group(2) == "1", int(m.group(3))) if m else (None, None, None), rec


def lb_case(label, which, version, expect_decision, expect_ran, exe=True, folder="launcher home", expect_exit=None, vr=True):
    """A fresh game, the launcher installed as `version` (None: not installed; exe: its D2RVRLauncher.exe is there), Setup `which` run silently:
    what was decided, whether the carried Setup was started, and that the mod got installed whatever happened."""
    fresh_game()
    home = work / folder
    if version is None and not exe:
        reg_clear()
    else:
        home.mkdir(parents=True, exist_ok=True)
        if exe:
            (home / "D2RVRLauncher.exe").write_bytes(b"MZ the installed launcher")
        else:
            (home / "D2RVRLauncher.exe").unlink(missing_ok=True)
        reg_set(version, home)
    before = reg_get()
    rc, log = run_setup("lb_" + re.sub(r"\W+", "_", label), which, vr=vr,
                        env_extra={"MOD_LAYOUT_TEST_LAUNCHER_SIM": "1", "MOD_LAYOUT_TEST_LAUNCHER_DIR": str(work / "launcher home new")})
    (decision, ran, code), rec = lb_result()
    ok_mod = (game / MOD / "d2rloader/plugins/d2rl-vrcam.dll").exists() and (game / MOD / "vrmod-manifest.json").exists() and (game / MOD / "D2R-VR.mpq/modinfo.json").exists()
    check(rc == 0 and ok_mod, f"{label}: Setup finishes (exit {rc}) and the mod is installed")
    check(decision == expect_decision and ran == expect_ran,
          f"{label}: decided '{expect_decision}', carried Setup {'started' if expect_ran else 'not started'} (got '{decision}', ran {ran})")
    if expect_exit is not None:
        check(code == expect_exit, f"{label}: its exit code {expect_exit} is logged, nothing more (got {code})")
    check((rec is not None) == bool(expect_ran), f"{label}: the stand-in {'left' if expect_ran else 'left no'} record")
    return rc, log, rec, before


# ------------------------------------------------------------------ sections 9 and 10: an update from a REAL layout
ISS169 = here / "fixtures" / "D2R_VR_Setup_0.169.iss"


def classify(before, after):
    """What became of every file of `before`: stayed (same path, same bytes), moved (the same bytes now under mods/D2R-VR), changed (same path, other bytes),
    gone (no path, no bytes): the last two include files replaced by the new Setup's own."""
    by_hash = {}
    for k, v in after.items():
        by_hash.setdefault(v, []).append(k)
    res = {"stayed": [], "moved": [], "changed": [], "gone": []}
    for k, v in before.items():
        if after.get(k) == v:
            res["stayed"].append(k)
        elif any(a.startswith(MOD + "/") for a in by_hash.get(v, [])):
            res["moved"].append(k)
        elif k in after:
            res["changed"].append(k)
        else:
            res["gone"].append(k)
    return res


def show(label, res):
    print(f"  [{label}] {sum(len(v) for v in res.values())} files before: {len(res['stayed'])} stayed, {len(res['moved'])} moved into mods\\D2R-VR, "
          f"{len(res['changed'])} replaced in place, {len(res['gone'])} gone (replaced by the new Setup's own files or removed)")
    for what in ("changed", "gone"):
        names = res[what]
        if names:
            print(f"      {what}: " + ", ".join(names[:14]) + (f", ... (+{len(names) - 14})" if len(names) > 14 else ""))


def top_leftovers(after, allowed_prefixes, allowed_names=()):
    """Files outside mods\\ that are neither the game's/D2RLoader's own nor expected: what an update left behind in the game folder."""
    return sorted(k for k in after if not k.startswith("mods/") and not k.startswith(tuple(allowed_prefixes)) and k not in allowed_names and not k.startswith("unins000"))


GAME_OWN = ("D2R.exe", "D2RLoader.exe", "D2RCore.dll", "d2rloader.mpq", "steam_api64.dll")


def migration_sections():
    sys.path.insert(0, str(here))
    import fixture_0169
    if not args.payload169:
        print("\n=== 9. skipped: --payload169 (D2R VR 0.169's staged payload) not given")
    else:
        print("\n=== 9. an update from a 0.169 layout derived from the 0.169 Setup script (its own [Files] lines, the real 0.169 payload files)")
        pay169 = Path(args.payload169)
        reshade = {"ReShade64.dll": b"MZ ReShade 6.x " + NOISE[:200000], "ReShade.ini": b"[GENERAL]\r\nEffectSearchPaths=.\\reshade-shaders\\Shaders\\**\r\nPresetPath=.\\ReShadePreset.ini\r\n",
                   "ReShadePreset.ini": b"Techniques=D2R_DepthFog@D2R_DepthFog.fx\r\n"}
        if args.real_game and (Path(args.real_game) / "ReShade64.dll").is_file():
            reshade["ReShade64.dll"] = (Path(args.real_game) / "ReShade64.dll").read_bytes()
        if game.exists():
            shutil.rmtree(game)
        game.mkdir(parents=True)
        for n in GAME_OWN:
            put(game / n, "stand-in for " + n)
        put(game / "d2rloader/config/input-bindings.toml", "[bindings]\r\n")
        made = fixture_0169.install_0169(game, pay169, ISS169.read_text(encoding="utf-8"), repo / "LICENSE")
        fixture_0169.run_0169(game, put, reshade, TOML.replace("allow_global_extensions = false", "allow_global_extensions = true"),
                              "[calibration]\r\nheight=1.77\r\n[hands]\r\nbow_draw=0 ; the player's own\r\n")
        bears_mod(game)
        # the layout really is the 0.169 Setup's
        need = ["d2rloader/plugins/d2rl-vrcam.dll", "d2rloader/plugins/d2r_vr.ini", "d2rloader/plugins/d2r_vr.default.ini", "d2rloader/d2r_vr_libs/d2r_vr_weight.dll",
                "d2rloader/d2r_vr_libs/d2r_vr_grip.dll", "D2R_VR_Settings.exe", "D2R_VR_LICENSE.txt", "reshade-shaders/Shaders/D2R_DepthFog.fx",
                "FlatVR_DepthProvider.addon64", "FlatVR_Keepalive.addonfx", "BodyWalkVR/BodyWalkVR.exe", "BodyWalkVR/portable.txt"]
        check(all(n in made for n in need) and any(n.startswith("reshade-shaders/Textures/D2R_Sky_ours/") for n in made) and not any("localappdata" in n.lower() for n in made),
              f"the fixture is what the 0.169 Setup script puts in the game folder ({len(made)} files from its [Files] lines, the real payload files)")
        before = snap(game)
        bear0 = sub(before, "mods/D2R-PCVR/")
        rc, log = run_setup("f1_install", "nolibs")
        after = snap(game)
        res = classify(before, after)
        check(rc == 0, "Setup runs over it")
        show("0.169 layout, a build that ships no libraries", res)
        mp, ml, mr = f"{MOD}/d2rloader/plugins/", f"{MOD}/d2rloader/d2r_vr_libs/", f"{MOD}/reshade/"
        same = lambda a, b: before.get(a) is not None and after.get(b) == before[a]
        check(f"d2rloader/plugins/d2rl-vrcam.dll" not in after and after.get(mp + "d2rl-vrcam.dll") == hashlib.sha256(DLL_V1).hexdigest(),
              "no plugin left among the global plugins (the launcher warns about a leftover); the mod has the new one")
        check("ini_sha=" + before["d2rloader/plugins/d2r_vr.ini"] in rd(work / "moved.txt"), "the player's d2r_vr.ini was moved into the mod BYTE FOR BYTE (before Setup adds keys)")
        keys = ini_keys(rd(game / MOD / "d2rloader/plugins/d2r_vr.ini"))
        check(keys.get(("hands", "bow_draw")) == "0" and keys.get(("calibration", "height")) == "1.77", "...and his values are still his after Setup (bow_draw 0, [calibration] height 1.77)")
        bk = sorted(k for k in before if k.startswith("d2rloader/plugins/d2r_vr.ini."))
        check(len(bk) >= 2 and all(same(k, mp + k.rsplit("/", 1)[1]) and k not in after for k in bk),
              f"the ini's backups ({', '.join(k.rsplit('/', 1)[1] for k in bk)}) moved into the mod's plugin folder byte for byte, none left in the global one")
        for n in ("d2r_vr_headset.ini", "d2r_vr_settings.ini", "d2r_vr_depth.ini", "d2r_vr_game_code.txt", "d2r_vr_reshade_ours.txt"):
            check(same("d2rloader/plugins/" + n, mp + n) and "d2rloader/plugins/" + n not in after, f"{n} moved byte for byte")
        icons = [k for k in before if k.startswith("d2rloader/plugins/d2r_vr_skill_icons/")]
        check(icons and all(same(k, mp + k[len("d2rloader/plugins/"):]) and k not in after for k in icons), f"the skill-icon cache ({len(icons)} files) moved into the mod")
        check(same("d2rloader/d2r_vr_libs/d2r_vr_weight.dll", ml + "d2r_vr_weight.dll") and same("d2rloader/d2r_vr_libs/d2r_vr_grip.dll", ml + "d2r_vr_grip.dll")
              and not (game / "d2rloader/d2r_vr_libs").exists(), "the 0.169 libraries moved into the mod's d2r_vr_libs byte for byte; the global d2r_vr_libs folder is gone")
        check(not any(k.startswith("d2rloader/plugins/") and k.lower().endswith(".dll") for k in after), "no .dll is left among the global plugins (D2RLoader rejects nothing)")
        for n in ("ReShade64.dll", "ReShade.ini", "ReShadePreset.ini", "ReShade.log"):
            check(same(n, mr + n) and n not in after, f"{n} (ReShade was ours) moved byte for byte into the mod's reshade folder")
        check(same("FlatVR_DepthProvider.log", mr + "FlatVR_DepthProvider.log") and "FlatVR_DepthProvider.addon64" not in after and "FlatVR_Keepalive.addonfx" not in after,
              "the add-on and its log left the game's root (the add-on is the new one, in the mod's reshade folder)")
        sky_real = [k for k in before if k.startswith("reshade-shaders/Textures/D2R_Sky_ours/") and not k.endswith(("D2R_Sky_act1.png", "D2R_Crosshair.png"))]
        check(sky_real and all(k not in after for k in sky_real) and all(after.get(mr + k) == before[k] for k in sky_real[:5]) and not (game / "reshade-shaders/Textures").exists(),
              f"the sky pictures ({len(sky_real)} real ones) moved into the mod's reshade folder, none left in the game's reshade-shaders")
        check("reshade-shaders/Shaders/D2R_DepthFog.fx" not in after and rd(game / MOD / "reshade/reshade-shaders/Shaders/D2R_DepthFog.fx").startswith("// D2R VR shader"), "the shader is the new Setup's, in the mod")
        bw = [k for k in before if k.startswith("BodyWalkVR/")]
        check(bw and all(same(k, MOD + "/" + k) and k not in after for k in bw) and not (game / "BodyWalkVR").exists(),
              f"BodyWalk Portable ({len(bw)} files, its settings.json too) moved into mods\\D2R-VR\\BodyWalkVR, byte for byte")
        check("D2R_VR_LICENSE.txt" not in after and after.get(MOD + "/D2R_VR_LICENSE.txt") == hashlib.sha256((repo / "LICENSE").read_bytes()).hexdigest(),
              "the licence beside D2R.exe is gone (InstallDelete); the mod has it")
        check(after["D2R_VR_Settings.exe"] != before["D2R_VR_Settings.exe"], "D2R VR Settings (beside D2R.exe) is the new one")
        # the game's, D2RLoader's and bearstonem's
        check(all(after.get(n) == before[n] for n in GAME_OWN) and after.get("d2rloader/config/input-bindings.toml") == before["d2rloader/config/input-bindings.toml"],
              "D2R.exe, D2RLoader.exe, D2RCore.dll, d2rloader.mpq and D2RLoader's other config files are untouched")
        check(all(after.get(k) == v for k, v in before.items() if k.startswith("d2rloader/logs/")), "D2RLoader's logs (d2rloader\\logs) are untouched")
        want_toml = (before_toml := (game / "d2rloader/config/d2rloader.toml").read_bytes())
        check(b'default_mod = "D2R-VR"' in want_toml and want_toml.replace(b'default_mod = "D2R-VR"', b'default_mod = ""') == TOML.replace("allow_global_extensions = false", "allow_global_extensions = true").encode(),
              "d2rloader.toml: only default_mod changed (\"\" -> \"D2R-VR\")")
        check(sub(after, "mods/D2R-PCVR/") == bear0, "bearstonem's mods\\D2R-PCVR: every file byte for byte the same")
        left = top_leftovers(after, ("d2rloader/config/", "d2rloader/logs/", "d2rloader/data/"), GAME_OWN + ("D2R_VR_Settings.exe",))
        check(left == [], f"nothing of ours is left in the game folder outside mods\\D2R-VR (left: {left})")
        rc, log = run_uninstall("f1_uninstall")
        gone = snap(game)
        check(not any(k.startswith("mods/D2R-VR") for k in gone) and sub(gone, "mods/D2R-PCVR/") == bear0, "the uninstaller removes mods\\D2R-VR whole (and takes the moved backups, BodyWalk Portable and the libraries with it); mods\\D2R-PCVR is untouched")
        check(all(gone.get(n) == before[n] for n in GAME_OWN) and b'default_mod = ""' in (game / "d2rloader/config/d2rloader.toml").read_bytes(), "...D2RLoader's files are untouched and default_mod is none again")
        check(top_leftovers(gone, ("d2rloader/config/", "d2rloader/logs/", "d2rloader/data/"), GAME_OWN) == [], "...and the game folder holds nothing of ours")

    print("\n=== 10. an update from a copy of the small files of a REAL current game folder (a 0.168-era global install plus weeks of test builds)")
    if not args.real_game or not (Path(args.real_game) / "D2R.exe").is_file():
        print("  skipped: --real-game (a real game folder, read only) not given")
        return
    snapdir = work.parent / (work.name + "_realsnap")
    # The section needs an EARLIER (global, pre-0.170) install: his d2rloader\plugins\d2r_vr.ini. Once the real folder has been updated to 0.170 (the mod layout) it has none -
    # the snapshot is then kept as it is if it is of an earlier state, and the section is skipped if there is none.
    if (Path(args.real_game) / "d2rloader/plugins/d2r_vr.ini").is_file():
        if snapdir.exists():
            shutil.rmtree(snapdir)
        copied = fixture_0169.snapshot_real(Path(args.real_game), snapdir)
    elif (snapdir / "d2rloader/plugins/d2r_vr.ini").is_file():
        copied = [p for p in snapdir.rglob("*") if p.is_file()]
        print("  (the real folder is in the mod layout now: the snapshot kept from its earlier state is used)")
    else:
        print("  skipped: the real game folder is already a 0.170 install (the mod layout, no d2rloader\\plugins\\d2r_vr.ini) and no snapshot of an earlier state is kept")
        return
    if game.exists():
        shutil.rmtree(game)
    shutil.copytree(snapdir, game)
    before = snap(game)
    mb = sum(1 for _ in before)
    print(f"  (copied {len(copied)} files of the real folder into the scratch one; {mb} files there with the stand-ins)")
    n_ini_bak = len([k for k in before if k.startswith("d2rloader/plugins/d2r_vr.ini.")])
    rc, log = run_setup("f2_install", "v1")
    after = snap(game)
    res = classify(before, after)
    check(rc == 0, "Setup runs over it")
    show("a real folder", res)
    mp, ml, mr = f"{MOD}/d2rloader/plugins/", f"{MOD}/d2rloader/d2r_vr_libs/", f"{MOD}/reshade/"
    same = lambda a, b: before.get(a) is not None and after.get(b) == before[a]
    ini_before = before["d2rloader/plugins/d2r_vr.ini"]
    check("ini_sha=" + ini_before in rd(work / "moved.txt"), "his d2r_vr.ini was moved into the mod BYTE FOR BYTE")
    final = ini_keys(rd(game / MOD / "d2rloader/plugins/d2r_vr.ini"))
    orig = ini_keys(rd(snapdir / "d2rloader/plugins/d2r_vr.ini"))
    changed_keys = {k: (orig[k], final.get(k)) for k in orig if k != ("mode", "platform") and final.get(k) != orig[k]}
    check(not changed_keys, f"...and after Setup every one of his {len(orig)} values is the same (changed: {changed_keys})")
    bk = sorted(k for k in before if k.startswith("d2rloader/plugins/d2r_vr.ini."))
    check(n_ini_bak >= 60 and all(same(k, mp + k.rsplit("/", 1)[1]) and k not in after for k in bk),
          f"all {len(bk)} d2r_vr.ini.* backups moved into the mod's plugin folder byte for byte, none left in the global one (they are not matched by any wildcard of the Setup)")
    icons = [k for k in before if k.startswith("d2rloader/plugins/d2r_vr_skill_icons/")]
    check(len(icons) >= 200 and all(same(k, mp + k[len("d2rloader/plugins/"):]) for k in icons) and not any(k in after for k in icons), f"the skill-icon cache ({len(icons)} files) moved")
    stays = [k for k in before if k.startswith("d2rloader/plugins/") and (k.startswith("d2rloader/plugins/d2rl-vrcam.dll.") or "/uitex_" in k)]
    check(stays and all(after.get(k) == before[k] for k in stays), f"the plugin's own dev backups (d2rl-vrcam.dll.before_*) and the uitex dumps ({len(stays)} here) stay where they were")
    check(all((game / "d2rloader/plugins" / d).is_dir() for d in ("d2r_vr_icons", "d2r_vr_potions")), "d2r-pcvr's leftover folders d2r_vr_icons and d2r_vr_potions are left alone")
    check("d2rloader/plugins/d2rl-vrcam.dll" not in after and not any(k.startswith("d2rloader/plugins/") and k.lower().endswith(".dll") for k in after),
          "no plugin and no library DLL is left among the global plugins")
    check((game / MOD / "d2rloader/d2r_vr_libs/d2r_vr_weight.dll").is_file() and (game / MOD / "d2rloader/d2r_vr_libs/d2r_vr_grip.dll").is_file()
          and same("d2rloader/plugins/d2r_vr_grip.dll.before_test58", mp + "d2r_vr_grip.dll.before_test58"),
          "the libraries (0.168.0 had them among the plugins) are in the mod's d2r_vr_libs; his d2r_vr_grip.dll.before_test58 went with the d2r_vr* files")
    for n in ("ReShade64.dll", "ReShade.ini", "ReShadePreset.ini", "ReShade.log", "ReShade.log1", "ReShade.ini.before_flicker_test"):
        if n in before:
            check(same(n, mr + n) and n not in after, f"{n} moved byte for byte into the mod's reshade folder (ReShade was ours)")
    addon_extra = [k for k in before if k.startswith("FlatVR_DepthProvider.") and not k.endswith(".addon64")]
    check(all(same(k, mr + k) and k not in after for k in addon_extra), f"the add-on's log and backups ({', '.join(addon_extra)}) moved with ReShade")
    shaders_bak = [k for k in before if k.startswith("reshade-shaders/Shaders/") and ".before_" in k or k.endswith(".prev") and k.startswith("reshade-shaders/")]
    check(shaders_bak and all(after.get(k) == before[k] for k in shaders_bak), f"his {len(shaders_bak)} backups of the effect (D2R_DepthFog.fx.before_*) stay in the game's reshade-shaders\\Shaders")
    sky_real = [k for k in before if k.startswith("reshade-shaders/Textures/D2R_Sky_ours/")]
    moved_sky = [k for k in sky_real if after.get(mr + k) == before[k]]
    check(sky_real and len(moved_sky) >= len(sky_real) - 5 and not (game / "reshade-shaders/Textures/D2R_Sky_ours").exists(), f"his sky and ceiling pictures ({len(moved_sky)} of {len(sky_real)} byte for byte; the rest are replaced by the new Setup's) moved into the mod")
    bw = [k for k in before if k.startswith("BodyWalkVR/")]
    check(bw and all(same(k, MOD + "/" + k) for k in bw) and not (game / "BodyWalkVR").exists(), f"BodyWalk Portable ({len(bw)} files) moved into mods\\D2R-VR\\BodyWalkVR")
    root_bak = [k for k in before if k.startswith("D2R_VR_Settings.exe.")]
    check(root_bak and all(after.get(k) == before[k] for k in root_bak), f"his backups of the Settings program in the game's root (D2R_VR_Settings.exe.*, {len(root_bak)} here) stay")
    check(after["D2R_VR_Settings.exe"] != before["D2R_VR_Settings.exe"] and "D2R_VR_LICENSE.txt" not in after and (MOD + "/D2R_VR_LICENSE.txt") in after,
          "D2R_VR_Settings.exe is the new one; the licence beside it moved into the mod")
    cfg = [k for k in before if k.startswith("d2rloader/config/") or k.startswith("d2rloader/data/") or k.startswith("d2rloader/logs/")]
    toml_k = "d2rloader/config/d2rloader.toml"
    check(all(after.get(k) == before[k] for k in cfg if k != toml_k), f"D2RLoader's config, data and logs ({len(cfg) - 1} files) are untouched")
    check(after[toml_k] != before[toml_k] and (game / toml_k).read_bytes().replace(b'default_mod = "D2R-VR"', b'default_mod = ""') == (snapdir / toml_k).read_bytes(),
          "d2rloader.toml: only default_mod changed")
    left = top_leftovers(after, ("d2rloader/config/", "d2rloader/logs/", "d2rloader/data/", "d2rloader/plugins/", "reshade-shaders/", "D2R_VR_Settings.exe", "D2R_Sky_ours/", "BodyWalkVR/"), GAME_OWN)
    check(left == [], f"outside mods\\D2R-VR, besides what stays on purpose, nothing of ours is in the game's root (left: {left})")
    remains = sorted(k for k in after if not k.startswith("mods/") and k not in GAME_OWN and not k.startswith(("d2rloader/config/", "d2rloader/logs/", "d2rloader/data/", "unins000")))
    kinds = {}
    for k in remains:
        kinds.setdefault(k.split("/")[0] if "/" in k else "(root)", []).append(k)
    print("  what stays in the game folder on purpose (the player's own and dev files): " + "; ".join(f"{k}: {len(v)}" for k, v in sorted(kinds.items())))
    for k, v in sorted(kinds.items()):
        sample = sorted({x if x.count("/") < 3 else "/".join(x.split("/")[:3]) + "/..." for x in v})
        print(f"      {k}: " + ", ".join(sample[:12]) + (f", ... (+{len(sample) - 12})" if len(sample) > 12 else ""))
    rc, log = run_uninstall("f2_uninstall")
    gone = snap(game)
    check(not any(k.startswith("mods/D2R-VR") for k in gone), "the uninstaller removes mods\\D2R-VR (the moved backups, skill icons, libraries, ReShade, BodyWalk Portable) whole")
    show("after the uninstall, against the real folder", classify(before, gone))
    check(all(gone.get(n) == before[n] for n in GAME_OWN) and b'default_mod = ""' in (game / toml_k).read_bytes(), "...D2RLoader's files are untouched, default_mod is none again")


def launcher_section():
    # three installers that carry a Setup (the stand-in; the stand-in that fails with exit code 5; a file that is not a program), one that carries none
    for tag, src in (("lb_ok", Path(args.standin)), ("lb_fail", Path(args.standin_fail)), ("lb_garbage", None)):
        for f in (payload / "launcher_setup").glob("*") if (payload / "launcher_setup").exists() else []:
            f.unlink()
        (payload / "launcher_setup").mkdir(parents=True, exist_ok=True)
        if src:
            shutil.copyfile(src, payload / "launcher_setup" / LAUNCHER_FILE)
        else:
            put(payload / "launcher_setup" / LAUNCHER_FILE, "this is not a program, only a file named like one\n")
        check(compile_setup(tag, launcher=True), f"the test installer that carries {tag[3:]} compiles (launcher_bundle.inc, launcher_bundle_files.inc)")
    shutil.rmtree(payload / "launcher_setup")

    # (a) no launcher installed: the carried Setup runs, with exactly the contract's switches, as the original user
    rc, log, rec, _ = lb_case("no launcher installed", "lb_ok", None, "missing", True, exe=False, expect_exit=0)
    cmd = re.search(r"commandline=(.*)", rec or "")
    cmdline = cmd.group(1).strip() if cmd else ""
    check(cmdline.endswith(" " + SWITCHES) and cmdline.count("/") == 4, f"...started with exactly {SWITCHES}")
    check(re.search(r"argc=4\b", rec or "") is not None and "elevated=0" in (rec or ""), "...four switches, and (this Setup is not elevated) not elevated")
    check(LAUNCHER_FILE in cmdline and "\\is-" in cmdline.lower() or LAUNCHER_FILE in cmdline, "...the file Setup unpacked into its temp folder")
    after = reg_get()
    check(after == {"Version": LAUNCHER_VERSION, "InstallDir": str(work / "launcher home new")} and (work / "launcher home new" / "D2RVRLauncher.exe").exists(),
          "...it did what the launcher's Setup does: Version and InstallDir are in the key, the exe is there (the stand-in's doing)")
    check("installing the D2R VR Launcher" in log and "exit code 0" in log, "...the Setup log says it ran and how it ended")
    check(not (game / "mods" / "D2R-PCVR").exists(), "...nothing in any other mod's folder")
    # (b) the same Setup again: now the carried version is installed
    rc, log2 = run_setup("lb_again", "lb_ok", env_extra={"MOD_LAYOUT_TEST_LAUNCHER_SIM": "1", "MOD_LAYOUT_TEST_LAUNCHER_DIR": str(work / "launcher home new")})
    (decision, ran, code), rec = lb_result()
    check(rc == 0 and decision == "current" and ran is False and rec is None, f"a second Setup finds it installed and leaves it ({decision}, ran {ran})")
    # (c) older
    for v in ("0.1.0", "0.1.9.9", "0.2.0.0.1", "abc", "0.1"):
        want = "current" if v == "0.2.0.0.1" else "older"
        lb_case(f"installed {v!r}", "lb_ok", v, want, want == "older")
    # (d) the same or newer: never a downgrade; numbers, not text (0.10.0 is newer than 0.2.0)
    for v in ("0.2.0", "0.2", "0.2.0.0", "0.2.0.1", "0.2.1", "0.10.0", "1.0", "10.0.0", "0.2.0-beta"):
        want = "newer" if v in ("0.2.0.1", "0.2.1", "0.10.0", "1.0", "10.0.0") else "current"
        lb_case(f"installed {v!r}", "lb_ok", v, want, False)
    # (e) half installed
    lb_case("Version there, exe missing", "lb_ok", "0.2.0", "exe missing", True, exe=False)
    fresh_game()
    reg_set(None, work / "launcher home")
    (work / "launcher home").mkdir(exist_ok=True)
    (work / "launcher home" / "D2RVRLauncher.exe").write_bytes(b"MZ")
    rc, log = run_setup("lb_noversion", "lb_ok", env_extra={})
    (decision, ran, code), rec = lb_result()
    check(rc == 0 and decision == "missing" and ran is True, f"an exe without a Version in the key is no installed launcher ({decision}, ran {ran})")
    # (f) the folder has spaces and letters outside ASCII (a Cyrillic user name): found all the same, not installed again
    cyr = "launcher Привет (x86)"
    oem = "cp%d" % ctypes.windll.kernel32.GetOEMCP()
    try:
        cyr.encode(oem)
        lb_case("installed in a folder with spaces, brackets and Cyrillic letters", "lb_ok", "0.2.0", "current", False, folder=cyr)
    except UnicodeEncodeError:
        print(f"  (skipped: this machine's console code page {oem} cannot hold Cyrillic letters)")
    # (g) the carried Setup fails (exit 5) or cannot be started: the mod is installed all the same
    rc, log, rec, _ = lb_case("the carried Setup exits with 5", "lb_fail", None, "missing", True, exe=False, expect_exit=5)
    check("exit code 5" in log, "...logged, nothing more")
    rc, log, rec, _ = lb_case("the carried file is not a program", "lb_garbage", None, "missing", False, exe=False)
    check("could not be started" in log, "...logged: it could not be started")
    # (h) a Setup that carries no launcher does nothing about it
    fresh_game()
    reg_clear()
    rc, log = run_setup("lb_none", "v1")
    (decision, ran, code), rec = lb_result()
    check(rc == 0 and decision == "not carried" and ran is False and reg_get() is None, "a Setup that carries no launcher says so, starts nothing and writes no key")
    # (i) the uninstaller of the mod leaves the launcher, its folder and its key
    fresh_game()
    reg_clear()
    rc, log = run_setup("lb_u_install", "lb_ok", env_extra={"MOD_LAYOUT_TEST_LAUNCHER_SIM": "1", "MOD_LAYOUT_TEST_LAUNCHER_DIR": str(work / "launcher home new")})
    key_before = reg_get()
    check(key_before is not None and key_before.get("Version") == LAUNCHER_VERSION, "the launcher is installed (by the carried Setup) before the mod is uninstalled")
    run_uninstall("lb_u_uninstall")
    check(not any(k.startswith("mods/") for k in snap(game)), "the uninstaller removes the mod")
    check(reg_get() == key_before and (work / "launcher home new" / "D2RVRLauncher.exe").exists(), "...and leaves the launcher: its registry key and its exe are as they were")
    # (j) flat mode installs the launcher too (it is not a VR thing)
    fresh_game()
    reg_clear()
    rc, log = run_setup("lb_flat", "lb_ok", vr=False, env_extra={})
    (decision, ran, code), rec = lb_result()
    check(rc == 0 and decision == "missing" and ran is True, "flat mode: the launcher is installed too")

def main():
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    out.mkdir()

    make_payload("v1", DLL_V1, DEFAULTS_V1, BASE_VERSION + "-launcher-test")
    ok = compile_setup("v1")
    check(ok, "the test installer compiles (mod_names.inc, mod_files.inc, mod_layout.inc, ini_choice.inc are valid Inno Setup)")
    if not ok:
        return 1
    manifest_v1 = (payload / "launcher/vrmod-manifest.json").read_bytes()
    icon_v1 = (payload / "launcher/launcher_icon.png").read_bytes()
    weight_v1 = (payload / "plugins/d2r_vr_weight.dll").read_bytes()
    grip_v1 = (payload / "plugins/d2r_vr_grip.dll").read_bytes()
    # a second installer for a build that ships no libraries (-WeightLibs without the private build: Setup skips them)
    make_payload("v1", DLL_V1, DEFAULTS_V1, BASE_VERSION + "-launcher-test", libs=False)
    check(compile_setup("nolibs"), "...and one for a build without the weight and grip libraries")
    if args.settings_exe:
        # ... and one that carries the real Settings program, for the player's dxgi.dll ReShade (section 2d)
        make_payload("v1", DLL_V1, DEFAULTS_V1, BASE_VERSION + "-launcher-test", settings_exe=args.settings_exe)
        check(compile_setup("realexe"), "...and one that carries the real D2R_VR_Settings.exe")
    make_payload("v1", DLL_V1, DEFAULTS_V1, BASE_VERSION + "-launcher-test")

    # ------------------------------------------------------------------ 1. a clean game
    print("\n=== 1. a clean game (VR)")
    fresh_game()
    B0 = snap(game)
    rc, log = run_setup("c1_install", "v1")
    check(rc == 0, "Setup runs")
    S = snap(game)
    check(sha(game / MOD / "d2rloader/plugins/d2rl-vrcam.dll") == hashlib.sha256(DLL_V1).hexdigest(), "the plugin is in the mod")
    check(not (game / "d2rloader/plugins").exists(), "...and nothing is in the global d2rloader\\plugins")
    ini = rd(game / MOD / "d2rloader/plugins/d2r_vr.ini")
    check(ini_keys(ini).get(("mode", "platform")) == "1" and ini_keys(ini).get(("hands", "bow_draw")) == "1", "d2r_vr.ini is the shipped default (platform 1 = VR)")
    check((game / MOD / "d2rloader/plugins/d2r_vr.default.ini").read_bytes() == DEFAULTS_V1.encode(), "d2r_vr.default.ini is the shipped default, as it is")
    check((game / MOD / "reshade/reshade-shaders/Shaders/D2R_DepthFog.fx").exists() and (game / MOD / "reshade/reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_act1.png").exists(),
          "the shader and the sky are in the mod's reshade folder")
    check((game / MOD / "reshade/FlatVR_DepthProvider.addon64").exists() and (game / MOD / "reshade/FlatVR_Keepalive.addonfx").exists(), "VR: FlatVR's add-on is in the mod's reshade folder")
    check((game / MOD / "d2rloader/d2r_vr_libs/d2r_vr_weight.dll").read_bytes() == weight_v1 and (game / MOD / "d2rloader/d2r_vr_libs/d2r_vr_grip.dll").read_bytes() == grip_v1,
          "the weight and grip libraries are in the mod's d2rloader\\d2r_vr_libs - the folder the plugin (vr\\weight_lib.cpp, grip_lib.cpp) and Settings look in first, the plugin's own folder being mods\\D2R-VR\\d2rloader\\plugins - byte for byte")
    check(not (game / MOD / "d2rloader/plugins/d2r_vr_weight.dll").exists() and not (game / MOD / "d2rloader/plugins/d2r_vr_grip.dll").exists(), "...and none among the mod's plugins (D2RLoader would reject them there)")
    check(not (game / "d2rloader/plugins").exists(), "...none of them in the global d2rloader\\plugins")
    check((game / "D2R_VR_Settings.exe").read_bytes().startswith(b"MZ SETTINGS v1"), "D2R VR Settings is beside D2R.exe")
    check(not (game / "mods/D2R-PCVR").exists(), "no other mod's folder is made")
    r = subprocess.run([sys.executable, "-I", str(repo / "tools/make_vrmod_manifest.py"), "--verify", str(game / MOD)], capture_output=True, text=True)
    check(r.returncode == 0 and "files listed in x-mod-files are as shipped" in r.stdout, "every file the manifest's x-mod-files lists is there with the shipped hash (make_vrmod_manifest.py --verify)")
    mx = json.loads(rd(game / MOD / "vrmod-manifest.json"))
    listed = {e["path"]: e for e in mx["x-mod-files"]}
    installed = {k[len(MOD) + 1:] for k in snap(game) if k.startswith(MOD + "/")}
    unlisted = sorted(installed - set(listed) - {"vrmod-manifest.json", "launcher_icon.png", "D2R-VR.mpq/modinfo.json"})
    check(not unlisted and set(listed) <= installed, f"x-mod-files is exactly the files Setup put in the mod (besides the manifest, its icon and modinfo.json) (unlisted: {unlisted})")
    check(listed["d2rloader/d2r_vr_libs/d2r_vr_weight.dll"]["sha256"] == hashlib.sha256(weight_v1).hexdigest() and listed["d2rloader/d2r_vr_libs/d2r_vr_weight.dll"]["optional"], "...the weight library with its hash, optional")
    # the launcher's files
    check((game / MOD / "vrmod-manifest.json").read_bytes() == manifest_v1, "vrmod-manifest.json is in the mod, byte for byte the staged one")
    check((game / MOD / "launcher_icon.png").read_bytes() == icon_v1, "launcher_icon.png is in the mod, byte for byte")
    r = subprocess.run([sys.executable, "-I", str(repo / "tools/make_vrmod_manifest.py"), "--check", str(game / MOD / "vrmod-manifest.json")], capture_output=True, text=True)
    check(r.returncode == 0, "the installed manifest passes tools\\make_vrmod_manifest.py --check (the launcher's schema and this mod's contract)  " + r.stdout.strip()[:80])
    m = json.loads(rd(game / MOD / "vrmod-manifest.json"))
    check(m["version"] == BASE_VERSION + "-launcher-test" and m["d2rloader_mod"]["name"] == "D2R-VR" and m["id"] == "d2r-vr", "...and names the mod D2R-VR, version as shown")
    # D2RLoader's side
    mi = rd(game / MOD / "D2R-VR.mpq/modinfo.json")
    check(json.loads(mi)["name"] == "D2R-VR" and json.loads(mi)["savepath"] == "../" and "D2R VR Authentic" in json.loads(mi)["description"], "modinfo.json: name D2R-VR, savepath ../, the display name in the description")
    toml = (game / "d2rloader/config/d2rloader.toml").read_bytes()
    exp = TOML.replace("allow_global_extensions = false", "allow_global_extensions = true").replace('default_mod = ""', 'default_mod = "D2R-VR"').encode()
    check(toml == exp, "d2rloader.toml: allow_global_extensions = true and default_mod = \"D2R-VR\", everything else byte for byte")
    # nothing outside the mod but Settings.exe, the uninstaller, the toml
    stray = sorted(k for k in S if not k.startswith(MOD + "/") and k not in B0 and not k.startswith("unins000") and k != "D2R_VR_Settings.exe")
    check(not stray, f"Setup wrote nothing outside mods\\D2R-VR but D2R_VR_Settings.exe and the uninstaller (stray: {stray})")
    check(all(S.get(k) == v for k, v in B0.items() if k != "d2rloader/config/d2rloader.toml"), "the game's own files are untouched")
    ready = rd(work / "ready.txt")
    check("take_new=0 changed=0" in ready, "first install: no question asked, nothing changed")
    # what the Arcane Sanctuary sky (claude/arcane-sky, merged) writes in a mod layout: the level's visual files into D2R-VR.mpq\data\... (only because
    # Setup made D2R-VR.mpq, WriteModInfo) and the sky pictures into the mod's reshade\reshade-shaders\Textures\D2R_Arcane - all of it must go with the mod
    # (RemoveMod takes the mod's folder whole: there is no [UninstallDelete] line for it any more)
    check((game / MOD / "D2R-VR.mpq/modinfo.json").is_file() and (game / MOD / "reshade/reshade-shaders/Textures").is_dir(),
          "Setup made D2R-VR.mpq (modinfo.json) and the mod's reshade\\reshade-shaders\\Textures, the two folders an arcane-sky --prepare-assets writes into")
    iss = (repo / "installer/D2R_VR_Setup.iss").read_text(encoding="utf-8", errors="replace")
    step = iss[iss.index("procedure CurStepChanged"):]
    check("PrepareAssets(Dir);" in step and step.index("ApplyIni(Dir);") < step.index("PrepareAssets(Dir);") < step.index("BundleLauncher;"),
          "the real Setup runs PrepareAssets after ApplyIni (the mod's folders and ini are there), before the launcher")
    check("D2R_Arcane" not in iss[iss.index("[UninstallDelete]"):iss.index("[InstallDelete]")],
          "...and no [UninstallDelete] line for D2R_Arcane (the pictures are inside the mod: RemoveMod takes them)")
    put(game / MOD / "D2R-VR.mpq/data/global/ui/layouts/arcane_visual.json", '{ "weather": false }')
    put(game / MOD / "D2R-VR.mpq/data/hd/env/preset/act2/arcane.json", "{}")
    put(game / MOD / "reshade/reshade-shaders/Textures/D2R_Arcane/stars.png", b"\x89PNG" + b"\0" * 64)
    put(game / MOD / "reshade/reshade-shaders/Textures/D2R_Arcane/made.json", '{ "build": "93787" }')
    # the uninstaller
    rc, log = run_uninstall("c1_uninstall")
    left = snap(game)
    gone_toml = {k: v for k, v in left.items() if k != "d2rloader/config/d2rloader.toml"}
    check({k: v for k, v in B0.items() if k != "d2rloader/config/d2rloader.toml"} == {k: v for k, v in gone_toml.items() if not k.startswith("unins000")},
          f"the uninstaller leaves exactly what was there before (left: {sorted(set(left) - set(B0))})")
    check(not (game / "mods").exists(), "...the mods folder it made goes too")
    toml = (game / "d2rloader/config/d2rloader.toml").read_bytes()
    check(b'default_mod = ""' in toml and b'default_mod = "D2R-VR"' not in toml, "...and default_mod is none again")
    check(not any(p.name.startswith("unins000") for p in game.iterdir()), "...no uninstaller is left")

    print("\n=== 1b. a clean game (flat)")
    fresh_game()
    rc, log = run_setup("c1b_install", "v1", vr=False)
    ini = ini_keys(rd(game / MOD / "d2rloader/plugins/d2r_vr.ini"))
    check(rc == 0 and ini.get(("mode", "platform")) == "0", "flat: platform 0 in d2r_vr.ini")
    check(not (game / MOD / "reshade/FlatVR_DepthProvider.addon64").exists(), "flat: no FlatVR add-on")
    check((game / MOD / "vrmod-manifest.json").exists() and (game / MOD / "launcher_icon.png").exists(), "flat: the launcher's files are in too")
    run_uninstall("c1b_uninstall")
    check(not (game / "mods").exists(), "flat: the uninstaller removes the mod")

    # ------------------------------------------------------------------ 2. an earlier global install
    print("\n=== 2. an earlier global install (0.167 and older, ReShade ours)")
    fresh_game("old_ours")
    B2 = snap(game)
    rc, log = run_setup("c2_install", "v1")
    check(rc == 0, "Setup runs")
    S = snap(game)
    P = game / "d2rloader/plugins"
    mp = game / MOD / "d2rloader/plugins"
    check("ini_sha=" + B2["d2rloader/plugins/d2r_vr.ini"] in rd(work / "moved.txt"), "the player's d2r_vr.ini is moved into the mod BYTE FOR BYTE (before Setup adds new keys)")
    final = ini_keys(rd(mp / "d2r_vr.ini"))
    orig = ini_keys(PLAYERS_INI)
    check(all(final.get(k) == v for k, v in orig.items() if k != ("mode", "platform")), "...and after Setup every value of the player's is still his (bow_draw 0, fog 55, [calibration]...)")
    check(final.get(("new", "thing")) is None and final.get(("mode", "platform")) == "1", "...[mode] platform is the wizard's choice (VR)")
    check(S["d2rloader/plugins/d2rl-vrcam.dll.prev"] == B2["d2rloader/plugins/d2rl-vrcam.dll.prev"] and "d2rloader/plugins/other_plugin.dll" in S,
          "backups of the plugin (d2rl-vrcam.dll.prev) and other people's plugins stay where they were")
    check(S.get(f"{MOD}/d2rloader/plugins/d2r_vr_game_code.txt") == B2["d2rloader/plugins/d2r_vr_game_code.txt"] and "d2rloader/plugins/d2r_vr_game_code.txt" not in S,
          "the dumps of ours by name (d2r_vr_game_code.txt...) moved into the mod with the ini")
    check(not (P / "d2rl-vrcam.dll").exists(), "the global d2rl-vrcam.dll is gone (D2RLoader would load it under any mod)")
    check(sha(mp / "d2rl-vrcam.dll") == hashlib.sha256(DLL_V1).hexdigest(), "...the mod has the new one")
    check(not (P / "d2r_vr_weight.dll").exists() and not (P / "d2r_vr_grip.dll").exists(), "the earlier install's weight and grip libraries are no longer in the global plugins folder")
    ml = game / MOD / "d2rloader/d2r_vr_libs"
    check((ml / "d2r_vr_weight.dll").read_bytes() == weight_v1 and (ml / "d2r_vr_grip.dll").read_bytes() == grip_v1 and not (mp / "d2r_vr_weight.dll").exists() and not (mp / "d2r_vr_grip.dll").exists(),
          "...they were moved into the mod's d2r_vr_libs (not among its plugins) and the shipped ones replaced them")
    check("moved" in log and "d2r_vr_weight.dll" in log and "d2r_vr_grip.dll" in log, "the Setup log says they moved")
    for n in ("d2r_vr_headset.ini", "d2r_vr_settings.ini", "d2r_vr_depth.ini", "d2r_vr_reshade_ours.txt"):
        check(S.get(f"{MOD}/d2rloader/plugins/{n}") == B2.get(f"d2rloader/plugins/{n}") and f"d2rloader/plugins/{n}" not in S, f"{n} moved into the mod, byte for byte")
    check(S.get(f"{MOD}/d2rloader/plugins/d2r_vr_skill_icons/sub/s2.png") == B2["d2rloader/plugins/d2r_vr_skill_icons/sub/s2.png"] and not (P / "d2r_vr_skill_icons").exists(),
          "the skill icons folder moved whole (subfolders too)")
    check((mp / "d2r_vr.default.ini").read_bytes() == DEFAULTS_V1.encode(), "d2r_vr.default.ini is the new Setup's")
    R = game / MOD / "reshade"
    check(sha(R / "ReShade64.dll") == B2["ReShade64.dll"] and sha(R / "ReShadePreset.ini") == B2["ReShadePreset.ini"], "ReShade (it was ours) moved into the mod's reshade, byte for byte")
    check((R / "ReShade.ini").read_bytes() == b"[GENERAL]\r\nEffectSearchPaths=.\\reshade-shaders\\Shaders\\**\r\n",
          "...its ini too, with ReShade 6.8's search path fixed ('\\**\\**' -> '\\**'), the rest byte for byte")
    for n in ("ReShade64.dll", "ReShade.ini", "ReShadePreset.ini", "FlatVR_DepthProvider.addon64", "FlatVR_Keepalive.addonfx"):
        check(not (game / n).exists(), f"{n} is no longer beside D2R.exe")
    check((R / "FlatVR_DepthProvider.addon64").read_bytes() == b"MZ ADDON v1", "FlatVR's add-on in the mod is the new one")
    check("old shader" not in rd(R / "reshade-shaders/Shaders/D2R_DepthFog.fx") and not (game / "reshade-shaders/Shaders/D2R_DepthFog.fx").exists(), "the shader moved, then the new one replaced it")
    check(sha(R / "reshade-shaders/Shaders/FlatVR_Keepalive.addonfx") == B2["reshade-shaders/Shaders/FlatVR_Keepalive.addonfx"] and
          not (game / "reshade-shaders/Shaders/FlatVR_Keepalive.addonfx").exists(), "FlatVR's keepalive effect moved")
    check(S.get("reshade-shaders/Shaders/Players_Own.fx") == B2["reshade-shaders/Shaders/Players_Own.fx"], "the player's own effect stays beside the game")
    check(rd(R / "reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_act1.png") == "SKY-A1 v1" and rd(R / "reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_old_only.png") == "OLD-ONLY",
          "the sky folder moved; the new pictures replaced the old ones of the same name")
    A = R / "reshade-shaders/Textures/D2R_Arcane"
    check(sha(A / "stars.png") == B2["reshade-shaders/Textures/D2R_Arcane/stars.png"] and sha(A / "clouds.png") == B2["reshade-shaders/Textures/D2R_Arcane/clouds.png"] and
          sha(A / "made.json") == B2["reshade-shaders/Textures/D2R_Arcane/made.json"] and not (game / "reshade-shaders/Textures/D2R_Arcane").exists(),
          "the Arcane Sanctuary's pictures (D2R_Arcane: stars, clouds, receipt) moved from the game's reshade-shaders into the mod's, byte for byte, none left in the game's folder")
    check("D2R VR: moved" in log and "global d2rloader\\plugins\\d2rl-vrcam.dll removed" in log, "the Setup log says what moved")
    check((game / MOD / "vrmod-manifest.json").read_bytes() == manifest_v1, "the launcher's manifest is in")
    toml = (game / "d2rloader/config/d2rloader.toml").read_bytes()
    check(b'default_mod = "D2R-VR"' in toml, "default_mod = D2R-VR")
    run_uninstall("c2_uninstall")
    left = snap(game)
    stay = {k: v for k, v in left.items() if not k.startswith("unins000")}
    check(not any(k.startswith("mods/") for k in stay), "the uninstaller removes mods\\D2R-VR (the player's moved settings with it, by design)")
    check(sub(stay, "reshade-shaders/Shaders/Players_Own") == sub(B2, "reshade-shaders/Shaders/Players_Own") and stay.get("d2rloader/plugins/other_plugin.dll") == B2["d2rloader/plugins/other_plugin.dll"],
          "...and leaves the player's own effect and the other plugin")

    print("\n=== 2b. what is in the mod already wins over what an earlier install left")
    fresh_game("old_ours")
    put(game / MOD / "d2rloader/plugins/d2r_vr.ini", "[mode]\r\nplatform=1\r\n[hands]\r\nbow_draw=0\r\n[mine]\r\nin_the_mod=1\r\n")
    put(game / MOD / "reshade/ReShade64.dll", "RESHADE ALREADY IN THE MOD")
    B2b = snap(game)
    rc, log = run_setup("c2b_install", "v1")
    check(rc == 0 and "in_the_mod" in rd(game / MOD / "d2rloader/plugins/d2r_vr.ini") and "fog=55" not in rd(game / MOD / "d2rloader/plugins/d2r_vr.ini"), "the mod's d2r_vr.ini is not replaced by the older one")
    check(rd(game / MOD / "reshade/ReShade64.dll") == "RESHADE ALREADY IN THE MOD" and (game / "ReShade64.dll").exists(), "the mod's ReShade is not replaced either; the one beside the game stays")
    check(sha(game / "d2rloader/plugins/d2r_vr.ini") == B2b["d2rloader/plugins/d2r_vr.ini"], "the older d2r_vr.ini stays where it was (nothing is lost)")
    run_uninstall("c2b_uninstall")

    print("\n=== 2b2. an earlier install with the libraries, from a Setup that ships none")
    fresh_game("old_ours")
    B2n = snap(game)
    rc, log = run_setup("c2n_install", "nolibs")
    mp = game / MOD / "d2rloader/plugins"
    ml = game / MOD / "d2rloader/d2r_vr_libs"
    check(rc == 0 and (ml / "d2r_vr_weight.dll").read_bytes() == OLD_WEIGHT and (ml / "d2r_vr_grip.dll").read_bytes() == OLD_GRIP and not (mp / "d2r_vr_weight.dll").exists(),
          "the earlier install's libraries are moved into the mod byte for byte and stay (this build ships none)")
    check(not (game / "d2rloader/plugins/d2r_vr_weight.dll").exists() and not (game / "d2rloader/plugins/d2r_vr_grip.dll").exists(), "...and are gone from the global folder")
    r = subprocess.run([sys.executable, "-I", str(repo / "tools/make_vrmod_manifest.py"), "--verify", str(game / MOD)], capture_output=True, text=True)
    check(r.returncode == 0 and not any("weight" in e["path"] for e in json.loads(rd(game / MOD / "vrmod-manifest.json"))["x-mod-files"]),
          "...the manifest does not list libraries this Setup did not ship, and --verify passes")
    run_uninstall("c2n_uninstall")
    check(not any(k.startswith("mods/") for k in snap(game)), "...and the uninstaller removes them with the mod")
    print("\n=== 2b3. a clean game, a build that ships none")
    fresh_game()
    rc, log = run_setup("c2n2_install", "nolibs")
    check(rc == 0 and not (game / MOD / "d2rloader/d2r_vr_libs").exists() and not (game / MOD / "d2rloader/plugins/d2r_vr_weight.dll").exists() and not (game / MOD / "d2rloader/plugins/d2r_vr_grip.dll").exists(),
          "no libraries are shipped, none are made up (Setup goes on without them)")
    run_uninstall("c2n2_uninstall")

    print("\n=== 2b4. an earlier global install of 0.169: the libraries in d2rloader\\d2r_vr_libs (and a stale 0.168 copy among the plugins)")
    fresh_game("old_ours")
    put(game / "d2rloader/d2r_vr_libs/d2r_vr_weight.dll", NEW_WEIGHT)
    put(game / "d2rloader/d2r_vr_libs/d2r_vr_grip.dll", NEW_GRIP)
    rc, log = run_setup("c2n3_install", "nolibs")
    ml = game / MOD / "d2rloader/d2r_vr_libs"
    check(rc == 0 and (ml / "d2r_vr_weight.dll").read_bytes() == NEW_WEIGHT and (ml / "d2r_vr_grip.dll").read_bytes() == NEW_GRIP,
          "the d2r_vr_libs copies (the newer ones) are moved into the mod's d2r_vr_libs, byte for byte")
    check(not (game / "d2rloader/plugins/d2r_vr_weight.dll").exists() and not (game / "d2rloader/plugins/d2r_vr_grip.dll").exists(),
          "...the stale copies among the plugins are removed (D2RLoader would reject them), not moved over the newer ones")
    check(not (game / MOD / "d2rloader/plugins/d2r_vr_weight.dll").exists() and not (game / MOD / "d2rloader/plugins/d2r_vr_grip.dll").exists(), "...and none lands among the mod's plugins")
    run_uninstall("c2n3_uninstall")
    check(not any(k.startswith("mods/") for k in snap(game)), "...the uninstaller takes the mod's libraries with the mod")

    print("\n=== 2b5. the mod itself from a 0.168 test build: the libraries among ITS plugins")
    fresh_game()
    put(game / MOD / "d2rloader/plugins/d2r_vr_weight.dll", OLD_WEIGHT)
    put(game / MOD / "d2rloader/plugins/d2r_vr_grip.dll", OLD_GRIP)
    put(game / MOD / "d2rloader/d2r_vr_libs/d2r_vr_grip.dll", NEW_GRIP)            # one of them is in d2r_vr_libs already: what is there wins
    rc, log = run_setup("c2n4_install", "nolibs")
    ml = game / MOD / "d2rloader/d2r_vr_libs"
    check(rc == 0 and (ml / "d2r_vr_weight.dll").read_bytes() == OLD_WEIGHT, "the weight library moves out of the mod's plugins into d2r_vr_libs, byte for byte")
    check((ml / "d2r_vr_grip.dll").read_bytes() == NEW_GRIP, "...the grip library already in d2r_vr_libs stays as it is")
    check(not (game / MOD / "d2rloader/plugins/d2r_vr_weight.dll").exists() and not (game / MOD / "d2rloader/plugins/d2r_vr_grip.dll").exists(),
          "...and none is left among the plugins (the duplicate of the grip library is removed)")
    rc, log = run_setup("c2n5_install", "v1")
    check(rc == 0 and (ml / "d2r_vr_weight.dll").read_bytes() == weight_v1 and (ml / "d2r_vr_grip.dll").read_bytes() == grip_v1, "an update that ships them replaces them in d2r_vr_libs")
    run_uninstall("c2n4_uninstall")

    print("\n=== 2c. an earlier install with a ReShade of the player's own (no 'ours' mark)")
    fresh_game("old_ours_players_reshade")
    B2c = snap(game)
    rc, log = run_setup("c2c_install", "v1")
    S = snap(game)
    check(rc == 0 and S["ReShade64.dll"] == B2c["ReShade64.dll"] and S["ReShade.ini"] == B2c["ReShade.ini"], "the player's own ReShade64.dll and ReShade.ini stay beside the game, untouched")
    check(not (game / MOD / "reshade/ReShade64.dll").exists(), "...and are not copied into the mod")
    run_uninstall("c2c_uninstall")
    left = snap(game)
    check(left.get("ReShade64.dll") == B2c["ReShade64.dll"] and left.get("ReShade.ini") == B2c["ReShade.ini"], "...and the uninstaller leaves them")

    print("\n=== 2d. a player's own ReShade as dxgi.dll beside the game: used as it is - never renamed, moved, replaced or deleted")
    OUR_EFFECT = b".\\mods\\D2R-VR\\reshade\\reshade-shaders\\Shaders\\**"
    OUR_TEXTURE = b".\\mods\\D2R-VR\\reshade\\reshade-shaders\\Textures\\**"

    def dxgi_state():
        st = os.stat(game / "dxgi.dll")
        return (game / "dxgi.dll").read_bytes(), st.st_mtime_ns

    # (a) Setup with the stand-in Settings program (nothing to run): the dxgi.dll alone, in three spellings of the old ini key - the old rule would have moved it
    # into the mod as ReShade64.dll (or deleted it); now the file is the same bytes with the same time, and nothing is made in its place
    for variant, ini_text in (("no [reshade] key", None), ("an old ini with [reshade] as_dxgi=1", "[reshade]\nas_dxgi=1\n[mode]\nplatform=1\n"),
                              ("an old ini with [reshade] as_dxgi=0", "[reshade]\nas_dxgi=0\n[mode]\nplatform=1\n")):
        fresh_game("dxgi")
        if ini_text:
            put(game / MOD / "d2rloader/plugins/d2r_vr.ini", ini_text)
        dx0 = dxgi_state()
        rc, log = run_setup("c2d_install", "v1")
        check(rc == 0 and dxgi_state() == dx0 and not (game / MOD / "reshade/ReShade64.dll").exists() and not (game / "dxgi.dll.off").exists() and not (game / "ReShade64.dll").exists(),
              f"{variant}: dxgi.dll stays byte for byte with the same time; nothing renamed, nothing made in its place")
        check("left exactly as it is" in log, "...and the Setup log says it is the player's own ReShade, left as it is")
        run_uninstall("c2d_uninstall")
        check(dxgi_state() == dx0 and not (game / "dxgi.dll.off").exists(), "...and the uninstaller leaves it, byte for byte with the same time")
    # the mod has a ReShade64.dll of its own (installed before) and the player has a dxgi.dll as well: the old rule DELETED the dxgi.dll; both stay now
    fresh_game("dxgi")
    put(game / MOD / "reshade/ReShade64.dll", b"MZ OUR OWN RESHADE")
    dx0 = dxgi_state()
    rc, log = run_setup("c2g_install", "v1")
    check(rc == 0 and dxgi_state() == dx0 and (game / MOD / "reshade/ReShade64.dll").read_bytes() == b"MZ OUR OWN RESHADE",
          "the mod's own ReShade64.dll and a dxgi.dll of the player's: both stay (the plugin joins the dxgi.dll and does not load the other)")
    run_uninstall("c2g_uninstall")
    check(dxgi_state() == dx0 and not (game / MOD).exists(), "...the uninstaller takes the mod's folder (its own ReShade) and leaves the player's dxgi.dll")
    # no dxgi.dll at all: nothing of ReShade is written outside the mod (D2R VR's own ReShade64.dll comes with the Install ReShade button, into mods\D2R-VR\reshade)
    fresh_game()
    rc, log = run_setup("c2h_install", "v1")
    check(rc == 0 and not (game / "dxgi.dll").exists() and not (game / "ReShade.ini").exists() and (game / MOD / "reshade/reshade-shaders/Shaders/D2R_DepthFog.fx").exists(),
          "no dxgi.dll: no ReShade.ini and no dxgi.dll are made beside the game; D2R VR's effect is in the mod's reshade folder")
    run_uninstall("c2h_uninstall")

    # (b) with the REAL D2R_VR_Settings.exe: his ReShade.ini gets D2R VR's two search paths (so his dxgi.dll ReShade finds the mod's effect and pictures), once, and the
    # uninstaller takes them out again
    if not args.settings_exe:
        print("  (skipped: --settings-exe, the real D2R_VR_Settings.exe, not given)")
    else:
        fresh_game("dxgi_ini")
        B2d = snap(game)
        ini0 = (game / "ReShade.ini").read_bytes()
        dx0 = dxgi_state()
        rc, log = run_setup("c2f_install", "realexe")
        S2d = snap(game)
        ini1 = (game / "ReShade.ini").read_bytes()
        check(rc == 0 and "--reshade-paths add: 0" in log, "Setup runs D2R_VR_Settings --reshade-paths add and it says 0 (the paths are in)")
        check(dxgi_state() == dx0 and not (game / "dxgi.dll.off").exists() and not (game / MOD / "reshade/ReShade64.dll").exists(),
              "his dxgi.dll is the same bytes with the same time; nothing renamed, nothing made in its place")
        check(OUR_EFFECT in ini1 and OUR_TEXTURE in ini1 and ini1 != ini0, "his ReShade.ini lists D2R VR's two folders (EffectSearchPaths, TextureSearchPaths)")
        check(ini1.replace(b"," + OUR_EFFECT, b"").replace(b"," + OUR_TEXTURE, b"") == ini0, "...and nothing else of his file changed (taking our two list items out gives his file, every byte)")
        check((game / "ReShade.ini.before_d2rvr").exists() and (game / "ReShade.ini.before_d2rvr").read_bytes() == ini0, "...a copy of his file as it was is beside it (ReShade.ini.before_d2rvr)")
        others = sorted(k for k in S2d if k not in B2d and not k.startswith(MOD + "/") and not k.startswith("unins000") and k not in ("D2R_VR_Settings.exe", "ReShade.ini.before_d2rvr"))
        check(not others and S2d["ReShadePreset.ini"] == B2d["ReShadePreset.ini"] and S2d["reshade-shaders/Shaders/Own.fx"] == B2d["reshade-shaders/Shaders/Own.fx"],
              f"his preset and his own effect are untouched, nothing else is made beside the game (stray: {others})")
        # an update over it: nothing changes again
        rc, log = run_setup("c2f_update", "realexe")
        check(rc == 0 and (game / "ReShade.ini").read_bytes() == ini1 and (game / "ReShade.ini.before_d2rvr").read_bytes() == ini0 and dxgi_state() == dx0,
              "a second Setup over it: his ReShade.ini is the same (the paths are added once), the copy is still his original, dxgi.dll as before")
        run_uninstall("c2f_uninstall")
        left2d = snap(game)
        check((game / "ReShade.ini").read_bytes() == ini0 and not (game / "ReShade.ini.before_d2rvr").exists(),
              "the uninstaller takes D2R VR's two folders out again: his ReShade.ini is his original, every byte, and the copy is gone")
        check(dxgi_state() == dx0 and not (game / "dxgi.dll.off").exists() and not (game / "mods").exists(), "...his dxgi.dll the same bytes with the same time; the mod's folder is gone")
        TOML_KEY = "d2rloader/config/d2rloader.toml"   # (D2RLoader's own setting: allow_global_extensions stays switched on, as it does after every uninstall)
        check({k: v for k, v in left2d.items() if not k.startswith("unins000") and k != TOML_KEY} == {k: v for k, v in B2d.items() if k != TOML_KEY},
              "...and the game folder is exactly what it was before Setup")
        # the Home button's case: the paths are not in, the game folder has no Settings run yet (Setup could not: the game was running) - covered by start_helper_test.py

    # ------------------------------------------------------------------ 2e. the settings program's shortcuts
    print("\n=== 2e. the settings program is \"D2R VR Authentic Settings\": its shortcuts, and the old names gone")
    SETNAME = "D2R VR Authentic Settings"
    shell = work / "shell"                                   # the stand-in's six places (mod_layout_test.iss defines them as folders here, never the real Start menu or desktop)
    SU, SA, DU, DA = shell / "start_user", shell / "start_all", shell / "desktop_user", shell / "desktop_all"

    def reset_shell():
        if shell.exists():
            shutil.rmtree(shell)
        for d in (SU, SA, DU, DA):
            d.mkdir(parents=True)

    def is_lnk(p):
        return p.is_file() and p.read_bytes()[:4] == b"\x4c\x00\x00\x00"      # a real Windows shortcut (ShellLinkHeader)

    OLD_NAMES = [SU / "D2R VR Settings.lnk", SA / "D2R VR Settings.lnk", DU / "D2R VR.lnk", DA / "D2R VR.lnk", DU / "D2R VR Authentic.lnk", DA / "D2R VR Authentic.lnk",
                 DA / "D2R VR Settings.lnk", DU / "D2R VR Settings.lnk"]
    THEIRS = {SU / "BodyWalk VR.lnk": b"BODYWALK", DU / "My Game.lnk": b"MINE", SA / "D2R VR Unleashed.lnk": b"BEARS", DA / "D2R VR Authentic Notes.txt": b"NOTES"}

    def theirs_ok():
        return all(p.is_file() and p.read_bytes() == v for p, v in THEIRS.items())

    # a first install: the three shortcuts, under the new name
    reset_shell()
    fresh_game()
    rc, log = run_setup("c2e_first", "v1")
    check(rc == 0 and is_lnk(SU / f"{SETNAME}.lnk") and is_lnk(DU / f"{SETNAME}.lnk") and is_lnk(SU / "D2R VR Authentic.lnk"),
          f"a first install makes the Start menu entry and the desktop shortcut \"{SETNAME}\" and the Start menu's game shortcut \"D2R VR Authentic\" (real .lnk files)")
    check(not any(p.exists() for p in OLD_NAMES), "...and none of the old names")
    check(not (DU / "D2R VR Authentic.lnk").exists(), "...the desktop has no \"D2R VR Authentic\" any more (that name opened the settings program)")
    run_uninstall("c2e_first_uninstall")
    check(not any(p.exists() for p in (SU / f"{SETNAME}.lnk", DU / f"{SETNAME}.lnk", SU / "D2R VR Authentic.lnk")), "...the uninstaller takes all three away")

    # an update over an earlier install that left its shortcuts under the old names, in both places (all users and this user), beside a player's own
    reset_shell()
    fresh_game("old_ours")
    for p in OLD_NAMES:
        p.write_bytes(b"OLD SHORTCUT " + p.parent.name.encode() + p.name.encode())
    for p, v in THEIRS.items():
        p.write_bytes(v)
    rc, log = run_setup("c2e_update", "v1")
    check(rc == 0 and not any(p.exists() for p in OLD_NAMES), "an update over an install with the old shortcuts (\"D2R VR Settings\" in the Start menu, \"D2R VR\" / \"D2R VR Authentic\" / \"D2R VR Settings\" "
          "on the desktop, all users and this user): every one of them is gone")
    check(is_lnk(SU / f"{SETNAME}.lnk") and is_lnk(DU / f"{SETNAME}.lnk") and is_lnk(SU / "D2R VR Authentic.lnk"), "...the new ones are there")
    check(theirs_ok(), "...a player's own shortcuts, BodyWalk's, another mod's (D2R VR Unleashed) and a file with a similar name are untouched, byte for byte")
    # again over it (the new names exist): they stay, still no old ones
    before_lnk = sorted(p.name for p in shell.rglob("*") if p.is_file())
    rc, log = run_setup("c2e_update2", "v1")
    check(rc == 0 and sorted(p.name for p in shell.rglob("*") if p.is_file()) == before_lnk and is_lnk(SU / f"{SETNAME}.lnk"), "a second Setup over it: the same files, the new shortcuts still there")
    # a copy of the old name appears again (a player copied it back, an old installer's remains): the uninstaller takes it too
    for p in (SU / "D2R VR Settings.lnk", DA / "D2R VR.lnk"):
        p.write_bytes(b"BACK AGAIN")
    run_uninstall("c2e_update_uninstall")
    left = sorted(str(p.relative_to(shell)).replace("\\", "/") for p in shell.rglob("*") if p.is_file())
    check(left == sorted(str(p.relative_to(shell)).replace("\\", "/") for p in THEIRS), f"after the uninstaller none of ours is left, new or old - only the others' (left: {left})")
    check(theirs_ok(), "...and those are byte for byte what they were")

    # the real Setup's text: the finish page, the task, the directory page, the program's window and its resource say the new name; the file name is the old one
    iss = (repo / "installer/D2R_VR_Setup.iss").read_text(encoding="utf-8", errors="replace")
    names_inc = (repo / "installer/mod_names.inc").read_text(encoding="utf-8", errors="replace")
    sc_inc = (repo / "installer/mod_shortcuts.inc").read_text(encoding="utf-8", errors="replace")
    check('#define SettingsDisplay "D2R VR Authentic Settings"' in names_inc, "mod_names.inc: SettingsDisplay is \"D2R VR Authentic Settings\"")
    check('Filename: "{app}\\D2R_VR_Settings.exe"; WorkingDir: "{app}"; Description: "Open {#SettingsDisplay}"; Flags: postinstall nowait skipifsilent shellexec' in iss,
          "the finish page's first entry is \"Open D2R VR Authentic Settings\" (the program is still {app}\\D2R_VR_Settings.exe, skipped in a silent run)")
    check('Description: "Open D2R VR Launcher"' in iss and "Flags: postinstall nowait skipifsilent shellexec unchecked" in iss, "...and \"Open D2R VR Launcher\" stays what it was: unticked, skipped in a silent run")
    check('Description: "Create a desktop shortcut to {#SettingsDisplay}"' in sc_inc and "{#ShellPrograms}\\{#SettingsDisplay}" in sc_inc and "{#ShellDesktop}\\{#SettingsDisplay}" in sc_inc,
          "the desktop task and the two shortcuts use the display name")
    check("({#SettingsDisplay} stays next to D2R.exe)" in iss, "the directory page names it too")
    no_old = [l for l in (iss + sc_inc).splitlines() if "D2R VR Settings" in l and not l.lstrip().startswith((";", "//", "Type: files;"))]
    check(not no_old, f"no line of Setup (outside comments and the old-name deletes) says \"D2R VR Settings\" any more ({no_old[:2]})")
    cpp = (repo / "tools/settings/settings.cpp").read_text(encoding="utf-8", errors="replace")
    rcf = (repo / "tools/settings/settings.rc").read_text(encoding="utf-8", errors="replace")
    check('constexpr const wchar_t* kAppName = L"D2R VR Authentic Settings";' in cpp and "(std::wstring(kAppName) + L\" \" D2RVR_VERSION_W).c_str()" in cpp,
          "the program's window title is \"D2R VR Authentic Settings <version>\"")
    check('L"D2R VR Settings"' not in cpp and 'VALUE "OriginalFilename", "D2R_VR_Settings.exe"' in rcf and 'VALUE "FileDescription", "D2R VR Authentic Settings"' in rcf and
          'VALUE "InternalName", "d2r-vr-settings"' in rcf and 'VALUE "ProductName", "D2R VR"' in rcf,
          "...its message boxes carry the name; the resource: FileDescription is the display name, internal / original file name / product name as they were")

    # ------------------------------------------------------------------ 3. d2r-pcvr as a mod of its own
    print("\n=== 3. d2r-pcvr as mods\\D2R-PCVR")
    for label, kinds in (("clean game", ("bear_mod",)), ("earlier D2R VR install", ("bear_mod", "old_ours"))):
        print(f"  -- {label}")
        fresh_game(*kinds)
        B3 = snap(game)
        bear0 = sub(B3, "mods/D2R-PCVR/")
        check(len(bear0) >= 10, f"his mod folder has {len(bear0)} files to watch")
        rc, log = run_setup("c3_install", "v1")
        S = snap(game)
        check(rc == 0, "Setup runs")
        check(sub(S, "mods/D2R-PCVR/") == bear0, "every file of mods\\D2R-PCVR is byte for byte the same after Setup (hashed, none added or removed)")
        check(not any(k.startswith("mods/D2R-VR/") and "pcvr" in k.lower() for k in S), "nothing of his is copied into ours")
        check(sub(S, "mods/D2R-VR/") != {}, "D2R VR is in mods\\D2R-VR")
        check(b'default_mod = "D2R-VR"' in (game / "d2rloader/config/d2rloader.toml").read_bytes(), "(no default mod was set: ours is)")
        rc, log = run_uninstall("c3_uninstall")
        S = snap(game)
        check(sub(S, "mods/D2R-PCVR/") == bear0, "every file of mods\\D2R-PCVR is byte for byte the same after the UNINSTALL")
        check(not any(k.startswith("mods/D2R-VR") for k in S), "mods\\D2R-VR is gone")
    print("  -- his default_mod stays")
    fresh_game("bear_mod")
    put(game / "d2rloader/config/d2rloader.toml", TOML.replace('default_mod = ""', 'default_mod = "D2R-PCVR"'))
    rc, log = run_setup("c3b_install", "v1")
    t = rd(game / "d2rloader/config/d2rloader.toml")
    check('default_mod = "D2R-PCVR"' in t and 'default_mod = "D2R-VR"' not in t, "Setup leaves default_mod = \"D2R-PCVR\" as the player (or he) set it")
    run_uninstall("c3b_uninstall")
    check('default_mod = "D2R-PCVR"' in rd(game / "d2rloader/config/d2rloader.toml"), "...and so does the uninstaller")
    print("  -- a default that was ours but is his now")
    fresh_game("bear_mod")
    rc, log = run_setup("c3c_install", "v1")
    put(game / "d2rloader/config/d2rloader.toml", rd(game / "d2rloader/config/d2rloader.toml").replace('default_mod = "D2R-VR"', 'default_mod = "D2R-PCVR"'))  # d2r-pcvr's installer chose itself
    run_uninstall("c3c_uninstall")
    check('default_mod = "D2R-PCVR"' in rd(game / "d2rloader/config/d2rloader.toml"), "d2r-pcvr set itself as the default after us: our uninstaller does not clear his")

    # ------------------------------------------------------------------ 4. bearstonem's legacy global install
    print("\n=== 4. bearstonem's legacy global install (d2rloader\\plugins\\d2rl-vrcam.dll + pcvr-install.json)")
    fresh_game("bear_legacy")
    B4 = snap(game)
    rc, log = run_setup("c4_install", "v1")
    S = snap(game)
    check(rc == 0, "Setup runs")
    check("is d2r-pcvr's" in log, "the Setup log says the global plugin is d2r-pcvr's and left alone")
    keep = {k: v for k, v in B4.items() if k != "d2rloader/config/d2rloader.toml"}
    check(all(S.get(k) == v for k, v in keep.items()), f"every one of his {len(keep)} files is byte for byte the same after Setup")
    check(sha(game / MOD / "d2rloader/plugins/d2rl-vrcam.dll") == hashlib.sha256(DLL_V1).hexdigest(), "D2R VR is in its own mod folder anyway")
    check(ini_keys(rd(game / MOD / "d2rloader/plugins/d2r_vr.ini")) == ini_keys(DEFAULTS_V1), "...with the shipped ini, not his d2r_vr.ini")
    rc, log = run_uninstall("c4_uninstall")
    S = snap(game)
    check(all(S.get(k) == v for k, v in keep.items()), "every one of his files is byte for byte the same after the UNINSTALL")
    check(not any(k.startswith("mods/") for k in S), "and the mod is gone")

    # ------------------------------------------------------------------ 5. an update
    print("\n=== 5. an update over an existing mods\\D2R-VR")
    fresh_game()
    rc, log = run_setup("c5_install1", "v1")
    mp = game / MOD / "d2rloader/plugins"
    # (a) the player changed his settings
    tuned = rd(mp / "d2r_vr.ini").replace("fog=30", "fog=77") + "[calibration]\r\nheight=1.82\r\n"
    put(mp / "d2r_vr.ini", tuned)
    put(game / MOD / "D2R-VR.mpq/modinfo.json", '{ "name": "D2R-VR", "savepath": "../", "description": "edited by the player" }')
    put(mp / "d2r_vr_headset.ini", "[headset]\nipd=0.07\n")
    put(game / MOD / "reshade/ReShade.ini", "[GENERAL]\nmine=1\n")
    put(game / MOD / "d2rloader/logs/d2r-vr-vrcam.log", "a log\n")
    after1 = snap(game)
    # (the manifest tool takes a label that starts with vrcam.cpp's version: the update is another label of it)
    make_payload("v2", DLL_V2, DEFAULTS_V2, BASE_VERSION + "-launcher-test2")
    ok2 = compile_setup("v2")
    check(ok2, "the second test installer compiles")
    manifest_v2 = (payload / "launcher/vrmod-manifest.json").read_bytes()
    rc, log = run_setup("c5_install2", "v2")
    check(rc == 0, "Setup (v2) runs over the existing mod")
    check((mp / "d2rl-vrcam.dll").read_bytes() == DLL_V2, "the plugin is the new one")
    check(b"v2" in (game / MOD / "d2rloader/d2r_vr_libs/d2r_vr_weight.dll").read_bytes() and b"v2" in (game / MOD / "d2rloader/d2r_vr_libs/d2r_vr_grip.dll").read_bytes()
          and (game / MOD / "d2rloader/d2r_vr_libs/d2r_vr_weight.dll").read_bytes() != weight_v1, "the weight and grip libraries are the new ones")
    r = subprocess.run([sys.executable, "-I", str(repo / "tools/make_vrmod_manifest.py"), "--verify", str(game / MOD)], capture_output=True, text=True)
    check(r.returncode == 0, "--verify passes after the update too (the player's d2r_vr.ini is allowed to differ)")
    cur = ini_keys(rd(mp / "d2r_vr.ini"))
    check(cur.get(("render", "fog")) == "77" and cur.get(("calibration", "height")) == "1.82", "the player's settings are kept (fog 77, [calibration] height 1.82)")
    check(cur.get(("new", "thing")) == "7", "...and the key v2 added is in with its default")
    check((mp / "d2r_vr.default.ini").read_bytes() == DEFAULTS_V2.encode(), "d2r_vr.default.ini is v2's")
    check("take_new=0 changed=1" in rd(work / "ready.txt"), "the question was asked (a changed ini); a silent run answers Yes = keep")
    check(rd(mp / "d2r_vr_headset.ini") == "[headset]\nipd=0.07\n" and rd(game / MOD / "reshade/ReShade.ini") == "[GENERAL]\nmine=1\n", "the headset ini and the mod's ReShade.ini are the player's")
    check(json.loads(rd(game / MOD / "D2R-VR.mpq/modinfo.json"))["description"] == "edited by the player", "modinfo.json is not rewritten")
    check((game / MOD / "vrmod-manifest.json").read_bytes() == manifest_v2 and manifest_v2 != manifest_v1 and json.loads(manifest_v2)["version"] == BASE_VERSION + "-launcher-test2",
          "the launcher's manifest is the update's (new version), not the old one")
    check(rd(game / MOD / "d2rloader/logs/d2r-vr-vrcam.log") == "a log\n", "logs are left")
    check(b'default_mod = "D2R-VR"' in (game / "d2rloader/config/d2rloader.toml").read_bytes(), "default_mod stays")
    # (b) an unchanged ini takes the new defaults silently
    print("  -- an ini the player never changed")
    fresh_game()
    rc, log = run_setup("c5_install3", "v1")
    rc, log = run_setup("c5_install4", "v2")
    check(rc == 0 and "take_new=1 changed=0" in rd(work / "ready.txt"), "an unchanged ini: no question, the new defaults")
    check(ini_keys(rd(mp / "d2r_vr.ini")) == ini_keys(DEFAULTS_V2), "...the file is v2's default")
    check(not (mp / "d2r_vr.ini.before-test").exists(), "...and no .before copy is made (nothing to lose)")
    # (c) the player says No: take the new defaults, his file is kept beside
    run_uninstall("c5_uninstall")

    # ------------------------------------------------------------------ 6. the uninstaller is exact
    print("\n=== 6. the uninstaller")
    check(True, "(cases 1-4 above check it file by file: only mods\\D2R-VR and the default it set)")

    # ------------------------------------------------------------------ 7. the launcher lookup
    print("\n=== 7. the launcher lookup for the finish page")
    txt = rd(work / "launcher.txt") if (work / "launcher.txt").exists() else ""
    found = re.search(r"exe=(.*) installed=(\d)", txt)
    exe_found = found.group(1).strip() if found else None
    want = ""
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\D2R VR Launcher") as k:
            d = winreg.QueryValueEx(k, "InstallDir")[0]
            if (Path(d) / "D2RVRLauncher.exe").is_file():
                want = str(Path(d) / "D2RVRLauncher.exe")
    except OSError:
        pass
    if not want:
        d = Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "D2R VR Launcher" / "D2RVRLauncher.exe"
        want = str(d) if d.is_file() else ""
    check(exe_found is not None and exe_found.lower() == want.lower() and found.group(2) == ("1" if want else "0"),
          f"LauncherExePath / LauncherInstalled agree with the registry and the default folder (found {exe_found!r}; launcher {'installed' if want else 'not installed'} on this machine)")

    # ------------------------------------------------------------------ 8. the launcher carried inside Setup
    print("\n=== 8. the launcher carried inside Setup (a stand-in for its Setup; a throw-away registry key)")
    try:
        launcher_section()
    finally:
        reg_clear()
    check(reg_get() is None, "the throw-away registry key is gone again")

    migration_sections()

    print(f"\n{checks - len(failures)}/{checks} checks passed -- {'ALL PASSED' if not failures else str(len(failures)) + ' FAILED'}")
    for f in failures:
        print("  FAIL:", f)
    if not failures:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print("scratch kept:", work)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
