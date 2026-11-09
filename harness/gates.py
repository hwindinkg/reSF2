#!/usr/bin/env python3
"""harness/gates.py - the single gate entry point for the reSF2 native port.

Why this exists
---------------
The port is a hand transcription of the shipped JS. Bugs reappear because the
checks are manual and half of the "gates" cannot fail. This runner is the one
place that decides whether the tree is acceptable, and it is allowed to say NO.

Rules (AGENTS.md, enforced here)
--------------------------------
RULE 0: every gate runs HIDDEN, strictly SEQUENTIALLY, in the foreground, with
`--watchdog` in its argv, and leaves no process behind. No OS input is ever
generated: the binaries drive themselves through `App::inject_click`/
`inject_key`, and the web oracle through console/trace hooks.

`reference/saves/save.xml` is the player's save: this runner takes its own
backup and restores it after the run (the binaries have their own guard; this
is the outer one). `reference/www` is snapshotted/restored too - OracleShell
copies `reference/runner/trace.js` into it (reference/traces/README.md:158).

Metrics
-------
Every gate reports a FAILURE COUNT (lower is better). Exit codes of "lying"
gates are ignored: the number is parsed from stdout. A gate whose anchor line
is missing FAILS - absence is not success. `now > baseline` is a REGRESSION;
`now < baseline` is an IMPROVEMENT (shown, exit stays 0, the baseline is only
moved with `--update-baseline --reason "..." --yes`).

Exit codes
----------
0  all OK (or improvements only)
1  regression / lying gate / timeout / missing input / gate without baseline
2  environment error (no game exe, no oracle output, stale OracleShell)

Usage
-----
  python harness/gates.py --fast
  python harness/gates.py --full
  python harness/gates.py --only verify-input,diff:survival
  python harness/gates.py --list
  python harness/gates.py --reparse            # re-read .harness/samples
  python harness/gates.py --update-baseline --only verify-input \
      --reason "why" --yes
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

# --------------------------------------------------------------------------
# Paths / constants
# --------------------------------------------------------------------------

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "build" / "app" / "game" / "Release" / "game.exe"
ORACLE = ROOT / "shell" / "bin" / "Release" / "net9.0-windows" / "OracleShell.exe"
PY = sys.executable

RES = "reference/www/res"
SAVE = ROOT / "reference" / "saves" / "save.xml"
SAVE_REL = "reference/saves/save.xml"

MSBUILD = Path(
    r"D:\MicrosoftVisualStudio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
)
SLN = ROOT / "build" / "sf2_native.sln"

H = ROOT / ".harness"
SAMPLES = H / "samples"
ARTIFACTS = H / "artifacts"
REPORTS = H / "reports"
BASELINE = ROOT / "harness" / "gates.baseline.json"

# Files OracleShell overwrites inside the reference tree (README:158) plus the
# two runner copies. Snapshotted before a run, restored after it.
REFWW = [
    ROOT / "reference" / "www" / "index.html",
    ROOT / "reference" / "www" / "trace.js",
    ROOT / "reference" / "www" / "microsite-game-interface.js",
]

CREATE_NO_WINDOW = 0x08000000 if os.name == "nt" else 0

# Oracle navigation is wall-clock (scripted taps): a bounded retry absorbs the
# jitter. A run that never produces a state trace is still an ERROR.
ORACLE_ATTEMPTS = 3


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def kill_tree(pid: int) -> None:
    """Kill ONLY our own process tree (RULE 0: never touch foreign processes)."""
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(pid), "/T", "/F"],
            capture_output=True,
            creationflags=CREATE_NO_WINDOW,
        )
    else:  # pragma: no cover - the port is Windows-only
        try:
            os.kill(pid, 9)
        except OSError:
            pass


def newest_mtime(paths) -> float:
    newest = 0.0
    for base in paths:
        p = ROOT / base
        if not p.exists():
            continue
        for root, _dirs, files in os.walk(p):
            for name in files:
                if name.endswith((".cpp", ".hpp", ".h", ".cc", ".cxx")):
                    try:
                        newest = max(newest, (Path(root) / name).stat().st_mtime)
                    except OSError:
                        pass
    return newest


class AnchorMissing(Exception):
    """The gate's expected output line is absent -> FAIL, never OK."""


class SaveGuard:
    """Outer save guard: back up before the run, restore after it."""

    def __init__(self) -> None:
        self.bak = H / "save.gates.bak"
        self.hash0 = ""
        self.violations: list[str] = []

    def backup(self) -> None:
        H.mkdir(parents=True, exist_ok=True)
        if SAVE.exists():
            shutil.copy2(SAVE, self.bak)
            self.hash0 = sha256(SAVE)

    def restore(self) -> None:
        if not self.bak.exists() or not self.hash0:
            return
        if not SAVE.exists() or sha256(SAVE) != self.hash0:
            shutil.copy2(self.bak, SAVE)
            if SAVE.exists() and sha256(SAVE) == self.hash0:
                self.violations.append("save.xml was modified; restored")

    def check(self, gate_id: str) -> None:
        """Restore the save if a gate rewrote it (silently).

        The DRIVER path resets the save to its post-tutorial baseline on every
        boot by design (`[save] driver baseline: users_default -> save.xml`),
        so this fires for every gate. Reporting it made the SAVE GUARD section
        26 identical lines of noise and hid real leaks; `restore()` at the end
        of the run still reports a save left modified.
        """
        if not self.hash0 or not SAVE.exists():
            return
        if sha256(SAVE) != self.hash0:
            shutil.copy2(self.bak, SAVE)


class RefWwwGuard:
    """Keep the reference tree byte-identical across a gate run."""

    def __init__(self) -> None:
        self.bak_dir = H / "refwww.bak"
        self.hashes: dict[str, str] = {}

    def backup(self) -> None:
        self.bak_dir.mkdir(parents=True, exist_ok=True)
        for path in REFWW:
            if not path.exists():
                continue
            self.hashes[str(path)] = sha256(path)
            shutil.copy2(path, self.bak_dir / path.name)

    def restore(self) -> list[str]:
        changed = []
        for path in REFWW:
            key = str(path)
            if key not in self.hashes:
                continue
            if not path.exists() or sha256(path) != self.hashes[key]:
                shutil.copy2(self.bak_dir / path.name, path)
                changed.append(path.name)
        return changed


