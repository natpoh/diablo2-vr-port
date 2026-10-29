"""D2R VR Settings' language files (lang/<code>.txt; the reader is tools/settings/lang.h, the program's side settings.cpp TR / TranslateItems).

lang/en.txt is made from the program itself: the item table's texts as D2R_VR_Settings.exe --lang-template lists them (keys made from
the ini section and key, or the tab's or group's title), then every TR / TRF / SAY call of tools/settings/settings.cpp and lang::TX call of the asset makers and shared/data_mods.h (the words the
code writes: status lines, messages). Each other file is kept in en.txt's order; beside it, lang/sync/<code>.txt remembers the English
each text was translated from (a short hash a key), so a text whose English changed since is known as stale.

  python tools/lang_sync.py en --exe <D2R_VR_Settings.exe>     lang/en.txt made again (and each language file laid out again)
  python tools/lang_sync.py check [--exe <exe>] [--strict]     per language: keys missing, stale, {placeholder} mismatches;
                                                               with --exe also whether en.txt is the program's. A warning only
                                                               (exit 0) unless --strict (the installer's build prints it)
  python tools/lang_sync.py batch <code> <out.txt>              the keys of <code> missing or stale, "key = English" a line, to translate
  python tools/lang_sync.py merge <code> <translated.txt>       a translated batch back into lang/<code>.txt (its hashes noted)
  python tools/lang_sync.py mt <code> --cmd "<command>"         batch, run a translator command ({in} {out} {lang} in it), merge
  python tools/lang_sync.py layout [<code>...]                   the files laid out again in en.txt's order (nothing translated)
  python tools/lang_sync.py accept <key>... [--lang <code>]      after an English fix that changes no meaning (a typo): those
                                                               translations are kept as made from the English now (not stale)

A language file: a header of comments (kept), then en.txt's keys in its order; a key without a translation yet is written as a
comment with the English ("# key = English"), a stale one keeps its translation under a "# English changed: ..." note until it is
merged again. language.name is the file's own (its name in the list) and never stale."""
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANG = ROOT / "lang"
SYNC = LANG / "sync"
SOURCES = [ROOT / "tools" / "settings" / f for f in ("settings.cpp", "arcane_assets.cpp", "props_assets.cpp", "boat_assets.cpp", "dome_assets.cpp")] + [ROOT / "shared" / "data_mods.h"]
OWN_KEYS = {"language.name"}   # the file's own: never batched, never stale
CALL = re.compile(r'\b(TR|TRF|SAY|TX)\(\s*L"')   # (TX: lang::TX in the asset makers and data_mods.h)

EN_HEADER = """\
# D2R VR Authentic Settings - English, the text built into the program (made by tools/lang_sync.py - do not edit by hand).
#
# TRANSLATING: copy this file to <code>.txt in this folder (de.txt, pl.txt, pt-BR.txt...: your language's code), change
# language.name below to your language's own name, and translate the text after each "=" - by hand, or give the file to an AI or a
# translator and ask it to keep every line's key. Keep as they are: the keys before the "=", every {name} in braces (the program puts
# a number or a name there; it may move anywhere in the sentence), ini names like [hud] bar_place or d2r_vr.ini, file names, units
# (cm, m/s, Hz, %), and \\n (a new line). A line you leave out, or leave empty, stays English. Lines starting with # are comments.
# Save as UTF-8 (Notepad's default). Your language appears in Settings > Home > Language; the program opens again in it.
# The same English under several keys is listed once: its translation is used for all of them.
"""


# ---- C++ wide string literals --------------------------------------------------------------------------------------------------------

def c_unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c != '\\':
            out.append(c); i += 1; continue
        n = s[i + 1]
        if n == 'u':
            out.append(chr(int(s[i + 2:i + 6], 16))); i += 6
        elif n == 'U':
            out.append(chr(int(s[i + 2:i + 10], 16))); i += 10
        elif n == 'x':
            j = i + 2
            while j < len(s) and s[j] in '0123456789abcdefABCDEF':
                j += 1
            out.append(chr(int(s[i + 2:j], 16))); i = j
        else:
            out.append({'n': '\n', 't': '\t', 'r': '\r', '0': '\0', '\\': '\\', '"': '"', "'": "'"}.get(n, n)); i += 2
    return ''.join(out)


