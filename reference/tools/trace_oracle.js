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

  var MAX_FIGHT_FRAMES = 600; /* stop when fight.frame reaches this */
  var MAX_RECORDS = 1500; /* backstop: stop after this many oracle records */
  var TRACE_CAP = 20000; /* window.__trace ring cap */
  var N0A_RECENT = 8; /* input events kept in input_buffer_state.recent */

  var or = {
    mc: null,
    fight: null,
    screenTop: null,
    timerHostObj: null,
    timerHostPath: null,
    stimulus: null,
    headerDone: false,
    lastFrame: -1,
    records: 0,
    hookedFight: null,
    hookedPaths: { iPa: [], N0a: [], O0a: [] },
    wrappedDe: [null, null], /* legacy: superseded by deList scan */
    deScanFight: null,
    deList: null, /* [{obj, label}] every de in the fight graph */
    deCount: 0,
    lastDecider: null, /* label of the de with the latest Pqb/ia event */
    methodHosts: null, /* {N0a:{host},O0a:{host}} for exact replay */
    orecSeq: 0, /* INPUT-REC sequence number */
    cO0a: 0, /* O0a (release) wrapper fire counter */
    deScanFight: null,
    deList: null, /* [{obj, label}] every de in the fight graph */
    deCount: 0,
    lastDecider: null, /* label of the de with the latest Pqb/ia event */
    lastMove: { Me: null, Enemy: null },
    lastPqb: { Me: null, Enemy: null },
    n0aRecent: [],
    cPqb: 0, cIa: 0, cN0a: 0, ciPa: 0, /* wrapper fire counters (trace_stats) */
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

  function wrapPqb(de, side) {
    var orig = de.Pqb;
    de.Pqb = function (a) {
      var r = orig.apply(this, arguments);
      try {
        or.cPqb++;
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
  }

  function wrapDeIa(de, side) {
    if (typeof de.ia !== "function") return false;
    var orig = de.ia;
    de.ia = function (a) {
      var fk0 = num(this.fk);
      var r = orig.apply(this, arguments);
      try {
        or.cIa++;
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

  function deLabel(fight, de) {
    try {
      if (fight.pb && de === fight.pb.nf) return "Me";
      if (fight.yb && de === fight.yb.nf) return "Enemy";
    } catch (e) { /* ignore */ }
    return null;
  }

  function ensureDeHooks(fight) {
    /* Wrap EVERY de instance in the fight graph (fighters, sub-fighters),
     * not just fight.pb/yb .nf — the tutorial may drive bodies whose de
     * lives elsewhere. Cached per fight identity; re-scanned on change. */
    if (or.deScanFight === fight && or.deList) {
      var stillThere = true;
      try {
        for (var s = 0; s < or.deList.length; s++) {
          if (!isDe(or.deList[s].obj)) { stillThere = false; break; }
        }
      } catch (e) { stillThere = false; }
      if (stillThere) return;
    }
    or.deScanFight = fight;
    or.deList = [];
    var found = [];
    try {
      var queue = [{ o: fight, d: 0 }];
      var seen = [fight];
      var visited = 0;
      while (queue.length && visited < 600) {
        var cur = queue.shift();
        if (cur.d > 0 && isDe(cur.o)) {
          var dup = false;
          for (var k = 0; k < found.length; k++) {
            if (found[k] === cur.o) { dup = true; break; }
          }
          if (!dup) found.push(cur.o);
        }
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
    var xCount = 0;
    for (var n = 0; n < found.length; n++) {
      try {
        var de = found[n];
        var label = deLabel(fight, de);
        if (!label) { xCount++; label = "X" + xCount; }
        if (!de.__orPqb) {
          de.__orPqb = true;
          wrapPqb(de, label);
        }
        if (!de.__orIa && typeof de.ia === "function") {
          de.__orIa = true;
          if (!wrapDeIa(de, label)) de.__orIa = false;
        }
        or.deList.push({ obj: de, label: label });
      } catch (e) { /* ignore */ }
    }
    or.deCount = found.length;
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
    if (fr >= MAX_FIGHT_FRAMES || or.records >= MAX_RECORDS) finish();
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