class RunLock:
    """One runner at a time (never parallelise gates)."""

    def __init__(self) -> None:
        self.path = H / "lock"
        self._fh = None

    def __enter__(self):
        H.mkdir(parents=True, exist_ok=True)
        self._fh = open(self.path, "a+")
        if os.name == "nt":
            import msvcrt

            try:
                msvcrt.locking(self._fh.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError:
                raise SystemExit(
                    "gates: another runner holds .harness/lock - refusing to run "
                    "in parallel (RULE 0)"
                )
        self._fh.seek(0)
        self._fh.truncate()
        self._fh.write(f"{os.getpid()} {datetime.now().isoformat(timespec='seconds')}\n")
        self._fh.flush()
        return self

    def __exit__(self, *exc):
        try:
            if os.name == "nt":
                import msvcrt

                self._fh.seek(0)
                msvcrt.locking(self._fh.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
        try:
            self._fh.close()
        except Exception:
            pass
        # Remove the file too: the plugin's "is a run active?" check reads the
        # PID from it, and a leftover file with a dead PID must not look busy.
        try:
            self.path.unlink()
        except OSError:
            pass


# --------------------------------------------------------------------------
# Process execution
# --------------------------------------------------------------------------


@dataclass
class ProcResult:
    argv: list[str]
    rc: int | None
    seconds: float
    timed_out: bool
    out_path: Path
    err_path: Path

    @property
    def out(self) -> str:
        return self.out_path.read_text(encoding="utf-8", errors="replace")

    @property
    def err(self) -> str:
        return self.err_path.read_text(encoding="utf-8", errors="replace")


def run_proc(argv: list[str], sample_id: str, timeout_s: int,
             extra_env: dict[str, str] | None = None) -> ProcResult:
    """Run one argv (NO SHELL), hidden, with a hard timeout; kill our tree.

    `extra_env` layers probe-only switches on top of the inherited environment
    (e.g. `SF2_FORCE_DISARM=1` for the disarm gate)."""
    SAMPLES.mkdir(parents=True, exist_ok=True)
    out_path = SAMPLES / f"{sample_id}.out"
    err_path = SAMPLES / f"{sample_id}.err"
    started = time.time()
    env = None
    if extra_env:
        env = dict(os.environ)
        env.update(extra_env)
    with open(out_path, "w", encoding="utf-8", errors="replace") as fo, open(
        err_path, "w", encoding="utf-8", errors="replace"
    ) as fe:
        proc = subprocess.Popen(
            argv,
            cwd=str(ROOT),
            stdout=fo,
            stderr=fe,
            stdin=subprocess.DEVNULL,
            env=env,
            creationflags=CREATE_NO_WINDOW,
        )
        timed_out = False
        try:
            rc = proc.wait(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            timed_out = True
            kill_tree(proc.pid)
            try:
                rc = proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                rc = None
    return ProcResult(argv, rc, time.time() - started, timed_out, out_path, err_path)


# --------------------------------------------------------------------------
# Parsers: stdout -> (failure_count, detail).  Missing anchor raises.
# --------------------------------------------------------------------------


def _need(pattern: str, text: str, flags: int = re.M) -> re.Match:
    m = re.search(pattern, text, flags)
    if not m:
        raise AnchorMissing(pattern)
    return m


def p_fraction(prefix: str, text: str, label: str = "") -> tuple[int, str]:
    """`[tag] RESULT p/c` -> failures = c - p."""
    m = _need(r"\[%s\]\s*RESULT\s+(\d+)\s*/\s*(\d+)" % re.escape(prefix), text)
    p, c = int(m.group(1)), int(m.group(2))
    if c <= 0:
        raise AnchorMissing("empty total")
    return c - p, f"{p}/{c}{(' ' + label) if label else ''}"


def p_probes(text: str) -> tuple[int, str]:
    m = _need(r"\[verify\]\s*probes:\s*(\d+)\s*/\s*(\d+)\s+PASS", text)
    p, c = int(m.group(1)), int(m.group(2))
    return c - p, f"probes {p}/{c}"


def p_save_fields(text: str) -> tuple[int, str]:
    m = _need(r"\[save-fields\]\s*RESULT\s+(PASS|FAIL)", text)
    return (0 if m.group(1) == "PASS" else 1), m.group(1)


def p_sps(text: str) -> tuple[int, str]:
    """`[sps] RESULT PASS (0 fail)` -> the fail count."""
    m = _need(r"\[sps\]\s*RESULT\s+(PASS|FAIL)\s*\((\d+)\s*fail\)", text)
    return int(m.group(2)), f"{m.group(1)} ({m.group(2)} fail)"


def p_tape(text: str) -> tuple[int, str]:
    """`--input-tape`: count `[tape] ... FAIL`; require the tape to have run."""
    lines = re.findall(r"^\[tape\].*$", text, re.M)
    if not lines:
        raise AnchorMissing("[tape] lines")
    fails = sum(1 for ln in lines if re.search(r"\bFAIL\b", ln))
    return fails, f"{len(lines) - fails}/{len(lines)} tape checks"


def p_tour_steps(tag: str, expected: int):
    """`[tag] ALL N STEPS DONE` + count `FAIL` in the tag's lines."""

    def parser(text: str) -> tuple[int, str]:
        m = re.search(r"\[%s\]\s*ALL\s+(\d+)\s+STEPS DONE" % re.escape(tag), text)
        if not m:
            raise AnchorMissing(f"[{tag}] ALL N STEPS DONE")
        total = int(m.group(1))
        lines = re.findall(r"^\[%s\].*$" % re.escape(tag), text, re.M)
        fails = sum(1 for ln in lines if re.search(r"\bFAIL\b", ln))
        missing = 0 if total == expected else 1
        return fails + missing, f"{total}/{expected} steps, {fails} FAIL"

    return parser


def p_passfail_lines(tag: str, min_lines: int = 1):
    """Count `FAIL` in lines of one tag; require the tag to appear."""

    def parser(text: str) -> tuple[int, str]:
        lines = re.findall(r"^\[%s\].*$" % re.escape(tag), text, re.M)
        if len(lines) < min_lines:
            raise AnchorMissing(f"[{tag}] lines")
        fails = sum(1 for ln in lines if re.search(r"\bFAIL\b", ln))
        return fails, f"{len(lines) - fails}/{len(lines)} {tag} checks"

    return parser


def p_bool(prefix: str, text: str) -> tuple[int, str]:
    """Last `[tag] RESULT PASS|FAIL` -> 0/1."""
    hits = re.findall(
        r"\[%s\][^\n]*?\b(PASS|FAIL)\b" % re.escape(prefix), text
    )
    if not hits:
        raise AnchorMissing(f"[{prefix}] PASS/FAIL")
    return (0 if hits[-1] == "PASS" else 1), hits[-1]


def p_boss_hit(text: str) -> tuple[int, str]:
    hits = re.findall(r"\b(HIT|MISS)\b", text)
    if not hits:
        raise AnchorMissing("HIT/MISS")
    return (0 if "HIT" in hits else 1), ("HIT" if "HIT" in hits else "MISS")


def p_disarm_ai(text: str) -> tuple[int, str]:
    """`SF2_FORCE_DISARM=1` + `SF2_TRACE_MOVELIST=1`: the AI must never start a
    move that is not in the fighter's own (weapon-filtered) list after the drop.

    JS `de.V1` (L601-602) `!this.model.me.includes(a)` -> the candidate is
    rejected; a start of a foreign-weapon move is the reported "the dropped
    weapon's animations still play". The run must PROVE it disarmed (an
    `after drop` line) or the gate is vacuous.
    """
    drops = re.findall(r"^\[movelist\].*after drop.*$", text, re.M)
    if not drops:
        raise AnchorMissing("[movelist] ... after drop")
    leaks = re.findall(r"^\[movelist\] AI start .* NOT in hb_ .*$", text, re.M)
    return len(leaks), f"{len(drops)} drop(s), {len(leaks)} foreign-move start(s)"


def p_banner_round(text: str) -> tuple[int, str]:
    """`SF2_TRACE_BANNER=1`: the round plate/number geometry must match the JS
    `Cr.init` (L84302-84321): the digit sits OUTSIDE the plate, 0.25a to the
    right of the art's right edge (`round.x = b.N - 0.25a`, both nodes shifted
    by -a/2). A number drawn over/before the art (the old port) fails.
    """
    rows = re.findall(
        r"\[banner\] kind=round art_l=(-?[\d.]+) art_r=(-?[\d.]+) "
        r"art_t=(-?[\d.]+) a=(-?[\d.]+) num_x=(-?[\d.]+) num_y=(-?[\d.]+)",
        text,
    )
    if not rows:
        raise AnchorMissing("[banner] kind=round geometry")
    bad = 0
    for art_l, art_r, art_t, a, num_x, num_y in rows:
        art_l, art_r, a = float(art_l), float(art_r), float(a)
        num_x, num_y = float(num_x), float(num_y)
        gap = num_x - art_r                      # must be 0.25a, outside
        if not (0.20 * a <= gap <= 0.30 * a):
            bad += 1
        if num_x <= art_r:                       # never over the art
            bad += 1
    return bad, f"{len(rows)} round banner(s), gap ok={len(rows) - bad}"


def p_style_player_only(text: str) -> tuple[int, str]:
    """JS `z3` (L17296 `if (a.model == 0) this.fe.Dsb(...)`) + `Jh.Gua` ->
    `Fh.b6`: the style PRIZE / achievement level is the PLAYER's own meter.
    The gate requires the enemy to have out-styled the player (so a max-of-both
    implementation differs) and the results' `style=` to equal the PLAYER's
    best level.
    """
    pm = [int(x) for x in re.findall(r"\[style\] P lvl=(\d+)", text)]
    em = [int(x) for x in re.findall(r"\[style\] P lvl=\d+.*?E lvl=(\d+)", text)]
    pr = re.findall(r"\[fight\] prize:.*?\bstyle=(\d+)", text)
    if not pm or not em or not pr:
        raise AnchorMissing("[style] P/E lvl + [fight] prize style=")
    pmax, emax, pstyle = max(pm), max(em), int(pr[-1])
    ok = (pstyle == pmax) and (emax > pmax)
    return (0 if ok else 1), (
        "player=%d enemy=%d prize=%d%s"
        % (pmax, emax, pstyle, "" if emax > pmax else " (enemy never ahead: weak)"))


def p_enemy_kit(text: str) -> tuple[int, str]:
    """A stage `<Warrior>` whose kit comes from `<Groups>` must be resolved to a
    group MEMBER (JS `efa` L7360 / `EQ` L50237): the member carries the
    `<Template>` + `<Items>`. Without it every ordinary fight (Duel/Stranger/
    hidden ambushes) spawned an item-less enemy - no weapon, and with the
    shipped `UnarmedDamage="0"` it dealt ZERO damage (the "enemies barely
    attack" report). The gate requires the Duel enemy to ship a kit AND a
    weapon.
    """
    items = re.findall(r"\[fight\] enemy items: (\d+)", text)
    wield = re.findall(r"\[fight\] wield: player='[^']*' enemy='([^']*)'", text)
    if not items or not wield:
        raise AnchorMissing("[fight] enemy items / wield")
    n = int(items[-1])
    w = wield[-1]
    ok = n >= 1 and w not in ("", "Fists")
    return (0 if ok else 1), "items=%d weapon='%s'" % (n, w)


def p_cloth_hangs(text: str) -> tuple[int, str]:
    """`SF2_TRACE_CLOTH=1` over the fidelity tour: the CLOTH edges of the
    shop/profile `Pi` model must stay near their rest length. The JS
    mass-weighted relax gives the movable side only its own mass share, so with
    the shipped masses (macro 0.00112 vs cloth 0.1) the cloth node free-fell
    away from its macro (the reported "the bandage rises and wobbles, the mesh
    stretches"): the worst cloth edge reached +181 over a rest length of 16.
    Skips the first 30 samples (the state inherited at entry) and bounds the
    steady state.
    """
    rows = re.findall(
        r"\[cloth\] edges=\d+ worst1=(\S+) d=\d+ rest=\d+ \(\+([\d.]+)\)", text)
    if len(rows) < 40:
        raise AnchorMissing("[cloth] edge lines")
    # Only the first 1-2 samples are the entry transient (the JS `Pi.Ex` runs
    # one solver step before the first draw, so it is small now: +13 vs +81
    # before the entry solve).
    worst = max((float(x), n) for n, x in rows[2:])
    return (0 if worst[0] <= 80.0 else 1), (
        "worst cloth edge after warmup +%.1f (%s)" % worst)


def p_kick_ko(text: str) -> tuple[int, str]:
    """`--kick-ko` ends with `[result] WIN` (no RESULT line)."""
    m = _need(r"^\[result\]\s+(WIN|LOSE|DRAW)\s*$", text)
    return (0 if m.group(1) == "WIN" else 1), m.group(1)


def p_evade_ib(text: str) -> tuple[int, str]:
    """`--evade-probe` + `SF2_TRACE_AI_QUICK`: how often the AI's throw-evasion
    (`<EvadeChance Animation="Throw">` -> `IB` -> `fk=9`) actually fires."""
    n = len(re.findall(r"^\[ib\] slot=\d+ names=\d+ -> IB=1$", text, re.M))
    return (0 if n >= 5 else 5 - n), "%d IB firings (need >= 5)" % n


def p_kv_result(tag: str):
    """`[tag] RESULT k=PASS ... -> PASS` (n/a = not part of this run)."""

    def parser(text: str) -> tuple[int, str]:
        m = _need(
            r"^\[%s\]\s*RESULT\s+(.*?)\s*->\s*(PASS|FAIL)\s*$" % re.escape(tag),
            text,
        )
        body, verdict = m.group(1), m.group(2)
        fails = len(re.findall(r"=FAIL\b", body))
        passes = len(re.findall(r"=PASS\b", body))
        return fails + (0 if verdict == "PASS" else 1), (
            "%d pass, %d fail, -> %s" % (passes, fails, verdict)
        )

    return parser


def p_arrow_result(tag: str):
    """`[tag] ... -> PASS` (last arrow verdict on the tag's lines)."""

    def parser(text: str) -> tuple[int, str]:
        lines = re.findall(r"^\[%s\].*$" % re.escape(tag), text, re.M)
        if not lines:
            raise AnchorMissing(f"[{tag}] lines")
        hits = re.findall(r"->\s*(PASS|FAIL)\s*$", lines[-1])
        if not hits:
            raise AnchorMissing(f"[{tag}] final verdict")
        return (0 if hits[-1] == "PASS" else 1), hits[-1]

    return parser


def p_dialog(text: str) -> tuple[int, str]:
    """`--dialog-verify`: `[dlgverify] PASS parse census (...)`."""
    m = _need(r"^\[dlgverify\]\s+(PASS|FAIL)\s+parse census", text)
    return (0 if m.group(1) == "PASS" else 1), "parse census " + m.group(1)


def p_subframe_over(text: str) -> tuple[int, str]:
    """Mismatched `sub`/`subn` frames after normalising the two encodings.

    `compare_pose.py` prints `total frames with subframe mismatch: N/M`. The
    field was emitted by both traces and compared by nothing, so a port playing
    the wrong play-buffer slot could not fail a gate.
    """
    pat = re.compile(r"total frames with subframe mismatch:\s*(\d+)/(\d+)")
    m = pat.search(text)
    if not m:
        raise AnchorMissing("total frames with subframe mismatch: N/M")
    return int(m.group(1)), "%s subframe-mismatch frames (of %s)" % (
        m.group(1), m.group(2))


def p_audio_total(text: str) -> tuple[int, str]:
    """compare_audio.py: per-frame audio divergences + whole-run event totals.

    The audio differential was fully implemented (`compare_audio.py`, the
    oracle's `{"t":"audio"}` line, `--dump-audio`) and NEVER wired: no gate
    anywhere mentioned audio, so a missing/extra SFX, a wrong music track or a
    bus-state divergence could not fail the build.
    """
    per = int(_need(r"total per-frame divergences:\s*(\d+)", text).group(1))
    cnt = int(_need(r"events with total-count mismatch:\s*(\d+)", text).group(1))
    return per + cnt, "per-frame=%d event-count=%d" % (per, cnt)


def p_pos_drift(text: str) -> tuple[int, str]:
    """Frames whose world-position offset leaves the scenario's constant.

    `compare_state.py` prints
      `  frames whose offset leaves that constant by more than N units: A/B`
    The absolute x/y offset is a CONSTANT per scenario (measured: -0.6 on
    boss-lynx, +78.98 on titan-ladder, and identical for both fighters), i.e. the
    pair is placed at a different arena origin - `fighters.sep_x` matches 0/368.
    What must NOT happen is that offset CHANGING inside a run, which would be a
    real position divergence. Gating `x`/`y` absolutely would just encode the
    origin; gating this residual detects the divergence.
    """
    pat = re.compile(
        r"frames whose offset leaves that constant by more than [\d.]+ units:"
        r"\s*(\d+)/(\d+)")
    m = pat.search(text)
    if not m:
        raise AnchorMissing("frames whose offset leaves that constant: A/B")
    return int(m.group(1)), "%s frames drift from the constant offset (of %s)" % (
        m.group(1), m.group(2))


def p_coverage_short(text: str) -> tuple[int, str]:
    """Alignment coverage as a FAILURE count.

    `compare_state.py` pairs frames by `(phase, move, af)`; frames that fail the
    key are dropped as "unaligned", so a port playing the WRONG move would still
    report 0 divergences. The gate must see that, otherwise a coverage collapse
    is invisible. Metric = 100 - coverage in per-mille, so 0 means perfect
    coverage and a regression is a positive number.
    """
    pat = re.compile(r"coverage \(matched/native\):\s*([0-9.]+)%")
    m = pat.search(text)
    if not m:
        raise AnchorMissing("coverage (matched/native): N%")
    pct = float(m.group(1))
    return int(round((100.0 - pct) * 10)), "%.1f%% of native frames aligned" % pct


def p_pose_over(text: str) -> tuple[int, str]:
    """Pose gate: the number of matched frames with ANY bone over tolerance.

    `compare_pose.py` prints one line per fighter:
      `  Me    mean .. max .. | frames with any bone over tolerance: 417/568`
    The metric is the SUM over both fighters, so a regression on either side
    moves it. Cloth/chain (`BODY-Node*`, `HEAD-Node*`) and the Mass=0 `COM`
    node are INCLUDED on purpose (they are the only bones that deviate today).
    """
    pat = re.compile(r"frames with any bone over tolerance:\s*(\d+)/(\d+)")
    hits = pat.findall(text)
    if not hits:
        raise AnchorMissing("frames with any bone over tolerance: N/M")
    total = sum(int(a) for a, _ in hits)
    frames = max(int(b) for _, b in hits)
    return total, f"{total} over-tolerance frames (of {frames})"


def p_pose_vel_over(text: str) -> tuple[int, str]:
    """Velocity gate: matched frames with ANY bone's true velocity over tolerance.

    `compare_pose.py` prints a single canonical total:
      `  total frames with any bone velocity over tolerance: N/M`
    It is a separate metric from the position deltas on purpose: the traces
    carry the real solver velocity (`ma - mf`), and a velocity-class regression
    would stay invisible in positions until the chain had already moved.
    """
    pat = re.compile(r"total frames with any bone velocity over tolerance:\s*(\d+)/(\d+)")
    m = pat.search(text)
    if not m:
        raise AnchorMissing("total frames with any bone velocity over tolerance: N/M")
    return int(m.group(1)), "%s over-tolerance velocity frames (of %s)" % (m.group(1), m.group(2))


def p_total_divergences(text: str) -> tuple[int, str]:
    m = _need(r"total divergences:\s*(\d+)", text)
    n = int(m.group(1))
    return n, f"{n} divergences"


def p_equip_fight(text: str) -> tuple[int, str]:
    """The PLAYER's stance/move list must follow the equipped Weapon slot.

    `--fight --loadout SAVE --equip-probe WEAPON_KNIVES` equips knives in the
    save and boots the real save-resolution path, so the reported
    "купил ножи, в бою остались кулаки" is caught: the move-list subtype must
    become `Knives` (JS `ra.Hza` L684-685 + `Fd` L808) and the knives stance
    must be in the list.
    """
    m = _need(r"player equip subtype='([^']*)'", text)
    if m.group(1) != "Knives":
        return 1, "player move-list subtype='%s' (expected Knives)" % m.group(1)
    if "KnivesStartStance" not in text:
        return 1, "no KnivesStartStance in the move list (fists kept)"
    n = re.search(r"player move list \((\d+) moves\)", text)
    return 0, "subtype=Knives, KnivesStartStance present, %s moves" % (
        n.group(1) if n else "?")


def p_end_stance_win(text: str) -> tuple[int, str]:
    """The winner must START its `Win_<weapon>` EndStance move.

    `--kick-ko` fights to a KO, so the phase-3 EndStance pick must resolve for
    the winner. Measured BEFORE the retry fix: ZERO `[stance] ... EndStance
    move=` lines in the whole harness log set (the one-shot latch marked the
    pick as done before it ran, and the winner's killing attack still held
    `<CurrentInterval Uninterrupt/>`, which rejects every `Win_*`).
    """
    wins = re.findall(r"\[stance\] F\d+ (\S+) EndStance move=(Win_\w+)", text)
    if not wins:
        return 1, "no 'EndStance move=Win_*' line (winner never played)"
    return 0, "%d win stance(s): %s" % (
        len(wins),
        ",".join(sorted({n for _, n in wins})),
    )


def p_ui_total(text: str) -> tuple[int, str]:
    """compare_ui.py: depth mismatch + per-entry mismatch + oracle-only."""
    dep = int(_need(r"matched transitions with depth mismatch:\s*(\d+)", text).group(1))
    st = int(
        _need(
            r"matched transitions with per-entry \(id,state,active\) mismatch:\s*(\d+)",
            text,
        ).group(1)
    )
    oo = int(_need(r"oracle-only:\s*(\d+)", text).group(1))
    return dep + st + oo, f"depth={dep} state={st} oracle-only={oo}"


PARSERS = {
    "probes": p_probes,
    "savefields": p_save_fields,
    "sps": p_sps,
    "tape": p_tape,
    "boss_hit": p_boss_hit,
    "kick_ko": p_kick_ko,
    "end_stance_win": p_end_stance_win,
    "equip_fight": p_equip_fight,
    "cloth_hangs": p_cloth_hangs,
    "enemy_kit": p_enemy_kit,
    "banner_round": p_banner_round,
    "style_player_only": p_style_player_only,
    "disarm_ai": p_disarm_ai,
    "dialog": p_dialog,
    "total_div": p_total_divergences,
        "cov_short": p_coverage_short,
    "ui_total": p_ui_total,
    # `stages.xml` L819-882: the Survival fight's `<Attributes Eclipse="1"
    # DamageFactor="-4000" ApplyTo="Player"/>` / `4000 ApplyTo="Bot"` must not
    # turn the player into a one-hit killer (reported bug).
    "survhit": p_kv_result("survhit"),
    "evade_ib": p_evade_ib,
    # The boss perks: the shield's ModAttributes (the measured 2^-1.6 damage
    # nerf) and the invisibility action (the model stops being drawn).
    "invis": p_kv_result("invis"),
    "shieldperk": p_kv_result("shield"),
    "roster": p_kv_result("roster"),
    "maplaunch": p_kv_result("maplaunch"),
}


# --------------------------------------------------------------------------
# Gate registry
# --------------------------------------------------------------------------


@dataclass
class Gate:
    id: str
    kind: str  # "gate" | "diff"
    tags: tuple[str, ...]  # subset of ("fast", "full")
    flag: str = ""  # CLI flag for kind="gate"
    watch: int = 300  # --watchdog seconds
    timeout_s: int = 0  # outer timeout (0 = watch + margin)
    parser: str = ""  # key into PARSERS
    scenario: str = ""  # kind="diff"
    note: str = ""
    env: dict[str, str] = field(default_factory=dict)  # probe-only switches
    args: tuple[str, ...] = ()  # extra argv AFTER the flag (e.g. a frame)

    def outer_timeout(self) -> int:
        return self.timeout_s or (self.watch + 120)


FLAG_GATES: list[Gate] = [
    Gate("verify-input", "gate", ("fast", "full"), "--verify-input", 180, parser="probes"),
    Gate("flow-verify", "gate", ("fast", "full"), "--flow-verify", 300, parser="flow"),
    Gate("save-fields-probe", "gate", ("fast", "full"), "--save-fields-probe", 180, parser="savefields"),
    Gate("settings-profile-shop-probe", "gate", ("fast", "full"), "--settings-profile-shop-probe", 300, parser="sps"),
    Gate("input-tape", "gate", ("full",), "--input-tape", 180, parser="tape"),
    Gate("ui-tour", "gate", ("full",), "--ui-tour", 600, parser="tour:15"),
    Gate("fidelity-tour", "gate", ("full",), "--fidelity-tour", 600, parser="fidelity:41"),
    Gate("headless-loop", "gate", ("full",), "--headless-loop", 700, parser="loop:13"),
    Gate("boss-hit-probe", "gate", ("full",), "--boss-hit-probe", 300, parser="boss_hit"),
    # Survival: one clean punch must not kill (measured -6.2% of max per hit,
    # enemy at 75% after four).
    Gate("survival-hit", "gate", ("fast", "full"), "--survival-hit-probe", 180,
         parser="survhit"),
    # The AI's throw evasion (`fk=9`): the evade slot only resolves when the
    # candidate's `<Distance Max="100">` passes, so the probe holds the gap at
    # 60 for the whole fight.
    Gate("evade-ib", "gate", ("full",), "--evade-probe", 300, parser="evade_ib",
         env={"SF2_TRACE_AI_QUICK": "1"}),
    # `PERK_INVISIBILITY` -> `ModInvisibility`: the perked model must stop being
    # DRAWN (the A/B capture at the same rendered frame loses 15369 px).
    Gate("boss-invis", "gate", ("full",), "--invis-probe", 300, parser="invis"),
    # `PERK_BOSS_WIDOW_SHIELDING` -> `ModAttributes DamageFactor=-16000`:
    # a punch must drop to 2^-1.6 = 0.3299 of its damage.
    Gate("boss-shield", "gate", ("full",), "--shield-probe", 300, parser="shieldperk"),
    # The boss-intro roster (`jk`) must SCROLL so the selected entry is centred
    # (`bQ.C(scrollX)` + `i*g*.8`): swept over every entry of C3_BOSS_TITAN.
    Gate("boss-roster-scroll", "gate", ("full",), "--roster-probe", 300,
         parser="roster", env={"SF2_ROSTER_BATTLE": "C3_BOSS_TITAN"}),
    # The Titan ladder (`Type=FINAL_BATTLE_TITAN`) must arm the `jk` roster
    # (`ca.hCa` L17224 includes `FightFinalTitan`) and launch fight 1 in the
    # FIGHT's location (`skyport`), not the battle's (`neural_network`).
    Gate("titan-roster-launch", "gate", ("full",), "--map-launch-probe", 300,
         parser="maplaunch",
         env={"SF2_MAP_LAUNCH": "C3_BOSS_TITAN", "SF2_ML_FRAMES": "420"}),
    Gate("kick-ko", "gate", ("full",), "--kick-ko", 300, parser="kick_ko"),
    # The winner's EndStance `Win_<weapon>` animation (bug Q): `--kick-ko`
    # fights to a KO, so the phase-3 pick must resolve and start the win clip.
    # RED before the retry fix (0 `[stance] ... EndStance move=` lines).
    Gate("end-stance-win", "gate", ("full",), "--kick-ko", 300,
         parser="end_stance_win"),
    # The cloth edges of the preview model must stay near their rest
    # length (the bandage/leg mesh stretch).
    Gate("cloth-hangs", "gate", ("full",), "--fidelity-tour", 600,
         parser="cloth_hangs", env={"SF2_TRACE_CLOTH": "1"}),
    # JS `de.V1` (L601-602) `!model.me.includes(a)`: after a DISARM the AI must
    # never resolve/start a foreign weapon's move. `SF2_FORCE_DISARM=1` makes
    # the drop deterministic (fight.cpp `[probe, authorised]`).
    Gate("disarm-ai", "gate", ("full",), "--kick-ko", 300, parser="disarm_ai",
         env={"SF2_FORCE_DISARM": "1", "SF2_TRACE_MOVELIST": "1"}),
    # JS `Cr.init` (L84302-84321): the round digit sits OUTSIDE the plate,
    # 0.25a to the right (`round.x = b.N - 0.25a` after both the art and
    # the number shift by -a/2). The banner shows at the round start; the
    # idle capture at fight frame 250 lands on it deterministically.
    # JS `z3` L17296: only model 0 (the PLAYER) feeds the style prize /
    # achievement counter; the enemy must not unlock "aggressor".
    # JS `efa` L7360: a `<Groups>` warrior resolves to a member WITH a kit.
    Gate("enemy-kit", "gate", ("full",), "--fight", 300, parser="enemy_kit",
         args=("--battle", "Duel", "--zone", "ZONE_1", "--fight-ordinal", "0",
               "--location", "mountain", "--headless", "60")),
    # The PLAYER's stance/move list must follow the equipped Weapon slot:
    # buy knives -> equip -> fight. `--loadout SAVE` = the real app resolution
    # path (the save's six equipped slots); `--equip-probe` writes the weapon
    # after the driver baseline.
    Gate("equip-fight", "gate", ("full",), "--fight", 300, parser="equip_fight",
         args=("--loadout", "SAVE", "--equip-probe", "WEAPON_KNIVES",
               "--headless", "40")),
    Gate("style-player-only", "gate", ("full",), "--kick-ko", 300,
         parser="style_player_only"),
    Gate("banner-round", "gate", ("full",), "--capture-idle-fight-at", 300,
         parser="banner_round", env={"SF2_TRACE_BANNER": "1"},
         args=("250",)),
    Gate("quest-verify", "gate", ("full",), "--quest-verify", 300, parser="kv:qverify"),
    # The FULL fresh tutorial chain (no seed): training fight -> the knives
    # Shop quest -> the Map (the reported "the quests stop after the
    # knives"), driven with real injected clicks.
    Gate("tutorial-chain-verify", "gate", ("full",), "--tutorial-chain-verify", 400,
         parser="kv:qverify"),
    Gate("quest-verify-buy", "gate", ("full",), "--quest-verify-buy", 300, parser="kv:qverify"),
    Gate("dialog-verify", "gate", ("full",), "--dialog-verify", 300, parser="dialog"),
    Gate("tutorial-real-verify", "gate", ("full",), "--tutorial-real-verify", 400, parser="kv:tutreal2"),
    Gate("doublesweep-verify", "gate", ("full",), "--doublesweep-verify", 180, parser="arrow:dsverify"),
    Gate("enemy-move-audit", "gate", ("full",), "--enemy-move-audit", 180, parser="arrow:emaudit"),
]

# Scenario recipes: reference/traces/README.md:187-214 verbatim for the five
# documented ones; `dojo-move` is the shared-input-script scenario
# (reference/tools/dojo_move.txt: hold D 3 s / A 3 s / jump / punch).
SCENARIOS: dict[str, dict] = {
    "dojo-demo": dict(
        oracle=["--query", "postTutorial=0",
                "--input-script", "reference/tools/state_dojo_demo.txt",
                "--timeout-ms", "40000"],
        native=["--fight", "--headless", "640", "--dump-state", "400", "--watchdog", "900"],
        timeout_s=300,
    ),
    "boss-lynx": dict(
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000",
                "--input-script", "reference/tools/state_boss_lynx.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "BOSS_LYNX", "--zone", "ZONE_1",
                "--fight-ordinal", "0", "--location", "moon",
                "--headless", "700", "--dump-state", "600",
                "--dump-pose", "600", "--watchdog", "900"],
        # [coverage] The pose gate used to exist only for the two dojo scenarios
        # and kusarigama, so the player's cloth/weapon geometry was UNGATED in
        # every campaign fight (hits, knockdowns, ragdoll). BOSS_LYNX is a FIXED
        # opponent with 100% state coverage (368/368 aligned frames, 0 state
        # divergences), i.e. the cleanest campaign case to gate first.
        pose=True,
        timeout_s=420,
    ),
    "tournament": dict(
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000&battle=Tournament",
                "--input-script", "reference/tools/state_tournament.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "Tournament", "--zone", "ZONE_1",
                "--fight-ordinal", "0", "--location", "arena",
                "--headless", "700", "--dump-state", "600",
                "--dump-pose", "600", "--watchdog", "900"],
        pose=True,
        timeout_s=420,
    ),
    "survival": dict(
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000&battle=Survival",
                "--input-script", "reference/tools/state_survival.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "Survival", "--zone", "ZONE_1",
                "--fight-ordinal", "0", "--location", "bamboo_grove",
                "--headless", "700", "--dump-state", "600",
                "--dump-pose", "600", "--watchdog", "900"],
        pose=True,
        timeout_s=420,
    ),
    # KUSARIGAMA: the same fight on both sides with the weapon equipped, so the
    # weapon's cloth chain (`mdl_weapon_kusarigama`: 11 `Cloth="1"` nodes) is
    # diffed against the oracle's own render of the same JS, not against a
    # hand-held live capture. Oracle: `?equip=WEAPON_KUSARIGAMA` patches the
    # seeded warrior (`reference/runner/index.html`); native: the real save path
    # (`--loadout SAVE --equip-probe`).
    "kusarigama": dict(
        # BOSS_LYNX (a FIXED opponent): the tournament's `<Groups>` pool picks
        # the opponent from the shared RNG stream, and the seed patch that adds
        # `?equip=` shifts it, so the two sides fought DIFFERENT warriors
        # (measured: oracle axe/throw enemy vs port DoubleSweep enemy). A fixed
        # boss keeps the enemy identical and isolates the player's weapon.
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000"
                "&battle=BOSS_LYNX&equip=WEAPON_KUSARIGAMA",
                "--input-script", "reference/tools/state_boss_lynx.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "BOSS_LYNX", "--zone", "ZONE_1",
                "--fight-ordinal", "0", "--location", "moon",
                "--loadout", "SAVE", "--equip-probe", "WEAPON_KUSARIGAMA",
                "--headless", "700", "--dump-state", "600", "--dump-pose", "600", "--watchdog", "900"],
        pose=True,
        timeout_s=420,
    ),
    # C3_BOSS_TITAN fight 1: the ladder whose `<Fight Location="skyport">`
    # overrides the battle's `neural_network` (JS `IIa` L7942-7943).
    "titan-ladder": dict(
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000"
                "&battle=C3_BOSS_TITAN&battleZone=ZONE_7",
                "--input-script", "reference/tools/state_tournament.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "C3_BOSS_TITAN", "--zone", "ZONE_7",
                "--fight-ordinal", "0", "--location", "skyport",
                "--headless", "700", "--dump-state", "600",
                "--dump-pose", "600", "--watchdog", "900"],
        pose=True,
        timeout_s=420,
    ),
    "challenge": dict(
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000&battle=Challenge&battleZone=ZONE_2",
                "--input-script", "reference/tools/state_challenge.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "Challenge", "--zone", "ZONE_2",
                "--fight-ordinal", "0", "--location", "autumn",
                "--headless", "700", "--dump-state", "600",
                "--dump-pose", "600", "--watchdog", "900"],
        pose=True,
        timeout_s=420,
    ),
    "dojo-move": dict(
        oracle=["--query", "postTutorial=0&maxFrames=1200",
                "--input-script", "reference/tools/dojo_move.txt",
                "--timeout-ms", "90000"],
        native=["--replay", "reference/tools/dojo_move.txt",
                "--headless", "900", "--dump-state", "900", "--dump-pose", "900",
                "--watchdog", "900"],
        timeout_s=420,
        pose=True,
    ),
}