def read_literals(text, at):
    """Wide literals from `at` joined (L"a" L"b", comments and new lines between): (value, end) or (None, at) when none starts there."""
    parts, i, n = [], at, len(text)
    while True:
        while i < n and text[i] in ' \t\r\n':
            i += 1
        if text.startswith('//', i):
            i = text.find('\n', i)
            continue
        if not text.startswith('L"', i):
            break
        j = i + 2
        while text[j] != '"':
            j += 2 if text[j] == '\\' else 1
        parts.append(c_unescape(text[i + 2:j]))
        i = j + 1
    return (''.join(parts), i) if parts else (None, at)


def code_texts():
    """(key, English, where) of each TR / TRF / SAY call whose key and English are literals; a call whose English is not (the shared
    headers' words) is listed by the program (--lang-template) and skipped here."""
    found = []
    for src in SOURCES:
        text = src.read_text(encoding="utf-8")
        for m in CALL.finditer(text):
            key, i = read_literals(text, m.end() - 2)
            if key is None:
                continue
            while text[i] in ' \t\r\n':
                i += 1
            if text[i] != ',':
                continue   # a key made at run time (L"status.bw_path." + ...): the program lists those
            en, j = read_literals(text, i + 1)
            if en is None:
                continue
            found.append((key, en, f"{src.name}:{text.count(chr(10), 0, m.start()) + 1}"))
    return found


# ---- the language files ---------------------------------------------------------------------------------------------------------------

def escape(s):
    out = []
    for i, c in enumerate(s):
        if c == '\n':
            out.append('\\n')
        elif c == '\r':
            continue
        elif c == '\\' and i + 1 < len(s) and s[i + 1] in 'n\\':
            out.append('\\\\')
        else:
            out.append(c)
    return ''.join(out)


def unescape(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s) and s[i + 1] in 'n\\':
            out.append('\n' if s[i + 1] == 'n' else '\\'); i += 2
        else:
            out.append(s[i]); i += 1
    return ''.join(out)


def read_text(path):
    raw = path.read_bytes()
    if raw[:2] == b'\xff\xfe':
        return raw[2:].decode('utf-16-le')
    if raw[:2] == b'\xfe\xff':
        return raw[2:].decode('utf-16-be')
    return raw.decode('utf-8-sig', errors='replace')


def parse(path):
    """(header comment lines, {key: text}) as the program reads the file (lang.h Parse): the later line of a key wins."""
    header, table, body = [], {}, False
    for line in read_text(path).split('\n'):
        s = line.strip()
        if not s or s[0] in '#;':
            if not body and s:
                header.append(line.rstrip('\r'))
            continue
        if '=' not in s:
            continue
        k, v = s.split('=', 1)
        k = k.strip()
        if k:
            table[k] = unescape(v.strip())
            body = True
    return header, table


def en_entries():
    """lang/en.txt as (kind, key or comment, English) in its order: ('key', k, en) and ('note', comment line, None)."""
    out = []
    for line in read_text(LANG / "en.txt").split('\n'):
        s = line.rstrip('\r')
        if s.startswith('# =====') or s.startswith('# ---'):
            out.append(('note', s, None))
        elif s.strip() and not s.lstrip().startswith(('#', ';')) and '=' in s:
            k, v = s.split('=', 1)
            out.append(('key', k.strip(), unescape(v.strip())))
    return out


def short_hash(text):
    return hashlib.sha1(text.encode('utf-8')).hexdigest()[:10]


def read_hashes(code):
    p = SYNC / f"{code}.txt"
    if not p.exists():
        return {}
    out = {}
    for line in p.read_text(encoding='utf-8').split('\n'):
        if '=' in line and not line.startswith('#'):
            k, v = line.split('=', 1)
            out[k.strip()] = v.strip()
    return out


def write_hashes(code, hashes):
    SYNC.mkdir(parents=True, exist_ok=True)
    lines = ["# lang_sync.py: the English each text of %s.txt was translated from (a short hash a key). Not read by the program." % code]
    lines += [f"{k} = {hashes[k]}" for k in sorted(hashes)]
    (SYNC / f"{code}.txt").write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


def placeholders(text):
    return sorted(re.findall(r'\{([A-Za-z0-9_]+)\}', text))


def languages():
    """The language files: every <code>.txt with a language.name but en.txt (README.txt is not one)."""
    return sorted(p.stem for p in LANG.glob('*.txt') if p.stem.lower() != 'en' and parse(p)[1].get('language.name'))


