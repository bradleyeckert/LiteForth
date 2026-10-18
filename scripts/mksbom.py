#!/usr/bin/env python3
"""Writes LiteForth's Software Bill of Materials, sbom.spdx.json (SPDX 2.3).

    mksbom.py            write sbom.spdx.json in the repository root
    mksbom.py --check    exit 1 if sbom.spdx.json isn't up to date
    mksbom.py --scan     list files with a copyright notice that no
                         component below claims (third-party code that
                         slipped in unlisted)

The components are listed in COMPONENTS below, by hand: what each is,
where it came from, its license, and which files of this repository hold
it. The script adds each file's SHA-1 and SHA-256, so the SBOM records
exactly what's here; --check (run by `make test`, unit_tests/sbom) fails
when a listed file changes, so a change to third-party code gets noticed
and the list kept right. doc/sbom.md describes the components.

The document's creation time is the last commit's time, so the file only
changes when the components do; --check ignores it.
"""
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT = os.path.join(ROOT, 'sbom.spdx.json')
REPO = 'https://github.com/bradleyeckert/LiteForth'

WCH_NOTICE = ('Copyright (c) Nanjing Qinheng Microelectronics Co., Ltd. Attention: This '
              'software (modified or not) and binary are used for microcontroller '
              'manufactured by Nanjing Qinheng Microelectronics.')

CH32 = 'src/target/CH32H417/'

# The files with WCH's notice outside SRC/ (which is all WCH's), from its
# SimulateCDC and SDIO examples
WCH_FILES = [CH32 + 'SRC/**/*'] + [CH32 + f for f in (
    'Common/PRINTF.h', 'Common/UART.c', 'Common/UART.h', 'Common/ch32h417_usbfs_device.c',
    'Common/ch32h417_usbfs_device.h', 'Common/hardware.c', 'Common/hardware.h',
    'Common/usb_desc.c', 'Common/usb_desc.h',
    'V3F/User/ch32h417_conf.h', 'V3F/User/ch32h417_it.c', 'V3F/User/ch32h417_it.h',
    'V3F/User/main.c', 'V3F/User/system_ch32h417.c', 'V3F/User/system_ch32h417.h',
    'V5F/User/ch32h417_conf.h', 'V5F/User/ch32h417_it.c', 'V5F/User/ch32h417_it.h',
    'V5F/User/main.c', 'V5F/User/sdio.c', 'V5F/User/sdio.h',
    'V5F/User/system_ch32h417.c', 'V5F/User/system_ch32h417.h')]

# Files changed from what the supplier published (checked against the
# originals when they were listed); the SBOM says so for each
MODIFIED = {
    'src/monocypher.c': 'One cast in Argon2 for MSVC C4244, marked "LiteForth:"; '
                        'Monocypher master makes the same change.',
}
for f in ('Common/UART.c', 'Common/ch32h417_usbfs_device.c', 'Common/hardware.c',
          'Common/hardware.h', 'Common/UART.h', 'Common/usb_desc.c', 'Common/usb_desc.h',
          'SRC/Debug/debug.c', 'SRC/Debug/debug.h', 'SRC/Ld/V3F/Link_v3f.ld',
          'SRC/Ld/V5F/Link_v5f.ld', 'V3F/User/ch32h417_it.c', 'V3F/User/main.c',
          'V3F/User/system_ch32h417.c', 'V5F/User/ch32h417_it.c', 'V5F/User/main.c',
          'V5F/User/system_ch32h417.c', 'V5F/User/system_ch32h417.h'):
    MODIFIED[CH32 + f] = 'Changed for LiteForth (USB CDC bridge, dual-core start-up, ' \
                         'memory map, debug output).'

