#!/usr/bin/env python3
"""Makes D2R VR Authentic's vrmod-manifest.json (+ its icon) for the shared VR mod launcher: what Setup writes into <game>\\mods\\D2R-VR\\.

    make_vrmod_manifest.py --out <folder> [--payload installer\\payload] [--version-label 0.168.0-launcher-test] [--commit abc1234] [--date 2026-10-18]
    make_vrmod_manifest.py --check <vrmod-manifest.json>      (checks a manifest that exists, e.g. the one Setup wrote)
    make_vrmod_manifest.py --verify <game>\\mods\\D2R-VR       (the installed mod against its manifest: every file x-mod-files lists)
    make_vrmod_manifest.py --selftest

The manifest is GENERATED, never hand-written: installer\\launcher\\vrmod-manifest.template.json (a copy of the launcher's example for
D2R-VR, examples/d2rloader-mods/d2r-vr/vrmod-manifest.json, with @VERSION@ in place of the version) is filled with the version, checked, and
written as vrmod-manifest.json together with the icon it names (launcher_icon.png), both into --out. The contract is the launcher's
docs/MOD_MANIFEST.md (schema vrmod-manifest/1 with d2rloader_mod). The version is vr/vrcam.cpp's g_info_version (the one number
build_installer.ps1, version.cmake and the plugin's VERSIONINFO read); --version-label is the version as SHOWN ("0.167.0-launcher-test") and must
start with it.

With --payload (build_installer.ps1 always gives it) the manifest also carries `x-mod-files` (an extension key, free in the launcher's schema): the
files Setup puts in the mod's folder - the plugin, the optional weight and grip libraries, d2r_vr.ini and its defaults, the shader, the sky, FlatVR's
add-on - each as {path relative to the mod folder, sha256 of the shipped file, optional, user_edits}. The list is not written by hand: it is
installer\\mod_files.inc (the very [Files] lines Setup compiles) read against the staged payload, so it is what Setup installs. optional: may be absent
(a library the build did not ship, the VR-only add-on); user_edits: the player's copy may differ (d2r_vr.ini). The manifest and its icon are not in it.

Checks (nothing is written when one fails):
  * the launcher's JSON Schema (installer\\launcher\\vrmod-manifest.1.schema.json, a copy of the launcher repository's; a small draft-07 subset
    validator is built in, no packages needed);
  * the rules a schema cannot say and the launcher's C++ validator enforces: a D2RLoader mod has no exclusive_group and no files;
    the icon is a file next to the manifest (a PNG or an ICO);
  * the contract of this mod: id d2r-vr, mod folder D2R-VR, START is `{game}/D2R_VR_Settings.exe --start`, the Settings button is that exe,
    the uninstaller is the game folder's unins000.exe, every path is under {game}; the D2RLoader archive hashes are the two the installer
    (installer\\D2R_VR_Setup.iss) accepts; the version is vrcam.cpp's.
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAUNCHER_DIR = ROOT / "installer" / "launcher"
TEMPLATE = LAUNCHER_DIR / "vrmod-manifest.template.json"
SCHEMA = LAUNCHER_DIR / "vrmod-manifest.1.schema.json"
ICON = LAUNCHER_DIR / "launcher_icon.png"
ISS = ROOT / "installer" / "D2R_VR_Setup.iss"
MOD_FILES_INC = ROOT / "installer" / "mod_files.inc"
MANIFEST_NAME = "vrmod-manifest.json"
PLACEHOLDER = "@VERSION@"
FILES_PLACEHOLDER = "@MOD_FILES@"
MOD_ID, MOD_FOLDER = "d2r-vr", "D2R-VR"


class ManifestError(Exception):
    pass


# ------------------------------------------------------------------ a draft-07 subset validator (the same as the launcher's tools/check_schema.py)

class Invalid(Exception):
    pass


def _type_ok(v, t):
    return {"object": isinstance(v, dict), "array": isinstance(v, list), "string": isinstance(v, str), "boolean": isinstance(v, bool),
            "integer": isinstance(v, int) and not isinstance(v, bool), "number": isinstance(v, (int, float)) and not isinstance(v, bool),
            "null": v is None}[t]


def validate(v, s, root, path="/"):
    """Raises Invalid(message) on the first problem."""
    if "$ref" in s:
        node = root
        for part in s["$ref"].lstrip("#/").split("/"):
            node = node[part]
        return validate(v, node, root, path)
    if "const" in s and v != s["const"]:
        raise Invalid(f"{path}: must be {s['const']!r}")
    if "enum" in s and v not in s["enum"]:
        raise Invalid(f"{path}: {v!r} is not one of {s['enum']}")
    if "type" in s:
        types = s["type"] if isinstance(s["type"], list) else [s["type"]]
        if not any(_type_ok(v, t) for t in types):
            raise Invalid(f"{path}: wrong type, wanted {types}")
    if isinstance(v, str):
        if "minLength" in s and len(v) < s["minLength"]:
            raise Invalid(f"{path}: too short")
        if "pattern" in s and not re.search(s["pattern"], v):
            raise Invalid(f"{path}: {v!r} does not match {s['pattern']}")
    if isinstance(v, list):
        if "minItems" in s and len(v) < s["minItems"]:
            raise Invalid(f"{path}: too few items")
        if "items" in s:
            for i, e in enumerate(v):
                validate(e, s["items"], root, f"{path}{i}/")
    if isinstance(v, dict):
        for k in s.get("required", []):
            if k not in v:
                raise Invalid(f"{path}: missing {k!r}")
        props, pats = s.get("properties", {}), s.get("patternProperties", {})
        for k, e in v.items():
            if k in props:
                validate(e, props[k], root, f"{path}{k}/")
                continue
            hit = [p for p in pats if re.search(p, k)]
            if hit:
                for p in hit:
                    validate(e, pats[p], root, f"{path}{k}/")
                continue
            ap = s.get("additionalProperties", True)
            if ap is False:
                raise Invalid(f"{path}: unknown field {k!r}")
            if isinstance(ap, dict):
                validate(e, ap, root, f"{path}{k}/")
    for sub in s.get("allOf", []):
        validate(v, sub, root, path)
    if "not" in s:
        try:
            validate(v, s["not"], root, path)
        except Invalid:
            pass
        else:
            raise Invalid(f"{path}: matches a 'not' rule")
    if "if" in s:
        try:
            validate(v, s["if"], root, path)
            cond = True
        except Invalid:
            cond = False
        branch = s.get("then") if cond else s.get("else")
        if branch:
            validate(v, branch, root, path)


# ------------------------------------------------------------------ the sources of truth

def version_from_source(root=ROOT):
    """vrcam.cpp's g_info_version: the one number (build_installer.ps1 and version.cmake read it the same way)."""
    text = (Path(root) / "vr" / "vrcam.cpp").read_text(encoding="utf-8", errors="replace")
    m = re.search(r'g_info_version\[\] = "([0-9.]+)"', text)
    if not m:
        raise ManifestError('no g_info_version[] = "x.y.z" in vr/vrcam.cpp')
    return m.group(1)


