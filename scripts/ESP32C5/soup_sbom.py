#!/usr/bin/env python3
"""soup_sbom.py -- the ESP32-C5 SOUP Software Bill of Materials.

Reads the pinned SOUP manifest (src/target/ESP32C5/soup.lock.json: ESP-IDF and
the third-party code it bundles) and writes an SPDX 2.3 document
(src/target/ESP32C5/soup.spdx.json), so a CVE against any bundled component
can be matched to this device (security-plan.md section 4.3). It complements
scripts/mksbom.py, which covers the code *in this repository*; ESP-IDF isn't
in the repo, so this SBOM is generated from the manifest rather than by
hashing files.

    soup_sbom.py              write src/target/ESP32C5/soup.spdx.json
    soup_sbom.py --check      exit 1 if the SBOM is out of date vs the manifest
    soup_sbom.py --list       print the components and their linked state
    soup_sbom.py --verify-idf $IDF_PATH
                              read the real versions from a local ESP-IDF
                              install (the manifest's 'confirm' files) and warn
                              on any mismatch -- the manifest stays the source
                              of truth; this only flags drift.

The document's creation time is the repository's last commit time, so the
file changes only when the manifest does; --check ignores the timestamp.
unit_tests/soup runs --check. See doc/ESP32C5/admin/soup.md.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MANIFEST = os.path.join(ROOT, 'src', 'target', 'ESP32C5', 'soup.lock.json')
OUT = os.path.join(ROOT, 'src', 'target', 'ESP32C5', 'soup.spdx.json')
REPO = 'https://github.com/bradleyeckert/LiteForth'

# linked state -> SPDX primaryPackagePurpose / a human note
LINK_NOTE = {
    'app': 'linked into the application image',
    'bootloader': 'linked into the 2nd-stage bootloader (secure-boot path)',
    'unlinked': 'present in ESP-IDF but not linked into the current image',
}


def spdx_id(text):
    return re.sub(r'[^A-Za-z0-9.-]', '-', text)


def git(*args):
    try:
        return subprocess.run(['git', '-C', ROOT] + list(args), capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ''


def load_manifest():
    with open(MANIFEST, encoding='utf-8') as f:
        return json.load(f)


def build(man):
    fw = man['framework']
    packages, rels = [], []
    root_id = 'SPDXRef-Product-ESP32C5'
    packages.append({
        'SPDXID': root_id,
        'name': 'LiteForth-ESP32C5',
        'versionInfo': f"esp-idf {fw['version']}",
        'downloadLocation': 'git+' + REPO,
        'homepage': REPO,
        'primaryPackagePurpose': 'FIRMWARE',
        'supplier': 'Person: Bradley N Eckert',
        'comment': 'The ESP32-C5 firmware product; the packages below are the '
                   'SOUP it is built from (' + fw.get('note', '') + ')',
    })
    for c in man['components']:
        pid = 'SPDXRef-Package-' + spdx_id(c['name'])
        pkg = {
            'SPDXID': pid,
            'name': c['name'],
            'versionInfo': c['version'],
            'downloadLocation': c['upstream'],
            'licenseConcluded': c['license'],
            'licenseDeclared': c['license'],
            'primaryPackagePurpose': 'LIBRARY' if c['name'] != 'esp-idf' else 'FRAMEWORK',
            'comment': f"[{c['linked']}] {LINK_NOTE.get(c['linked'], c['linked'])}. "
                       f"{c.get('surface', '')}",
        }
        refs = []
        if c.get('cpe'):
            refs.append({'referenceCategory': 'SECURITY', 'referenceType': 'cpe23Type',
                         'referenceLocator': c['cpe']})
        if c.get('purl'):
            refs.append({'referenceCategory': 'PACKAGE-MANAGER', 'referenceType': 'purl',
                         'referenceLocator': c['purl']})
        if refs:
            pkg['externalRefs'] = refs
        packages.append(pkg)
        # A linked component is a runtime dependency of the product; an
        # unlinked one is only an optional (present-but-excluded) dependency.
        rel = 'DEPENDS_ON' if c['linked'] != 'unlinked' else 'OPTIONAL_DEPENDENCY_OF'
        if rel == 'DEPENDS_ON':
            rels.append({'spdxElementId': root_id, 'relatedSpdxElement': pid,
                         'relationshipType': 'DEPENDS_ON'})
        else:
            rels.append({'spdxElementId': pid, 'relatedSpdxElement': root_id,
                         'relationshipType': 'OPTIONAL_DEPENDENCY_OF'})

    body = {'packages': packages, 'relationships': rels}
    created = git('log', '-1', '--format=%ct')
    created = (time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(int(created)))
               if created else '1970-01-01T00:00:00Z')
    doc = {
        'spdxVersion': 'SPDX-2.3', 'dataLicense': 'CC0-1.0', 'SPDXID': 'SPDXRef-DOCUMENT',
        'name': 'LiteForth-ESP32C5-SOUP',
        'documentNamespace': REPO + '/sbom/esp32c5-soup/' + hashlib.sha256(
            json.dumps(body, sort_keys=True).encode()).hexdigest()[:32],
        'creationInfo': {
            'created': created,
            'creators': ['Tool: scripts/ESP32C5/soup_sbom.py', 'Person: Bradley N Eckert'],
        },
        'comment': 'The pinned ESP-IDF SOUP for the ESP32-C5 (src/target/ESP32C5/'
                   'soup.lock.json). Versions marked CONFIRM/confirm in the manifest '
                   'must be checked against the pinned IDF install.',
        **body,
    }
    return doc


def comparable(doc):
    """The document minus its timestamp and namespace (which track the commit)."""
    d = json.loads(json.dumps(doc))
    d.get('creationInfo', {}).pop('created', None)
    d.pop('documentNamespace', None)
    return d


def verify_idf(man, idf_path):
    """Read the true versions from a local ESP-IDF and warn on drift. Best
    effort: each component names the file to read in its 'confirm' field; we
    parse the few we can read mechanically, and just report the rest."""
    import glob
    probes = {
        'mbedtls': ('components/mbedtls/mbedtls/include/mbedtls/build_info.h',
                    r'MBEDTLS_VERSION_STRING\s+"([0-9.]+)"'),
        'freertos-kernel': ('components/freertos/FreeRTOS-Kernel/include/task.h',
                            r'tskKERNEL_VERSION_NUMBER\s+"V?([0-9.]+)"'),
        'lwip': ('components/lwip/lwip/src/include/lwip/init.h',
                 r'LWIP_VERSION_MAJOR\s+(\d+).*?LWIP_VERSION_MINOR\s+(\d+).*?'
                 r'LWIP_VERSION_REVISION\s+(\d+)'),
    }
    pinned = {c['name']: c['version'] for c in man['components']}
    mism = 0
    for name, (rel, pat) in probes.items():
        hits = glob.glob(os.path.join(idf_path, rel))
        if not hits:
            print(f'  ?  {name}: {rel} not found under {idf_path}')
            continue
        text = open(hits[0], encoding='utf-8', errors='replace').read()
        m = re.search(pat, text, re.S)
        if not m:
            print(f'  ?  {name}: version pattern not found in {rel}')
            continue
        found = '.'.join(g for g in m.groups() if g is not None) if m.groups() and \
            len([g for g in m.groups() if g]) > 1 else m.group(1)
        want = pinned.get(name, '?')
        ok = found == want
        mism += 0 if ok else 1
        print(f'  {"ok" if ok else "DRIFT"}  {name}: manifest {want}, IDF {found}')
    print(f'\nsoup_sbom --verify-idf: {mism} mismatch(es). The framework version, '
          f'git commit and the components not probed above must be confirmed by hand.')
    return 1 if mism else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description='ESP32-C5 SOUP SBOM (SPDX 2.3).')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--verify-idf', metavar='IDF_PATH')
    a = ap.parse_args(argv)
    man = load_manifest()

    if a.list:
        print(f"ESP-IDF {man['framework']['version']} SOUP:")
        for c in man['components']:
            print(f"  [{c['linked']:10}] {c['name']:16} {c['version']:12} {c['license']}")
        return 0
    if a.verify_idf:
        return verify_idf(man, a.verify_idf)

    doc = build(man)
    text = json.dumps(doc, indent=2, ensure_ascii=False) + '\n'
    if a.check:
        try:
            with open(OUT, encoding='utf-8') as f:
                old = json.load(f)
        except (OSError, ValueError):
            print('soup_sbom: soup.spdx.json is missing or unreadable; run scripts/ESP32C5/soup_sbom.py')
            return 1
        if comparable(old) != comparable(json.loads(text)):
            print('soup_sbom: soup.spdx.json is out of date vs soup.lock.json: '
                  'run scripts/ESP32C5/soup_sbom.py')
            return 1
        print('soup_sbom: soup.spdx.json is up to date')
        return 0

    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    linked = sum(1 for c in man['components'] if c['linked'] != 'unlinked')
    print(f"soup.spdx.json: ESP-IDF {man['framework']['version']}, "
          f"{len(man['components'])} components ({linked} linked)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
