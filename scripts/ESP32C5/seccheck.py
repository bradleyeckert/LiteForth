#!/usr/bin/env python3
"""seccheck.py -- ESP32-C5 security-configuration gate.

Parses an ESP-IDF sdkconfig and checks it against LiteForth's production
security baseline (doc/ESP32C5/admin/security-plan.md section 7.1): Secure
Boot v2 on, flash encryption in Release mode, the ROM download back door
closed, OTA anti-rollback on, no insecure "allow" escapes, and the software
hardening that costs nothing to leave on. It turns the baseline into a build
gate instead of a set of intentions, so an automated attacker never finds a
control that quietly regressed.

    seccheck.py sdkconfig                 # check a built config (production)
    seccheck.py --profile dev sdkconfig   # development build (keys/fuses absent)
    seccheck.py --list                    # print the baseline and exit

Profiles:
  production  every baseline control must be set (a release candidate).
  dev         controls that need the signing keys / burned eFuses (secure
              boot, flash encryption, download-disable, anti-rollback) are
              reported as NOT-YET instead of failures, so a normal dev build
              passes -- but an *insecure escape* ("allow" options, development
              flash-encryption mode) is still a hard failure in every profile,
              because those are never acceptable.

Exit status is nonzero if any check fails for the chosen profile. The
sdkconfig security symbol names are stable across ESP-IDF 5.x; if a name
differs in your IDF version, adjust RULES below (one place).

The companion overlay src/target/ESP32C5/sdkconfig.secure.defaults enables
this baseline for a release build; unit_tests/seccheck exercises this script.
"""
import argparse
import sys

# Each rule: (kind, keys, title, rationale)
#   must    : every key must be =y (production gate)
#   one_of  : at least one key must be =y
#   forbid  : none of the keys may be =y, in ANY profile (insecure escape)
#   should  : recommended; a warning if not =y, never fails the build
RULES = [
    ('must',   ['CONFIG_SECURE_BOOT'],
     'Secure Boot enabled',
     'Only vendor-signed bootloader + app run; the first line against modified firmware.'),
    ('must',   ['CONFIG_SECURE_BOOT_V2_ENABLED'],
     'Secure Boot v2 (not v1)',
     'v2 is the RSA/ECDSA scheme with key revocation; v1 is deprecated/weaker.'),
    ('one_of', ['CONFIG_SECURE_BOOT_V2_RSA_ENABLED', 'CONFIG_SECURE_BOOT_V2_ECDSA_ENABLED'],
     'A Secure Boot v2 signature scheme is chosen',
     'RSA-3072 or ECDSA (P-256/384) on the C5; pick one explicitly.'),
    ('must',   ['CONFIG_SECURE_FLASH_ENC_ENABLED'],
     'Flash encryption enabled',
     'Firmware/IP and secrets at rest are XTS-AES encrypted; key stays in eFuse.'),
    ('must',   ['CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE'],
     'Flash encryption in RELEASE mode',
     'Release makes it permanent and closes the re-encryption/readback back doors that Development leaves open.'),
    ('one_of', ['CONFIG_SECURE_DISABLE_ROM_DL_MODE', 'CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE'],
     'ROM serial download back door closed',
     'Disable it, or restrict it to secure download; otherwise the chip can be reflashed/dumped over UART/USB. Updates then go via OTA.'),
    ('must',   ['CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE'],
     'OTA rollback enabled',
     'A bad update is rolled back instead of bricking the device.'),
    ('must',   ['CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK'],
     'OTA anti-rollback (downgrade protection) enabled',
     'Refuses an older, vulnerable image via the SECURE_VERSION eFuse -- blocks downgrade attacks.'),
    ('must',   ['CONFIG_SECURE_FLASH_CHECK_ENC_EN_IN_APP'],
     'App verifies flash encryption at runtime',
     'The app refuses to run on a chip where encryption was somehow disabled.'),

    # Insecure escapes: forbidden in EVERY profile.
    ('forbid', ['CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT'],
     'Flash encryption Development mode',
     'Development mode keeps the UART re-encryption back door open; never ship it.'),
    ('forbid', ['CONFIG_SECURE_BOOT_ALLOW_JTAG'],
     'Secure Boot "allow JTAG"',
     'Leaves JTAG open on a secure-boot device -- a direct key/memory extraction path.'),
    ('forbid', ['CONFIG_SECURE_BOOT_ALLOW_ROM_BASIC'],
     'Secure Boot "allow ROM BASIC"',
     'Leaves the ROM BASIC interpreter reachable: an unauthenticated console.'),
    ('forbid', ['CONFIG_SECURE_BOOT_ALLOW_SHORT_APP_PARTITION'],
     'Secure Boot "allow short app partition"',
     'Weakens the signed-image size assumptions.'),
    ('forbid', ['CONFIG_SECURE_BOOT_ALLOW_UNUSED_DIGEST_SLOTS'],
     'Secure Boot "allow unused digest slots"',
     'Prevents revoking spare key slots, leaving extra valid signing keys.'),
    ('forbid', ['CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC'],
     'UART bootloader "allow encrypt"',
     'Lets the download mode re-encrypt flash -- a reflash path on a "locked" part.'),
    ('forbid', ['CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_DEC'],
     'UART bootloader "allow decrypt"',
     'Lets the download mode decrypt flash -- a firmware/secret dump path.'),
    ('forbid', ['CONFIG_SECURE_FLASH_UART_BOOTLOADER_ALLOW_CACHE'],
     'UART bootloader "allow cache"',
     'Exposes decrypted flash through the cache in download mode.'),

    # Software hardening: cheap, recommended.
    ('should', ['CONFIG_COMPILER_STACK_CHECK_MODE_STRONG', 'CONFIG_COMPILER_STACK_CHECK_MODE_ALL',
                'CONFIG_COMPILER_STACK_CHECK_MODE_NORM'],
     'Stack-smashing protection on',
     'Catches stack buffer overflows -- a common target of fuzzed/automated input.'),
    ('should', ['CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE'],
     'Assertions enabled',
     'Keeps the sanity checks that turn a corrupted state into a controlled abort.'),
    ('should', ['CONFIG_NVS_ENCRYPTION'],
     'NVS encryption on',
     'Encrypts secrets kept in NVS (needed once keys/credentials are stored there).'),
    ('should', ['CONFIG_BOOTLOADER_WDT_ENABLE'],
     'Bootloader watchdog on',
     'Recovers from a hang during boot (resiliency).'),
]