# kind: 'code' (built into the firmware or lf), 'test' (unit tests only),
# 'data' (in a tracked block image), or 'tool' (needed by a script, not in
# this repository)
COMPONENTS = [
    {
        'id': 'Monocypher', 'name': 'Monocypher', 'version': '4.0.2', 'kind': 'code',
        'supplier': 'Person: Loup Vaillant',
        'download': 'https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.2.tar.gz',
        'purl': 'pkg:github/LoupVaillant/Monocypher@4.0.2',
        'license': 'CC0-1.0 OR BSD-2-Clause',
        'copyright': 'Copyright (c) 2017-2023 Loup Vaillant, Michael Savage, Fabio Scotoni',
        'files': ['src/monocypher.c', 'src/monocypher.h', 'src/monocypher-ed25519.c',
                  'src/monocypher-ed25519.h', 'src/monocypher-LICENCE.md'],
        'summary': 'X25519 and Ed25519 (with SHA-512) for sealed block files',
    },
    {
        'id': 'blake2s', 'name': 'blake2s', 'version': 'd887870', 'kind': 'code',
        'supplier': 'Person: Bradley N Eckert',
        'download': 'git+https://github.com/bradleyeckert/blake2s@d887870',
        'purl': 'pkg:github/bradleyeckert/blake2s@d887870',
        'license': 'CC0-1.0 OR OpenSSL OR Apache-2.0',
        'copyright': 'Copyright 2012 Samuel Neves; changes by Bradley N Eckert',
        'files': ['src/blake2s.c', 'src/blake2s.h'],
        'summary': 'BLAKE2s (RFC 7693), adapted from the BLAKE2 reference code',
    },
    {
        'id': 'WCH-CH32H417-EVT', 'name': 'CH32H417 EVT (WCH SDK and examples)',
        'version': '6d1e469', 'kind': 'code',
        'supplier': 'Organization: Nanjing Qinheng Microelectronics Co., Ltd. (WCH)',
        'download': 'git+https://github.com/openwch/ch32h417@6d1e469',
        'purl': 'pkg:github/openwch/ch32h417@6d1e469',
        'license': 'LicenseRef-WCH-EVT',
        'copyright': 'Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.',
        'files': WCH_FILES,
        'summary': 'EVT/EXAM/SRC (core, debug, linker scripts, peripheral library, start-up) '
                   'and files from the SimulateCDC and SDIO examples, for the CH32H417 target',
    },
    {
        'id': 'Unity', 'name': 'Unity', 'version': '2.7.2', 'kind': 'test',
        'supplier': 'Organization: ThrowTheSwitch.org',
        'download': 'https://github.com/ThrowTheSwitch/Unity',
        'purl': 'pkg:github/ThrowTheSwitch/Unity@v2.7.2',
        'license': 'MIT',
        'copyright': 'Copyright (c) 2007-26 Mike Karlesky, Mark VanderVoord, & Greg Williams',
        'files': ['src/target/desktop/unit_tests/unity/*'],
        'summary': 'C unit test framework, used only by the desktop unit tests',
    },
    {
        'id': 'Noto-Sans-CJK', 'name': 'Noto Sans CJK SC (glyphs)', 'version': 'NOASSERTION',
        'kind': 'data',
        'supplier': 'Organization: Google LLC and Adobe Inc.',
        'download': 'https://github.com/notofonts/noto-cjk',
        'purl': None,
        'license': 'OFL-1.1',
        'copyright': 'Copyright 2014-2021 Adobe (http://www.adobe.com/), with Reserved Font '
                     'Name Source',
        'files': [],
        'summary': 'Glyphs rendered by scripts/mkfont.py into the fonts at blocks 100 and 120 '
                   'of bin/lfblocks.fb4 (forth/glyphs.txt, forth/digits.txt)',
    },
    {
        'id': 'python-cryptography', 'name': 'cryptography (Python)', 'version': 'NOASSERTION',
        'kind': 'tool', 'supplier': 'Organization: Python Cryptographic Authority',
        'download': 'https://pypi.org/project/cryptography/',
        'purl': 'pkg:pypi/cryptography', 'license': 'Apache-2.0 OR BSD-3-Clause',
        'copyright': 'NOASSERTION', 'files': [],
        'summary': 'Used by scripts/sealblocks.py and its tests; not part of this repository',
    },
    {
        'id': 'python-pillow', 'name': 'Pillow', 'version': 'NOASSERTION', 'kind': 'tool',
        'supplier': 'Organization: Python Pillow',
        'download': 'https://pypi.org/project/pillow/', 'purl': 'pkg:pypi/pillow',
        'license': 'MIT-CMU', 'copyright': 'NOASSERTION', 'files': [],
        'summary': 'Used by scripts/mkfont.py; not part of this repository',
    },
    {
        'id': 'python-fonttools', 'name': 'fontTools', 'version': 'NOASSERTION', 'kind': 'tool',
        'supplier': 'Organization: fonttools',
        'download': 'https://pypi.org/project/fonttools/', 'purl': 'pkg:pypi/fonttools',
        'license': 'MIT', 'copyright': 'NOASSERTION', 'files': [],
        'summary': 'Used by scripts/mkfont.py; not part of this repository',
    },
]

