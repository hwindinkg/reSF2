"""Compare the Math.random draw counter per frame: oracle vs port (survival).

The oracle side is patched in-memory (the shipped `sf2.502f0946.js` gets a
Math.random wrapper that logs `[mr] n=<count>`), the port side uses
SF2_TRACE_MR=1. Prints the first frames where the cumulative count differs.
"""

import io
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JS = os.path.join(ROOT, 'reference', 'www', 'sf2.502f0946.js')
BAK = os.path.join(ROOT, '.harness', 'sf2.mr.bak.js')
CONSOLE = os.path.join(ROOT, 'reference', 'traces', 'console.log')
ORACLE = os.path.join(ROOT, 'shell', 'bin', 'Release', 'net9.0-windows',
                      'OracleShell.exe')
GAME = os.path.join(ROOT, 'build', 'app', 'game', 'Release', 'game.exe')


def patch_js():
    shutil.copy2(JS, BAK)
    s = io.open(JS, encoding='utf-8', errors='replace').read()
    marker = 'Da.g="496";'
    if marker not in s:
        print('marker not found; aborting')
        return False
    wrap = ('window.__MR=window.__MR||{n:0};if(!window.__MRW){window.__MRW=1;'
            'var _mr=Math.random;Math.random=function(){var _v=_mr();'
            'window.__MR.n++;console.log("[mr] n="+window.__MR.n+" v="+_v);'
            'return _v;};}')
    s = s.replace(marker, wrap + marker, 1)
    io.open(JS, 'w', encoding='utf-8', newline='').write(s)
    return True


def restore_js():
    if os.path.exists(BAK):
        shutil.copy2(BAK, JS)
        os.remove(BAK)


def run_oracle():
    io.open(CONSOLE, 'w', encoding='utf-8').write('')
    subprocess.run([ORACLE, '--query',
                    'postTutorial=1&skipFightNone=1&maxFrames=1200&battle=Survival',
                    '--input-script', 'reference/tools/state_survival.txt',
                    '--timeout-ms', '90000'],
                   cwd=ROOT, capture_output=True, timeout=180)


def oracle_counts():
    reseed = None
    cur = None
    per = {}
    for line in io.open(CONSOLE, encoding='utf-8', errors='replace'):
        if 'math.random reseeded' in line:
            m = re.search(r'drawsBefore=(\d+)', line)
            if m:
                reseed = int(m.group(1))
            continue
        if '"t":"state"' in line:
            m = re.search(r'"f":(\d+)', line)
            if m:
                cur = int(m.group(1))
                per.setdefault(cur, 0)
            continue
        m = re.search(r'\[mr\] n=(\d+)', line)
        if m and cur is not None and reseed is not None:
            if int(m.group(1)) > reseed:
                per[cur] += 1
    return per


def run_port():
    env = dict(os.environ)
    env['SF2_TRACE_MR'] = '1'
    out = subprocess.run(
        [GAME, '--fight', '--battle', 'Survival', '--zone', 'ZONE_1',
         '--fight-ordinal', '0', '--location', 'bamboo_grove',
         '--headless', '700', '--dump-state', '600', '--watchdog', '900',
         'reference/www/res', 'reference/saves/save.xml'],
        cwd=ROOT, capture_output=True, env=env, timeout=600)
    return (out.stdout.decode('utf-8', 'replace') + chr(10) + out.stderr.decode('utf-8', 'replace')).splitlines()


def port_counts(lines):
    seen = False
    cur = None
    per = {}
    for line in lines:
        if '[mr-reseed]' in line:
            seen = True
            continue
        m = re.search(r'\[frame\] f=(\d+)', line)
        if m:
            per[int(m.group(1))] = per.get(int(m.group(1)), 0) + 1
            continue
        if seen and '[mr]' in line:
            if cur is not None:
                per[cur] = per.get(cur, 0) + 1
    return per


def main():
    if not patch_js():
        return 1
    try:
        run_oracle()
        oc = oracle_counts()
    finally:
        restore_js()
    pc = port_counts(run_port())
    if not oc:
        print('oracle: no [mr] lines (patch failed?)')
        return 1
    ofs = sorted(oc)
    print('oracle frames %d..%d draws=%d' % (ofs[0], ofs[-1], sum(oc.values())))
    if not pc:
        print('port: no [mr] lines')
        return 1
    pfs = sorted(pc)
    print('port frames %d..%d draws=%d' % (pfs[0], pfs[-1], sum(pc.values())))
    # align by the first state frame of each side
    off = ofs[0] - pfs[0]
    print('offset oracle = port + %d' % off)
    ocum = pcum = 0
    bad = 0
    for f in pfs:
        pcum += pc.get(f, 0)
        ocum += oc.get(f + off, 0)
        if pcum != ocum:
            print('first cumulative mismatch: port f=%d (%d) vs oracle f=%d (%d)'
                  % (f, pcum, f + off, ocum))
            bad += 1
            break
    if not bad:
        print('cumulative counts match over all %d port frames' % len(pfs))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