def installer_archive_hashes(iss=ISS):
    """The two D2RLoader ZIP hashes D2R_VR_Setup.iss accepts (the current one and the historical one)."""
    text = Path(iss).read_text(encoding="utf-8", errors="replace")
    out = []
    for name in ("D2RLoaderSha256", "D2RLoaderSha256Historical"):
        m = re.search(r'#define\s+' + name + r'\s+"([0-9a-fA-F]{64})"', text)
        if not m:
            raise ManifestError(f"{iss}: no #define {name}")
        out.append(m.group(1).lower())
    return out


def sha256_file(p):
    import hashlib
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ------------------------------------------------------------------ x-mod-files: what Setup puts in the mod's folder

_FIELD = re.compile(r'\s*(\w+)\s*:\s*(?:"([^"]*)"|([^;"]*))\s*(?:;|$)')
_SRC_PREFIX = "{#PayloadDir}\\"
_DST_PREFIX = "{app}\\{#ModDir}"


def parse_mod_files_inc(text):
    """The [Files] lines of installer\\mod_files.inc as dicts {Source, DestDir, DestName, Flags, Check} (the same lines Inno Setup compiles)."""
    lines = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("Source:"):
            continue
        fields, pos = {}, 0
        while pos < len(line):
            m = _FIELD.match(line, pos)
            if not m or m.end() == pos:
                raise ManifestError(f"mod_files.inc: cannot read the line {line!r}")
            fields[m.group(1)] = m.group(2) if m.group(2) is not None else m.group(3).strip()
            pos = m.end()
        lines.append(fields)
    if not lines:
        raise ManifestError("mod_files.inc: no Source: lines")
    return lines