# What each kind means to the root package
RELATIONSHIP = {'code': 'CONTAINS', 'test': 'CONTAINS', 'data': 'CONTAINS',
                'tool': 'DEPENDS_ON'}
PURPOSE = {'code': 'LIBRARY', 'test': 'LIBRARY', 'data': 'OTHER', 'tool': 'LIBRARY'}


def git(*args):
    try:
        return subprocess.run(['git', '-C', ROOT] + list(args), capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ''


def expand(patterns):
    """The repository files the patterns name, sorted, as relative paths."""
    out = set()
    for p in patterns:
        for f in glob.glob(os.path.join(ROOT, p), recursive=True):
            if os.path.isfile(f):
                out.add(os.path.relpath(f, ROOT).replace(os.sep, '/'))
    return sorted(out)


def digests(path):
    """SHA-1 and SHA-256 of a file as git stores it: a Windows checkout may
    have CRLF line endings, so they count as LF."""
    with open(os.path.join(ROOT, path), 'rb') as f:
        data = f.read().replace(b'\r\n', b'\n')
    return hashlib.sha1(data).hexdigest(), hashlib.sha256(data).hexdigest()


def spdx_id(text):
    return re.sub(r'[^A-Za-z0-9.-]', '-', text)


def version():
    """LiteForth's version from TF_VERSION (x.xx) in options.h."""
    with open(os.path.join(ROOT, 'src/target/desktop/options.h')) as f:
        m = re.search(r'#define\s+TF_VERSION\s+(\d+)', f.read())
    v = int(m.group(1)) if m else 0
    return f'{v // 100}.{v % 100:02d}'


def build():
    files, packages, rels = [], [], []
    root_id = 'SPDXRef-Package-LiteForth'
    packages.append({
        'SPDXID': root_id, 'name': 'LiteForth', 'versionInfo': version(),
        'supplier': 'Person: Bradley N Eckert', 'originator': 'Person: Bradley N Eckert',
        'downloadLocation': 'git+' + REPO, 'homepage': REPO,
        'licenseConcluded': 'NOASSERTION', 'licenseDeclared': 'MIT',
        'copyrightText': 'Copyright (c) 2026 Bradley N Eckert', 'filesAnalyzed': False,
        'primaryPackagePurpose': 'FIRMWARE',
        'comment': 'A Forth system for 32-bit MCUs (CH32H417, STM32H743) with a desktop '
                   'build, lf. Its own files are MIT; the packages it contains keep their '
                   'own licenses, and the files listed here belong to them.',
    })
    rels.append({'spdxElementId': 'SPDXRef-DOCUMENT', 'relationshipType': 'DESCRIBES',
                 'relatedSpdxElement': root_id})
    for c in COMPONENTS:
        pid = 'SPDXRef-Package-' + spdx_id(c['id'])
        paths = expand(c['files'])
        if c['files'] and not paths:
            raise SystemExit(f'mksbom: no files for {c["name"]}: {c["files"]}')
        pkg = {
            'SPDXID': pid, 'name': c['name'], 'versionInfo': c['version'],
            'supplier': c['supplier'], 'downloadLocation': c['download'],
            'licenseConcluded': c['license'], 'licenseDeclared': c['license'],
            'copyrightText': c['copyright'], 'summary': c['summary'],
            'primaryPackagePurpose': PURPOSE[c['kind']],
        }
        if c['purl']:
            pkg['externalRefs'] = [{'referenceCategory': 'PACKAGE-MANAGER',
                                    'referenceType': 'purl', 'referenceLocator': c['purl']}]
        if paths:
            sha1s = []
            pkg['hasFiles'] = []
            for p in paths:
                fid = 'SPDXRef-File-' + spdx_id(p)
                s1, s256 = digests(p)
                sha1s.append(s1)
                entry = {'SPDXID': fid, 'fileName': './' + p,
                         'checksums': [{'algorithm': 'SHA1', 'checksumValue': s1},
                                       {'algorithm': 'SHA256', 'checksumValue': s256}],
                         'licenseConcluded': c['license'],
                         'licenseInfoInFiles': ['NOASSERTION'],
                         'copyrightText': c['copyright']}
                if p in MODIFIED:
                    entry['comment'] = 'Modified: ' + MODIFIED[p]
                files.append(entry)
                pkg['hasFiles'].append(fid)
            pkg['filesAnalyzed'] = True
            pkg['packageVerificationCode'] = {
                'packageVerificationCodeValue': hashlib.sha1(
                    ''.join(sorted(sha1s)).encode()).hexdigest()}
            pkg['licenseInfoFromFiles'] = ['NOASSERTION']
        else:
            pkg['filesAnalyzed'] = False
        packages.append(pkg)
        rels.append({'spdxElementId': root_id, 'relationshipType': RELATIONSHIP[c['kind']],
                     'relatedSpdxElement': pid})
        if c['kind'] == 'test':
            rels.append({'spdxElementId': pid, 'relationshipType': 'TEST_DEPENDENCY_OF',
                         'relatedSpdxElement': root_id})
        if c['kind'] == 'tool':
            rels.append({'spdxElementId': pid, 'relationshipType': 'DEV_TOOL_OF',
                         'relatedSpdxElement': root_id})
    listed = {f['fileName'][2:] for f in files}
    unknown = sorted(set(MODIFIED) - listed)
    if unknown:
        raise SystemExit(f'mksbom: MODIFIED names files no component lists: {unknown}')
    created = git('log', '-1', '--format=%ct')
    if created:
        created = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(int(created)))
    body = {'packages': packages, 'files': files, 'relationships': rels}
    doc = {
        'spdxVersion': 'SPDX-2.3', 'dataLicense': 'CC0-1.0', 'SPDXID': 'SPDXRef-DOCUMENT',
        'name': 'LiteForth',
        'documentNamespace': REPO + '/sbom/' + hashlib.sha256(
            json.dumps(body, sort_keys=True).encode()).hexdigest()[:32],
        'creationInfo': {
            'created': created or '1970-01-01T00:00:00Z',
            'creators': ['Tool: scripts/mksbom.py', 'Person: Bradley N Eckert'],
            'comment': 'Made by scripts/mksbom.py from its component list; '
                       'doc/sbom.md describes it.'},
        'hasExtractedLicensingInfos': [{
            'licenseId': 'LicenseRef-WCH-EVT', 'name': 'WCH EVT notice',
            'extractedText': WCH_NOTICE,
            'comment': 'The notice at the top of every WCH file; WCH publishes no other '
                       'license for the EVT.'}],
    }
    doc.update(body)
    return doc