# Every scenario is gated. `kusarigama` joined once its root cause was fixed:
# the intro plate is raised only when BOTH fighters finished an `EndsStage="1"`
# move (JS `kg` L15777 -> `wd.OCa`), so the FIGHT plate now restarts at F178 and
# the round goes live at F248 — the oracle's own timing (+130 trace offset).
# That took the diff from 1394 to a stable 159 (the remainder starts at
# nat f=301 / ora f=431, a 2.4-unit sep_x drift and the hit that follows it).
UNGATED_SCENARIOS: set[str] = set()

DIFF_GATES: list[Gate] = [
    Gate(f"diff:{name}", "diff", ("full",), scenario=name,
         timeout_s=SCENARIOS[name]["timeout_s"], parser="total_div")
    for name in SCENARIOS if name not in UNGATED_SCENARIOS
]


# --------------------------------------------------------------------------
# Gate execution
# --------------------------------------------------------------------------


@dataclass
class GateResult:
    gate_id: str
    kind: str
    metric: int | None = None
    detail: str = ""
    seconds: float = 0.0
    verdict: str = ""
    baseline: int | None = None
    error: str = ""
    extra: dict = field(default_factory=dict)

    @property
    def failed(self) -> bool:
        return self.verdict in ("REGRESSION", "FAIL", "TIMEOUT", "NO-BASELINE", "LIE")


