#!/usr/bin/env python3
"""D2R_VR_Settings.exe --start: the launch contract of the shared VR mod launcher (docs/launcher_authentic.md), headless.

It runs the real Settings exe in a scratch folder that stands in for a game folder - never the player's - with fake_loader.exe as
D2RLoader.exe, and checks the exit codes (0 started / 1 no d2r_vr.ini / 2 no D2RLoader.exe / 3 D2RLoader.exe would not start / 4 the game
runs), that D2R VR Authentic is started as `D2RLoader.exe -mod D2R-VR` (and an install from before the mod layout without it), what the
child got in its environment (XR_RUNTIME_JSON and VRMOD_*), what went to stdout and to d2rloader\\logs\\d2r_vr_launch.txt, that no window
opened, and that nothing outside the scratch folder was written (the game's Settings.json, d2r_vr.ini and its neighbours).

Only the VR + native OpenXR + "Controls without BodyWalk" path is run: it starts no BodyWalk and does not meet the game's window
(flat mode and FlatVR would read the player's Saved Games and start the real BodyWalk).

    python tools/start_helper_test.py <D2R_VR_Settings.exe> <fake_loader.exe> [scratch dir]
"""
import ctypes
import os
import re
import hashlib
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

failures = []
checks = 0

MOD_PLUGINS = Path("mods") / "D2R-VR" / "d2rloader" / "plugins"       # the D2RLoader mod D2R-VR (shared/mod_layout.h)
GLOBAL_PLUGINS = Path("d2rloader") / "plugins"                          # an install from before the mod layout
MOD_LOGS = Path("mods") / "D2R-VR" / "d2rloader" / "logs"              # the launch log of the mod layout (an earlier install: d2rloader\\logs)


def check(ok, what, detail=""):
    global checks
    checks += 1
    if not ok:
        failures.append(what)
        print(f"FAIL  {what}  {detail}")
    else:
        print(f"ok    {what}")


def rd(p):
    try:
        return Path(p).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def windows_of(pid):
    """Visible top-level windows of a process (the exe must open none)."""
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(hwnd, _):
        p = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found


def running(name):
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return name.lower() in out.lower()


def make_game(root: Path, settings: Path, loader, ini, layout="mod"):
    """A fake game folder: D2R_VR_Settings.exe beside the (fake) D2R.exe, d2r_vr.ini in the mod (layout "mod") or in the global plugins
    folder (layout "global", an earlier install), D2RLoader.exe."""
    if root.exists():
        shutil.rmtree(root)
    plugins = root / (MOD_PLUGINS if layout == "mod" else GLOBAL_PLUGINS)
    plugins.mkdir(parents=True)
    (root / "d2rloader").mkdir(exist_ok=True)
    shutil.copy2(settings, root / "D2R_VR_Settings.exe")
    if loader is not None:
        shutil.copy2(loader, root / "D2RLoader.exe")
    if ini is not None:
        (plugins / "d2r_vr.ini").write_text(ini, encoding="utf-8")
    return root


def game_settings_for(root: Path) -> Path:
    return root.parent / (root.name + "_Settings.json")


def real_game_settings():
    """The player's own Settings.json (Saved Games\\Diablo II Resurrected), only to prove a start leaves it alone: (sha256, size) or None."""
    p = Path(os.environ.get("USERPROFILE", "")) / "Saved Games" / "Diablo II Resurrected" / "Settings.json"
    try:
        data = p.read_bytes()
    except OSError:
        return None
    return hashlib.sha256(data).hexdigest(), len(data)