def comparable(doc):
    d = dict(doc)
    d['creationInfo'] = {k: v for k, v in d['creationInfo'].items() if k != 'created'}
    return d


def scan():
    """Files with a copyright notice that no component claims."""
    claimed = set()
    for c in COMPONENTS:
        claimed.update(expand(c['files']))
    tracked = git('ls-files').splitlines() or expand(['**/*'])
    odd = []
    for p in tracked:
        if p in claimed or not re.search(r'\.(c|h|S|s|ld|py|f)$', p):
            continue
        try:
            with open(os.path.join(ROOT, p), encoding='latin-1') as f:
                head = f.read(4000)
        except OSError:
            continue
        for m in re.finditer(r'(?i)copyright[^\n]{0,80}', head):
            if 'Eckert' not in m.group(0) and 'sbom' not in p:
                odd.append(f'{p}: {m.group(0).strip()}')
                break
    return odd


def main(argv):
    doc = build()
    text = json.dumps(doc, indent=2, ensure_ascii=False) + '\n'
    if '--scan' in argv:
        odd = scan()
        print('\n'.join(odd) if odd else 'mksbom: every copyright notice belongs to a component')
        return 1 if odd else 0
    if '--check' in argv:
        try:
            with open(OUT, encoding='utf-8') as f:
                old = json.load(f)
        except (OSError, ValueError):
            print('mksbom: sbom.spdx.json is missing or unreadable; run scripts/mksbom.py')
            return 1
        if comparable(old) != comparable(json.loads(text)):
            print('mksbom: sbom.spdx.json is out of date: a listed file changed, or the '
                  'component list did. Check the change, then run scripts/mksbom.py')
            return 1
        print('mksbom: sbom.spdx.json is up to date')
        return 0
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    n = sum(1 for p in doc['packages'][1:])
    print(f'sbom.spdx.json: LiteForth {version()}, {n} components, {len(doc["files"])} files')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