def parse_metric(parser: str, text: str) -> tuple[int, str]:
    """Resolve a parser key to a function and run it."""
    if parser in PARSERS:
        return PARSERS[parser](text)
    if parser.startswith("bool:"):
        return p_bool(parser.split(":", 1)[1], text)
    if parser.startswith("kv:"):
        return p_kv_result(parser.split(":", 1)[1])(text)
    if parser.startswith("arrow:"):
        return p_arrow_result(parser.split(":", 1)[1])(text)
    if parser == "flow":
        return p_fraction("flowverify", text)
    if parser.startswith("tour:"):
        return p_tour_steps("tour", int(parser.split(":", 1)[1]))(text)
    if parser.startswith("fidelity:"):
        return p_tour_steps("fidelity", int(parser.split(":", 1)[1]))(text)
    if parser.startswith("loop:"):
        return p_tour_steps("loop", int(parser.split(":", 1)[1]))(text)
    raise AnchorMissing(f"unknown parser {parser}")


def build_gate(force: bool) -> GateResult:
    """Build the native exe: cmake first (documented), MSBuild fallback."""
    started = time.time()
    newest = newest_mtime(["core", "app"])
    if not force and GAME.exists() and GAME.stat().st_mtime >= newest:
        return GateResult(
            "build", "build", 0,
            f"up-to-date (exe newer than every core/app source)", time.time() - started,
        )
    recipe = ""
    result = run_proc(
        ["cmake", "--build", str(ROOT / "build"), "--config", "Release"],
        "build-cmake", 900,
    )
    if result.rc == 0 and GAME.exists():
        recipe = f"cmake ({result.seconds:.1f}s)"
    else:
        if not MSBUILD.exists():
            return GateResult("build", "build", None, verdict="ERROR",
                              error=f"no MSBuild at {MSBUILD}")
        result = run_proc(
            [str(MSBUILD), str(SLN), "/p:Configuration=Release", "/m", "/nologo"],
            "build-msbuild", 1200,
        )
        if result.rc != 0 or not GAME.exists():
            tail = "\n".join(result.out.strip().splitlines()[-6:])
            return GateResult("build", "build", 1, verdict="FAIL",
                              error=f"build failed (rc={result.rc})\n{tail}")
        recipe = f"msbuild ({result.seconds:.1f}s)"
    # The OracleShell must not be older than its sources (it is not built here:
    # the .NET shell is built by `dotnet build shell`).
    stale = [
        p.name for p in (ROOT / "shell").glob("*.cs")
        if ORACLE.exists() and p.stat().st_mtime > ORACLE.stat().st_mtime
    ]
    note = "" if not stale else f"; WARNING stale OracleShell ({', '.join(stale)})"
    return GateResult("build", "build", 0, recipe + note, time.time() - started)


