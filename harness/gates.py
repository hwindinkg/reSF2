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
        if not self.hash0 or not SAVE.exists():
            return
        if sha256(SAVE) != self.hash0:
            self.violations.append(f"{gate_id}: save.xml changed during the gate")
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


def run_proc(argv: list[str], sample_id: str, timeout_s: int) -> ProcResult:
    """Run one argv (NO SHELL), hidden, with a hard timeout; kill our tree."""
    SAMPLES.mkdir(parents=True, exist_ok=True)
    out_path = SAMPLES / f"{sample_id}.out"
    err_path = SAMPLES / f"{sample_id}.err"
    started = time.time()
    with open(out_path, "w", encoding="utf-8", errors="replace") as fo, open(
        err_path, "w", encoding="utf-8", errors="replace"
    ) as fe:
        proc = subprocess.Popen(
            argv,
            cwd=str(ROOT),
            stdout=fo,
            stderr=fe,
            stdin=subprocess.DEVNULL,
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


def p_kick_ko(text: str) -> tuple[int, str]:
    """`--kick-ko` ends with `[result] WIN` (no RESULT line)."""
    m = _need(r"^\[result\]\s+(WIN|LOSE|DRAW)\s*$", text)
    return (0 if m.group(1) == "WIN" else 1), m.group(1)


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


def p_total_divergences(text: str) -> tuple[int, str]:
    m = _need(r"total divergences:\s*(\d+)", text)
    n = int(m.group(1))
    return n, f"{n} divergences"


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
    "dialog": p_dialog,
    "total_div": p_total_divergences,
    "ui_total": p_ui_total,
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

    def outer_timeout(self) -> int:
        return self.timeout_s or (self.watch + 120)


FLAG_GATES: list[Gate] = [
    Gate("verify-input", "gate", ("fast", "full"), "--verify-input", 180, parser="probes"),
    Gate("flow-verify", "gate", ("fast", "full"), "--flow-verify", 300, parser="flow"),
    Gate("save-fields-probe", "gate", ("fast", "full"), "--save-fields-probe", 180, parser="savefields"),
    Gate("settings-profile-shop-probe", "gate", ("fast", "full"), "--settings-profile-shop-probe", 300, parser="sps"),
    Gate("input-tape", "gate", ("full",), "--input-tape", 180, parser="tape"),
    Gate("ui-tour", "gate", ("full",), "--ui-tour", 600, parser="tour:15"),
    Gate("fidelity-tour", "gate", ("full",), "--fidelity-tour", 600, parser="fidelity:40"),
    Gate("headless-loop", "gate", ("full",), "--headless-loop", 700, parser="loop:13"),
    Gate("boss-hit-probe", "gate", ("full",), "--boss-hit-probe", 300, parser="boss_hit"),
    Gate("kick-ko", "gate", ("full",), "--kick-ko", 300, parser="kick_ko"),
    Gate("quest-verify", "gate", ("full",), "--quest-verify", 300, parser="kv:qverify"),
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
                "--headless", "700", "--dump-state", "600", "--watchdog", "900"],
        timeout_s=420,
    ),
    "tournament": dict(
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000&battle=Tournament",
                "--input-script", "reference/tools/state_tournament.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "Tournament", "--zone", "ZONE_1",
                "--fight-ordinal", "0", "--location", "arena",
                "--headless", "700", "--dump-state", "600", "--watchdog", "900"],
        timeout_s=420,
    ),
    "survival": dict(
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000&battle=Survival",
                "--input-script", "reference/tools/state_survival.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "Survival", "--zone", "ZONE_1",
                "--fight-ordinal", "0", "--location", "bamboo_grove",
                "--headless", "700", "--dump-state", "600", "--watchdog", "900"],
        timeout_s=420,
    ),
    "challenge": dict(
        oracle=["--query", "postTutorial=1&skipFightNone=1&maxFrames=6000&battle=Challenge&battleZone=ZONE_2",
                "--input-script", "reference/tools/state_challenge.txt",
                "--timeout-ms", "60000"],
        native=["--fight", "--battle", "Challenge", "--zone", "ZONE_2",
                "--fight-ordinal", "0", "--location", "autumn",
                "--headless", "700", "--dump-state", "600", "--watchdog", "900"],
        timeout_s=420,
    ),
    "dojo-move": dict(
        oracle=["--query", "postTutorial=0&maxFrames=1200",
                "--input-script", "reference/tools/dojo_move.txt",
                "--timeout-ms", "90000"],
        native=["--replay", "reference/tools/dojo_move.txt",
                "--headless", "900", "--dump-state", "900", "--watchdog", "900"],
        timeout_s=420,
    ),
}

DIFF_GATES: list[Gate] = [
    Gate(f"diff:{name}", "diff", ("full",), scenario=name,
         timeout_s=SCENARIOS[name]["timeout_s"], parser="total_div")
    for name in SCENARIOS
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
    argv = [str(GAME), gate.flag, "--watchdog", str(gate.watch), RES, SAVE_REL]
    res = run_proc(argv, gate.id, gate.outer_timeout())
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
    run_proc([PY, "reference/tools/extract_layer.py", "ui"],
             f"{scenario}-extract-ui", 120)
    oracle_ui = ROOT / "reference" / "traces" / "oracle_ui.jsonl"
    if oracle_ui.exists():
        shutil.copy2(oracle_ui, art / "oracle_ui.jsonl")

    # 2. Native side (same input stream, --dump-ui is free).
    nres = run_proc(
        [str(GAME)] + cfg["native"] + ["--dump-ui", RES, SAVE_REL],
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
    print(f"reSF2 gates — mode={mode}  commit={commit_id()}  "
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
                print(f"    · {ln}")
            if r.extra.get("ui_metric") is not None:
                print(f"    · ui stack: {r.extra['ui_detail']}"
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
                  + (f" — {r.error.splitlines()[0]}" if r.error else ""))
    if improvements:
        print("\nIMPROVEMENTS (exit stays 0; move the baseline with a reason):")
        for r in improvements:
            base = baseline["gates"].get(r.gate_id, {})
            print(f"  {r.gate_id}: baseline {base.get('metric')} -> now {r.metric}"
                  f"  →  python harness/gates.py --update-baseline "
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
    args = ap.parse_args(argv)

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
            r.verdict = verdict_for(r.metric, base)
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
                # RULE 0: strictly sequential; fail fast on an environment error.
                if r.verdict == "ERROR":
                    break

            for r in results:
                base = baseline["gates"].get(r.gate_id, {}).get("metric")
                r.baseline = base
                if not r.verdict:
                    r.verdict = verdict_for(r.metric, base)
        finally:
            save_guard.restore()
            changed = refwww.restore()

    # Baseline updates are explicit, per gate, with a reason.
    if args.update_baseline:
        moved = []
        for r in results:
            if wanted and r.gate_id not in wanted:
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
    return exit_code


def verdict_for(metric: int | None, base: int | None) -> str:
    if metric is None:
        return "FAIL"
    if base is None:
        return "NO-BASELINE"
    if metric > base:
        return "REGRESSION"
    if metric < base:
        return "IMPROVEMENT"
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