def mod_files_from_payload(payload, inc=MOD_FILES_INC):
    """x-mod-files: every file the [Files] lines of mod_files.inc install into the mod, as the staged payload holds it.
    Not listed: the launcher's own manifest and icon (payload\\launcher; a manifest cannot hold its own hash), and a file marked
    skipifsourcedoesntexist that this build did not stage (then Setup does not ship it either)."""
    payload = Path(payload)
    out, seen = [], set()
    for f in parse_mod_files_inc(Path(inc).read_text(encoding="utf-8")):
        src, dst = f.get("Source", ""), f.get("DestDir", "")
        if not src.startswith(_SRC_PREFIX):
            raise ManifestError(f"mod_files.inc: {src!r} is not under {{#PayloadDir}}")
        if not dst.startswith(_DST_PREFIX):
            raise ManifestError(f"mod_files.inc: {dst!r} is outside the mod folder - a mod's installer writes nothing outside its own folder")
        rel_src = src[len(_SRC_PREFIX):].replace("\\", "/")
        if rel_src.startswith("launcher/"):
            continue
        rel_dir = dst[len(_DST_PREFIX):].replace("\\", "/").strip("/")
        flags = set(f.get("Flags", "").replace(",", " ").split())
        check = f.get("Check", "")
        if "*" in rel_src:
            folder, pattern = rel_src.rsplit("/", 1)
            staged = sorted(p for p in (payload / folder).glob(pattern) if p.is_file())
            if not staged:
                raise ManifestError(f"nothing staged for {rel_src!r} (looked in {payload})")
        else:
            p = payload / rel_src
            if not p.is_file():
                if "skipifsourcedoesntexist" in flags:
                    continue
                raise ManifestError(f"nothing staged for {rel_src!r} (looked in {payload})")
            staged = [p]
        for p in staged:
            name = f.get("DestName") or p.name
            path = (rel_dir + "/" if rel_dir else "") + name
            e = {"path": path, "sha256": sha256_file(p), "optional": "skipifsourcedoesntexist" in flags or "IsVR" in check}
            if "onlyifdoesntexist" in flags:
                e["user_edits"] = True
            if path.lower() in seen:
                raise ManifestError(f"mod_files.inc installs {path} twice")
            seen.add(path.lower())
            out.append(e)
    return sorted(out, key=lambda e: e["path"].lower())


def check_mod_files(lst):
    """What the schema cannot say about x-mod-files: '/' separators, nothing outside the folder, no duplicates, not the manifest itself."""
    seen = set()
    for e in lst:
        path = e["path"]
        if "\\" in path or path.startswith("/") or ".." in path.split("/") or "//" in path or path.endswith("/"):
            raise ManifestError(f"x-mod-files: {path!r} is not a clean relative path with '/' separators")
        if path.lower() == MANIFEST_NAME:
            raise ManifestError("x-mod-files: the manifest cannot list itself")
        if path.lower() in seen:
            raise ManifestError(f"x-mod-files: {path} is listed twice")
        seen.add(path.lower())


def verify_installed(mod_folder, m):
    """The installed mod against its manifest: a list of problems (empty = every file x-mod-files lists is there with the shipped hash;
    optional ones may be absent, user_edits ones may differ)."""
    problems = []
    for e in m.get("x-mod-files", []):
        p = Path(mod_folder) / e["path"]
        if not p.is_file():
            if not e.get("optional"):
                problems.append(f"missing: {e['path']}")
        elif not e.get("user_edits") and sha256_file(p) != e["sha256"].lower():
            problems.append(f"changed: {e['path']}")
    return problems