def run_flag_gate(gate: Gate) -> GateResult:
    argv = [str(GAME), gate.flag, *gate.args, "--watchdog", str(gate.watch),
            RES, SAVE_REL]
    res = run_proc(argv, gate.id, gate.outer_timeout(), gate.env or None)
    started = res.seconds
    if res.timed_out:
        return GateResult(gate.id, "gate", None, verdict="TIMEOUT",
                          error=f"timeout after {gate.outer_timeout()}s",
                          seconds=started)
    try:
        metric, detail = parse_metric(gate.parser, res.out)
    except AnchorMissing as exc:
        tail = "\n".join(res.out.strip().splitlines()[-8:])
        return GateResult(gate.id, "gate", None, verdict="FAIL",
                          error=f"anchor missing: {exc}\n--- stdout tail ---\n{tail}",
                          seconds=started)
    return GateResult(gate.id, "gate", metric, detail, started)


def run_diff_gate(gate: Gate) -> GateResult:
    """Oracle run -> extract -> native run -> compare (state + UI stack)."""
    scenario = gate.scenario
    cfg = SCENARIOS[scenario]
    art = ARTIFACTS / scenario
    if art.exists():
        shutil.rmtree(art)
    art.mkdir(parents=True, exist_ok=True)
    started = time.time()

    # 1. Oracle side (the capture file accumulates: start from an empty log).
    #    The hub -> Map -> battle navigation is WALL-CLOCK (scripted taps), so
    #    a cold WebView2 boot can make a tap land early and the fight never
    #    start. That is navigation jitter, not a behavioural result: retry a
    #    bounded number of times and RECORD the attempt count. A scenario that
    #    never produces a trace still fails (never silently "OK").
    console = ROOT / "reference" / "traces" / "console.log"
    console.parent.mkdir(parents=True, exist_ok=True)
    oracle_state = ROOT / "reference" / "traces" / "oracle_state.jsonl"
    attempts = 0
    while attempts < ORACLE_ATTEMPTS:
        attempts += 1
        suffix = "" if attempts == 1 else f"-try{attempts}"
        console.write_text("", encoding="utf-8")
        run_proc([str(ORACLE)] + cfg["oracle"], f"{scenario}-oracle{suffix}",
                 cfg["timeout_s"])
        shutil.copy2(console, art / f"oracle_console{suffix}.log")
        ex = run_proc([PY, "reference/tools/extract_state.py"],
                      f"{scenario}-extract{suffix}", 120)
        if oracle_state.exists() and oracle_state.stat().st_size > 0:
            break
    if not oracle_state.exists() or oracle_state.stat().st_size == 0:
        return GateResult(gate.id, "diff", None, verdict="ERROR",
                          error="oracle produced no state trace after %d attempt(s) "
                                "(see .harness/artifacts/%s/oracle_console*.log)"
                                % (attempts, scenario),
                          seconds=time.time() - started)
    shutil.copy2(oracle_state, art / "oracle_state.jsonl")
    run_proc([PY, "reference/tools/extract_layer.py", "audio"],
             f"{scenario}-extract-audio", 120)
    oracle_audio = ROOT / "reference" / "traces" / "oracle_audio.jsonl"
    if oracle_audio.exists() and oracle_audio.stat().st_size > 0:
        shutil.copy2(oracle_audio, art / "oracle_audio.jsonl")
    run_proc([PY, "reference/tools/extract_layer.py", "ui"],
             f"{scenario}-extract-ui", 120)
    oracle_ui = ROOT / "reference" / "traces" / "oracle_ui.jsonl"
    if oracle_ui.exists():
        shutil.copy2(oracle_ui, art / "oracle_ui.jsonl")

    # 2. Native side (same input stream, --dump-ui is free).
    #    [coverage] `--dump-audio N` feeds `compare_audio.py`: the audio layer
    #    was implemented on both sides and never compared by any gate.
    dump_audio = 0
    _nat = cfg["native"]
    if "--fight" in _nat:
        for i, a in enumerate(_nat):
            if a == "--dump-state" and i + 1 < len(_nat):
                try:
                    dump_audio = int(_nat[i + 1])
                except ValueError:
                    dump_audio = 0
        stale = ROOT / "reference" / "traces" / "native_audio.jsonl"
        if dump_audio and stale.exists():
            stale.unlink()
    nres = run_proc(
        [str(GAME)] + cfg["native"] +
        (["--dump-audio", str(dump_audio)] if dump_audio else []) +
        ["--dump-ui", RES, SAVE_REL],
        f"{scenario}-native", cfg["timeout_s"],
    )
    if nres.timed_out:
        return GateResult(gate.id, "diff", None, verdict="TIMEOUT",
                          error=f"native timeout after {cfg['timeout_s']}s",
                          seconds=time.time() - started)
    native_state = ROOT / "reference" / "traces" / "native_state.jsonl"
    if not native_state.exists() or native_state.stat().st_size == 0:
        return GateResult(gate.id, "diff", None, verdict="ERROR",
                          error="native produced no state trace",
                          seconds=time.time() - started)
    shutil.copy2(native_state, art / "native_state.jsonl")
    native_ui = ROOT / "reference" / "traces" / "native_ui.jsonl"
    if native_ui.exists():
        shutil.copy2(native_ui, art / "native_ui.jsonl")
    native_audio = ROOT / "reference" / "traces" / "native_audio.jsonl"
    audio_ok = False
    if dump_audio and native_audio.exists() and native_audio.stat().st_size > 0:
        shutil.copy2(native_audio, art / "native_audio.jsonl")
        audio_ok = True

    # 3. State diff (the metric of this gate).
    report = REPORTS / f"state_{scenario}.txt"
    cmp_res = run_proc(
        [PY, "reference/tools/compare_state.py", "--gate", "--label", scenario,
         "--out", str(report.relative_to(ROOT))],
        f"{scenario}-compare", 300,
    )
    if cmp_res.rc not in (0, 1):
        return GateResult(gate.id, "diff", None, verdict="ERROR",
                          error=f"compare_state exited {cmp_res.rc}",
                          seconds=time.time() - started)
    try:
        metric, detail = p_total_divergences(cmp_res.out)
    except AnchorMissing as exc:
        return GateResult(gate.id, "diff", None, verdict="FAIL",
                          error=f"state compare anchor missing: {exc}",
                          seconds=time.time() - started)

    # 3a. ALIGNMENT COVERAGE on the same state compare (see p_coverage_short):
    # a wrong move silently unaligns frames and would otherwise read as 0
    # divergences.
    cov_metric = None
    cov_detail = ""
    pos_res_metric = None
    pos_res_detail = ""
    try:
        with open(REPORTS / f"state_{scenario}.txt", encoding="utf-8",
                  errors="replace") as fh:
            cov_metric, cov_detail = p_coverage_short(fh.read())
    except (OSError, AnchorMissing):
        cov_detail = "coverage anchor missing"
    try:
        with open(REPORTS / f"state_{scenario}.txt", encoding="utf-8",
                  errors="replace") as fh:
            pos_res_metric, pos_res_detail = p_pos_drift(fh.read())
    except (OSError, AnchorMissing):
        pos_res_detail = "position-offset anchor missing"

    # 3b. POSE diff on the same run (per-bone, per-frame). The metric is the
    # number of matched frames with ANY bone over the comparator's tolerance.
    # Policy: cloth/chain nodes (`BODY-Node*`, `HEAD-Node*`) and the Mass=0
    # `COM` node are INCLUDED (measured: only they deviate; the skeletal bones
    # match) - the baseline records that number, so a cloth regression shows up.
    pose_metric = None
    pose_detail = ""
    posevel_metric = None
    posevel_detail = ""
    # Scenarios without `pose` never enter the block below, so these must be
    # initialised here (the same UnboundLocalError class as `posevel_metric`
    # earlier - caught by --full, not by --fast, because --fast runs one
    # scenario).
    sub_metric = None
    sub_detail = ""
    if cfg.get("pose"):
        native_pose = ROOT / "reference" / "traces" / "native_pose.jsonl"
        if native_pose.exists() and native_pose.stat().st_size > 0:
            shutil.copy2(native_pose, art / "native_pose.jsonl")
            oracle_console = art / "oracle_console.log"
            pose_rows = 0
            if oracle_console.exists():
                with open(oracle_console, encoding="utf-8", errors="replace") as fin, \
                        open(art / "oracle_pose.jsonl", "w", encoding="utf-8",
                             newline="\n") as fout:
                    for line in fin:
                        if '"t":"run_start"' in line:
                            fout.seek(0)
                            fout.truncate()
                            pose_rows = 0
                            continue
                        i2 = line.find('{"t":"frame"')
                        if i2 < 0:
                            continue
                        fout.write(line[i2:].strip() + "\n")
                        pose_rows += 1
            if pose_rows > 0:
                pose_report = REPORTS / f"pose_{scenario}.txt"
                pr = run_proc(
                    [PY, "reference/tools/compare_pose.py",
                     "--native", str((art / "native_pose.jsonl").relative_to(ROOT).as_posix()),
                     "--oracle", str((art / "oracle_pose.jsonl").relative_to(ROOT).as_posix()),
                     "--rel-root",
                     "--out", str(pose_report.relative_to(ROOT))],
                    f"{scenario}-pose", 180,
                )
                try:
                    pose_metric, pose_detail = p_pose_over(pr.out)
                except AnchorMissing:
                    pose_detail = "pose anchor missing"
                try:
                    posevel_metric, posevel_detail = p_pose_vel_over(pr.out)
                except AnchorMissing:
                    posevel_detail = "velocity anchor missing"
                try:
                    sub_metric, sub_detail = p_subframe_over(pr.out)
                except AnchorMissing:
                    sub_detail = "subframe anchor missing"

    # 3c. AUDIO diff on the same run (see p_audio_total: previously ungated).
    audio_metric = None
    audio_detail = ""
    if dump_audio and (art / "native_audio.jsonl").exists() and \
            (art / "oracle_audio.jsonl").exists():
        audio_report = REPORTS / f"audio_{scenario}.txt"
        au = run_proc(
            [PY, "reference/tools/compare_audio.py", "--label", scenario,
             "--native", str((art / "native_audio.jsonl").relative_to(ROOT).as_posix()),
             "--oracle", str((art / "oracle_audio.jsonl").relative_to(ROOT).as_posix()),
             "--out", str(audio_report.relative_to(ROOT))],
            f"{scenario}-cmp-audio", 180,
        )
        try:
            audio_metric, audio_detail = p_audio_total(au.out)
        except AnchorMissing:
            audio_detail = "audio anchor missing"

    # 4. UI stack diff on the same run (artifacts are in place).
    ui_detail = ""
    ui_metric = None
    if native_ui.exists() and oracle_ui.exists():
        ui_report = REPORTS / f"ui_{scenario}.txt"
        ui_res = run_proc(
            [PY, "reference/tools/compare_ui.py", "--gate", "--label", scenario,
             "--native", str(native_ui.relative_to(ROOT).as_posix()),
             "--oracle", str(oracle_ui.relative_to(ROOT).as_posix()),
             "--out", str(ui_report.relative_to(ROOT))],
            f"{scenario}-cmp-ui", 120,
        )
        try:
            ui_metric, ui_detail = p_ui_total(ui_res.out)
        except AnchorMissing:
            ui_detail = "ui anchor missing"
    return GateResult(gate.id, "diff", metric, detail,
                      time.time() - started,
                      extra={"ui_metric": ui_metric, "ui_detail": ui_detail,
                             "pose_metric": pose_metric, "pose_detail": pose_detail,
                             "cov_metric": cov_metric, "cov_detail": cov_detail,
                             "sub_metric": sub_metric, "sub_detail": sub_detail,
                             "pos_res_metric": pos_res_metric,
                             "pos_res_detail": pos_res_detail,
                             "audio_metric": audio_metric,
                             "audio_detail": audio_detail,
                             "posevel_metric": posevel_metric,
                             "posevel_detail": posevel_detail,
                             "attempts": attempts,
                             "artifacts": str(art.relative_to(ROOT))})


