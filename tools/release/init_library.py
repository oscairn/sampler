#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build the Sampler's init library: an Oscairn library archive holding one sound, "Init".

    init_library.py <built.clap> <samplepack> <out.olib>

The sound is one element on one waveform over one generated, looped sample: a 441 Hz tone
(exactly 100 frames a cycle, so the whole second loops without a click), harmonics 1-5 at
1/n^1.5, mono 16-bit 44.1 kHz, peak -12 dBFS. The host refuses a sound whose saved plugin
version or audio outputs differ from the build's, so both are read from the built .clap,
loaded here with ctypes: it must be a build this machine can run (the release workflow runs
this on its native linux-x86_64 job). The archive is deterministic: the same build and
samplepack tool give the same bytes. Only the standard library, release.conf and the
samplepack tool are used.
"""
import base64
import ctypes
import hashlib
import io
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
import zipfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from release_conf import REPO, conf

RATE, FRAMES, F0 = 44100, 44100, 441
ROOT_KEY = 69  # A4, 440 Hz
SOUND_NAME = 'Init'
# The sound's id (INIT_SOUND in release.conf) is the first 16 hex digits of the SHA-256 of
# "sound", "sampler-init" and "Init" joined by NUL bytes; it is kept as a value so that a change
# here never moves it.
ELEMENT = {'ampeg_attack': 5, 'ampeg_release': 55, 'waveform': 1}  # element ampeg_attack=5 ampeg_release=55 waveform=1
EPOCH = (1980, 1, 1, 0, 0, 0)
CONTEXTS = 8  # the plugin's sound contexts: its snapshot has a line for each
POLYPHONY = 64
# The host's types for samples and waveforms, which an archive holding either carries.
TYPES = {f'Types/org.oscairn.sample-types/{kind}.json': (
    f'{{"format": "oscairn.type", "version": 1, "id": "org.oscairn.sample-types.{kind}", "name": "{kind.title()}", "plural": "{folder}",\n'
    f' "recognize": {{"extensions": [".{ext}"]}}, "storage": "content", "folder": "{folder}", "identity": "content", "handler": "{kind}", "fields": []}}\n').encode()
    for kind, folder, ext in (('sample', 'Samples', 'osp'), ('waveform', 'Waveforms', 'owf'))}


def compact(doc):
    return json.dumps(doc, separators=(',', ':'), ensure_ascii=False, allow_nan=False)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


# What the build says: the descriptor's version and the audio outputs, through the CLAP ABI.

class _Version(ctypes.Structure):
    _fields_ = [('major', ctypes.c_uint32), ('minor', ctypes.c_uint32), ('revision', ctypes.c_uint32)]


class _Entry(ctypes.Structure):
    _fields_ = [('version', _Version), ('init', ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_char_p)),
                ('deinit', ctypes.CFUNCTYPE(None)), ('get_factory', ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_char_p))]


class _Descriptor(ctypes.Structure):
    _fields_ = [('clap_version', _Version)] + [(k, ctypes.c_char_p) for k in (
        'id', 'name', 'vendor', 'url', 'manual_url', 'support_url', 'version', 'description')]


class _PortInfo(ctypes.Structure):  # clap_audio_port_info_t
    _fields_ = [('id', ctypes.c_uint32), ('name', ctypes.c_char * 256), ('flags', ctypes.c_uint32),
                ('channel_count', ctypes.c_uint32), ('port_type', ctypes.c_char_p), ('in_place_pair', ctypes.c_uint32)]


_HOST_EXT = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p)
_HOST_CALL = ctypes.CFUNCTYPE(None, ctypes.c_void_p)


class _Host(ctypes.Structure):  # clap_host_t: offers no extensions, ignores requests
    _fields_ = [('version', _Version), ('host_data', ctypes.c_void_p)] + [(k, ctypes.c_char_p) for k in (
        'name', 'vendor', 'url', 'version_')] + [('get_extension', _HOST_EXT)] + [(k, _HOST_CALL) for k in (
        'request_restart', 'request_process', 'request_callback')]


def read_build(clap, plugin_id):
    """(version, [{"id", "channels", "flags"}] for each audio output) of the plugin `plugin_id` in the build."""
    lib = ctypes.CDLL(os.path.abspath(clap), mode=ctypes.RTLD_LOCAL)
    entry = _Entry.in_dll(lib, 'clap_entry')
    if not entry.init(os.path.abspath(clap).encode()):
        sys.exit(f'init_library.py: {clap}: clap_entry.init failed')
    host_ext, host_call = _HOST_EXT(lambda h, i: None), _HOST_CALL(lambda h: None)
    host = _Host(_Version(1, 2, 0), None, b'init-library', b'', b'', b'0', host_ext, host_call, host_call, host_call)
    try:
        factory = entry.get_factory(b'clap.plugin-factory')
        # clap_plugin_factory_t: get_plugin_count, get_plugin_descriptor, create_plugin
        create = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p)(
            ctypes.cast(factory, ctypes.POINTER(ctypes.c_void_p))[2])
        plugin = create(factory, ctypes.addressof(host), plugin_id.encode())
        if not plugin:
            sys.exit(f'init_library.py: {clap} has no plugin {plugin_id}')
        # clap_plugin_t: desc, plugin_data, init, destroy, ..., get_extension (10)
        pf = ctypes.cast(plugin, ctypes.POINTER(ctypes.c_void_p))
        try:
            if not ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_void_p)(pf[2])(plugin):
                sys.exit(f'init_library.py: {plugin_id}: init failed')
            version = ctypes.cast(pf[0], ctypes.POINTER(_Descriptor)).contents.version.decode()
            ext = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p)(pf[10])(plugin, b'clap.audio-ports')
            if not ext:
                sys.exit(f'init_library.py: {plugin_id} has no audio ports')
            # clap_plugin_audio_ports_t: count, get
            fn = ctypes.cast(ext, ctypes.POINTER(ctypes.c_void_p))
            count = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p, ctypes.c_bool)(fn[0])(plugin, False)
            get = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_bool, ctypes.POINTER(_PortInfo))(fn[1])
            outputs = []
            for i in range(count):
                info = _PortInfo()
                if not get(plugin, i, False, ctypes.byref(info)):
                    sys.exit(f'init_library.py: audio output {i} cannot be read')
                outputs.append({'id': info.id, 'channels': info.channel_count, 'flags': info.flags})
            return version, outputs
        finally:
            _HOST_CALL(pf[3])(plugin)
    finally:
        entry.deinit()


# The sound's binary form: the ids, their order and their defaults are the plugin's own tables.

def table(name):
    """[(id, default)] of the X(enum, "id", low, high, default) rows of core/<name>, in order."""
    rows = []
    with open(os.path.join(REPO, 'core', name), encoding='utf-8') as f:
        for line in f:
            if line.startswith('X('):
                _, pid, _, _, default = line.split('//')[0].rstrip().rstrip(')').split(', ', 4)
                rows.append((pid.strip('"'), int(default)))
    return rows


def crc32c(data):
    c = 0xFFFFFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0x82F63B78 if c & 1 else c >> 1
    return c ^ 0xFFFFFFFF


def snapshot(waveform_id):
    """The plugin's sound-contexts snapshot as it saves one: the sound in slot 0, the other slots free."""
    part, elements = table('partparams.inc'), table('params.inc')
    index = {pid: i for i, (pid, _) in enumerate(elements)}
    values = sorted((index[pid], x) for pid, x in ELEMENT.items() if x != elements[index[pid]][1])
    body = struct.pack('<4H', 0, 1, 0, len(part)) + struct.pack(f'<{len(part)}h', *[d for _, d in part])
    body += struct.pack('<H', len(values)) + b''.join(struct.pack('<Hh', i, x) for i, x in values)
    body += struct.pack('<H', 1) + bytes.fromhex(waveform_id)
    blob = b'OSMP' + struct.pack('<HHII', 1, 0, 16 + len(body), zlib.crc32(body)) + body
    # the shell's values: level, pan, polyphony, the 8 knobs at the sound's value, ribbon, 2 switches, 11 destinations, output bus
    shell = (1, 0, POLYPHONY, *[dict(part)['knob1'] / 1023] * 8, 0, 0, 0, *[0] * 11, 0)
    text = ' '.join(f"{struct.unpack('f', struct.pack('f', x))[0]:.17g}" for x in shell)
    return (f'oscairn-sampler-contexts/3\n1 "{SOUND_NAME}" {text} "" "" {base64.b64encode(blob).decode()}\n'
            + '0\n' * (CONTEXTS - 1) + '0 ' * 16 + '\n').encode()