def _strings(node):
    if isinstance(node, str):
        yield node
    elif isinstance(node, list):
        for e in node:
            yield from _strings(e)
    elif isinstance(node, dict):
        for k, e in node.items():
            yield k
            yield from _strings(e)


# ------------------------------------------------------------------ the checks

def check_manifest(m, folder, schema_path=SCHEMA, expect_version=None, iss=ISS):
    """Raises ManifestError for the first problem. `folder` is where the manifest and its icon are (the mod's folder)."""
    schema = json.loads(Path(schema_path).read_text(encoding="utf-8"))
    try:
        validate(m, schema, schema)
    except Invalid as e:
        raise ManifestError(f"schema: {e}")
    # what the schema cannot say (the launcher's C++ validator does)
    if "d2rloader_mod" not in m:
        raise ManifestError("d2rloader_mod: D2R VR Authentic is a D2RLoader mod")
    if "exclusive_group" in m:
        raise ManifestError("exclusive_group: a D2RLoader mod is not swapped: leave it out")
    if "files" in m:
        raise ManifestError("files: a D2RLoader mod is installed by its own installer and not swapped: leave files out")
    icon = m.get("icon")
    if icon:
        p = Path(folder) / icon
        if not p.is_file():
            raise ManifestError(f"icon: {icon} is not next to the manifest")
        head = p.read_bytes()[:8]
        if not (head.startswith(b"\x89PNG\r\n\x1a\n") or head[:4] == b"\x00\x00\x01\x00"):
            raise ManifestError(f"icon: {icon} is neither a PNG nor an ICO")
        if p.stat().st_size > 2 << 20:
            raise ManifestError(f"icon: {icon} is {p.stat().st_size} bytes (a small file is wanted: 2 MB at most)")
    # this mod's contract
    if m["id"] != MOD_ID:
        raise ManifestError(f"id: must be {MOD_ID}")
    if m["d2rloader_mod"]["name"] != MOD_FOLDER:
        raise ManifestError(f"d2rloader_mod.name: must be {MOD_FOLDER} (the mod folder and D2RLoader's -mod name)")
    if m["name"] != "D2R VR Authentic":
        raise ManifestError("name: must be D2R VR Authentic")
    opts = m.get("launch", {}).get("options", [])
    if not opts:
        raise ManifestError("launch.options: Play needs one option")
    o = opts[0]
    if o.get("kind") != "exec" or o.get("exe") != "{game}/D2R_VR_Settings.exe" or o.get("args") != ["--start"]:
        raise ManifestError("launch.options[0]: START is `{game}/D2R_VR_Settings.exe --start`")
    if m.get("settings_app", {}).get("exe") != "{game}/D2R_VR_Settings.exe":
        raise ManifestError("settings_app.exe: the Settings button is {game}/D2R_VR_Settings.exe")
    if m.get("uninstall", {}).get("exe") != "{game}/unins000.exe":
        raise ManifestError("uninstall.exe: the uninstaller is {game}/unins000.exe")
    for log in m.get("logs", []):
        if not log.startswith("{game}/"):
            raise ManifestError(f"logs: {log} is not under {{game}}")
    if "x-mod-files" in m:
        check_mod_files(m["x-mod-files"])
    for s in _strings(m):
        if re.search(r"@[A-Z_]+@", s):
            raise ManifestError(f"a placeholder is left: {s!r}")
    # D2RLoader: the archive hashes both mods' installers accept = the ones D2R_VR_Setup.iss carries
    want = sorted(installer_archive_hashes(iss))
    rq = [r for r in m.get("requires", []) if r.get("id") == "d2rloader"]
    if not rq:
        raise ManifestError("requires: D2RLoader is missing")
    got = sorted(h.lower() for h in rq[0].get("x-archive", {}).get("sha256_any_of", []))
    if got != want:
        raise ManifestError(f"requires[d2rloader].x-archive.sha256_any_of {got} is not what {Path(iss).name} accepts {want}")
    if expect_version is not None and m["version"] != expect_version:
        raise ManifestError(f"version: {m['version']} is not {expect_version}")


