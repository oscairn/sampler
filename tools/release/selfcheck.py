#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-check of oscairn-release.json against plugin-release.schema.json's rules.

    selfcheck.py <distdir> [manifest.json]    manifest: <distdir>/oscairn-release.json

The checks packages-side import-release.py runs that need no third-party library:
required fields, no unknown fields, the constants, enums, patterns and array
bounds (retranscribed from oscairn/packages schemas/plugin-release.schema.json at
PR #18 head, and for dependencies.packages and init at PR #33 head), the schema's rule that a macOS build is a Mach-O file and not a zip,
and that every builds/licenseFiles sha256 and size match the actual files in <distdir>;
for an init library, that its file matches and its archive's id is in dependencies.packages.

Known deviation: the schema requires source.tag == "v" + package.version, but every
plugin repo tags oscairn-vX.Y.Z (upstream v* tags exist in forks), so TAG_PATTERN
below keeps the real tag name. Change it once the schema adopts oscairn-vX.Y.Z.
"""
import json
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from release_conf import conf, sha256

ID = r'^[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)+$'
SEMVER = r'^[0-9]+\.[0-9]+\.[0-9]+$'
TAG_PATTERN = r'^' + re.escape(conf()['TAG_PREFIX']) + r'v[0-9]+\.[0-9]+\.[0-9]+$'

# name -> (required, {property: (pattern | None, maxlen | None)})
OBJECTS = {
    '': (['format', 'version', 'package', 'plugins', 'builds', 'source', 'license',
          'licenseFiles', 'clap', 'oscairnClap'],
         {'format': ('^oscairn\\.plugin-release$', None), 'version': (None, None),
          'package': (None, None), 'plugins': (None, None), 'builds': (None, None),
          'source': (None, None), 'dependencies': (None, None), 'init': (None, None),
          'license': (r'^[A-Za-z0-9.+() -]{1,256}$', None),
          'licenseFiles': (None, None),
          'clap': (SEMVER, None), 'oscairnClap': (r'^[0-9a-f]{7,40}$', None)}),
    'package': (['id', 'version', 'name', 'summary'],
                {'id': (ID, 128), 'version': (SEMVER, 32),
                 'name': (r'^.', 128), 'summary': (r'^.', 500)}),
    'plugins': (['format', 'id', 'version', 'name', 'kind'],
                {'format': ('^clap$', None), 'id': (ID, 128),
                 'version': (r'^[0-9][A-Za-z0-9.+_-]{0,63}$', None),
                 'name': (r'^.', 128), 'kind': ('^synth$|^effect$|^note-effect$', None)}),
    'builds': (['platform', 'file', 'sha256', 'size'],
               {'platform': ('^linux-arm64$|^linux-x86_64$|^macos-arm64$|^macos-x86_64$|^windows-x86_64$', None),
                'file': (r'^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$', None),
                'sha256': (r'^[0-9a-f]{64}$', None), 'size': (None, None)}),
    'source': (['url', 'revision', 'tag'],
               {'url': (r'^https://github\.com/oscairn/[A-Za-z0-9._-]+$', None),
                'revision': (r'^[0-9a-f]{40}$', None), 'tag': (TAG_PATTERN, None)}),
    'licenseFiles': (['file', 'sha256'],
                     {'file': (r'^licenses/[A-Za-z0-9][A-Za-z0-9._-]{0,127}$', None),
                      'sha256': (r'^[0-9a-f]{64}$', None)}),
}
ARRAY_BOUNDS = {'plugins': (1, 64), 'builds': (1, 8), 'licenseFiles': (1, 64), 'assets': (0, 64), 'packages': (0, 64)}
# element schema name for array fields
ARRAY_ITEMS = {'plugins': 'plugins', 'builds': 'builds', 'licenseFiles': 'licenseFiles', 'assets': 'assets',
               'packages': 'packages'}
# object fields of the top object, and their schema names
NESTED = {'package': 'package', 'source': 'source', 'dependencies': 'dependencies', 'init': 'init', 'library': 'library'}
OBJECTS['dependencies'] = ([], {'assets': (None, None), 'packages': (None, None)})
OBJECTS['packages'] = (['id', 'version'], {'id': (ID, 128), 'version': (r'^[A-Za-z0-9][A-Za-z0-9.+_-]{0,63}$', None)})
OBJECTS['init'] = (['library', 'sound'], {'library': (None, None), 'sound': (r'^[A-Za-z0-9][A-Za-z0-9._:@+/-]{0,255}$', None)})
OBJECTS['library'] = (['file', 'sha256', 'size'], {'file': (OBJECTS['builds'][1]['file'][0], None),
                                                  'sha256': (r'^[0-9a-f]{64}$', None), 'size': (None, None),
                                                  'license': (None, 256)})
OBJECTS['assets'] = (['id', 'version'], {'id': (ID, 128), 'version': (r'^[0-9][A-Za-z0-9.+_-]{0,63}$', None)})
CONSTS = {'': {'format': 'oscairn.plugin-release', 'version': 1}}


def check_value(name, key, value, pattern, maxlen, path, bad):
    if key in CONSTS.get(name, {}):
        if value != CONSTS[name][key]:
            bad(f'{path}: {value!r} != {CONSTS[name][key]!r}')
        return
    if key == 'size':
        if not isinstance(value, int) or isinstance(value, bool) or not 1 <= value <= 1992294400:
            bad(f'{path}: {value!r} is not an integer in [1, 1992294400]')
        return
    if not isinstance(value, str):
        bad(f'{path}: {value!r} is not a string')
        return
    if maxlen is not None and len(value) > maxlen:
        bad(f'{path}: longer than {maxlen} characters')
    if pattern and not re.match(pattern, value):
        bad(f'{path}: {value!r} does not match {pattern}')


def check_object(name, value, path, bad):
    required, props = OBJECTS[name]
    if not isinstance(value, dict):
        return bad(f'{path}: {value!r} is not an object')
    for key in required:
        if key not in value:
            bad(f'{path}: missing required {key!r}')
    for key, v in value.items():
        if key not in props:
            bad(f'{path}: unknown field {key!r}')
            continue
        if key in ARRAY_ITEMS:
            lo, hi = ARRAY_BOUNDS[ARRAY_ITEMS[key]]
            if not isinstance(v, list):
                bad(f'{path}: {key} is not an array')
                continue
            if not lo <= len(v) <= hi:
                bad(f'{path}: {key} has {len(v)} items, outside [{lo}, {hi}]')
            for i, e in enumerate(v):
                check_object(ARRAY_ITEMS[key], e, f'{path}.{key}[{i}]', bad)
        elif key in NESTED:
            check_object(NESTED[key], v, f'{path}.{key}', bad)
        else:
            pattern, maxlen = props[key]
            check_value(name, key, v, pattern, maxlen, f'{path}.{key}', bad)


MACHO_MAGICS = (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',  # arm64 / x86_64 thin
                b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xce',  # 32-bit
                b'\xca\xfe\xba\xbe', b'\xca\xfe\xb1\xbf')  # fat


def is_macho(path):
    with open(path, 'rb') as f:
        return f.read(4) in MACHO_MAGICS


# ELF e_machine (little-endian) of a Linux build, by platform suffix.
ELF_MACHINES = {'linux-arm64': 183, 'linux-x86_64': 62}


def elf_machine(path):
    with open(path, 'rb') as f:
        head = f.read(20)
    if head[:4] != b'\x7fELF' or head[4] != 2 or head[5] != 1:  # 64-bit, little-endian
        return None
    return int.from_bytes(head[18:20], 'little')


def check_files(dist, manifest, bad):
    for row in manifest.get('builds', []):
        path = os.path.join(dist, row['file'])
        if not os.path.isfile(path):
            bad(f"builds: {row['file']} not in {dist}")
            continue
        if row['platform'].startswith('macos-') and not is_macho(path):
            bad(f"builds: {row['file']}: a macOS build must be a Mach-O file, not a zip or archive")
        if row['platform'] in ELF_MACHINES and elf_machine(path) != ELF_MACHINES[row['platform']]:
            bad(f"builds: {row['file']}: a {row['platform']} build must be a 64-bit little-endian ELF "
                f"of machine {ELF_MACHINES[row['platform']]}")
        if sha256(path) != row['sha256']:
            bad(f"builds: {row['file']}: sha256 mismatch")
        if os.path.getsize(path) != row['size']:
            bad(f"builds: {row['file']}: size mismatch")
    for row in manifest.get('licenseFiles', []):
        path = os.path.join(dist, row['file'])
        if not os.path.isfile(path):
            bad(f"licenseFiles: {row['file']} not in {dist}")
        elif sha256(path) != row['sha256']:
            bad(f"licenseFiles: {row['file']}: sha256 mismatch")
    check_init(dist, manifest, bad)


def check_init(dist, manifest, bad):
    """The init library is a file of the release (not another one), as the import checks: its sha256 and size,
    that it is a library archive whose id dependencies.packages names, and that it holds the sound."""
    init = manifest.get('init')
    if not init:
        return
    library = init.get('library', {})
    if library.get('file') in {os.path.basename(r['file']) for r in manifest.get('builds', []) + manifest.get('licenseFiles', [])}:
        bad(f"init: {library['file']} is also another file of the release")
    path = os.path.join(dist, str(library.get('file')))
    if not os.path.isfile(path):
        return bad(f"init: {library.get('file')} not in {dist}")
    if sha256(path) != library.get('sha256'):
        bad(f"init: {library['file']}: sha256 mismatch")
    if os.path.getsize(path) != library.get('size'):
        bad(f"init: {library['file']}: size mismatch")
    try:
        with zipfile.ZipFile(path) as z:
            held = json.loads(z.read('library.json'))
    except (zipfile.BadZipFile, KeyError, ValueError) as e:
        return bad(f"init: {library['file']} is not a library archive: {e}")
    if held.get('id') not in [n.get('id') for n in manifest.get('dependencies', {}).get('packages', [])]:
        bad(f"init: the library is {held.get('id')}, which dependencies.packages does not name")
    if held.get('license') != library.get('license', manifest.get('license')):
        bad(f"init: the library's license is {held.get('license')!r}, not the manifest's")
    source = manifest.get('source', {})
    if held.get('source') != {'url': source.get('url'), 'revision': source.get('revision')}:
        bad("init: the library's source is not the release's url and revision")
    if init.get('sound') not in [i.get('id') for i in held.get('items', [])]:
        bad(f"init: the library has no item {init.get('sound')}")


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit('usage: selfcheck.py <distdir> [manifest.json]')
    dist = sys.argv[1]
    path = sys.argv[2] if len(sys.argv) == 3 else os.path.join(dist, 'oscairn-release.json')
    with open(path, encoding='utf-8') as f:
        manifest = json.load(f)
    problems = []
    check_object('', manifest, 'manifest', problems.append)
    check_files(dist, manifest, problems.append)
    if problems:
        for p in problems:
            print(f'selfcheck: {p}', file=sys.stderr)
        sys.exit(1)
    print(f'selfcheck: {path} passes')


if __name__ == '__main__':
    main()