def tone():
    raw = [sum(math.sin(2 * math.pi * h * F0 * i / RATE) / h ** 1.5 for h in range(1, 6)) for i in range(FRAMES)]
    scale = 10 ** (-12 / 20) * 32767 / max(abs(x) for x in raw)
    return [round(x * scale) for x in raw]


def build(clap, samplepack, c):
    """{path: bytes} of the archive's files in archive order, library.json first."""
    version, outputs = read_build(clap, c['PACKAGE_ID'])
    item_id = c['INIT_SOUND']
    with tempfile.TemporaryDirectory() as tmp:
        wav, osp, owf = (os.path.join(tmp, 'tone' + ext) for ext in ('.wav', '.osp', '.owf'))
        with wave.open(wav, 'wb') as w:
            w.setnchannels(1), w.setsampwidth(2), w.setframerate(RATE)
            w.writeframes(struct.pack(f'<{FRAMES}h', *tone()))
        sample_id = subprocess.run([samplepack, 'pack', '-o', osp, wav], capture_output=True, text=True,
                                   check=True).stdout.split()[0]
        with open(osp, 'rb') as f:
            sample = f.read()
        # One key bank over every key and velocity, root A4, tuned by the tone's offset from it (-1200 log2(f/440)
        # cents, in hundredths), looped over the whole file: lo, hi, vel lo, vel hi, root, transpose, loop, sample
        # index, pad, tune, level, pan, pad, start, end, loop start, loop end.
        tune = round(-1200 * math.log2(F0 / 440) * 100)
        body = struct.pack('<HH', 1, 1) + bytes.fromhex(sample_id) + \
            struct.pack('<5BbBBHHiihHIIII', 0, 127, 1, 127, ROOT_KEY, 0, 1, 0, 0, 0, tune, 0, 0, 0, 0, 0, 0, FRAMES)
        waveform = b'OPWF' + struct.pack('<HHII', 1, 0, 16 + len(body), crc32c(body)) + body
        waveform_id = sha256(b'oscairn-waveform-1\0' + body)
        with open(owf, 'wb') as f:
            f.write(waveform)
        ok = subprocess.run([samplepack, 'waveform', 'check', owf], capture_output=True, text=True)
        show = subprocess.run([samplepack, 'waveform', 'show', owf], capture_output=True, text=True)
        if ok.returncode or f'id {waveform_id}\n' not in show.stdout:
            sys.exit(f'init_library.py: samplepack does not take the waveform {waveform_id}: {ok.stderr.strip()}')

    node_files = {  # the node's saved state, under nodes/node-1/
        'snapshot': b'state 1\n',
        'state/plugin.id': c['PACKAGE_ID'].encode(),
        'state/state.clap': snapshot(waveform_id),
    }
    node = {'key': 'node-1', 'name': SOUND_NAME,
            'plugin': {'format': 'clap', 'id': c['PACKAGE_ID'], 'version': version, 'pathHint': c['NAME'] + '.clap'},
            'engine': 'node-1', 'inputs': [], 'outputs': outputs, 'noteInputs': 1, 'noteOutputs': 0, 'assets': [],
            'enabled': True, 'sound': None}
    doc = {'format': 'oscairn.sound', 'version': 8,
           'info': {'id': item_id, 'name': SOUND_NAME, 'description': 'A generated tone on one waveform.',
                    'tags': [c['PACKAGE_ID']], 'color': '', 'revision': sha256(f'{item_id}\0revision'.encode())[:16],
                    'parent': '', 'hash': '', 'origin': None},
           'libraries': [], 'nodes': [node], 'wires': [], 'part': {'chain': ['node-1'], 'splits': []},
           'mappings': [], 'controls': [], 'assignments': [], 'lock': {'plugins': []}}
    # info.hash: SHA-256 over the document without its info, a blank line, then "<path> <sha256>" of each node file.
    summary = compact({**doc, 'info': None}).replace(',"info":null', ',"info":', 1) + '\n\n'
    summary += ''.join(f'{p} {sha256(d)}\n' for p, d in sorted((f'node-1/{p}', d) for p, d in node_files.items()))
    doc['info']['hash'] = sha256(summary.encode())

    folder = f'Sounds/{SOUND_NAME} {item_id}.osound'
    files = {f'Samples/{sample_id}.osp': sample, f'Waveforms/{waveform_id}.owf': waveform}
    files.update({f'{folder}/nodes/node-1/{p}': d for p, d in sorted(node_files.items())})
    files[f'{folder}/sound.json'] = (compact(doc) + '\n').encode()
    files.update(TYPES)
    manifest = {'format': 'oscairn.library', 'version': 3, 'id': c['INIT_LIBRARY_ID'], 'name': c['INIT_LIBRARY_NAME'],
                'kind': 'library', 'items': [{'kind': 'sound', 'id': item_id, 'name': SOUND_NAME, 'path': folder}],
                'plugins': [], 'files': {p: sha256(d) for p, d in files.items()}}
    return {'library.json': (compact(manifest) + '\n').encode(), **files}


def archive(files):
    """The zip as bytes: the given order, fixed dates, modes and compression, so equal content is equal bytes."""
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            entry = zipfile.ZipInfo(name, EPOCH)
            entry.create_system = 3
            entry.external_attr = (0o100644 if name.startswith(('Samples/', 'Waveforms/', 'Sounds/')) else 0o600) << 16
            z.writestr(entry, data, zipfile.ZIP_DEFLATED)
    return out.getvalue()


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: init_library.py <built.clap> <samplepack> <out.olib>')
    clap, samplepack, out = sys.argv[1:]
    c = conf()
    data = archive(build(clap, samplepack, c))
    assert data == archive(build(clap, samplepack, c)), 'two builds of the archive differ'
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        manifest = json.loads(z.read('library.json'))
        assert manifest['id'] == c['INIT_LIBRARY_ID']
        assert set(manifest['files']) == set(z.namelist()) - {'library.json'}
        for name, digest in manifest['files'].items():
            assert sha256(z.read(name)) == digest, f'{name}: hash differs from the manifest'
    with open(out, 'wb') as f:
        f.write(data)
    print(f'init_library.py: wrote {out} ({len(data)} bytes, sha256 {sha256(data)})')


if __name__ == '__main__':
    main()
