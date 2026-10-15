"""Checks - and, with --fix, cleans - a BodyWalk profile (installer/bodywalk_profile.json) of device identifiers.

  python tools/profile_sanitize.py --check <file>      exit 1 when anything is found (the release scripts run this)
  python tools/profile_sanitize.py --fix <file>        blanks what --check finds, in place, keeping every key and the file's layout
  python tools/profile_sanitize.py --selftest          the rules on a made-up profile, nested levels included

Why: BodyWalk drops a profile's top-level *_serial / *_mac keys when it publishes or imports one, but a profile also
carries presets nested a few levels down (engine_presets[].tracker_serial ...), and those kept the author's tracker
serial in the shipped 0.165.0 profile. This walks the whole tree, at any depth.

What counts, wherever it stands:
  - a string value under a key that is, or ends with, "serial" or "mac" (tracker_serial, hmd_serial, squat_serial, device_mac ...),
    unless it is empty or the generic "HMD" / "None" the profile uses for "the headset" / "nobody";
  - a string shaped like a MAC address (aa:bb:cc:dd:ee:ff or aa-bb-cc-dd-ee-ff), under any key;
  - a string with a Windows user folder in it (C:\\Users\\<name>\\..., /Users/<name>/) or an e-mail address, under any key.
Only the key paths and counts are printed, never the values.
"""
import json
import re
import sys

BS = chr(92)
GENERIC = {'', 'HMD', 'None'}
_MAC = re.compile(r'(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}')
_USER_PATH = re.compile(r'[A-Za-z]:' + BS + BS + r'Users' + BS + BS + r'|/Users/|' + BS + BS + r'Users' + BS + BS)
_MAIL = re.compile(r'[\w.+-]+@[\w-]+\.[\w.]+')
_ID_KEY = re.compile(r'(?:^|_)(?:serial|mac)$', re.I)


def _generic_path(path):
    return re.sub(r'\[\d+\]', '[]', path)


def _bad_string(key, value):
    """The rule that flags this string, or None."""
    if key is not None and _ID_KEY.search(key) and value not in GENERIC:
        return 'serial/mac key'
    if _MAC.search(value):
        return 'MAC-shaped'
    if _USER_PATH.search(value):
        return 'user folder'
    if _MAIL.search(value):
        return 'e-mail'
    return None


def sanitize(node, path='', key=None, found=None):
    """Returns (clean copy, found) where found maps (generic path, rule) -> count. Keys, order and types stay."""
    if found is None:
        found = {}
    if isinstance(node, dict):
        out = {}
        for k, v in node.items():
            out[k], _ = sanitize(v, path + '/' + k, k, found)
        return out, found
    if isinstance(node, list):
        return [sanitize(v, '%s[%d]' % (path, i), key, found)[0] for i, v in enumerate(node)], found
    if isinstance(node, str):
        rule = _bad_string(key, node)
        if rule:
            found[(_generic_path(path), rule)] = found.get((_generic_path(path), rule), 0) + 1
            return '', found
        return node, found
    if key is not None and _ID_KEY.search(key) and node not in (None, 0, False):
        # a serial that is not a string (a number): not blanked here, but never left unnoticed
        found[(_generic_path(path), 'serial/mac key of a non-string value')] = found.get((_generic_path(path), 'serial/mac key of a non-string value'), 0) + 1
    return node, found


def dump(obj):
    return json.dumps(obj, indent=4, ensure_ascii=False) + '\n'


def report(found):
    for (path, rule), n in sorted(found.items()):
        print('  %-64s %-34s x%d' % (path or '/', rule, n))


def _read(path):
    raw = open(path, 'rb').read().decode('utf-8')
    return raw, raw.replace('\r\n', '\n')


def check(path):
    raw, text = _read(path)
    _, found = sanitize(json.loads(text))
    if found:
        print('%s: device identifiers found (%d places):' % (path, sum(found.values())))
        report(found)
        return 1
    print('%s: no serial / MAC / user-folder values at any depth' % path)
    return 0


def fix(path):
    raw, text = _read(path)
    obj = json.loads(text)
    if dump(obj) != text:
        print('%s: its layout is not what json.dumps(indent=4, ensure_ascii=False) makes; refusing to rewrite it' % path)
        return 2
    clean, found = sanitize(obj)
    if not found:
        print('%s: nothing to clean' % path)
        return 0
    out = dump(clean)
    open(path, 'wb').write((out.replace('\n', '\r\n') if '\r\n' in raw else out).encode('utf-8'))
    print('%s: cleaned (%d values blanked):' % (path, sum(found.values())))
    report(found)
    return 0


def selftest():
    probe = {
        'tracker_serial': 'ABC123XYZ789',                                      # top level
        'hmd_serial': 'HMD',                                                   # generic: stays
        'name': 'Tracker #1',                                                  # not an id key: stays
        'engine_presets': [
            {'tracker_serial': 'ZZZ99', 'hmd_serial': 'HMD', 'squat_serial': '', 'step': {'left_mac': '12:34:56:78:9A:BC'}},
            {'deep': [[{'phone_mac': 'aa-bb-cc-dd-ee-ff', 'turn_serial': 'S1', 'note': 'ok'}]]},
            {'tracker_serial': 'None', 'label': 'at C:' + BS + 'Users' + BS + 'someone' + BS + 'x'},
        ],
        'misc': {'a': 'contact me@example.org', 'b': '00:11:22:33:44:55 is the phone', 'serial': 'Q', 'mac': 'M', 'count': 3},
    }
    clean, found = sanitize(probe)
    total = sum(found.values())
    assert total == 10, (total, found)
    assert clean['tracker_serial'] == '' and clean['hmd_serial'] == 'HMD' and clean['name'] == 'Tracker #1'
    assert clean['engine_presets'][0]['tracker_serial'] == '' and clean['engine_presets'][0]['hmd_serial'] == 'HMD'
    assert clean['engine_presets'][0]['step']['left_mac'] == ''
    assert clean['engine_presets'][1]['deep'][0][0]['phone_mac'] == '' and clean['engine_presets'][1]['deep'][0][0]['note'] == 'ok'
    assert clean['engine_presets'][2]['tracker_serial'] == 'None' and clean['engine_presets'][2]['label'] == ''
    assert clean['misc']['a'] == '' and clean['misc']['b'] == '' and clean['misc']['serial'] == '' and clean['misc']['mac'] == ''
    assert clean['misc']['count'] == 3
    assert list(clean) == list(probe) and list(clean['misc']) == list(probe['misc'])       # keys and order kept
    again, found2 = sanitize(clean)
    assert not found2 and again == clean                                                    # idempotent
    assert ('/engine_presets[]/tracker_serial', 'serial/mac key') in found                 # paths are reported without the index
    print('profile_sanitize selftest: ok (%d places in the probe, found at depths 1 - 5)' % total)
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == '--selftest':
        return selftest()
    if len(argv) == 3 and argv[1] == '--check':
        return check(argv[2])
    if len(argv) == 3 and argv[1] == '--fix':
        return fix(argv[2])
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
