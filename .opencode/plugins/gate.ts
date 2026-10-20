/**
 * reSF2 gate plugin.
 *
 * One job: make a broken edit impossible to miss. After every `edit` /
 * `write` / `patch` inside `core/` or `app/`, run the fast gate set
 * (`harness/gates.py --fast`) and, if it is not green, put a compact report
 * into the tool result in the SAME turn. Green = silence (no context noise).
 *
 * RULE 0 (see AGENTS.md): the runner itself is strictly sequential (it holds
 * `.harness/lock`), hides its windows, puts `--watchdog` in every argv, and
 * kills only its own process tree. This plugin never starts a second run in
 * parallel: if the lock exists, it skips.
 *
 * Paths: never touches `reference/` (the JS is the reference) and never edits
 * `reference/saves/save.xml` (the runner backs it up and restores it).
 */
import type { Plugin } from "@opencode-ai/plugin";
import { tool } from "@opencode-ai/plugin";
import { spawn } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";

const EDIT_TOOLS = new Set(["edit", "write", "patch"]);
const TRIGGER_DIRS = ["core", "app"];
const BUDGET_MS = 10 * 60 * 1000;

type GateRow = {
  id: string;
  metric: number | null;
  baseline: number | null;
  verdict: string;
  detail?: string;
  error?: string;
  ui_metric?: number | null;
};

type GateJson = {
  mode: string;
  commit: string;
  exit: number;
  duration_s: number;
  gates: GateRow[];
};

const BAD_VERDICTS = new Set([
  "REGRESSION",
  "FAIL",
  "TIMEOUT",
  "NO-BASELINE",
  "LIE",
  "ERROR",
]);

function killTree(pid: number | undefined): void {
  if (!pid) return;
  if (process.platform === "win32") {
    spawn("taskkill", ["/PID", String(pid), "/T", "/F"], { windowsHide: true });
  } else {
    try {
      process.kill(pid, "SIGKILL");
    } catch {
      /* already gone */
    }
  }
}

/**
 * Is a gate run active? `.harness/lock` holds `<pid> <timestamp>`; a leftover
 * file whose PID is dead is stale (a crashed run) and must NOT block the hook.
 */
function gateRunning(directory: string): boolean {
  const lock = path.join(directory, ".harness", "lock");
  if (!existsSync(lock)) return false;
  try {
    const pid = parseInt(
      readFileSync(lock, "utf8").trim().split(/\s+/)[0] ?? "0",
      10,
    );
    if (!Number.isFinite(pid) || pid <= 0) return true;
    process.kill(pid, 0); // throws when the process does not exist
    return true;
  } catch {
    return false;
  }
}

function runGates(
  directory: string,
  args: string[],
  timeoutMs: number,
): Promise<{ code: number; out: string; err: string }> {
  return new Promise((resolve) => {
    const child = spawn("python", [path.join("harness", "gates.py"), ...args], {
      cwd: directory,
      windowsHide: true,
      stdio: ["ignore", "pipe", "pipe"],
    });
    let out = "";
    let err = "";
    child.stdout.on("data", (d) => (out += d.toString()));
    child.stderr.on("data", (d) => (err += d.toString()));
    const timer = setTimeout(() => killTree(child.pid), timeoutMs);
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({ code: code ?? -1, out, err });
    });
    child.on("error", (e) => {
      clearTimeout(timer);
      resolve({ code: -1, out, err: String(e) });
    });
  });
}

/** The compact "what broke" block: at most 4 gates + the first divergent field. */
function compactReport(directory: string): string | null {
  try {
    const data = JSON.parse(
      readFileSync(path.join(directory, ".harness", "last.json"), "utf8"),
    ) as GateJson;
    const bad = data.gates.filter((g) => BAD_VERDICTS.has(g.verdict));
    if (!bad.length) return null;
    return bad
      .slice(0, 4)
      .map((g) => {
        const base = g.baseline === null ? "-" : String(g.baseline);
        const now = g.metric === null ? "-" : String(g.metric);
        const why = (g.error || g.detail || "").split("\n")[0];
        const ui =
          g.ui_metric === null || g.ui_metric === undefined
            ? ""
            : ` ui=${g.ui_metric}`;
        return `${g.id}: ${base} -> ${now}${ui}  ${g.verdict}${why ? "  (" + why + ")" : ""}`;
      })
      .join("\n");
  } catch {
    return null;
  }
}

export const GatePlugin: Plugin = async ({ directory }) => ({
  tool: {
    gate: tool({
      description:
        "Run the reSF2 gate runner (harness/gates.py) and return its table. " +
        "fast = build + verify-input/flow-verify/save-fields/settings-profile-shop; " +
        "full = 16 flag gates + 6 oracle-vs-native differentials.",
      args: {
        mode: tool.schema
          .enum(["fast", "full"])
          .describe("fast set or the full set"),
        only: tool.schema
          .array(tool.schema.string())
          .optional()
          .describe("run only these gate ids (e.g. diff:survival)"),
      },
      async execute(args) {
        const argv: string[] = [args.mode === "full" ? "--full" : "--fast"];
        if (args.only && args.only.length) argv.push("--only", args.only.join(","));
        argv.push("--json", ".harness/last.json");
        const res = await runGates(directory, argv, BUDGET_MS);
        const body = (res.out || "").trim();
        const err = (res.err || "").trim();
        if (res.code === -1 && !body) {
          return `GATE ERROR: could not start the runner\n${err}`;
        }
        return `gate exit ${res.code}\n\n${body || err}`;
      },
    }),
  },

  "tool.execute.after": async (input, output) => {
    if (!EDIT_TOOLS.has(input.tool)) return;
    const raw = input.args?.filePath ?? input.args?.path ?? "";
    if (typeof raw !== "string" || !raw) return;
    const rel = path
      .relative(directory, path.resolve(directory, raw))
      .split(path.sep)
      .join("/");
    if (!TRIGGER_DIRS.some((d) => rel === d || rel.startsWith(d + "/"))) return;
    // A gate run is already active (the runner holds the OS lock): never parallel.
    if (gateRunning(directory)) return;

    const res = await runGates(
      directory,
      ["--fast", "--json", ".harness/last.json"],
      BUDGET_MS,
    );
    if (res.code === 0) return; // green = silence

    const tag = res.code === 2 ? "GATE ERROR:" : "GATE FAIL:";
    const report =
      compactReport(directory) ??
      (res.out || res.err || "").trim().split("\n").slice(-15).join("\n");
    output.output = `${tag}\n${report}\n\n${output.output}`;
  },
});