def status(code, en):
    """{key: 'ok' | 'missing' | 'stale'} for each key of en.txt, and the placeholder mismatches [(key, want, have)]."""
    _, table = parse(LANG / f"{code}.txt")
    hashes = read_hashes(code)
    state, bad = {}, []
    for kind, key, english in en:
        if kind != 'key' or key in OWN_KEYS:
            continue
        text = table.get(key, '')
        if not text:
            state[key] = 'missing'
            continue
        state[key] = 'stale' if key in hashes and hashes[key] != short_hash(english) else 'ok'
        if placeholders(text) != placeholders(english):
            bad.append((key, placeholders(english), placeholders(text)))
    return state, bad


def layout(code, en):
    """lang/<code>.txt written again in en.txt's order: its header, its translations, the rest as commented English."""
    path = LANG / f"{code}.txt"
    header, table = parse(path)
    hashes = read_hashes(code)
    lines = header[:] if header else [f"# D2R VR Authentic Settings - {code}"]
    lines.append('')
    lines.append(f"language.name = {escape(table.get('language.name', code))}")
    for kind, key, english in en:
        if kind == 'note':
            lines += ['', key]
            continue
        if key in OWN_KEYS:
            continue
        text = table.get(key, '')
        if not text:
            lines.append(f"# {key} = {escape(english)}")
            continue
        if key in hashes and hashes[key] != short_hash(english):
            lines.append(f"# English changed: {escape(english)}")
        lines.append(f"{key} = {escape(text)}")
    # keys the file has that en.txt does not list - one whose English is the same as another's (en.txt lists that English once) given
    # its own wording, or one the program has dropped: kept as they are (the program uses the first, ignores the second)
    listed = {k for kind, k, _ in en if kind == 'key'}
    own = [k for k in table if k not in listed]
    if own:
        lines += ['', "# ===== Keys of this file that en.txt does not list (an English used under several keys, worded here for one of them) ====="]
        lines += [f"{k} = {escape(table[k])}" for k in own]
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


# ---- en.txt from the program ----------------------------------------------------------------------------------------------------------

def template_from_exe(exe):
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "items.txt"
        r = subprocess.run([str(exe), "--lang-template", str(out)])
        if r.returncode != 0 or not out.exists():
            sys.exit(f"{exe} --lang-template failed ({r.returncode})")
        return out.read_text(encoding='utf-8')


def build_en(exe):
    """en.txt's text: the header, the program's item texts, then the code's words (a key the item part has with the same English is not
    listed twice; with other English it is an error, as is one key with two Englishes in the code)."""
    items = template_from_exe(exe)
    have = {}
    for line in items.split('\n'):
        if line.strip() and not line.startswith('#') and ' = ' in line:
            k, v = line.split(' = ', 1)
            have[k] = unescape(v)
    errors, code_lines, code_keys = [], [], {}
    for key, en, where in code_texts():
        if key in code_keys:
            if code_keys[key][0] != en:
                errors.append(f"{where}: {key} has other English at {code_keys[key][1]}")
            continue
        code_keys[key] = (en, where)
        if key in have:
            if have[key] != en:
                errors.append(f"{where}: {key} is an item text with other English")
            continue
        code_lines.append(f"{key} = {escape(en)}")
    if errors:
        sys.exit("lang_sync: " + "\n".join(errors))
    text = EN_HEADER + "\nlanguage.name = English\n" + items.rstrip('\n') + "\n\n# ===== The words the program writes (status lines, messages, buttons) =====\n"
    return text + '\n'.join(code_lines) + '\n'


# ---- commands --------------------------------------------------------------------------------------------------------------------------

def cmd_en(args):
    exe = args[args.index('--exe') + 1]
    (LANG / "en.txt").write_text(build_en(exe), encoding='utf-8', newline='\n')
    en = en_entries()
    for code in languages():
        layout(code, en)
    print(f"lang/en.txt: {sum(1 for e in en if e[0] == 'key')} keys; {len(languages())} language file(s) laid out again")


