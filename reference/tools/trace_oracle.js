/*
 * trace_oracle.js — Phase 1 oracle instrumentation for the SF2 web build.
 *
 * Master copy lives at reference/tools/trace_oracle.js. OracleShell injects
 * this file's text via WebView2 AddScriptToExecuteOnDocumentCreated, i.e. it
 * runs BEFORE any page script (before the game boots). It coexists with
 * reference/runner/trace.js v2 (pose tracer): separate flags, chained
 * Function.prototype.bind hooks, separately wrapped app.aa.
 *
 * Verified method names (current reference/www/sf2.502f0946.js):
 *   - de.Pqb   — AI main decision (class de; instance per fighter as f.nf,
 *                `this.nf = new de(...)`). Wrapper logs fk/aqa/return/candidates.
 *   - de.ia    — per-frame decision pass (eh wait counter, dqb zone, Pqb call).
 *                Wrapper logs fk before/after + return (chosen move).
 *   - Sf.iPa   — round timer tick (`--this.xU; this.NF = this.xU/60|0`;
 *                xU init `this.round.gma*60+1`). NOTE: the brief guessed this
 *                name right — iPa lives on the fight screen class Sf, and the
 *                fight controller (ca, top screen's Ig) drives it. The tracer
 *                duck-types: it wraps iPa/N0a wherever they are found on the
 *                fight object graph (fight object first, then one level deep)
 *                and reports hooked paths in the header record.
 *   - ca.N0a   — input gate (`this.eu==1 ? phase-1 WC buffer : phase-2 yJa`).
 *                Wrapper logs control code + phase + resulting WC state.
 *
 * Zero behavior change: every patch calls the original and returns its value;
 * every read is try/catch-guarded; failures are reported as [ORACLE] ERR lines
 * and never thrown into game code. Method entries/exits go to window.__trace
 * (in-page ring, capped); per-frame state goes to console as
 * "[ORACLE] {json}" lines (captured by OracleShell into console.log).
 *
 * Deterministic harness (entropy pinning — game code untouched, and the pins
 * are recorded in every trace header so the port can reproduce the streams):
 *   - Date frozen to T0 (L.seed, Da.rL()/Da.IT() reseeds, InstallID all derive
 *     from ed.getDate().getTime(); Xx is a portable LCG
 *     mf = (mf*1103515245+12345) mod 2^31, so Da.jf() streams are reproducible).
 *   - Math.random replaced with mulberry32(0xC0FFEE) (used by oa.eT: particle
 *     jitter + Ie.Gb() tactic-range evaluation when min != max).
 *   - OracleShell additionally launches with a fresh browser profile per run
 *     (clean localStorage/saves).
 */