def make(out, version_label=None, commit=None, date=None, payload=None, root=ROOT, template=TEMPLATE, schema=SCHEMA, icon=ICON, iss=ISS,
         inc=MOD_FILES_INC):
    """Fills the template, checks it, writes <out>\\vrmod-manifest.json and the icon. Returns the manifest's path.
    With `payload` (the staged files) the manifest lists the files Setup puts in the mod (x-mod-files); without it the key is left out."""
    base = version_from_source(root)
    version = version_label or base
    if version.split("-", 1)[0] != base:
        raise ManifestError(f"version label {version!r} does not start with vrcam.cpp's g_info_version {base}")
    text = Path(template).read_text(encoding="utf-8")
    if text.count(f'"{PLACEHOLDER}"') != 1:
        raise ManifestError(f"the template must hold exactly one {PLACEHOLDER}")
    if text.count(f'"{FILES_PLACEHOLDER}"') != 1:
        raise ManifestError(f"the template must hold exactly one {FILES_PLACEHOLDER}")
    files = mod_files_from_payload(payload, inc) if payload else None
    m = json.loads(text.replace(f'"{PLACEHOLDER}"', json.dumps(version)).replace(f'"{FILES_PLACEHOLDER}"', json.dumps(files)))
    if files is None:
        del m["x-mod-files"]
    if commit or date:
        build = {}
        if commit:
            build["commit"] = commit
        if date:
            build["date"] = date
        items = list(m.items())               # (after "description", where the schema lists it)
        at = next((i for i, (k, _) in enumerate(items) if k == "description"), len(items) - 1) + 1
        items.insert(at, ("build", build))
        m = dict(items)
    out = Path(out)
    if not m.get("icon"):
        raise ManifestError("the template names no icon")
    body = json.dumps(m, indent=2, ensure_ascii=False) + "\n"
    with tempfile.TemporaryDirectory(prefix="vrmod_manifest_") as stage:   # checked where nothing is left behind if it fails
        shutil.copyfile(icon, Path(stage) / m["icon"])
        check_manifest(m, stage, schema, version, iss)
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(icon, out / (m["icon"] + ".part"))
    (out / (MANIFEST_NAME + ".part")).write_text(body, encoding="utf-8", newline="\n")
    for f in (m["icon"], MANIFEST_NAME):        # (the manifest last: a manifest is never there without its icon)
        os.replace(out / (f + ".part"), out / f)
    return out / MANIFEST_NAME


# ------------------------------------------------------------------ self test