# --------------------------------------------------------------------------
# Baseline
# --------------------------------------------------------------------------


def load_baseline() -> dict:
    if not BASELINE.exists():
        return {"_meta": {}, "gates": {}}
    with open(BASELINE, encoding="utf-8") as fh:
        data = json.load(fh)
    data.setdefault("gates", {})
    return data


def save_baseline(data: dict) -> None:
    BASELINE.parent.mkdir(parents=True, exist_ok=True)
    with open(BASELINE, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, indent=2, ensure_ascii=False)
        fh.write("\n")


def commit_id() -> str:
    try:
        return subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], cwd=str(ROOT),
            capture_output=True, text=True, creationflags=CREATE_NO_WINDOW,
        ).stdout.strip() or "?"
    except OSError:
        return "?"


# --------------------------------------------------------------------------
# Report
# --------------------------------------------------------------------------


def print_report(results: list[GateResult], baseline: dict, mode: str,
                 duration: float, save_guard: SaveGuard, refwww_changed: list[str],
                 timeline: dict[str, list[str]]) -> None:
    print()
    print(f"reSF2 gates - mode={mode}  commit={commit_id()}  "
          f"exe={GAME.relative_to(ROOT) if GAME.exists() else 'MISSING'}  "
          f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 92)
    print(f"{'gate':34s} {'baseline':>9s} {'now':>9s}  verdict")
    print("-" * 92)
    for r in results:
        base = baseline["gates"].get(r.gate_id, {}).get("metric")
        base_s = "-" if base is None else str(base)
        now_s = "-" if r.metric is None else str(r.metric)
        line = f"{r.gate_id:34s} {base_s:>9s} {now_s:>9s}  {r.verdict}"
        if r.detail:
            line += f"  ({r.detail})"
        print(line)
        if r.error:
            for ln in r.error.splitlines():
                print(f"    ! {ln}")
        if r.kind == "diff":
            for ln in timeline.get(r.gate_id, [])[:5]:
                print(f"    * {ln}")
            if r.extra.get("attempts", 1) > 1:
                print(f"    * oracle navigation retries: {r.extra['attempts']} "
                      f"(wall-clock taps; the trace itself is deterministic)")
            if r.extra.get("ui_metric") is not None:
                print(f"    * ui stack: {r.extra['ui_detail']}"
                      f" (metric {r.extra['ui_metric']})")
    print("-" * 92)

    regressions = [r for r in results if r.verdict == "REGRESSION"]
    failures = [r for r in results if r.failed and r.verdict != "REGRESSION"]
    improvements = [r for r in results if r.verdict == "IMPROVEMENT"]
    if regressions:
        print("\nREGRESSIONS (exit 1):")
        for r in regressions:
            base = baseline["gates"].get(r.gate_id, {})
            print(f"  {r.gate_id}: baseline {base.get('metric')} -> now {r.metric}")
    if failures:
        print("\nFAILED GATES (exit 1):")
        for r in failures:
            print(f"  {r.gate_id}: {r.verdict}"
                  + (f" - {r.error.splitlines()[0]}" if r.error else ""))
    if improvements:
        print("\nIMPROVEMENTS (exit stays 0; move the baseline with a reason):")
        for r in improvements:
            base = baseline["gates"].get(r.gate_id, {})
            print(f"  {r.gate_id}: baseline {base.get('metric')} -> now {r.metric}"
                  f"  ->  python harness/gates.py --update-baseline "
                  f"--only {r.gate_id} --reason \"...\" --yes")
    if save_guard.violations:
        print("\nSAVE GUARD:")
        for v in save_guard.violations:
            print(f"  {v}")
    if refwww_changed:
        print(f"\nreference/www restored: {', '.join(refwww_changed)}")
    print(f"\ntotal {duration:.1f}s")