(function () {
  "use strict";
  if (window.__oracleP1) return;
  window.__oracleP1 = true;

  /* ================= 1. Deterministic harness ================= */

  var T0 = 1720000000000; /* fixed wall-clock, ms epoch */
  var MR_SEED = 0xC0FFEE;

  var RealDate = window.Date;
  function FixedDate() {
    var args = arguments.length ? Array.prototype.slice.call(arguments) : [T0];
    var self = new (Function.prototype.bind.apply(RealDate, [null].concat(args)))();
    Object.setPrototypeOf(self, FixedDate.prototype);
    return self;
  }
  FixedDate.prototype = Object.create(RealDate.prototype);
  FixedDate.prototype.constructor = FixedDate;
  FixedDate.now = function () { return T0; };
  FixedDate.parse = RealDate.parse;
  FixedDate.UTC = RealDate.UTC;
  window.Date = FixedDate;

  var mrState = MR_SEED;
  Math.random = function () {
    mrState |= 0;
    mrState = (mrState + 0x6D2B79F5) | 0;
    var t = Math.imul(mrState ^ (mrState >>> 15), 1 | mrState);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };

  window.__trace = []; /* method entry/exit events, capped */
  window.__oracleDone = false;
  window.__oracleHarness = {
    dateFixed: T0,
    mathRandom: "mulberry32(0xC0FFEE)"
  };

  /* ================= 2. Config ================= */

  /* UI-tour mode (phase1 step9): the old auto-finish (fight.frame >= 600, i.e.
   * ~10s of fight) closed OracleShell before a scripted tour could reach the
   * later screens, so only boot.png was ever produced. Both defaults are now
   * effectively unreachable (1e9): the shell closes on its --timeout-ms safety
   * timeout / window close instead, letting a tour span every screen. This
   * changes only WHEN the trace stops, never what is recorded, so the trace
   * stays deterministic (same pins: Date frozen + mulberry32). Callers that
   * still need the Phase-1 gate set window.__oracleLimits =
   * {maxFightFrames:600,maxRecords:1500} any time before the cap would hit
   * (read live below, so a runner/index.html script can arm it pre-tick). */
  var DEFAULT_MAX_FIGHT_FRAMES = 1000000000; /* stop when fight.frame reaches this */
  var DEFAULT_MAX_RECORDS = 1000000000; /* backstop: stop after this many oracle records */

  function limit(name, fallback) {
    try {
      var o = window.__oracleLimits;
      if (o && typeof o[name] === "number") return o[name];
    } catch (e) { /* ignore */ }
    return fallback;
  }
  var TRACE_CAP = 20000; /* window.__trace ring cap */
  var N0A_RECENT = 8; /* input events kept in input_buffer_state.recent */

  var or = {
    mc: null,
    fight: null,
    app: null,
    screenTop: null,
    timerHostObj: null,
    timerHostPath: null,
    stimulus: null,
    headerDone: false,
    lastFrame: -1,
    records: 0,
    lastRTag: null,
    rFrames: 0,
    rFightFrames: 0,
    hookedFight: null,
    hookedPaths: { iPa: [], N0a: [], O0a: [] },
    deList: null, /* [{obj, label}] the fighters' own de (for the header) */
    deCount: 0,
    lastDecider: null, /* label of the de/Gc with the latest decision event */
    methodHosts: null, /* {N0a:{host},O0a:{host}} for exact replay */
    orecSeq: 0, /* INPUT-REC sequence number */
    cO0a: 0, /* O0a (release) wrapper fire counter */
    lastMove: { Me: null, Enemy: null },
    lastPqb: { Me: null, Enemy: null },
    n0aRecent: [],
    cPqb: 0, cIa: 0, cN0a: 0, ciPa: 0, /* wrapper fire counters (trace_stats) */
    lastRandom: { Me: null, Enemy: null }, /* last Gc.jL (Random-path pick) */
    lastPkbPre: { Me: null, Enemy: null }, /* Gc.Pkb INPUT list (pre-M7/Ts filter) */
    deProtoWrapped: false, /* de.prototype.Pqb/ia wrapped (covers every de) */
    gcProtoWrapped: false, /* Gc.prototype.jL wrapped (Random candidate pick) */
    hjaWrapped: false, /* fighter.prototype.hJa wrapped (Random timing) */
    errLogged: false
  };

  /* ================= 3. Helpers ================= */

  function num(v) {
    return typeof v === "number" && isFinite(v) ? v : null;
  }

  function frameNo() {
    try {
      if (or.fight && typeof or.fight.frame === "number") return or.fight.frame;
    } catch (e) { /* ignore */ }
    return null;
  }

  function pushTrace(ev) {
    try {
      if (window.__trace.length < TRACE_CAP) window.__trace.push(ev);
    } catch (e) { /* ignore */ }
  }

  function emit(obj) {
    try {
      console.log("[ORACLE] " + JSON.stringify(obj));
    } catch (e) { /* ignore */ }
  }

  function orFail(err) {
    if (or.errLogged) return;
    or.errLogged = true;
    try {
      console.log("[ORACLE] ERR " + (err && err.message ? err.message : String(err)));
    } catch (e) { /* ignore */ }
  }

  /* Find a method by name on obj's own props + prototype chain (depth-capped). */
  function findMethod(obj, name) {
    var o = obj, depth = 0;
    while (o && depth < 4) {
      try {
        if (Object.prototype.hasOwnProperty.call(o, name) &&
            typeof o[name] === "function") {
          return { host: o, fn: o[name] };
        }
      } catch (e) { /* ignore */ }
      try {
        o = Object.getPrototypeOf(o);
      } catch (e) { break; }
      depth++;
    }
    return null;
  }

  /* fight object first, then one level deep (covers screen/controller nesting). */
  function findMethodDeep(fight, name) {
    var hit = findMethod(fight, name);
    if (hit) { hit.path = "fight"; return hit; }
    try {
      var keys = Object.keys(fight);
      for (var i = 0; i < keys.length; i++) {
        var v = null;
        try { v = fight[keys[i]]; } catch (e) { continue; }
        if (v && (typeof v === "object" || typeof v === "function") && v !== fight) {
          var h2 = findMethod(v, name);
          if (h2) { h2.path = "fight." + keys[i]; return h2; }
        }
      }
    } catch (e) { /* ignore */ }
    return null;
  }

  function isDe(x) {
    try {
      return !!x && typeof x.Pqb === "function" && typeof x.fk === "number";
    } catch (e) { return false; }
  }

  function moveLabel(r) {
    if (r == null) return null;
    try {
      if (typeof r === "string") return r;
      if (typeof r === "number") return "#" + r;
      var cands = ["name", "Eza", "jb", "ID", "FileName"];
      for (var i = 0; i < cands.length; i++) {
        var v = r[cands[i]];
        if (typeof v === "string" && v) return v;
      }
    } catch (e) { /* ignore */ }
    return "?";
  }

  function sideOf(fight, f) {
    try {
      if (f === fight.pb) return "Me";
      if (f === fight.yb) return "Enemy";
    } catch (e) { /* ignore */ }
    return "?";
  }

  function fighterAt(fight, side) {
    try {
      return side === "Me" ? fight.pb : fight.yb;
    } catch (e) { return null; }
  }

  /* The AI-controlled side: parameters.Fj is the "NotAI" flag (false = AI).
   * Falls back to "Enemy" when ambiguous. */
  function aiSide(fight) {
    try {
      var meNotAi = !!(fight.pb && fight.pb.parameters && fight.pb.parameters.Fj);
      var enNotAi = !!(fight.yb && fight.yb.parameters && fight.yb.parameters.Fj);
      if (meNotAi && !enNotAi) return "Enemy";
      if (enNotAi && !meNotAi) return "Me";
    } catch (e) { /* ignore */ }
    return "Enemy";
  }

  /* Input event detail: Df carries {control, index} (+Gfa() type:
   * 0 touch / 1 keyboard / 2 gamepad). Coordinates do NOT survive device
   * mapping, so (control, index, type) is sufficient for exact replay. */
  function inputDetail(a) {
    var c = null, x = null, t = null;
    try {
      if (a && typeof a.control !== "undefined") c = a.control;
    } catch (e) { /* ignore */ }
    try {
      if (a && typeof a.index === "number") x = a.index;
    } catch (e) { /* ignore */ }
    try {
      if (a && typeof a.Gfa === "function") {
        var g = a.Gfa();
        if (typeof g === "number") t = g;
      }
    } catch (e) { /* ignore */ }
    return { c: c, x: x, t: t };
  }

  /* Effective-input recorder: every input that passes Za gating and reaches
   * the fight is logged for exact replay (record_inputs.py converts these
   * to `atframe press/release` lines). */
  function emitInputRec(m, det) {
    try {
      or.orecSeq++;
      console.log("[INPUT-REC] " + JSON.stringify(
        { f: frameNo(), i: or.orecSeq, m: m, c: det.c, x: det.x, t: det.t }));
    } catch (e) { /* ignore */ }
  }

  /* Exact replay: re-inject a recorded effective input by calling the game's
   * own input method with an equivalent {control, index, Gfa} object. Calls
   * the WRAPPED method, so replayed inputs are logged identically to live
   * ones (INPUT-REC echo + recent ring) — the echo is the mechanical
   * replay-fidelity check. Za DEa lesson gates are still bypassed (no DOM),
   * deterministically: the recorded stream was already post-gate. */
  function replayInput(it) {
    try {
      var name = it.t === "press" ? "N0a" : "O0a";
      var self = or.fight;
      var fn = null;
      try {
        var h = or.methodHosts && or.methodHosts[name];
        if (h && h.host && typeof h.host[name] === "function") {
          self = h.host;
          fn = h.host[name];
        } else if (self && typeof self[name] === "function") {
          fn = self[name];
        }
      } catch (e) { return; }
      if (!fn || !self) return;
      var tt = (typeof it.tt === "number") ? it.tt : 0;
      var fake = {
        control: it.c,
        index: (typeof it.x === "number") ? it.x : 0,
        Gfa: (function (t) { return function () { return t; }; })(tt)
      };
      fn.call(self, fake);
    } catch (e) { /* ignore */ }
  }

  /* ================= 4. Method wrappers (log in/out, delegate) ================= */

  /* Side of a `de`/fighter-AI instance: compare it to the fighters' own AI
   * (JS `this.nf = new de(...)`, L20548/L20783). Resolved at CALL time, so a
   * single PROTOTYPE wrapper covers both sides (the old per-instance scan
   * captured the side once and missed the real fight's de entirely). */
  function deSide(fight, de) {
    try {
      if (!de || !fight) return null;
      if (fight.pb && de === fight.pb.nf) return "Me";
      if (fight.yb && de === fight.yb.nf) return "Enemy";
    } catch (e) { /* ignore */ }
    return null;
  }

  /* Duck-typed animation/move name for a `kd` candidate (`Ti`, `.animation`)
   * or a bare animation object. */
  function animLabel(x) {
    if (x == null) return null;
    try {
      if (typeof x === "string") return x;
      var a = x.animation != null ? x.animation : x;
      var cands = ["name", "Eza", "jb", "ID", "FileName"];
      for (var i = 0; i < cands.length; i++) {
        var v = a[cands[i]];
        if (typeof v === "string" && v) return v;
      }
    } catch (e) { /* ignore */ }
    return "?";
  }

  function deProtoOf(de) {
    try {
      var p = Object.getPrototypeOf(de);
      if (p && typeof p.Pqb === "function") return p;
    } catch (e) { /* ignore */ }
    return null;
  }

  /* Wrap `de.prototype.Pqb` / `de.prototype.ia` (the class methods). The AI
   * main decision (`Pqb`) and the per-frame pass (`ia`) live on the PROTOTYPE,
   * so one wrap covers every de instance (current and future). The wrapper
   * records the branch (`fk`/`aqa`), the return (chosen move) and the
   * candidate count (`ld.length`). */
  function wrapDeProto(proto) {
    if (!proto || proto.__orPqbProto) return false;
    if (typeof proto.Pqb !== "function") return false;
    var origPqb = proto.Pqb;
    proto.Pqb = function (a) {
      var r = origPqb.apply(this, arguments);
      try {
        or.cPqb++;
        var side = deSide(or.fight, this) || "X";
        or.lastDecider = side;
        var entry = {
          m: "Pqb", side: side, f: frameNo(),
          fk: num(this.fk), aqa: num(this.aqa),
          ret: moveLabel(r),
          cand: (this.ld && typeof this.ld.length === "number") ? this.ld.length : null
        };
        pushTrace(entry);
        or.lastPqb[side] = entry;
        if (entry.ret != null) or.lastMove[side] = entry.ret;
      } catch (e) { /* ignore */ }
      return r;
    };
    if (typeof proto.ia === "function") {
      var origIa = proto.ia;
      proto.ia = function (a, b) {
        var fk0 = num(this.fk);
        var r = origIa.apply(this, arguments);
        try {
          or.cIa++;
          var side = deSide(or.fight, this) || "X";
          or.lastDecider = side;
          pushTrace({
            m: "de.ia", side: side, f: frameNo(),
            fk0: fk0, fk1: num(this.fk), aqa: num(this.aqa),
            ret: moveLabel(r)
          });
          var lbl = moveLabel(r);
          if (lbl != null) or.lastMove[side] = lbl;
        } catch (e) { /* ignore */ }
        return r;
      };
    }
    proto.__orPqbProto = true;
    return true;
  }

  function wrapFightMethod(target, rootName, name) {
    var found = findMethodDeep(target, name);
    if (!found) return null;
    var host = found.host;
    try {
      if (host.__orWrapped && host.__orWrapped[name]) {
        return found.path; /* already wrapped */
      }
    } catch (e) { /* ignore */ }
    var orig = found.fn;
    if (name === "iPa") {
      host[name] = function () {
        var x0 = num(this.xU);
        var r = orig.apply(this, arguments);
        try {
          pushTrace({ m: "iPa", f: frameNo(), xU0: x0, xU1: num(this.xU) });
        } catch (e) { /* ignore */ }
        return r;
      };
    } else if (name === "N0a" || name === "O0a") {
      var isPress = (name === "N0a");
      host[name] = function (a) {
        var r = orig.apply(this, arguments);
        try {
          if (isPress) or.cN0a++; else or.cO0a++;
          var det = inputDetail(a);
          pushTrace({ m: name, f: frameNo(), control: det.c,
            index: det.x, eu: num(this.eu) });
          if (isPress) {
            or.n0aRecent.push({ f: frameNo(), control: det.c });
            while (or.n0aRecent.length > N0A_RECENT) or.n0aRecent.shift();
          }
          emitInputRec(name, det);
        } catch (e) { /* ignore */ }
        return r;
      };
    } else {
      return null;
    }
    try {
      host.__orWrapped = host.__orWrapped || {};
      host.__orWrapped[name] = true;
      host.__orOrig = host.__orOrig || {};
      if (!host.__orOrig[name]) host.__orOrig[name] = orig;
      or.methodHosts = or.methodHosts || {};
      or.methodHosts[name] = { host: host };
    } catch (e) { /* ignore */ }
    return rootName + "." + found.path;
  }

  function ensureFightHooks(fight) {
    /* Retry the timer-host search while iPa is still unhooked (the screen
     * graph may complete a few ticks after the fight object appears). */
    if (or.hookedFight === fight && or.hookedPaths.iPa.length) return;
    if (or.hookedFight !== fight) {
      or.hookedFight = fight;
      or.hookedPaths = { iPa: [], N0a: [], O0a: [] };
    }
    /* N0a/O0a live on the fight controller (ca, top screen's Ig). */
    ["N0a", "O0a"].forEach(function (nm) {
      try {
        var p = wrapFightMethod(fight, "fight", nm);
        if (p && or.hookedPaths[nm].indexOf(p) < 0) or.hookedPaths[nm].push(p);
      } catch (e) { /* ignore */ }
    });
    try {
      var host = timerHost(fight);
      if (host) {
        var hit = findMethod(host, "iPa");
        if (hit && !(hit.host.__orWrapped && hit.host.__orWrapped.iPa)) {
          var orig = hit.fn;
          var h2 = hit.host;
          h2.iPa = function () {
            var x0 = num(this.xU);
            var r = orig.apply(this, arguments);
            try {
              or.ciPa++;
              pushTrace({ m: "iPa", f: frameNo(), xU0: x0, xU1: num(this.xU) });
            } catch (e2) { /* ignore */ }
            return r;
          };
          try {
            h2.__orWrapped = h2.__orWrapped || {};
            h2.__orWrapped.iPa = true;
          } catch (e2) { /* ignore */ }
        }
        if (or.timerHostPath &&
            or.hookedPaths.iPa.indexOf(or.timerHostPath) < 0) {
          or.hookedPaths.iPa.push(or.timerHostPath);
        }
      }
    } catch (e) { /* ignore */ }
  }

  function ensureDeHooks(fight) {
    /* Once the de PROTOTYPE is wrapped, every de instance is covered, so no
     * scan is needed. (The old per-instance BFS scan missed the real fight's
     * de entirely — the fight graph is built a few ticks after the fight
     * object appears, and the scan cached the empty result: `deCount:0`,
     * `Pqb:0` in every boss-lynx record.) */
    if (or.deProtoWrapped) return;
    /* Direct: the fighter's own AI (JS `this.nf = new de(...)`). */
    var inst = null;
    try { if (fight.pb && isDe(fight.pb.nf)) inst = fight.pb.nf; } catch (e) { /* ignore */ }
    if (!inst) {
      try { if (fight.yb && isDe(fight.yb.nf)) inst = fight.yb.nf; } catch (e) { /* ignore */ }
    }
    var proto = inst ? deProtoOf(inst) : null;
    /* Fallback: BFS over the fight graph (covers a sub-fighter's de). */
    if (!proto) {
      try {
        var queue = [{ o: fight, d: 0 }];
        var seen = [fight];
        var visited = 0;
        while (queue.length && visited < 600 && !proto) {
          var cur = queue.shift();
          if (cur.d > 0 && isDe(cur.o)) { proto = deProtoOf(cur.o); break; }
          if (cur.d >= 5) continue;
          var keys = null;
          try {
            if (cur.o && (typeof cur.o === "object" || typeof cur.o === "function")) {
              keys = Object.keys(cur.o);
            }
          } catch (e) { continue; }
          if (!keys) continue;
          for (var i = 0; i < keys.length; i++) {
            var v = null;
            try { v = cur.o[keys[i]]; } catch (e2) { continue; }
            if (v && (typeof v === "object" || typeof v === "function")) {
              var known = false;
              for (var m = 0; m < seen.length; m++) {
                if (seen[m] === v) { known = true; break; }
              }
              if (!known) {
                seen.push(v);
                visited++;
                queue.push({ o: v, d: cur.d + 1 });
              }
            }
          }
        }
      } catch (e) { /* ignore */ }
    }
    if (!proto) return;
    if (wrapDeProto(proto)) or.deProtoWrapped = true;
    /* Header labels: the fighters' own de instances. */
    or.deList = [];
    try { if (fight.pb && isDe(fight.pb.nf)) or.deList.push({ obj: fight.pb.nf, label: "Me" }); }
    catch (e) { /* ignore */ }
    try { if (fight.yb && isDe(fight.yb.nf)) or.deList.push({ obj: fight.yb.nf, label: "Enemy" }); }
    catch (e) { /* ignore */ }
    or.deCount = or.deList.length;
  }

  /* Random-tactic path (JS `wd.Anb` L499 -> `hJa` L500 -> `ca.Vgb` L508 ->
   * `Gc.Vkb` L671 -> `Ih(2,Vb,!0)` -> the next `Gc.ia`/`Gnb` pass builds the
   * per-fighter candidate lists (`Rwa`/`EZa`) and dispatches `Gc.DK` ->
   * `Gc.Pkb` -> `Gc.jL` = the `Md.jL` weighted roulette). A `Type=="Random"`
   * tactic NEVER calls `de.Pqb`/`de.ia` (`Anb` routes type 1 to `hJa`), so the
   * de wrappers above record nothing for it; these record the candidate set +
   * the roulette pick. The `Gc` instance is `ca.Bg` (`this.Bg = new Gc`,
   * L15469) — reachable from the fight controller (`or.fight` = the `ca`). */
  function ensureRandomHooks(fight) {
    if (!or.gcProtoWrapped) {
      try {
        var gc = fight.Bg;
        var proto = gc ? Object.getPrototypeOf(gc) : null;
        if (proto && typeof proto.jL === "function") {
          var origJL = proto.jL;
          proto.jL = function (a, b) {
            var r = origJL.apply(this, arguments);
            try {
              if (b && b.length) {
                var names = [];
                for (var i = 0; i < b.length; i++) names.push(animLabel(b[i]));
                var side = deSide(or.fight, a && a.nf) || "?";
                var chosen = animLabel(r);
                var refs = {};
                try { refs.mpivot = Math.round(a.da.Fe().ma.x * 100) / 100; } catch (e) { /* ignore */ }
                try { refs.mroot = Math.round(a.oa.Fe().ma.x * 100) / 100; } catch (e) { /* ignore */ }
                try { refs.enpivot = Math.round(a.jb.da.Fe().ma.x * 100) / 100; } catch (e) { /* ignore */ }
                try { refs.enn = Math.round(a.jb.oa.Ic("NPivot").ma.x * 100) / 100; } catch (e) { /* ignore */ }
                try { refs.mn = Math.round(a.oa.Ic("NPivot").ma.x * 100) / 100; } catch (e) { /* ignore */ }
                var entry = {
                  m: "Gc.jL", side: side, f: frameNo(),
                  cand: names, n: b.length, chosen: chosen, refs: refs
                };
                pushTrace(entry);
                or.lastRandom[side] = entry;
                if (chosen != null) {
                  or.lastMove[side] = chosen;
                  or.lastDecider = side;
                }
              }
            } catch (e) { /* ignore */ }
            return r;
          };
          proto.__orGcProto = true;
          or.gcProtoWrapped = true;
        }
        if (proto && typeof proto.Pkb === "function" && !proto.__orPkbProto) {
          var origPkb = proto.Pkb;
          proto.Pkb = function (a, b) {
            try {
              if (b && b.length) {
                var pn = [];
                var pq = [];
                for (var i = 0; i < b.length; i++) {
                  pn.push(animLabel(b[i]));
                  /* `M7.$Q` (the `Pu` mirror-exclusive list, `ra.b1a` L684) +
                   * `priority`: the exact per-candidate data the `M7.Wcb`
                   * filter (L676) and the `Aua` max-priority pick consume. */
                  var q = [];
                  try {
                    var qq = b[i] && b[i].animation && b[i].animation.M7
                        ? b[i].animation.M7.$Q : null;
                    if (qq) { for (var qi = 0; qi < qq.length; qi++) q.push(animLabel(qq[qi])); }
                  } catch (e) { /* ignore */ }
                  var pr = 0;
                  try { pr = b[i].animation.priority | 0; } catch (e) { /* ignore */ }
                  pq.push({ n: pn[pn.length - 1], p: pr, q: q });
                }
                var pside = deSide(or.fight, a && a.nf) || "?";
                or.lastPkbPre[pside] = { f: frameNo(), n: b.length, cand: pn, det: pq };
                if (!or.meList) or.meList = {};
                if (!or.meList[pside]) {
                  var me = [];
                  try {
                    if (a && a.me && a.me.length) {
                      for (var j = 0; j < a.me.length; j++) me.push(animLabel(a.me[j]));
                    }
                  } catch (e) { /* ignore */ }
                  var d2 = [];
                  try {
                    var dl = a && a.Su && typeof a.Su.dea === "function" ? a.Su.dea(2) : null;
                    if (dl && dl.length) {
                      for (var j2 = 0; j2 < dl.length; j2++) d2.push(animLabel(dl[j2]));
                    }
                  } catch (e) { /* ignore */ }
                  or.meList[pside] = { me: me, dea2: d2 };
                }
                if (!or.distHooked) {
                  try {
                    var mv0 = b[0].animation;
                    var cc0 = null;
                    for (var bi = 0; bi < b.length && cc0 == null; bi++) {
                      var mvq = b[bi].animation;
                      var tsq = mvq && mvq.va ? mvq.va.Ts : null;
                      if (!tsq) continue;
                      for (var q = 0; q < tsq.length; q++) {
                        if (tsq[q] && typeof tsq[q].FK !== "undefined") { cc0 = tsq[q]; break; }
                      }
                    }
                    console.log(JSON.stringify({ t: "disthook", found: !!cc0,
                      ts0: !!(mv0 && mv0.va && mv0.va.Ts && mv0.va.Ts.length) }));
                    var cproto = cc0 ? Object.getPrototypeOf(cc0) : null;
                    if (cproto && typeof cproto.he === "function" && !cproto.__orDist) {
                      var ohe = cproto.he;
                      cproto.he = function (ct) {
                        var r = ohe.apply(this, arguments);
                        try {
                          var ff = frameNo();
                          if (this.qdb === 0 && this.FK && this.GK && ct &&
                              ff != null && ff >= 325 && ff <= 340) {
                            var fx = this.FK.OQ(ct), tx = this.GK.OQ(ct);
                            var bb = (tx - fx) * ct.Wl;
                            var ydx = null;
                            try {
                              var yd = this.FK.CBa(ct).Yd;
                              ydx = yd && yd.ma ? Math.round(yd.ma.x * 100) / 100 : null;
                            } catch (e2) { /* ignore */ }
                            console.log(JSON.stringify({ t: "distlog", f: ff,
                              mv: or.curMove, mn: this.min, mx: this.max,
                              fx: Math.round(fx * 100) / 100,
                              tx: Math.round(tx * 100) / 100, wl: ct.Wl,
                              ydx: ydx,
                              b: Math.round(bb * 100) / 100, r: r }));
                          }
                        } catch (e) { /* ignore */ }
                        return r;
                      };
                      cproto.__orDist = true;
                    }
                    var mproto = mv0 ? Object.getPrototypeOf(mv0) : null;
                    if (mproto && typeof mproto.nw === "function" && !mproto.__orNw) {
                      var onw = mproto.nw;
                      mproto.nw = function () {
                        or.curMove = this.name;
                        var rr = onw.apply(this, arguments);
                        or.curMove = null;
                        return rr;
                      };
                      mproto.__orNw = true;
                    }
                    or.distHooked = true;
                  } catch (e) { /* ignore */ }
                }
              }
            } catch (e) { /* ignore */ }
            return origPkb.apply(this, arguments);
          };
          proto.__orPkbProto = true;
        }
      } catch (e) { /* ignore */ }
    }
    if (!or.hjaWrapped) {
      try {
        var f = fight.pb || fight.yb;
        var fp = f ? Object.getPrototypeOf(f) : null;
        if (fp && typeof fp.hJa === "function") {
          var origHja = fp.hJa;
          fp.hJa = function (a) {
            var tG0 = num(this.tG), wN0 = !!this.wN;
            var r = origHja.apply(this, arguments);
            try {
              var side = deSide(or.fight, this.nf) || "?";
              var fr = frameNo();
              pushTrace({ m: "hJa", side: side, f: fr,
                tG0: tG0, wN0: wN0, tG1: num(this.tG), wN1: !!this.wN });
              /* Per-frame `hJa` countdown record (the Random-tactic arm/fire
               * timeline): the same values as the `__trace` ring entry, also
               * emitted to the console so the native port's `tG`/`wN` can be
               * diffed frame-by-frame against the JS. */
              emit({ t: "hja", f: fr, side: side, tG0: tG0, wN0: wN0,
                tG1: num(this.tG), wN1: !!this.wN });
              var e2 = or.lastRandom[side];
              if (e2) e2.hja = { f: frameNo(), tG0: tG0, tG1: num(this.tG) };
            } catch (e) { /* ignore */ }
            return r;
          };
          fp.__orHja = true;
          or.hjaWrapped = true;
        }
      } catch (e) { /* ignore */ }
    }
    /* The `Md.jL` weighted-roulette roll: `Da.pg.s4(sum)` (L26988) — the raw
     * `Da.pg` draw the Random-tactic pick consumes. Logging it lets the port's
     * `[random] roll=` be diffed against the JS draw directly (the frame alone
     * cannot distinguish "same roll, different list" from "different roll"). */
    if (!or.pgWrapped) {
      try {
        var pg = null;
        /* Diagnostic export injected into the served bundle: `window.__orDa`
         * is the `Da` class (static `pg` = the shared `Rk` stream, JS L2701
         * `Da.pg=new Rk(L.seed)`). The bundle is a closure so `Da` is not a
         * global; the export is a 1-line harness patch reverted after the
         * trace. Falls back to the old instance scan if absent. */
        try {
          if (window.__orDa && window.__orDa.pg) pg = window.__orDa.pg;
        } catch (eDa) { /* ignore */ }
        try {
          if (!pg && or.fight.Da && or.fight.Da.constructor &&
              or.fight.Da.constructor.pg) {
            pg = or.fight.Da.constructor.pg;
          }
        } catch (e0) { /* ignore */ }
        if (!pg) {
          var ks = Object.keys(or.fight);
          for (var ki = 0; ki < ks.length && !pg; ki++) {
            var vv = null;
            try { vv = or.fight[ks[ki]]; } catch (e1) { continue; }
            if (vv && vv.constructor && vv.constructor.pg) {
              pg = vv.constructor.pg;
            }
          }
        }
        if (pg && typeof pg.s4 === "function") {
          var origS4 = pg.s4;
          pg.s4 = function (a) {
            var r = origS4.apply(this, arguments);
            try {
              if (a > 50) emit({ t: "roll", f: frameNo(), d: a, r: r });
            } catch (e2) { /* ignore */ }
            return r;
          };
          /* Per-draw counter (SF2_TRACE_PRNG analog): every `Rk.jf()` draw
           * (s4/dT/cT all funnel through jf) is logged with its 1-based index
           * and value plus the consuming JS caller frame, so the JS and port
           * draw SEQUENCES can be diffed index-by-index. */
          if (!or.pgJfWrapped) {
            var origJf = pg.jf;
            or.pgDraw = 0;
            pg.jf = function () {
              var v = origJf.apply(this, arguments);
              try {
                or.pgDraw++;
                var c = "";
                var c2 = "";
                try {
                  var st = new Error().stack || "";
                  var ls = st.split("\n");
                  c = (ls[2] || "").trim();
                  c2 = (ls[3] || "").trim() + " | " + (ls[4] || "").trim();
                } catch (e4) { /* ignore */ }
                emit({ t: "draw", i: or.pgDraw, v: v, f: frameNo(), c: c, c2: c2 });
              } catch (e3) { /* ignore */ }
              return v;
            };
            or.pgJfWrapped = true;
          }
          or.pgWrapped = true;
        }
      } catch (e) { /* ignore */ }
    }
  }

  function deOfLabel(fight, label) {
    try {
      if (label === "Me" || label === "Enemy") return deOf(fight, label);
      if (or.deList) {
        for (var i = 0; i < or.deList.length; i++) {
          if (or.deList[i].label === label) return or.deList[i].obj;
        }
      }
    } catch (e) { /* ignore */ }
    return null;
  }

  /* ================= 5. State readers ================= */

  /* Evaluated per-frame AI chance values on the de instance (the numbers the
   * decision actually consumes: dqb trio + mQ rolls + tactic thresholds). */
  var CHANCE_KEYS = ["CZ", "bda", "tba", "tua", "dua", "Bpa", "rqa", "oqa",
    "qPa", "vO", "Awa", "pua", "oua", "eh", "fk", "aqa", "oC"];

  function chancesOf(de) {
    var out = {};
    if (!de) return out;
    for (var i = 0; i < CHANCE_KEYS.length; i++) {
      try {
        var v = de[CHANCE_KEYS[i]];
        if (typeof v === "number" && isFinite(v)) out[CHANCE_KEYS[i]] = v;
      } catch (e) { /* ignore */ }
    }
    return out;
  }

  /* WC input buffer: -1 = empty, else the buffered input (duck-typed). */
  function wcState(f) {
    var empty = true, control = null;
    try {
      var w = f && f.WC;
      empty = (w === -1 || w == null);
      if (!empty && w && typeof w.control !== "undefined") control = w.control;
    } catch (e) { /* ignore */ }
    return { empty: empty, control: control };
  }

  /* Authoritative block state: the game's own interval query (also used by
   * CurrentInterval conditions every frame, so it is side-effect free). */
  function blockInfo(f) {
    try {
      var da = f && f.da;
      if (!da || typeof da.yD !== "function") return { a: 0, n: null };
      var iv = da.yD(5);
      if (iv == null) return { a: 0, n: null };
      var n = null;
      try {
        if (typeof iv.name === "string") n = iv.name;
        else if (typeof iv.Name === "string") n = iv.Name;
      } catch (e) { /* ignore */ }
      return { a: 1, n: n };
    } catch (e) { return { a: 0, n: null }; }
  }

  function cfOf(f) {
    try {
      var da = f && f.da;
      if (da && typeof da.Xh === "number") return da.Xh;
    } catch (e) { /* ignore */ }
    return null;
  }

  function camOf(fight) {
    var cx = 0, cy = 0, zoom = 1;
    try {
      if (fight.Ta && fight.Ta.Go && fight.Ta.Go.ma) {
        var m = fight.Ta.Go.ma;
        if (typeof m.x === "number" && isFinite(m.x)) cx = m.x;
        if (typeof m.y === "number" && isFinite(m.y)) cy = m.y;
      }
    } catch (e) { /* ignore */ }
    try {
      if (fight.Ta && fight.Ta.ia && typeof fight.Ta.ia.Bj === "number" &&
          isFinite(fight.Ta.ia.Bj)) {
        zoom = fight.Ta.ia.Bj;
      }
    } catch (e) { /* ignore */ }
    return { cx: cx, cy: cy, zoom: zoom };
  }

  function deOf(fight, side) {
    try {
      var f = fighterAt(fight, side);
      if (f && isDe(f.nf)) return f.nf;
    } catch (e) { /* ignore */ }
    return null;
  }

  /* ================= 5b. Render-state dump (phase1 step9 residuals) =====
   * Static analysis left two residuals unresolved:
   *   (a) the shop/profile destination "scrim" multiplier (a measured
   *       port/oracle luminance ratio of ~0.64 on the Profile backdrop),
   *   (b) the ~48px fight-centre enemy offset.
   * Both are decided at runtime by the display graph, so this section walks
   * the live `Ke` display list (verbatim engine objects) and emits the raw
   * node transforms/colours/alphas:
   *   - every display node: wrapper class, alpha (`mn()`), alpha effect
   *     (`Ke.oQ(5).opacity`), visible (`Ke.Kw`), local x/y (`ya`/`ra`),
   *     scale (`Eb`/`Rm`), size (`fa`), tint/colour (`.sf()` -> RGBA),
   *   - the `Pi` destination object (`Qa` bg sprite + `W9` full-screen
   *     `Fc.Ed(1342177280, node.L)` dim quad) and `ma.KI` (global fade rect),
   *   - `ma.Kq` HUD scale rect, renderer `clearColor`,
   *   - at a fight frame: each fighter's root-node transform (`oa.Fe().ma`)
   *     and the skeleton screen-space bbox.
   * Records are emitted as "[ORACLE] {t:'render_screen'|'render_fight',...}"
   * on screen-class change / periodically, so the volume stays bounded.
   */

  function rnum(v) {
    return (typeof v === "number" && isFinite(v)) ? v : null;
  }

  function r5(v) {
    return typeof v === "number" && isFinite(v) ? Math.round(v * 1e5) / 1e5 : null;
  }

  /* RGBA of a node wrapper (R/Fc/...): `sf()` returns the colour effect H. */
  function nodeColor(n) {
    try {
      if (n && typeof n.sf === "function") {
        var c = n.sf();
        if (c && typeof c.x === "number" && isFinite(c.x)) {
          return { r: r5(c.x), g: r5(c.y), b: r5(c.z), a: r5(c.w) };
        }
      }
    } catch (e) { /* ignore */ }
    return null;
  }

  function nodeAlpha(n) {
    try {
      if (n && typeof n.mn === "function") return r5(n.mn());
    } catch (e) { /* ignore */ }
    return null;
  }

  /* Alpha effect directly on the Ke display object (oQ(5) = `vf` opacity). */
  function keAlphaEff(ke) {
    try {
      if (ke && typeof ke.oQ === "function") {
        var ef = ke.oQ(5);
        if (ef && typeof ef.opacity === "number") return r5(ef.opacity);
      }
    } catch (e) { /* ignore */ }
    return null;
  }

  /* One display node -> compact record. `n` is the wrapper (Ke.Ze). */
  function nodeRec(n, ke, depth) {
    var rec = { d: depth };
    try { rec.cn = (n && n.constructor && n.constructor.name) || null; } catch (e) { /* ignore */ }
    var a = nodeAlpha(n); if (a != null) rec.a = a;
    var ae = keAlphaEff(ke); if (ae != null) rec.ae = ae;
    try { rec.vis = (ke && typeof ke.Kw !== "undefined") ? !!ke.Kw : null; } catch (e) { /* ignore */ }
    try {
      if (n) {
        rec.x = rnum(n.ya); rec.y = rnum(n.ra);
        rec.sx = rnum(n.Eb); rec.sy = rnum(n.Rm);
        if (n.fa) { rec.w = rnum(n.fa.x); rec.h = rnum(n.fa.y); }
      }
    } catch (e) { /* ignore */ }
    try { if (ke && typeof ke.name === "string" && ke.name) rec.nm = ke.name; } catch (e) { /* ignore */ }
    var col = nodeColor(n); if (col) rec.col = col;
    return rec;
  }

  /* Walk a Ke sub-tree (children are a sibling linked list via `.Ma`). */
  function walkKe(ke, out, depth) {
    if (!ke || depth > 10 || out.length > 3000) return;
    var c = null;
    try { c = ke.children; } catch (e) { return; }
    var guard = 0;
    while (c && guard++ < 4000 && out.length < 3000) {
      var rec = null;
      try {
        rec = nodeRec(c.Ze, c, depth);
      } catch (e) { rec = { d: depth, err: 1 }; }
      out.push(rec);
      try { walkKe(c, out, depth + 1); } catch (e) { /* ignore */ }
      try { c = c.Ma; } catch (e) { break; }
    }
  }

  /* Renderer clear colour: `Pf.clearColor` (H) reached via app/stage. */
  function clearColorOf() {
    var cands = [];
    try { if (or.app) cands.push(or.app.Ha); } catch (e) { /* ignore */ }
    try { if (or.mc && or.mc.K) { cands.push(or.mc.K.Ha); cands.push(or.mc.K); } } catch (e) { /* ignore */ }
    for (var i = 0; i < cands.length; i++) {
      try {
        var cc = cands[i] && cands[i].clearColor;
        if (cc && typeof cc.x === "number") {
          return { r: r5(cc.x), g: r5(cc.y), b: r5(cc.z), a: r5(cc.w) };
        }
      } catch (e) { /* ignore */ }
    }
    return null;
  }

  function staticOf(screen, key) {
    try { return screen.constructor ? screen.constructor[key] : null; } catch (e) { return null; }
  }

  function renderScreenRecord(tag) {
    try {
      if (!or.mc || !or.mc.stack || !or.mc.stack.length) return;
      var stack = or.mc.stack;
      var top = stack[stack.length - 1];
      var out = {
        t: "render_screen", tag: tag, f: frameNo(),
        screen: (top && top.constructor) ? top.constructor.name : null,
        stack: stack.length
      };
      try { out.screen_mLa = (top && typeof top.dJ === "function") ? top.dJ() : null; } catch (e) { /* ignore */ }
      try { out.screen_alpha = nodeAlpha(top); } catch (e) { /* ignore */ }
      out.clearColor = clearColorOf();
      try {
        var ki = staticOf(top, "KI"); /* global fade rect `ma.KI` */
        if (ki) {
          out.ma_KI = { a: nodeAlpha(ki), col: nodeColor(ki) };
        }
      } catch (e) { /* ignore */ }
      try {
        var kq = staticOf(top, "Kq"); /* HUD scale rect `ma.Kq` */
        if (kq) out.ma_Kq = { N: rnum(kq.N), P: rnum(kq.P), W: rnum(kq.W) };
      } catch (e) { /* ignore */ }
      /* The `Pi` destination of the current screen (`this.Ad`). */
      try {
        var ad = top && top.Ad;
        if (ad) {
          out.Pi = {
            a: nodeAlpha(ad),
            J9: (function () { try { return ad.J9 ? { x: rnum(ad.J9.x), y: rnum(ad.J9.y) } : null; } catch (e) { return null; } })(),
            Qa: nodeRec(ad.Qa, ad.Qa && ad.Qa.L, 0),
            Qa_col: nodeColor(ad.Qa),
            W9: nodeRec(ad.W9, ad.W9 && ad.W9.L, 0),
            W9_col: nodeColor(ad.W9)
          };
        }
      } catch (e) { /* ignore */ }
      /* Full display tree of every stacked screen + the root stage container. */
      var recs = [];
      for (var i = 0; i < stack.length; i++) {
        try { walkKe(stack[i].node && stack[i].node.L, recs, 0); } catch (e) { /* ignore */ }
      }
      try { if (or.mc.K && or.mc.K.cf) walkKe(or.mc.K.cf, recs, 0); } catch (e) { /* ignore */ }
      out.nodes = recs;
      emit(out);
    } catch (e) { orFail(e); }
  }

  /* Fight-frame fighter geometry: root-node transform + screen bbox. */
  function fighterRec(f, side) {
    var o = { side: side };
    try { o.cls = (f && f.constructor) ? f.constructor.name : null; } catch (e) { /* ignore */ }
    try {
      if (f && f.oa && typeof f.oa.Fe === "function") {
        var r = f.oa.Fe();
        if (r && r.ma) o.root = { x: rnum(r.ma.x), y: rnum(r.ma.y), z: rnum(r.ma.z) };
      }
    } catch (e) { /* ignore */ }
    /* `oa.Eu` = the `_CenterOfMass_` node the JS camera targets
     * (`Dl.mea(a.Eu,b.Eu)` -> midpoint). Dump it to compare with the port's
     * `world_x()` anchor (the fight-centre residual). */
    try {
      if (f && f.oa && f.oa.Eu && f.oa.Eu.ma) {
        o.com = { x: rnum(f.oa.Eu.ma.x), y: rnum(f.oa.Eu.ma.y), z: rnum(f.oa.Eu.ma.z) };
      }
    } catch (e) { /* ignore */ }
    /* NotAI flag (`parameters.Fj`): true = human-controlled. The AI side is
     * the true enemy; this pins which of fight.pb/fight.yb it is. */
    try {
      o.notai = (f && f.parameters) ? !!f.parameters.Fj : null;
    } catch (e) { /* ignore */ }
    try {
      if (f && f.parameters && f.parameters.position) {
        o.px = rnum(f.parameters.position.x);
        o.py = rnum(f.parameters.position.y);
      }
    } catch (e) { /* ignore */ }
    try { o.facing = (f && typeof f.hd === "function") ? rnum(f.hd()) : null; } catch (e) { /* ignore */ }
    try { o.rr = (f && f.rR) ? 1 : 0; } catch (e) { /* ignore */ }
    try {
      var ns = f.oa && f.oa.Va && f.oa.Va.all;
      if (ns) {
        var mnx = 1e9, mxx = -1e9, mny = 1e9, mxy = -1e9;
        for (var i = 0; i < ns.length; i++) {
          var m = ns[i] && ns[i].ma;
          if (m && typeof m.x === "number" && isFinite(m.x)) {
            if (m.x < mnx) mnx = m.x; if (m.x > mxx) mxx = m.x;
            if (m.y < mny) mny = m.y; if (m.y > mxy) mxy = m.y;
          }
        }
        if (mxx > mnx) o.bbox = { x0: rnum(mnx), x1: rnum(mxx), y0: rnum(mny), y1: rnum(mxy) };
      }
    } catch (e) { /* ignore */ }
    return o;
  }

  function renderFightRecord(fight) {
    try {
      var out = {
        t: "render_fight", tag: "fight", f: frameNo(),
        camera: camOf(fight),
        fighters: [
          fighterRec(fighterAt(fight, "Me"), "Me"),
          fighterRec(fighterAt(fight, "Enemy"), "Enemy")
        ]
      };
      try {
        var mgr = or.mc, top = mgr && mgr.stack && mgr.stack[mgr.stack.length - 1];
        var ki = staticOf(top, "KI");
        if (ki) out.ma_KI = { a: nodeAlpha(ki), col: nodeColor(ki) };
        out.clearColor = clearColorOf();
      } catch (e) { /* ignore */ }
      emit(out);
    } catch (e) { orFail(e); }
  }

  /* Per-frame driver for the render dump (runs on ALL screens, unlike
   * `oracleTick` which needs a fight object). */
  function renderTick() {
    if (window.__oracleDone) return;
    try {
      if (!or.mc || !or.mc.stack || !or.mc.stack.length) return;
      var top = or.mc.stack[or.mc.stack.length - 1];
      var cls = (top && top.constructor) ? top.constructor.name : "?";
      if (cls !== or.lastRTag) {
        or.lastRTag = cls;
        renderScreenRecord("screen_change");
      } else if (or.rFrames % 240 === 0) {
        renderScreenRecord("periodic");
      }
      or.rFrames++;
      var f = fightObj();
      if (f) {
        if (or.rFightFrames % 30 === 0) renderFightRecord(f);
        or.rFightFrames++;
      }
    } catch (e) { orFail(e); }
  }

  /* ================= 6. Records ================= */

  function emitHeader(fight) {
    var pqb = {};
    var ia = {};
    try {
      if (or.deList) {
        for (var i = 0; i < or.deList.length; i++) {
          var lb = or.deList[i].label;
          var dd = or.deList[i].obj;
          pqb[lb] = !!dd.__orPqb;
          ia[lb] = !!dd.__orIa;
        }
      }
    } catch (e) { /* ignore */ }
    emit({
      t: "oracle_header",
      js: "sf2.502f0946.js",
      harness: window.__oracleHarness,
      ai_side: aiSide(fight),
      hooked: {
        iPa: or.hookedPaths.iPa.slice(),
        N0a: or.hookedPaths.N0a.slice(),
        O0a: or.hookedPaths.O0a.slice(),
        Pqb: pqb,
        "de.ia": ia
      }
    });
  }

  function oracleRecord(fight) {
    var side = or.lastDecider || aiSide(fight);
    var de = deOfLabel(fight, side);
    var me = fighterAt(fight, "Me");
    var en = fighterAt(fight, "Enemy");
    var wMe = wcState(me);
    var wEn = wcState(en);
    var bMe = blockInfo(me);
    var bEn = blockInfo(en);
    var pqb = or.lastPqb[side] || null;
    var tmr = timerOf(fight);
    return {
      t: "oracle",
      f: num(fight.frame),
      phase: num(fight.eu),
      round: (fight.round && typeof fight.round.round === "number")
        ? fight.round.round
        : null,
      cf: cfOf(me),
      cf_enemy: cfOf(en),
      ai_side: side,
      ai_branch: de ? num(de.fk) : null,
      ai_zone: de ? num(de.aqa) : null,
      chosen_move: or.lastMove[side],
      chosen_candidates: pqb && typeof pqb.cand === "number" ? pqb.cand : null,
      random: or.lastRandom[side] || null,
      random_pre: or.lastPkbPre[side] || null,
      me_list: or.meList ? (or.meList[side] || null) : null,
      chances: chancesOf(de),
      input_buffer_state: {
        me_empty: wMe.empty,
        me_control: wMe.control,
        enemy_empty: wEn.empty,
        enemy_control: wEn.control,
        recent: or.n0aRecent.slice()
      },
      round_timer_xU: tmr.xU,
      round_timer_NF: tmr.NF,
      trace_stats: { Pqb: or.cPqb, ia: or.cIa, N0a: or.cN0a, O0a: or.cO0a,
        iPa: or.ciPa, deCount: or.deCount, decider: or.lastDecider },
      block_state: { me: bMe.a, enemy: bEn.a },
      block_info: { me: bMe.n, enemy: bEn.n },
      camera: camOf(fight)
    };
  }

  /* ================= 7b. Frame-exact stimulus (page side) ================= */
  /* The shell forwards the fixed input script's `atframe` commands once via
   * window.__oracleStimulus. Items fire IN-TICK (before the tick simulates),
   * so landing frames are exact and identical across runs. Tap/key hold =
   * down at tick F, up at tick F+7 (~117ms, mirrors shell TapHoldMs). Drag =
   * down at (x1,y1) F, moves through F+3/F+5, up at (x2,y2) F+8. */
  var STIM_HOLD_FRAMES = 7;
  var STIM_DRAG_UP = 8;

  function pollStimulus() {
    try {
      if (!or.stimulus && window.__oracleStimulus &&
          window.__oracleStimulus.length) {
        or.stimulus = window.__oracleStimulus;
        console.log("[ORACLE] stimulus armed: " +
          window.__oracleStimulus.length + " items");
      }
    } catch (e) { /* ignore */ }
  }

  function stimTarget() {
    try {
      return document.getElementById("gfx") || document;
    } catch (e) { return document; }
  }

  function firePointer(target, type, x, y, touchType) {
    try {
      var po = {
        bubbles: true, cancelable: true,
        clientX: x, clientY: y,
        pointerId: 1, pointerType: "touch", isPrimary: true
      };
      target.dispatchEvent(new PointerEvent(type, po));
      var touch = new Touch(
        { identifier: 1, target: target, clientX: x, clientY: y });
      var to = {
        bubbles: true, cancelable: true,
        touches: touchType === "touchend" ? [] : [touch],
        targetTouches: touchType === "touchend" ? [] : [touch],
        changedTouches: [touch]
      };
      target.dispatchEvent(new TouchEvent(touchType, to));
    } catch (e) { /* ignore */ }
  }

  function fireMove(it, x, y) {
    firePointer(stimTarget(), "pointermove", x, y, "touchmove");
  }

  function fireDown(it) {
    var target = stimTarget();
    try {
      if (it.t === "key") {
        var kd = new KeyboardEvent("keydown",
          { bubbles: true, cancelable: true, keyCode: it.c, which: it.c });
        target.dispatchEvent(kd);
      } else if (it.t === "drag") {
        firePointer(target, "pointerdown", it.x1, it.y1, "touchstart");
      } else {
        firePointer(target, "pointerdown", it.x, it.y, "touchstart");
      }
    } catch (e) { /* ignore */ }
  }

  function fireUp(it) {
    var target = stimTarget();
    try {
      if (it.t === "key") {
        var ku = new KeyboardEvent("keyup",
          { bubbles: true, cancelable: true, keyCode: it.c, which: it.c });
        target.dispatchEvent(ku);
      } else if (it.t === "drag") {
        firePointer(target, "pointerup", it.x2, it.y2, "touchend");
      } else {
        firePointer(target, "pointerup", it.x, it.y, "touchend");
      }
    } catch (e) { /* ignore */ }
  }

  function stimTickPre() {
    pollStimulus();
    if (!or.stimulus || !or.stimulus.length) return;
    var fight = fightObj();
    if (!fight) return;
    var fr = num(fight.frame);
    if (fr == null) return;
    var i, it;
    for (i = 0; i < or.stimulus.length; i++) {
      it = or.stimulus[i];
      if (it.done) continue;
      if (fr < it.f) continue;
      /* Exact replay items call the game's own input methods directly. */
      if (it.t === "press" || it.t === "release") {
        replayInput(it);
        it.done = true;
        continue;
      }
      if (it.down) continue;
      fireDown(it);
      it.down = true;
      it.upAt = fr + (it.t === "drag" ? STIM_DRAG_UP : STIM_HOLD_FRAMES);
      if (it.t === "drag") {
        it.mx1 = it.f + 3;
        it.mx2 = it.f + 5;
      }
    }
    for (i = 0; i < or.stimulus.length; i++) {
      it = or.stimulus[i];
      if (it.down && !it.up && fr >= it.upAt) {
        fireUp(it);
        it.up = true;
        it.done = true;
      } else if (it.t === "drag" && it.down && !it.done) {
        /* joystick waypoints: mid then 3/4 toward the end */
        if (!it.m1 && fr >= it.mx1) {
          it.m1 = true;
          fireMove(it, (it.x1 + it.x2) / 2, (it.y1 + it.y2) / 2);
        } else if (it.m1 && !it.m2 && fr >= it.mx2) {
          it.m2 = true;
          fireMove(it, (it.x1 + 3 * it.x2) / 4, (it.y1 + 3 * it.y2) / 4);
        }
      }
    }
  }

  /* ================= 7. Per-tick driver ================= */

  function fightObj() {
    var mgr = or.mc;
    if (!mgr || !mgr.stack || mgr.stack.length === 0) return null;
    try {
      var top = mgr.stack[mgr.stack.length - 1];
      if (!top || !top.Ig) return null;
      var fight = top.Ig;
      if (!fight || typeof fight.frame !== "number") return null;
      or.screenTop = top; /* fight screen (Sf): owns iPa + xU/NF timer */
      return fight;
    } catch (e) { return null; }
  }

  /* Round timer state: xU/NF live with iPa (fight screen Sf), NOT on the
   * fight controller. Found by BFS over the screen graph (cached per
   * screen identity); the header reports the path. */
  function bfsFind(root, pred, maxDepth, maxVisit) {
    var seen = [root];
    function seenHas(o) {
      for (var i = 0; i < seen.length; i++) {
        if (seen[i] === o) return true;
      }
      return false;
    }
    var queue = [{ o: root, p: "root", d: 0 }];
    var visited = 0;
    while (queue.length) {
      var cur = queue.shift();
      if (cur.d > 0) {
        var ok = false;
        try { ok = !!pred(cur.o); } catch (e) { /* ignore */ }
        if (ok) return { obj: cur.o, path: cur.p };
      }
      if (cur.d >= maxDepth) continue;
      var keys = null;
      try {
        if (cur.o && (typeof cur.o === "object" || typeof cur.o === "function")) {
          keys = Object.keys(cur.o);
        }
      } catch (e) { continue; }
      if (!keys) continue;
      for (var i = 0; i < keys.length; i++) {
        if (visited >= maxVisit) return null;
        var v = null;
        try { v = cur.o[keys[i]]; } catch (e) { continue; }
        if (v && (typeof v === "object" || typeof v === "function") &&
            !seenHas(v)) {
          seen.push(v);
          visited++;
          queue.push({ o: v, p: cur.p + "." + keys[i], d: cur.d + 1 });
        }
      }
    }
    return null;
  }

  function timerHost(fight) {
    /* Fast path: previously found host still valid. */
    try {
      if (or.timerHostObj && typeof or.timerHostObj.iPa === "function" &&
          typeof or.timerHostObj.xU === "number") {
        return or.timerHostObj;
      }
    } catch (e) { /* ignore */ }
    or.timerHostObj = null;
    or.timerHostPath = null;
    var roots = [];
    try { if (or.screenTop) roots.push(or.screenTop); } catch (e) { /* ignore */ }
    roots.push(fight);
    for (var i = 0; i < roots.length; i++) {
      var hit = null;
      try {
        hit = bfsFind(roots[i], function (o) {
          try {
            return typeof o.iPa === "function" &&
              typeof o.xU === "number" && typeof o.NF === "number";
          } catch (e2) { return false; }
        }, 4, 400);
      } catch (e) { /* ignore */ }
      if (hit) {
        or.timerHostObj = hit.obj;
        or.timerHostPath = (i === 0 ? "screen" : "fight") + "." + hit.path;
        return hit.obj;
      }
    }
    return null;
  }

  function timerOf(fight) {
    var host = null;
    try { host = timerHost(fight); } catch (e) { /* ignore */ }
    try {
      if (host && typeof host.xU === "number" &&
          typeof host.NF === "number") {
        return { xU: host.xU, NF: host.NF };
      }
    } catch (e) { /* ignore */ }
    return { xU: null, NF: null };
  }

  function oracleTick() {
    /* After finish: stay silent so the tail (frames past the done boundary,
     * whose count depends on poll timing) cannot pollute the trace. */
    if (window.__oracleDone) return;
    var fight = fightObj();
    if (!fight) return;
    or.fight = fight;
    ensureFightHooks(fight);
    ensureDeHooks(fight);
    ensureRandomHooks(fight);
    if (!or.headerDone) {
      or.headerDone = true;
      emitHeader(fight);
      try {
        console.log("[ORACLE] hooked iPa=" +
          JSON.stringify(or.hookedPaths.iPa) + " N0a=" +
          JSON.stringify(or.hookedPaths.N0a));
      } catch (e) { /* ignore */ }
    }
    var fr = num(fight.frame);
    if (fr == null || fr === or.lastFrame) return; /* one record per fight frame */
    or.lastFrame = fr;
    emit(oracleRecord(fight));
    or.records++;
    if (fr >= limit("maxFightFrames", DEFAULT_MAX_FIGHT_FRAMES) ||
        or.records >= limit("maxRecords", DEFAULT_MAX_RECORDS)) finish();
  }

  function finish() {
    if (window.__oracleDone) return;
    window.__oracleDone = true;
    try {
      console.log("[ORACLE] done records=" + or.records +
        " lastFrame=" + or.lastFrame +
        " traceEvents=" + window.__trace.length);
    } catch (e) { /* ignore */ }
    try { window.close(); } catch (e) { /* ignore */ }
  }

  /* ================= 8. Hook scaffolding (bind detection) ================= */

  function hookApp(app) {
    if (!app || app.__oracleHooked) return false;
    try {
      if (typeof app.aa !== "function" || !app.root || !app.Sc) return false;
    } catch (e) { return false; }
    try {
      app.__oracleHooked = true;
      or.app = app;
      var origAa = app.aa;
      app.aa = function (a) {
        try {
          stimTickPre(); /* frame-exact stimulus BEFORE the tick simulates */
        } catch (err) {
          orFail(err);
        }
        var r = origAa.apply(this, arguments);
        try {
          oracleTick();
        } catch (err) {
          orFail(err);
        }
        try {
          renderTick();
        } catch (err) {
          orFail(err);
        }
        return r;
      };
      if (typeof app.lbb === "function" && !app.__oracleLbb) {
        app.__oracleLbb = true;
        var origLbb = app.lbb;
        app.lbb = function (a) {
          var r2 = origLbb.apply(this, arguments);
          try {
            var node = this.root && this.root.af;
            while (node) {
              if (node.Mr && node.Mr.stack) { or.mc = node.Mr; break; }
              node = node.Ma;
            }
          } catch (e2) { /* ignore */ }
          return r2;
        };
      }
      return true;
    } catch (e) { return false; }
  }

  try {
    console.log("[ORACLE] instrument ready, harness=" +
      JSON.stringify(window.__oracleHarness));
  } catch (e) { /* ignore */ }

  if (typeof Function.prototype.bind === "function") {
    var origBind = Function.prototype.bind;
    Function.prototype.bind = function (thisArg) {
      var bound = origBind.apply(this, arguments);
      try {
        if (thisArg && typeof thisArg.aa === "function" &&
            thisArg.root && thisArg.Sc) {
          if (hookApp(thisArg)) {
            try {
              console.log("[ORACLE] hooked app instance aa");
            } catch (e) { /* ignore */ }
          }
        }
      } catch (e) { /* ignore */ }
      return bound;
    };
  } else {
    orFail(new Error("Function.prototype.bind unavailable"));
  }
})();