def selftest():
    failures = []

    def expect(ok, what):
        print(("  ok    " if ok else "  FAIL  ") + what)
        if not ok:
            failures.append(what)

    base = version_from_source()
    with tempfile.TemporaryDirectory(prefix="vrmod_selftest_") as t:
        t = Path(t)
        p = make(t / "a")
        m = json.loads(p.read_text(encoding="utf-8"))
        expect(m["version"] == base and m["id"] == MOD_ID, f"a manifest is made with vrcam.cpp's version ({base})")
        expect((t / "a" / m["icon"]).is_file(), "...and its icon beside it")
        check_manifest(m, t / "a", expect_version=base)
        expect(True, "...which passes every check")
        p2 = make(t / "b", version_label=base + "-launcher-test", commit="abc1234", date="2026-10-17")
        m2 = json.loads(p2.read_text(encoding="utf-8"))
        expect(m2["version"] == base + "-launcher-test" and m2["build"] == {"commit": "abc1234", "date": "2026-10-17"}, "a version label and build info go in")

        def bad(what, edit):
            mm = json.loads(json.dumps(m))
            edit(mm)
            try:
                check_manifest(mm, t / "a", expect_version=base)
            except ManifestError as e:
                expect(True, f"refused: {what}  ({str(e)[:70]})")
                return
            expect(False, f"refused: {what}")

        mm = json.loads(json.dumps(m))
        mm["x-extension"] = {"free": True}
        try:
            check_manifest(mm, t / "a", expect_version=base)
            expect(True, "an x- key is allowed")
        except ManifestError as e:
            expect(False, f"an x- key is allowed ({e})")
        bad("an unknown key", lambda x: x.update({"colour": "red"}))
        bad("files with a D2RLoader mod", lambda x: x.update({"files": [{"dst": "{game}/a", "role": "generated"}]}))
        bad("an exclusive_group with a D2RLoader mod", lambda x: x.update({"exclusive_group": "d2r"}))
        bad("no d2rloader_mod", lambda x: x.pop("d2rloader_mod"))
        bad("another mod folder", lambda x: x["d2rloader_mod"].update({"name": "D2R-PCVR"}))
        bad("a bad id", lambda x: x.update({"id": "D2R VR"}))
        expect(m["settings_app"].get("label") == "D2R VR Authentic Settings" and m["settings_app"]["exe"] == "{game}/D2R_VR_Settings.exe",
               "the settings button is labelled with the display name (settings_app.label); the program is still {game}/D2R_VR_Settings.exe")
        bad("an empty settings_app label", lambda x: x["settings_app"].update({"label": ""}))
        bad("a settings_app label that is not text", lambda x: x["settings_app"].update({"label": 5}))
        bad("a bad version", lambda x: x.update({"version": "0.167"}))
        bad("another version than the source's", lambda x: x.update({"version": "9.9.9"}))
        bad("a placeholder left", lambda x: x.update({"description": "@VERSION@"}))
        bad("START without --start", lambda x: x["launch"]["options"][0].update({"args": []}))
        bad("an icon that is not there", lambda x: x.update({"icon": "nope.png"}))
        bad("an icon that leaves the folder", lambda x: x.update({"icon": "../x.png"}))
        bad("a store that does not exist", lambda x: x["game"].update({"stores": ["epic"]}))
        bad("another D2RLoader archive hash", lambda x: x["requires"][0]["x-archive"].update({"sha256_any_of": ["0" * 64]}))
        bad("a log outside the game folder", lambda x: x["logs"].append("C:/x.log"))
        bad("a hash that is not 64 hex digits", lambda x: x["requires"][0]["files"][0].update({"sha256_any_of": ["abc"]}))
        # the icon must be a picture
        (t / "a" / "not_png.png").write_bytes(b"hello")
        mm = json.loads(json.dumps(m))
        mm["icon"] = "not_png.png"
        try:
            check_manifest(mm, t / "a", expect_version=base)
            expect(False, "refused: an icon that is not a PNG")
        except ManifestError:
            expect(True, "refused: an icon that is not a PNG")
        # a failed make writes nothing
        try:
            make(t / "c", version_label="9.9.9")
            expect(False, "a version label that is not vrcam.cpp's stops make")
        except ManifestError:
            expect(not (t / "c").exists(), "a version label that is not vrcam.cpp's stops make, and nothing is written")
        # the template itself
        tpl = TEMPLATE.read_text(encoding="utf-8")
        expect(tpl.count(f'"{PLACEHOLDER}"') == 1 and tpl.count(f'"{FILES_PLACEHOLDER}"') == 1, "the template holds one version and one file-list placeholder")
        expect("x-mod-files" not in m, "without a payload the manifest has no x-mod-files")

        # ---- x-mod-files: a payload staged the way installer\mod_files.inc says
        def stage(root, libs=True, skip=()):
            for f in parse_mod_files_inc(MOD_FILES_INC.read_text(encoding="utf-8")):
                rel = f["Source"][len(_SRC_PREFIX):].replace("\\", "/")
                if rel.startswith("launcher/") or rel in skip or (not libs and ("d2r_vr_weight" in rel or "d2r_vr_grip" in rel)):
                    continue
                names = [rel] if "*" not in rel else [rel.replace("*", "D2R_Sky_act1"), rel.replace("*", "D2R_Sky_act2")]
                for n in names:
                    q = Path(root) / n
                    q.parent.mkdir(parents=True, exist_ok=True)
                    q.write_bytes(("staged " + n).encode())

        pay = t / "payload"
        stage(pay)
        p3 = make(t / "d", payload=pay)
        m3 = json.loads(p3.read_text(encoding="utf-8"))
        files = {e["path"]: e for e in m3["x-mod-files"]}
        P = "d2rloader/plugins/"
        L = "d2rloader/d2r_vr_libs/"        # the optional weight and grip libraries are not among the plugins
        expect(set(files) >= {P + "d2rl-vrcam.dll", L + "d2r_vr_weight.dll", L + "d2r_vr_grip.dll", P + "d2r_vr.ini", P + "d2r_vr.default.ini",
                              "reshade/reshade-shaders/Shaders/D2R_DepthFog.fx", "reshade/reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_act1.png",
                              "reshade/reshade-shaders/Textures/D2R_Sky_ours/D2R_Sky_act2.png", "reshade/FlatVR_DepthProvider.addon64"},
               "x-mod-files lists the plugin, both libraries, the ini and its defaults, the shader, the sky (a wildcard expanded), FlatVR's add-on")
        expect(not any(k in files for k in ("vrmod-manifest.json", "launcher_icon.png")) and not any(k.startswith("..") for k in files),
               "...but not the manifest and its icon, and nothing outside the mod folder")
        expect(files[P + "d2rl-vrcam.dll"]["sha256"] == sha256_file(pay / "plugins/d2rl-vrcam.dll") and files[P + "d2rl-vrcam.dll"]["optional"] is False,
               "...with the shipped file's hash; the plugin is not optional")
        expect(files[L + "d2r_vr_weight.dll"]["optional"] and files[L + "d2r_vr_grip.dll"]["optional"] and files["reshade/FlatVR_DepthProvider.addon64"]["optional"],
               "...the libraries and the VR-only add-on are optional")
        expect(files[P + "d2r_vr.ini"].get("user_edits") is True and "user_edits" not in files[P + "d2r_vr.default.ini"], "...d2r_vr.ini is the player's (user_edits), its defaults are not")
        expect(files[P + "d2r_vr.ini"]["sha256"] == files[P + "d2r_vr.default.ini"]["sha256"], "...and the seed and the defaults are the same shipped file")
        expect([e["path"] for e in m3["x-mod-files"]] == sorted((e["path"] for e in m3["x-mod-files"]), key=str.lower), "...sorted by path")
        check_manifest(m3, t / "d")
        expect(True, "...the manifest passes every check")
        # a build that does not ship the libraries
        pay2 = t / "payload2"
        stage(pay2, libs=False)
        m4 = json.loads(make(t / "e", payload=pay2).read_text(encoding="utf-8"))
        expect(not any("d2r_vr_weight" in e["path"] or "d2r_vr_grip" in e["path"] for e in m4["x-mod-files"]) and len(m4["x-mod-files"]) == len(m3["x-mod-files"]) - 2,
               "a build without the weight and grip libraries does not list them (Setup does not ship them either)")
        # a build with a file missing that Setup needs
        pay3 = t / "payload3"
        stage(pay3, skip=("plugins/d2rl-vrcam.dll",))
        try:
            make(t / "f", payload=pay3)
            expect(False, "a payload without the plugin stops make")
        except ManifestError:
            expect(not (t / "f").exists(), "a payload without the plugin stops make, and nothing is written")
        # an .inc line that installs outside the mod folder
        bad_inc = t / "bad.inc"
        bad_inc.write_text('Source: "{#PayloadDir}\\plugins\\d2rl-vrcam.dll"; DestDir: "{app}\\d2rloader\\plugins"; Flags: ignoreversion\n', encoding="utf-8")
        try:
            mod_files_from_payload(pay, bad_inc)
            expect(False, "refused: a [Files] line that installs outside the mod folder")
        except ManifestError:
            expect(True, "refused: a [Files] line that installs outside the mod folder")

        def bad_files(what, edit):
            mm = json.loads(json.dumps(m3))
            edit(mm["x-mod-files"])
            try:
                check_manifest(mm, t / "d")
            except ManifestError as e:
                expect(True, f"refused: {what}  ({str(e)[:60]})")
                return
            expect(False, f"refused: {what}")

        bad_files("a path with a backslash", lambda l: l[0].update({"path": "d2rloader\\plugins\\x.dll"}))
        bad_files("a path that leaves the mod folder", lambda l: l[0].update({"path": "../x.dll"}))
        bad_files("an absolute path", lambda l: l[0].update({"path": "/x.dll"}))
        bad_files("a path listed twice", lambda l: l.append(dict(l[0])))
        bad_files("the manifest listing itself", lambda l: l[0].update({"path": "vrmod-manifest.json"}))
        bad_files("a hash that is not 64 hex digits", lambda l: l[0].update({"sha256": "abc"}))
        bad_files("a key an entry does not have", lambda l: l[0].update({"mtime": 1}))
        # the installed mod against its manifest
        mod = t / "mod"
        for e in m3["x-mod-files"]:
            q = mod / e["path"]
            q.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(_source_of(e, pay), q)
        expect(verify_installed(mod, m3) == [], "an install that is as shipped verifies (every listed file there, the hashes the shipped ones)")
        (mod / (L + "d2r_vr_grip.dll")).unlink()
        (mod / (P + "d2r_vr.ini")).write_bytes(b"[mode]\nplatform=1\n; the player's own\n")
        expect(verify_installed(mod, m3) == [], "...also without an optional library, and with the player's own d2r_vr.ini")
        (mod / (P + "d2rl-vrcam.dll")).write_bytes(b"another plugin")
        (mod / "reshade/reshade-shaders/Shaders/D2R_DepthFog.fx").unlink()
        got = verify_installed(mod, m3)
        expect(sorted(got) == ["changed: " + P + "d2rl-vrcam.dll", "missing: reshade/reshade-shaders/Shaders/D2R_DepthFog.fx"],
               f"...a changed plugin and a missing shader are told ({got})")
    print(f"\n{'ALL PASSED' if not failures else str(len(failures)) + ' FAILED'}")
    return 1 if failures else 0