def start(root: Path, env_extra=None, timeout=30, exe=None):
    env = dict(os.environ)
    for k in list(env):
        if k.startswith("VRMOD_") or k == "XR_RUNTIME_JSON":
            del env[k]
    # D2R VR Settings reads and writes the game's Settings.json (window, frame cap, interface size) on START: a test gives it a file of its own,
    # outside the "game folder" and never the player's (D2RVR_GAME_SETTINGS_JSON, 0.168). Not there unless a case puts one there.
    env["D2RVR_GAME_SETTINGS_JSON"] = str(game_settings_for(root))
    env.update(env_extra or {})
    p = subprocess.Popen([str(exe or root / "D2R_VR_Settings.exe"), "--start"], cwd=str(root), env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL)
    seen = []
    t0 = time.time()
    while p.poll() is None and time.time() - t0 < timeout:
        seen += windows_of(p.pid)
        time.sleep(0.02)
    if p.poll() is None:
        p.kill()
        return None, "", "", seen
    out, err = p.communicate()
    return p.returncode, out.decode("utf-8", "replace"), err.decode("utf-8", "replace"), seen


def wait_file(path: Path, secs=10):
    t0 = time.time()
    while time.time() - t0 < secs:
        if path.exists() and path.stat().st_size > 0:
            time.sleep(0.1)
            return rd(path)
        time.sleep(0.05)
    return None


def kv(text):
    return dict(line.split("=", 1) for line in (text or "").splitlines() if "=" in line)