def cmd_check(args):
    strict = '--strict' in args
    problems = 0
    if '--exe' in args:
        made = build_en(args[args.index('--exe') + 1])
        if made != read_text(LANG / "en.txt").replace('\r\n', '\n'):
            print("WARNING lang/en.txt is not the program's English: python tools/lang_sync.py en --exe <D2R_VR_Settings.exe>")
            problems += 1
    en = en_entries()
    total = sum(1 for e in en if e[0] == 'key' and e[1] not in OWN_KEYS)
    for code in languages():
        state, bad = status(code, en)
        missing = sum(1 for s in state.values() if s == 'missing')
        stale = sum(1 for s in state.values() if s == 'stale')
        line = f"lang/{code}.txt: {total - missing - stale} of {total} translated, {missing} missing, {stale} stale, {len(bad)} placeholder mismatch(es)"
        print(("WARNING " if missing or stale or bad else "") + line)
        for key, want, have in bad[:10]:
            print(f"    {key}: wants {{{'}, {'.join(want)}}} has {{{'}, {'.join(have)}}}" if want or have else f"    {key}")
        problems += bool(missing or stale or bad)
    return 1 if strict and problems else 0


def cmd_batch(args):
    code, out = args[0], Path(args[1])
    en = en_entries()
    state, _ = status(code, en)
    lines = [f"# lang_sync batch for {code}: translate the text after each \"=\", keep the keys, {{placeholders}}, ini names, units and \\n"]
    n = 0
    for kind, key, english in en:
        if kind == 'key' and state.get(key) in ('missing', 'stale'):
            lines.append(f"{key} = {escape(english)}")
            n += 1
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print(f"{out}: {n} key(s) of {code} to translate")


def cmd_merge(args):
    code, done = args[0], Path(args[1])
    path = LANG / f"{code}.txt"
    if not path.exists():
        path.write_text(f"# D2R VR Authentic Settings - {code}\n", encoding='utf-8')
    en = {k: e for kind, k, e in en_entries() if kind == 'key'}
    _, got = parse(done)
    header, table = parse(path)
    hashes = read_hashes(code)
    taken, refused = 0, []
    for key, text in got.items():
        if key not in en or not text:
            continue
        if key not in OWN_KEYS and placeholders(text) != placeholders(en[key]):
            refused.append(key)
            continue
        table[key] = text
        if key not in OWN_KEYS:
            hashes[key] = short_hash(en[key])
        taken += 1
    # written through layout (en.txt's order), from this table
    body = [f"{k} = {escape(v)}" for k, v in table.items()]
    path.write_text('\n'.join(header + [''] + body) + '\n', encoding='utf-8', newline='\n')
    write_hashes(code, hashes)
    layout(code, en_entries())
    print(f"lang/{code}.txt: {taken} merged" + (f", {len(refused)} refused for their {{placeholders}}: {', '.join(refused[:8])}" if refused else ""))


def cmd_mt(args):
    code, command = args[0], args[args.index('--cmd') + 1]
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = Path(tmp) / f"{code}.todo.txt", Path(tmp) / f"{code}.done.txt"
        cmd_batch([code, str(src)])
        r = subprocess.run(command.format(**{'in': str(src), 'out': str(dst), 'lang': code}), shell=True)
        if r.returncode != 0 or not dst.exists():
            sys.exit(f"the translator command failed ({r.returncode})")
        cmd_merge([code, str(dst)])


def cmd_accept(args):
    """accept <key>... [--lang <code>]: the translations of these keys stand for the English as it is now (an English fix - a typo, a
    space - that changes nothing a translation says): their hashes noted, so they are not stale."""
    keys = [a for a in args if not a.startswith('--')]
    codes = [args[args.index('--lang') + 1]] if '--lang' in args else languages()
    keys = [k for k in keys if k != (codes[0] if '--lang' in args else None)]
    en = {k: e for kind, k, e in en_entries() if kind == 'key'}
    for code in codes:
        _, table = parse(LANG / f"{code}.txt")
        hashes = read_hashes(code)
        n = 0
        for k in keys:
            if k in en and table.get(k):
                hashes[k] = short_hash(en[k])
                n += 1
        write_hashes(code, hashes)
        layout(code, en_entries())
        print(f"lang/{code}.txt: {n} key(s) accepted for their English now")


def cmd_layout(args):
    en = en_entries()
    for code in args or languages():
        layout(code, en)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, args = sys.argv[1], sys.argv[2:]
    r = {'en': cmd_en, 'check': cmd_check, 'batch': cmd_batch, 'merge': cmd_merge, 'mt': cmd_mt, 'layout': cmd_layout, 'accept': cmd_accept}.get(cmd)
    if not r:
        sys.exit(__doc__)
    sys.exit(r(args) or 0)


if __name__ == '__main__':
    main()