def write_json(path: Path, results: list[GateResult], mode: str,
               duration: float, exit_code: int, baseline: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {
        "tool": "harness/gates.py",
        "version": 1,
        "mode": mode,
        "commit": commit_id(),
        "date": datetime.now().isoformat(timespec="seconds"),
        "duration_s": round(duration, 1),
        "exit": exit_code,
        "gates": [
            {
                "id": r.gate_id,
                "kind": r.kind,
                "metric": r.metric,
                "baseline": baseline["gates"].get(r.gate_id, {}).get("metric"),
                "verdict": r.verdict,
                "detail": r.detail,
                "seconds": round(r.seconds, 1),
                "error": r.error,
                **r.extra,
            }
            for r in results
        ],
    }
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, indent=2, ensure_ascii=False)
        fh.write("\n")


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------


def main(argv=None) -> int:
    # The Windows console defaults to a legacy code page; the report uses
    # ASCII, but child output/errors can carry UTF-8. Never crash on a glyph.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="reSF2 gate runner (single entry).")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--fast", action="store_true")
    mode.add_argument("--full", action="store_true")
    mode.add_argument("--list", action="store_true")
    ap.add_argument("--only", default="", help="comma-separated gate ids")
    ap.add_argument("--reparse", action="store_true",
                    help="re-read .harness/samples instead of running")
    ap.add_argument("--force-build", action="store_true",
                    help="build even when the exe is newer than the sources")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--reason", default="", help="why the baseline moves")
    ap.add_argument("--yes", action="store_true",
                    help="confirm a baseline update (the human said ok)")
    ap.add_argument("--json", default="", help="write the machine report here")
    ap.add_argument("--readme", action="store_true",
                    help="regenerate README.md's status block and exit")
    ap.add_argument("--check-readme", action="store_true",
                    help="exit 1 when README.md's status block is stale")
    args = ap.parse_args(argv)

    if args.readme or args.check_readme:
        import readme_status  # same directory
        return readme_status.rewrite(check_only=args.check_readme)

    all_gates = FLAG_GATES + DIFF_GATES
    if args.list:
        for g in all_gates:
            tags = "+".join(g.tags)
            print(f"{g.id:34s} {tags:10s} {g.kind:5s} "
                  f"{g.flag or g.scenario}")
        return 0

    if not args.fast and not args.full and not args.only and not args.reparse:
        ap.print_help()
        return 2

    wanted = [s for s in args.only.split(",") if s] if args.only else []
    if wanted:
        missing = [w for w in wanted if w not in {g.id for g in all_gates}]
        if missing:
            print(f"gates: unknown gate id(s): {', '.join(missing)}", file=sys.stderr)
            return 2
        selected = [g for g in all_gates if g.id in wanted]
    elif args.full:
        selected = [g for g in all_gates if "full" in g.tags]
    else:
        selected = [g for g in all_gates if "fast" in g.tags]

    mode_name = "only" if wanted else ("full" if args.full else "fast")
    baseline = load_baseline()

    if args.update_baseline:
        if not args.reason.strip():
            print("gates: --update-baseline requires --reason \"...\"", file=sys.stderr)
            return 2
        if not args.yes:
            print("gates: --update-baseline requires --yes (human confirmation)",
                  file=sys.stderr)
            return 2
        if not wanted and not args.full:
            print("gates: --update-baseline requires --only <ids> or --full",
                  file=sys.stderr)
            return 2

    # Environment preflight: a missing OracleShell is an environment error (it
    # is not built here). A missing game.exe is NOT: the build gate builds it
    # (the exe is a build artifact, and a failed build must be the build
    # gate's FAIL, not an environment error).
    errs = []
    if not ORACLE.exists():
        errs.append(f"missing {ORACLE.relative_to(ROOT)}")
    if errs:
        for e in errs:
            print(f"gates: environment error: {e}", file=sys.stderr)
        return 2

    results: list[GateResult] = []
    timeline: dict[str, list[str]] = {}
    started = time.time()
    changed: list[str] = []

    if args.reparse:
        # Development aid: re-parse the last captured stdout of each gate.
        for g in selected:
            out_path = SAMPLES / f"{g.id}.out"
            if not out_path.exists():
                results.append(GateResult(g.id, g.kind, None, verdict="FAIL",
                                          error=f"no sample at {out_path}"))
                continue
            try:
                metric, detail = parse_metric(g.parser, out_path.read_text(
                    encoding="utf-8", errors="replace"))
                results.append(GateResult(g.id, g.kind, metric, detail))
            except AnchorMissing as exc:
                results.append(GateResult(g.id, g.kind, None, verdict="FAIL",
                                          error=f"anchor missing: {exc}"))
        baseline["gates"] = baseline.get("gates", {})
        for r in results:
            base = baseline["gates"].get(r.gate_id, {}).get("metric")
            r.baseline = base
            r.verdict = verdict_for(r.metric, base, r.kind)
        print_report(results, baseline, "reparse", 0.0, SaveGuard(), [], {})
        return 0

    with RunLock():
        save_guard = SaveGuard()
        save_guard.backup()
        refwww = RefWwwGuard()
        refwww.backup()
        try:
            build_res = build_gate(args.force_build)
            if build_res.verdict == "ERROR":
                print(f"gates: {build_res.error}", file=sys.stderr)
                return 2
            if build_res.metric:
                # A failed build stops everything else: nothing else is meaningful.
                results.append(build_res)
                print_report(results, baseline, mode_name, time.time() - started,
                             save_guard, [], {})
                return 1

            for g in selected:
                if g.kind == "gate":
                    r = run_flag_gate(g)
                else:
                    r = run_diff_gate(g)
                save_guard.check(g.id)
                if g.kind == "diff":
                    timeline[g.id] = first_divergences(
                        REPORTS / f"state_{g.scenario}.txt")
                results.append(r)
                if (g.kind == "diff" and
                        r.extra.get("sub_metric") is not None and
                        r.extra.get("pose_metric") is not None):
                    results.append(GateResult(
                        f"sub:{g.scenario}", "sub", r.extra["sub_metric"],
                        r.extra.get("sub_detail", ""), 0.0,
                        extra={"n": 1}))
                    timeline[f"sub:{g.scenario}"] = []
                if g.kind == "diff" and r.extra.get("ui_metric") is not None:
                    results.append(GateResult(
                        f"ui:{g.scenario}", "ui", r.extra["ui_metric"],
                        r.extra.get("ui_detail", ""), 0.0,
                        extra={"n": 1}))
                    timeline[f"ui:{g.scenario}"] = []
                if g.kind == "diff" and r.extra.get("audio_metric") is not None:
                    results.append(GateResult(
                        f"audio:{g.scenario}", "audio", r.extra["audio_metric"],
                        r.extra.get("audio_detail", ""), 0.0,
                        extra={"n": 1}))
                    timeline[f"audio:{g.scenario}"] = []
                if (g.kind == "diff" and
                        r.extra.get("pos_res_metric") is not None):
                    results.append(GateResult(
                        f"posres:{g.scenario}", "posres",
                        r.extra["pos_res_metric"],
                        r.extra.get("pos_res_detail", ""), 0.0,
                        extra={"n": 1}))
                    timeline[f"posres:{g.scenario}"] = []
                if g.kind == "diff" and r.extra.get("cov_metric") is not None:
                    results.append(GateResult(
                        f"cov:{g.scenario}", "cov", r.extra["cov_metric"],
                        r.extra.get("cov_detail", ""), 0.0,
                        extra={"n": 1}))
                    timeline[f"cov:{g.scenario}"] = []
                if (g.kind == "diff" and
                        r.extra.get("posevel_metric") is not None and
                        r.extra.get("pose_metric") is not None):
                    results.append(GateResult(
                        f"posevel:{g.scenario}", "posevel",
                        r.extra["posevel_metric"],
                        r.extra.get("posevel_detail", ""), 0.0,
                        extra={"n": 1}))
                    timeline[f"posevel:{g.scenario}"] = []
                if g.kind == "diff" and r.extra.get("pose_metric") is not None:
                    results.append(GateResult(
                        f"pose:{g.scenario}", "pose", r.extra["pose_metric"],
                        r.extra.get("pose_detail", ""), 0.0,
                        extra={"n": 1}))
                    timeline[f"pose:{g.scenario}"] = []
                # RULE 0: strictly sequential; fail fast on an environment error.
                if r.verdict == "ERROR":
                    break

            for r in results:
                base = baseline["gates"].get(r.gate_id, {}).get("metric")
                r.baseline = base
                if not r.verdict:
                    r.verdict = verdict_for(r.metric, base, r.kind)
        finally:
            save_guard.restore()
            changed = refwww.restore()

    # Baseline updates are explicit, per gate, with a reason.
    if args.update_baseline:
        moved = []
        for r in results:
            # A pose:<scenario> result belongs to its diff:<scenario> gate,
            # so asking to (re)baseline the diff also baselines the pose.
            pose_of = (r.gate_id.startswith("pose:") or
                       r.gate_id.startswith("posevel:") or
                       r.gate_id.startswith("cov:") or
                       r.gate_id.startswith("ui:") or
                       r.gate_id.startswith("sub:") or
                       r.gate_id.startswith("posres:") or
                       r.gate_id.startswith("audio:")) and (
                "diff:" + r.gate_id.split(":", 1)[1]) in wanted
            if wanted and r.gate_id not in wanted and not pose_of:
                continue
            if r.metric is None:
                print(f"gates: refusing to baseline {r.gate_id}: no metric",
                      file=sys.stderr)
                continue
            baseline["gates"][r.gate_id] = {
                "metric": r.metric,
                "commit": commit_id(),
                "reason": args.reason.strip(),
                "date": datetime.now().isoformat(timespec="seconds"),
            }
            moved.append(f"{r.gate_id}={r.metric}")
        baseline.setdefault("_meta", {})["updated"] = datetime.now().isoformat(
            timespec="seconds")
        baseline["_meta"]["mode"] = mode_name
        save_baseline(baseline)
        print(f"gates: baseline updated ({len(moved)}): {', '.join(moved)}")

    exit_code = 0
    if any(r.verdict == "ERROR" for r in results):
        exit_code = 2
    elif any(r.failed for r in results):
        exit_code = 1

    used_refwww = changed
    print_report(results, baseline, mode_name, time.time() - started,
                 save_guard, used_refwww, timeline)
    print(f"exit {exit_code}")
    if args.json:
        write_json(ROOT / args.json if not os.path.isabs(args.json) else Path(args.json),
                   results, mode_name, time.time() - started, exit_code, baseline)
    # A full run is the moment the status is fully known: refresh the README's
    # generated block so the repo can never advertise a stale state. Best
    # effort - a README hiccup must not turn a green gate run red.
    if args.full:
        try:
            import readme_status
            readme_status.rewrite()
        except Exception as exc:  # noqa: BLE001 - diagnostics only
            print(f"gates: README status not regenerated: {exc}")
    return exit_code