def _source_of(entry, payload):
    """The staged file an x-mod-files entry was made from (the selftest copies it into a pretend install)."""
    for f in parse_mod_files_inc(MOD_FILES_INC.read_text(encoding="utf-8")):
        rel = f["Source"][len(_SRC_PREFIX):].replace("\\", "/")
        if rel.startswith("launcher/"):
            continue
        rel_dir = f["DestDir"][len(_DST_PREFIX):].replace("\\", "/").strip("/")
        if "*" in rel:
            folder, pattern = rel.rsplit("/", 1)
            for p in sorted((Path(payload) / folder).glob(pattern)):
                if (rel_dir + "/" if rel_dir else "") + p.name == entry["path"]:
                    return p
        elif (rel_dir + "/" if rel_dir else "") + (f.get("DestName") or Path(rel).name) == entry["path"]:
            return Path(payload) / rel
    raise ManifestError(f"selftest: no source for {entry['path']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="the folder to write vrmod-manifest.json and the icon to")
    ap.add_argument("--version-label", help="the version as shown, e.g. 0.167.0-launcher-test (must start with vrcam.cpp's g_info_version)")
    ap.add_argument("--commit")
    ap.add_argument("--date")
    ap.add_argument("--payload", help="the staged payload (installer\\payload): the manifest then lists the files Setup puts in the mod (x-mod-files)")
    ap.add_argument("--check", help="check an existing vrmod-manifest.json (its icon is looked for beside it)")
    ap.add_argument("--verify", help="an installed mod folder (<game>\\mods\\D2R-VR): its manifest is checked, then every file x-mod-files lists")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    try:
        if a.selftest:
            return selftest()
        if a.check or a.verify:
            p = Path(a.check) if a.check else Path(a.verify) / MANIFEST_NAME
            m = json.loads(p.read_text(encoding="utf-8"))
            check_manifest(m, p.parent)
            print(f"OK {p}  ({m['name']} {m['version']})")
            if a.verify:
                problems = verify_installed(a.verify, m)
                for q in problems:
                    print("  " + q)
                if problems:
                    return 1
                print(f"OK {len(m.get('x-mod-files', []))} files listed in x-mod-files are as shipped")
            return 0
        if not a.out:
            ap.error("--out <folder> is needed")
        p = make(a.out, a.version_label, a.commit, a.date, a.payload)
        m = json.loads(p.read_text(encoding="utf-8"))
        print(f"vrmod-manifest.json: {m['name']} {m['version']} -> {p}")
        return 0
    except ManifestError as e:
        print(f"make_vrmod_manifest: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