def tree_state(root: Path):
    """Every file under root and its size: to see that a start writes nothing but d2rloader\\logs\\d2r_vr_launch.txt and the fake's record."""
    return {p.relative_to(root).as_posix(): p.stat().st_size for p in root.rglob("*") if p.is_file()}


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    settings, loader = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    scratch = Path(sys.argv[3]) if len(sys.argv) > 3 else Path(tempfile.mkdtemp(prefix="d2rvr_start_"))
    scratch = scratch.resolve()
    if running("D2R.exe") or running("D2RLoader.exe"):
        print("The game (D2R.exe / D2RLoader.exe) is running on this machine: --start would refuse (exit 4) - close it and run again.")
        return 3
    for p in (settings, loader):
        if not p.is_file():
            print("not found:", p)
            return 2
    ini_native = "[mode]\nplatform=1\n[openxr]\non=1\ndirect_input=1\n"
    real0 = real_game_settings()

    # 1. no d2r_vr.ini anywhere
    root = make_game(scratch / "g1", settings, loader, None)
    code, out, err, wins = start(root)
    check(code == 1, "no d2r_vr.ini -> exit 1", f"(got {code})")
    check("no d2r_vr.ini" in out, "...and says so on stdout", repr(out))
    check(not wins, "...opens no window", str(wins))
    check(not (root / "fake_loader_started.txt").exists(), "...starts nothing")
    log = rd(root / "d2rloader" / "logs" / "d2r_vr_launch.txt") or ""
    check("not started" in log, "...and writes d2r_vr_launch.txt", repr(log))

    # 2. no D2RLoader.exe
    root = make_game(scratch / "g2", settings, None, ini_native)
    code, out, err, wins = start(root)
    check(code == 2, "no D2RLoader.exe -> exit 2", f"(got {code})")
    check("D2RLoader.exe is not in the game's folder" in out, "...says so on stdout", repr(out))
    check(not wins, "...opens no window")

    # 3. the game runs (a D2R.exe process)
    root = make_game(scratch / "g3", settings, loader, ini_native)
    fake_game = scratch / "g3_proc" / "D2R.exe"
    fake_game.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "PING.EXE", fake_game)
    hold = subprocess.Popen([str(fake_game), "-n", "40", "127.0.0.1"], stdout=subprocess.DEVNULL)
    try:
        time.sleep(0.5)
        code, out, err, wins = start(root)
    finally:
        hold.kill()
        hold.wait()
    check(code == 4, "the game running -> exit 4", f"(got {code})")
    check("running already" in out, "...says so on stdout", repr(out))
    check(not (root / "fake_loader_started.txt").exists(), "...starts nothing")

    # 4. the D2RLoader mod D2R-VR, native OpenXR, own controls: started as `-mod D2R-VR`, with the launcher's environment inherited by D2RLoader.exe
    root = make_game(scratch / "g4", settings, loader, ini_native)
    env = {
        "XR_RUNTIME_JSON": r"C:\fake runtime\vdxr\runtime.json",
        "VRMOD_ACTIVE_ID": "d2r-vr",
        "VRMOD_ACTIVE_VERSION": "0.167.0",
        "VRMOD_RUNTIME": "vdxr",
        "VRMOD_STORE": "steam",
        "VRMOD_GAME_BUILD": "3.3.93787",
    }
    ini_path = root / MOD_PLUGINS / "d2r_vr.ini"
    before_ini = ini_path.read_bytes()
    files_before = tree_state(root)
    t0 = time.time()
    code, out, err, wins = start(root, env)
    took = time.time() - t0
    check(code == 0, "the mod D2R-VR, native OpenXR + own controls -> exit 0", f"(got {code}; stdout {out!r})")
    check(took < 8, "...returns at once (it does not wait for the game)", f"({took:.1f} s)")
    check(not wins, "...opens no window", str(wins))
    started = kv(wait_file(root / "fake_loader_started.txt"))
    check(bool(started), "...D2RLoader.exe was started")
    check(started.get("argline") == "-mod D2R-VR", "...as `D2RLoader.exe -mod D2R-VR`", repr(started.get("argline")))
    check(started.get("cwd", "").rstrip("\\").lower() == str(root).lower(), "...from the game's folder", started.get("cwd", ""))
    for k, v in env.items():
        check(started.get(k) == v, f"...and got {k}", f"({started.get(k)!r})")
    check("D2RLoader.exe -mod D2R-VR started (VR)" in out, "...stdout: started (VR), with the mod", repr(out))
    check("by the VR mod launcher as d2r-vr 0.167.0, runtime vdxr, store steam" in out, "...stdout: who started it", repr(out))
    check("start: done" in out, "...stdout: done")
    log = rd(root / MOD_LOGS / "d2r_vr_launch.txt") or ""
    check("-mod D2R-VR started (VR)" in log and "start: done" in log, "...d2r_vr_launch.txt has the same lines", repr(log))
    check(len(log.splitlines()) == len(out.splitlines()), "...one log line per stdout line", f"{len(log.splitlines())} vs {len(out.splitlines())}")
    check(all(l[:4].isdigit() and l[4] == "-" for l in log.splitlines()), "...each log line is time-stamped")
    check(ini_path.read_bytes() == before_ini, "...d2r_vr.ini unchanged (VR, no square window)")
    check(not any(root.rglob("Settings.json")) and not game_settings_for(root).exists(), "...no Settings.json written")
    check(not (root / "BodyWalkVR").exists(), "...no BodyWalk started or put in")
    wait_file(root / "fake_loader_started.txt")
    files_after = tree_state(root)
    new = sorted(set(files_after) - set(files_before))
    gone_or_changed = sorted(k for k in files_before if files_after.get(k) != files_before[k])
    check(new == sorted(["mods/D2R-VR/d2rloader/logs/d2r_vr_launch.txt", "fake_loader_started.txt"]) and not gone_or_changed,
          "...nothing written but the launch log, in the MOD's own logs folder (and the fake's record); no file changed; nothing in the global d2rloader",
          f"(new {new}, changed {gone_or_changed})")

    # 5. a second start while the first loader has gone (nothing runs): the log is appended, not replaced
    (root / "fake_loader_started.txt").unlink()
    for _ in range(100):   # the first fake loader has exited (the next start would see D2RLoader.exe running and refuse)
        if not running("D2RLoader.exe"):
            break
        time.sleep(0.05)
    code2, out2, _, _ = start(root, {})
    log2 = rd(root / MOD_LOGS / "d2r_vr_launch.txt") or ""
    check(code2 == 0, "a second start -> exit 0", f"(got {code2})")
    check(log2.startswith(log) and len(log2) > len(log), "...d2r_vr_launch.txt is appended to")
    started2 = kv(wait_file(root / "fake_loader_started.txt"))
    check(started2.get("XR_RUNTIME_JSON") == "" and started2.get("VRMOD_ACTIVE_ID") == "", "...without the launcher: no XR_RUNTIME_JSON, no VRMOD_*")
    check(started2.get("argline") == "-mod D2R-VR", "...still `-mod D2R-VR`")
    check("by the VR mod launcher" not in out2, "...and says nothing of a launcher")

    # 6. an install from before the mod layout (the global plugin; the exe in d2rloader\plugins beside its ini): started as it always was, no -mod
    root = make_game(scratch / "g6", settings, loader, ini_native, layout="global")
    (root / GLOBAL_PLUGINS / "D2R_VR_Settings.exe").write_bytes((root / "D2R_VR_Settings.exe").read_bytes())
    (root / "D2R_VR_Settings.exe").unlink()
    code, out, _, wins = start(root, exe=root / GLOBAL_PLUGINS / "D2R_VR_Settings.exe")
    check(code == 0, "an earlier install (global plugin, the exe in d2rloader\\plugins) -> exit 0 too", f"(got {code}; {out!r})")
    started = kv(wait_file(root / "fake_loader_started.txt"))
    check(started.get("cwd", "").rstrip("\\").lower() == str(root).lower(), "...D2RLoader.exe from the game's folder, not from plugins")
    check(started.get("argline") == "", "...started without -mod, as before", repr(started.get("argline")))

    # 6b. the same earlier install with the exe beside D2R.exe
    root = make_game(scratch / "g6b", settings, loader, ini_native, layout="global")
    code, out, _, _ = start(root)
    started = kv(wait_file(root / "fake_loader_started.txt"))
    check(code == 0 and started.get("argline") == "", "an earlier install with the exe beside D2R.exe: exit 0, no -mod", f"(got {code}, {started.get('argline')!r})")

    # 7. both an ini in the mod and one in the global plugins: the mod's (the player's settings moved there by Setup) is the one read
    root = make_game(scratch / "g7", settings, loader, ini_native)
    (root / GLOBAL_PLUGINS).mkdir(parents=True, exist_ok=True)
    (root / GLOBAL_PLUGINS / "d2r_vr.ini").write_text(ini_native, encoding="utf-8")   # (native too: a wrong pick must not start the real BodyWalk)
    code, out, _, wins = start(root)
    started = kv(wait_file(root / "fake_loader_started.txt"))
    mods_ini = str(root / MOD_PLUGINS / "d2r_vr.ini")
    check(code == 0 and started.get("argline") == "-mod D2R-VR", "an ini in the mod and an older one in d2rloader\\plugins: started as the mod",
          f"(got {code}; {out!r})")
    check(f"settings {mods_ini}".lower() in out.lower(), "...and the mod's ini is the one read (stdout names it)", repr(out))

    # 8. D2RLoader.exe that cannot be started (not a program): exit 3, no error box of the shell's
    root = make_game(scratch / "g8", settings, None, ini_native)
    (root / "D2RLoader.exe").write_text("this is not a program", encoding="utf-8")
    code, out, err, wins = start(root, timeout=20)
    check(code == 3, "a D2RLoader.exe that is not a program -> exit 3", f"(got {code}; {out!r})")
    check("could not be started" in out, "...says so on stdout", repr(out))
    check(not wins, "...and opens no window, no shell error box", str(wins))

    # 9. START also sets the game's interface size in its Settings.json for VR (0.168): the test's own file, never the player's
    root = make_game(scratch / "g9", settings, loader, ini_native)
    gs = game_settings_for(root)
    original = '{\n  "Safe Screen Percent": 100,\n  "Windowed": 1\n}\n'
    gs.write_text(original, encoding="utf-8")
    code, out, err, wins = start(root)
    after = gs.read_text(encoding="utf-8")
    kept = after.replace(re.search(r'"Safe Screen Percent": (\d+)', after).group(0), '"Safe Screen Percent": 100') if re.search(r'"Safe Screen Percent": (\d+)', after) else ""
    check(code == 0, "START with a Settings.json (the test's own): exit 0", f"(got {code}; {out!r})")
    check(after != original and kept == original, "...VR starts the interface below 100 in that file, everything else of it byte for byte", repr(after))
    check(kept == original and "Safe Screen Percent\": 100" not in after, "...and the number it wrote is the file's, not 100 any more", repr(after))
    check(not wins, "...opens no window")
    started = kv(wait_file(root / "fake_loader_started.txt"))
    check(started.get("argline") == "-mod D2R-VR", "...and still starts the mod")

    # 10. --prepare-assets (Setup runs it once after the ini is in; [sky] arcane_sky): the mod's ini decides, the pictures go where ReShade is. No game archives
    # here, so a run that gets as far as opening them ends with exit 1 and writes nothing; 0 = unticked, 2 = no ReShade folder to put them in.
    def prepare(root):
        env = dict(os.environ)
        env["D2RVR_GAME_SETTINGS_JSON"] = str(game_settings_for(root))
        p = subprocess.run([str(root / "D2R_VR_Settings.exe"), "--prepare-assets"], cwd=str(root), env=env, capture_output=True, timeout=60, stdin=subprocess.DEVNULL)
        return p.returncode

    mod_reshade = Path("mods") / "D2R-VR" / "reshade"
    root = make_game(scratch / "g10a", settings, loader, "[sky]\narcane_sky=0\n")
    (root / GLOBAL_PLUGINS).mkdir(parents=True, exist_ok=True)
    (root / GLOBAL_PLUGINS / "d2r_vr.ini").write_text("[sky]\narcane_sky=1\n", encoding="utf-8")   # (the hard-coded global path would say 1)
    before = tree_state(root)
    check(prepare(root) == 0 and tree_state(root) == before, "--prepare-assets: [sky] arcane_sky=0 in the MOD's ini (the global ini says 1) -> exit 0, nothing made")

    root = make_game(scratch / "g10b", settings, loader, "[sky]\narcane_sky=1\n")
    (root / mod_reshade).mkdir(parents=True)
    (root / mod_reshade / "ReShade64.dll").write_bytes(b"MZ")
    (root / "reshade-shaders" / "Textures").mkdir(parents=True)          # the player's own ReShade folder beside the game: not the mod's
    before = tree_state(root)
    check(prepare(root) == 2 and tree_state(root) == before,
          "--prepare-assets: the mod has its own ReShade but no reshade-shaders\\Textures yet -> exit 2 (the game's folder is not used), nothing written")

    (root / mod_reshade / "reshade-shaders" / "Textures").mkdir(parents=True)
    before = tree_state(root)
    check(prepare(root) == 1 and tree_state(root) == before,
          "--prepare-assets: the mod's reshade\\reshade-shaders\\Textures there, no game archives -> exit 1 (it got as far as the archives), nothing written")

    root = make_game(scratch / "g10c", settings, loader, "[sky]\narcane_sky=1\n")   # a mod without ReShade64.dll (the player's own ReShade is dxgi.dll beside the game)
    (root / "dxgi.dll").write_bytes(b"MZ")
    (root / "reshade-shaders" / "Textures").mkdir(parents=True)     # (his own Textures folder in the game's folder is not where D2R VR's pictures go)
    check(prepare(root) == 2, "--prepare-assets: no ReShade64.dll in the mod, a dxgi.dll beside the game, no reshade-shaders\\Textures in the mod -> exit 2")
    (root / mod_reshade / "reshade-shaders" / "Textures").mkdir(parents=True)
    before = tree_state(root)
    check(prepare(root) == 1 and tree_state(root) == before,
          "--prepare-assets: the mod's reshade\\reshade-shaders\\Textures there (its pictures go there whoever loads ReShade), no game archives -> exit 1, nothing written")

    # 11. --reshade-paths (Setup: add at the end, the uninstaller: remove; the Home button: add): D2R VR's two folders in the search paths of the player's dxgi.dll ReShade
    # (shared/reshade_paths.h). The dxgi.dll itself is never touched. add: 0 in, 1 could not write, 2 nothing to do (no dxgi.dll), 3 the game runs; check: 0 in, 1 not, 2 no dxgi.dll
    # (read_bytes, not read_text: a text read turns the CRLF of the ini into LF)
    def reshade_paths(root, verb):
        p = subprocess.run([str(root / "D2R_VR_Settings.exe"), "--reshade-paths", verb], cwd=str(root), capture_output=True, timeout=60, stdin=subprocess.DEVNULL)
        return p.returncode

    stock = "[GENERAL]\r\nEffectSearchPaths=.\\reshade-shaders\\Shaders\\**\r\nTextureSearchPaths=.\\reshade-shaders\\Textures\\**\r\nPresetPath=.\\ReShadePreset.ini\r\n"
    ours_fx = ".\\mods\\D2R-VR\\reshade\\reshade-shaders\\Shaders\\**"
    ours_tx = ".\\mods\\D2R-VR\\reshade\\reshade-shaders\\Textures\\**"
    root = make_game(scratch / "g11a", settings, loader, "[mode]\nplatform=1\n")
    check(reshade_paths(root, "add") == 2 and reshade_paths(root, "check") == 2 and not (root / "ReShade.ini").exists(), "--reshade-paths: no dxgi.dll next to D2R.exe -> add / check say 2 (nothing to do), no ReShade.ini is made")
    (root / "dxgi.dll").write_bytes(b"MZ PLAYERS RESHADE")
    dxb = (root / "dxgi.dll").read_bytes()
    dxt = os.stat(root / "dxgi.dll").st_mtime_ns
    (root / "ReShade.ini").write_text(stock, encoding="utf-8", newline="")
    check(reshade_paths(root, "check") == 1, "...a dxgi.dll and his ReShade.ini without our folders: check says 1")
    check(reshade_paths(root, "add") == 0, "...add -> 0")
    t = (root / "ReShade.ini").read_bytes().decode("utf-8")
    check(ours_fx in t and ours_tx in t and reshade_paths(root, "check") == 0, "...his ReShade.ini lists the mod's reshade-shaders\\Shaders and \\Textures, check says 0")
    check(t.replace("," + ours_fx, "").replace("," + ours_tx, "") == stock and (root / "ReShade.ini.before_d2rvr").read_bytes().decode("utf-8") == stock,
          "...nothing else of his file changed, and a copy of it as it was is kept beside it")
    check((root / "dxgi.dll").read_bytes() == dxb and os.stat(root / "dxgi.dll").st_mtime_ns == dxt, "...his dxgi.dll is the same bytes with the same time")
    check(reshade_paths(root, "add") == 0 and (root / "ReShade.ini").read_bytes().decode("utf-8") == t, "...a second add changes nothing")
    # the game runs: ReShade writes its ini back when the game ends, so an edit now would be lost
    root = make_game(scratch / "g11b", settings, loader, "[mode]\nplatform=1\n")
    (root / "dxgi.dll").write_bytes(b"MZ")
    (root / "ReShade.ini").write_text(stock, encoding="utf-8", newline="")
    fake_game = scratch / "g11_proc" / "D2R.exe"
    fake_game.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "PING.EXE", fake_game)
    hold = subprocess.Popen([str(fake_game), "-n", "40", "127.0.0.1"], stdout=subprocess.DEVNULL)
    try:
        time.sleep(0.5)
        rc_running = reshade_paths(root, "add")
    finally:
        hold.kill()
        hold.wait()
    check(rc_running == 3 and (root / "ReShade.ini").read_bytes().decode("utf-8") == stock, "--reshade-paths add while the game runs -> 3, his ReShade.ini untouched")
    # ReShade's own redirect: [INSTALL] BasePath in the ReShade.ini next to the exe moves ReShade's base - that folder's ReShade.ini is the one that counts, with absolute paths
    root = make_game(scratch / "g11c", settings, loader, "[mode]\nplatform=1\n")
    (root / "dxgi.dll").write_bytes(b"MZ")
    (root / "myreshade").mkdir()
    (root / "myreshade" / "ReShade.ini").write_text(stock, encoding="utf-8", newline="")
    (root / "ReShade.ini").write_text("[INSTALL]\r\nBasePath=.\\myreshade\r\n", encoding="utf-8", newline="")
    redirect0 = (root / "ReShade.ini").read_bytes().decode("utf-8")
    check(reshade_paths(root, "add") == 0, "[INSTALL] BasePath=.\\myreshade in the ReShade.ini beside the exe: add -> 0")
    inner = (root / "myreshade" / "ReShade.ini").read_bytes().decode("utf-8")
    abs_fx = str(root / "mods" / "D2R-VR" / "reshade").lower() + "\\reshade-shaders\\shaders\\**"
    check(abs_fx in inner.lower() and (root / "ReShade.ini").read_bytes().decode("utf-8") == redirect0,
          "...the paths go into the redirected ReShade.ini (absolute, its base is not the game's folder); the file beside the exe is untouched")
    check(reshade_paths(root, "check") == 0 and reshade_paths(root, "remove") == 0 and (root / "myreshade" / "ReShade.ini").read_bytes().decode("utf-8") == stock,
          "...remove gives the redirected file back as it was")
    # remove: the uninstaller's call - his file is back byte for byte (also a UTF-8 file with a mark), and the copy is gone
    root = make_game(scratch / "g11d", settings, loader, "[mode]\nplatform=1\n")
    (root / "dxgi.dll").write_bytes(b"MZ")
    bom = b"\xef\xbb\xbf" + stock.encode("utf-8")
    (root / "ReShade.ini").write_bytes(bom)
    check(reshade_paths(root, "add") == 0 and (root / "ReShade.ini").read_bytes().startswith(b"\xef\xbb\xbf") and ours_fx.encode() in (root / "ReShade.ini").read_bytes(),
          "a ReShade.ini with a UTF-8 mark: the paths go in, the mark stays")
    check(reshade_paths(root, "remove") == 0 and (root / "ReShade.ini").read_bytes() == bom and not (root / "ReShade.ini.before_d2rvr").exists(),
          "remove: his file is back byte for byte and the copy is gone")
    (root / "dxgi.dll").unlink()
    check(reshade_paths(root, "remove") == 0 and (root / "ReShade.ini").read_bytes() == bom, "remove without a dxgi.dll there: nothing to take out, nothing changes")
    # no ReShade.ini at all (a fresh dxgi.dll): one is made with ReShade's stock lists and ours
    root = make_game(scratch / "g11e", settings, loader, "[mode]\nplatform=1\n")
    (root / "dxgi.dll").write_bytes(b"MZ")
    check(reshade_paths(root, "add") == 0 and (root / "ReShade.ini").exists(), "no ReShade.ini yet: add makes one")
    t = (root / "ReShade.ini").read_bytes().decode("utf-8")
    check("EffectSearchPaths=.\\reshade-shaders\\Shaders\\**," + ours_fx in t and "TextureSearchPaths=.\\reshade-shaders\\Textures\\**," + ours_tx in t,
          "...with ReShade's stock folders first and ours after them")
    # an install from before the mod layout (the global plugin): not the mod, nothing to do
    root = make_game(scratch / "g11f", settings, loader, "[mode]\nplatform=1\n", layout="global")
    (root / "dxgi.dll").write_bytes(b"MZ")
    (root / "ReShade.ini").write_text(stock, encoding="utf-8", newline="")
    check(reshade_paths(root, "add") == 2 and (root / "ReShade.ini").read_bytes().decode("utf-8") == stock, "an install from before the mod layout: add -> 2, his ReShade.ini untouched (the files are beside the game there)")

    # 12. [mods] merge_data_mods (shared/data_mods.h): a mod D2RMM built (mods\D2RMM) loads with D2R VR - its files copied into D2R-VR.mpq at --start,
    # copied again when D2RMM builds again, taken out when the tick is off; D2R VR's own file stays
    root = make_game(scratch / "g12", settings, loader, ini_native)
    ours_mpq = root / "mods" / "D2R-VR" / "D2R-VR.mpq"
    rm_mpq = root / "mods" / "D2RMM" / "D2RMM.mpq"
    (ours_mpq / "data" / "hd").mkdir(parents=True)
    (ours_mpq / "modinfo.json").write_text('{"name":"D2R-VR","savepath":"../"}', encoding="utf-8")
    (ours_mpq / "data" / "hd" / "ours.json").write_text("ours", encoding="utf-8")
    (rm_mpq / "data" / "global" / "excel").mkdir(parents=True)
    (rm_mpq / "modinfo.json").write_text('{\n  "name": "D2RMM",\n  "savepath": "D2RMM/"\n}', encoding="utf-8")
    (rm_mpq / "data" / "global" / "excel" / "weapons.txt").write_text("rm 1", encoding="utf-8")
    (rm_mpq / "data" / "hd").mkdir(parents=True)
    (rm_mpq / "data" / "hd" / "ours.json").write_text("theirs", encoding="utf-8")
    merged = ours_mpq / "data" / "global" / "excel" / "weapons.txt"

    def start_again(root):
        for _ in range(100):
            if not running("D2RLoader.exe"):
                break
            time.sleep(0.05)
        f = root / "fake_loader_started.txt"
        if f.exists():
            f.unlink()
        code, out, _, _ = start(root)
        return code, out, kv(wait_file(f))

    code, out, started = start_again(root)
    check(code == 0 and started.get("argline") == "-mod D2R-VR", "a D2RMM mod in mods\\D2RMM: --start -> exit 0, still `-mod D2R-VR`", f"(got {code}; {out!r})")
    check(rd(merged) == "rm 1", "...D2RMM's file is in D2R VR's mod before the game starts", repr(rd(merged)))
    check(rd(ours_mpq / "data" / "hd" / "ours.json") == "ours", "...D2R VR's own file of the same name stays")
    check("other mods: merged into D2R VR: D2RMM" in out, "...stdout says so", repr(out))
    check("its own save folder (D2RMM/) is not used" in out, "...and that D2RMM's save folder is not taken", repr(out))
    log12 = rd(root / MOD_LOGS / "d2r_vr_data_mods.txt") or ""
    check("weapons.txt" not in log12 and "ours.json: D2R VR has its own - D2R VR's kept" in log12, "...d2r_vr_data_mods.txt names the choice", repr(log12))
    time.sleep(0.05)
    (rm_mpq / "data" / "global" / "excel" / "weapons.txt").write_text("rm 2 built again", encoding="utf-8")
    code, out, _ = start_again(root)
    check(code == 0 and rd(merged) == "rm 2 built again", "D2RMM built again: the next --start takes the new file", repr(rd(merged)))
    (root / MOD_PLUGINS / "d2r_vr.ini").write_text(ini_native + "[mods]\nmerge_data_mods=0\n", encoding="utf-8")
    code, out, _ = start_again(root)
    check(code == 0 and not merged.exists() and rd(ours_mpq / "data" / "hd" / "ours.json") == "ours",
          "[mods] merge_data_mods=0: the next --start takes D2RMM's files out again, D2R VR's own stays", f"({out!r})")
    check(not (root / "mods" / "D2R-VR" / "data_mods.txt").exists() and rd(rm_mpq / "data" / "global" / "excel" / "weapons.txt") == "rm 2 built again",
          "...no record left, D2RMM's own folder untouched")

    check(real_game_settings() == real0, "no start touched the player's own Settings.json (hash and size the same before and after)")

    print(f"\n{checks - len(failures)}/{checks} checks passed")
    if not failures:
        shutil.rmtree(scratch, ignore_errors=True)
    else:
        print("scratch kept:", scratch)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
