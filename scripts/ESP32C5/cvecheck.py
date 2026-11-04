#!/usr/bin/env python3
"""cvecheck.py -- the ESP32-C5 SOUP CVE monitor.

Matches the pinned SOUP versions (src/target/ESP32C5/soup.lock.json) against
security advisories and flags any that affect a *linked* component -- an
unpatched, networked SOUP CVE being the highest-probability finding for an
automated attacker (security-plan.md sections 4.4 and 7.2).

    cvecheck.py            check the pinned versions against the curated,
                           checked-in advisory backstop (advisories.json)
    cvecheck.py --online   also query the OSV.dev feed for new advisories
                           (needs network; degrades to offline on failure)
    cvecheck.py --strict   also fail on an unlinked component (not just linked)
    cvecheck.py --list     print what is monitored

Exit status is nonzero when a monitored component is affected by an unpatched
advisory: a linked component always (or any component with --strict). An
unlinked component's match is reported as informational (it is not in the
current image) unless --strict.

The offline backstop (advisories.json) always runs, so CI is deterministic and
needs no network; unit_tests/soup exercises the matcher on fixtures. The
--online OSV query is the standing live monitor, run on a schedule by
.github/workflows/soup-cve.yml. See doc/ESP32C5/admin/soup.md.
"""
import argparse
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MANIFEST = os.path.join(ROOT, 'src', 'target', 'ESP32C5', 'soup.lock.json')
ADVISORIES = os.path.join(ROOT, 'src', 'target', 'ESP32C5', 'advisories.json')
OSV_API = 'https://api.osv.dev/v1/query'


def vtuple(v):
    """A comparable tuple from a version string: numeric parts, trailing
    non-numeric dropped. '3.6.3' -> (3,6,3); 'V11.1.0' -> (11,1,0)."""
    out = []
    for part in str(v).lstrip('vV').replace('-', '.').split('.'):
        num = ''
        for ch in part:
            if ch.isdigit():
                num += ch
            else:
                break
        if num == '':
            break
        out.append(int(num))
    return tuple(out) or (0,)


def vle(a, b):
    return vtuple(a) <= vtuple(b)


def vlt(a, b):
    return vtuple(a) < vtuple(b)


def affected(version, adv):
    """Is `version` affected by advisory `adv`?"""
    if version in adv.get('versions', []):
        return True
    for r in adv.get('ranges', []):
        intro = r.get('introduced', '0')
        fixed = r.get('fixed')
        if vle(intro, version) and (fixed is None or vlt(version, fixed)):
            return True
    return False


def load(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def osv_query(purl, version):
    """Query OSV.dev for one component. Returns a list of advisory dicts in our
    shape, or raises on a network/parse error (the caller degrades)."""
    import urllib.request
    body = json.dumps({'version': version, 'package': {'purl': purl}}).encode()
    req = urllib.request.Request(OSV_API, data=body,
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=20) as r:
        data = json.load(r)
    out = []
    for v in data.get('vulns', []):
        out.append({'id': v.get('id', '?'),
                    'summary': (v.get('summary') or v.get('details', ''))[:120],
                    'link': f"https://osv.dev/vulnerability/{v.get('id', '')}",
                    '_osv': True})
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description='ESP32-C5 SOUP CVE monitor.')
    ap.add_argument('--online', action='store_true', help='also query the OSV feed')
    ap.add_argument('--strict', action='store_true', help='fail on unlinked matches too')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args(argv)

    man = load(MANIFEST)
    comps = {c['name']: c for c in man['components']}

    if a.list:
        db = load(ADVISORIES)
        print(f"Monitoring ESP-IDF {man['framework']['version']} SOUP "
              f"({len(comps)} components) against {len(db['advisories'])} curated "
              f"advisories (updated {db['updated']})"
              + (" + OSV online" if a.online else "") + ":")
        for c in man['components']:
            print(f"  [{c['linked']:10}] {c['name']:16} {c['version']}")
        return 0

    db = load(ADVISORIES)
    print(f"cvecheck: ESP-IDF {man['framework']['version']} SOUP vs "
          f"advisories.json (updated {db['updated']})"
          + (" + OSV" if a.online else "") + "\n")

    # Gather advisories per component: curated, then (optional) live OSV.
    by_comp = {}
    for adv in db['advisories']:
        by_comp.setdefault(adv['component'], []).append(adv)

    online_ok = True
    if a.online:
        for name, c in comps.items():
            purl = c.get('purl')
            if not purl:
                continue
            try:
                for adv in osv_query(purl, c['version']):
                    adv['component'] = name
                    adv.setdefault('ranges', [])
                    adv['versions'] = [c['version']]   # OSV already matched this version
                    by_comp.setdefault(name, []).append(adv)
            except Exception as e:                      # network, parse, timeout
                online_ok = False
                print(f"  (OSV query for {name} failed: {e}; using offline backstop)")

    linked_hits = unlinked_hits = 0
    for name, c in comps.items():
        for adv in by_comp.get(name, []):
            if not affected(c['version'], adv):
                continue
            linked = c['linked'] != 'unlinked'
            tag = 'FAIL' if (linked or a.strict) else 'info'
            if linked:
                linked_hits += 1
            else:
                unlinked_hits += 1
            where = c['linked']
            print(f"  {tag}  {adv['id']}  {name} {c['version']} [{where}]")
            print(f"        {adv.get('summary', '')}")
            if adv.get('link'):
                print(f"        {adv['link']}")

    print()
    if a.online and not online_ok:
        print("cvecheck: NOTE the live OSV feed was unreachable; only the offline "
              "backstop was checked.")
    fail = linked_hits + (unlinked_hits if a.strict else 0)
    if fail:
        print(f"cvecheck: {linked_hits} linked, {unlinked_hits} unlinked advisory "
              f"match(es). A linked (or --strict) match fails the gate: pin a fixed "
              f"IDF/component or document the exception.")
        return 1
    if unlinked_hits:
        print(f"cvecheck: 0 linked matches; {unlinked_hits} on unlinked components "
              f"(informational -- not in the current image).")
        return 0
    print("cvecheck: no known advisories affect the pinned components.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