# Rules whose controls depend on the signing keys / burned eFuses, so a dev
# build legitimately lacks them: downgraded to NOT-YET under --profile dev.
HARDWARE_DEPENDENT = {
    'CONFIG_SECURE_BOOT', 'CONFIG_SECURE_BOOT_V2_ENABLED',
    'CONFIG_SECURE_BOOT_V2_RSA_ENABLED', 'CONFIG_SECURE_BOOT_V2_ECDSA_ENABLED',
    'CONFIG_SECURE_FLASH_ENC_ENABLED', 'CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE',
    'CONFIG_SECURE_DISABLE_ROM_DL_MODE', 'CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE',
    'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE', 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK',
    'CONFIG_SECURE_FLASH_CHECK_ENC_EN_IN_APP',
}


def parse_sdkconfig(path):
    """Return {CONFIG_X: value}. 'y'/'n'(from 'is not set')/number/string."""
    cfg = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line.startswith('# CONFIG_') and line.endswith(' is not set'):
                cfg[line[2:].split(' is not set')[0].strip()] = 'n'
            elif line.startswith('CONFIG_') and '=' in line:
                k, v = line.split('=', 1)
                cfg[k.strip()] = v.strip().strip('"')
    return cfg


def main():
    ap = argparse.ArgumentParser(description='ESP32-C5 security-config gate.')
    ap.add_argument('sdkconfig', nargs='?', help='the sdkconfig to check')
    ap.add_argument('--profile', choices=['production', 'dev'], default='production')
    ap.add_argument('--list', action='store_true', help='print the baseline and exit')
    a = ap.parse_args()

    if a.list:
        for kind, keys, title, why in RULES:
            print(f'[{kind:6}] {title}\n         {" / ".join(keys)}\n         {why}')
        return 0
    if not a.sdkconfig:
        ap.error('give an sdkconfig path (or --list)')

    cfg = parse_sdkconfig(a.sdkconfig)
    fails = warns = 0

    def on(keys):
        return any(cfg.get(k) == 'y' for k in keys)

    print(f'seccheck: {a.sdkconfig}  (profile: {a.profile})\n')
    for kind, keys, title, why in RULES:
        if kind == 'forbid':
            bad = [k for k in keys if cfg.get(k) == 'y']
            if bad:
                print(f'  FAIL  {title}: {", ".join(bad)} is set')
                print(f'        {why}')
                fails += 1
            else:
                print(f'  ok    {title}: not set')
        elif kind in ('must', 'one_of'):
            if on(keys):
                print(f'  ok    {title}')
            elif a.profile == 'dev' and all(k in HARDWARE_DEPENDENT for k in keys):
                print(f'  ..    {title}: not yet (dev build; set at provisioning)')
            else:
                print(f'  FAIL  {title}: need {" or ".join(keys) if kind=="one_of" else keys[0]}=y')
                print(f'        {why}')
                fails += 1
        elif kind == 'should':
            if on(keys):
                print(f'  ok    {title}')
            else:
                print(f'  WARN  {title}: recommend {" or ".join(keys)}=y')
                warns += 1

    print(f'\nseccheck: {fails} failure(s), {warns} warning(s) [{a.profile}]')
    if fails:
        print('seccheck: security baseline NOT met.')
        return 1
    print('seccheck: security baseline met.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