# Derived metrics (pose/velocity/subframe/coverage/UI/audio/position) are counts
# of matched frames, and the oracle navigation is wall-clock: the captured frame
# window can shift by a frame or two between runs, which moves those counts by
# 1-2 even with identical behaviour. Measured directly on survival, whose pose
# metric alternated 535/536/536 across three runs. Behaviour gates (`diff:`)
# keep the exact comparison.
GATE_TOL = {
    "pose": 2,
    "posevel": 2,
    "sub": 0,
    "cov": 0,
    "posres": 1,
    "ui": 2,
    "audio": 2,
}


def verdict_for(metric: int | None, base: int | None,
                kind: str | None = None) -> str:
    if metric is None:
        return "FAIL"
    if base is None:
        return "NO-BASELINE"
    tol = GATE_TOL.get(kind or "", 0)
    if metric > base + tol:
        return "REGRESSION"
    if metric < base:
        return "IMPROVEMENT"
    if metric > base:
        return "OK"           # inside the tolerance band
    return "OK"


def first_divergences(report: Path) -> list[str]:
    """The first rows of compare_state's `[divergence timeline]` block."""
    if not report.exists():
        return []
    rows: list[str] = []
    in_block = False
    for line in report.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("[divergence timeline]"):
            in_block = True
            continue
        if in_block:
            if not line.strip():
                break
            rows.append(line.strip())
            if len(rows) >= 5:
                break
    return rows


if __name__ == "__main__":
    sys.exit(main())
