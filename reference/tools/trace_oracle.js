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
    headerDone: false,
    lastFrame: -1,
    records: 0,
    hookedFight: null,
    hookedPaths: { iPa: [], N0a: [] },
    wrappedDe: [null, null], /* per side index: {obj} */
    lastMove: { Me: null, Enemy: null },
    lastPqb: { Me: null, Enemy: null },
    n0aRecent: [],
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

  /* ================= 4. Method wrappers (log in/out, delegate) ================= */

  function wrapPqb(de, side) {
    var orig = de.Pqb;
    de.Pqb = function (a) {
      var r = orig.apply(this, arguments);
      try {
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

  function wrapFightMethod(fight, name) {
    var found = findMethodDeep(fight, name);
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
    } else if (name === "N0a") {
      host[name] = function (a) {
        var r = orig.apply(this, arguments);
        try {
          var ctl = (a && typeof a.control !== "undefined") ? a.control : null;
          pushTrace({ m: "N0a", f: frameNo(), control: ctl, eu: num(this.eu) });
          or.n0aRecent.push({ f: frameNo(), control: ctl });
          while (or.n0aRecent.length > N0A_RECENT) or.n0aRecent.shift();
        } catch (e) { /* ignore */ }
        return r;
      };
    } else {
      return null;
    }
    try {
      host.__orWrapped = host.__orWrapped || {};
      host.__orWrapped[name] = true;
    } catch (e) { /* ignore */ }
    return found.path;
  }

  function ensureFightHooks(fight) {
    if (or.hookedFight === fight) return;
    or.hookedFight = fight;
    or.hookedPaths = { iPa: [], N0a: [] };
    ["iPa", "N0a"].forEach(function (name) {
      try {
        var p = wrapFightMethod(fight, name);
        if (p) or.hookedPaths[name].push(p);
      } catch (e) { /* ignore */ }
    });
  }

  function ensureDeHooks(fight) {
    ["Me", "Enemy"].forEach(function (side, idx) {
      try {
        var f = fighterAt(fight, side);
        var de = null;
        try {
          if (f && isDe(f.nf)) de = f.nf;
        } catch (e) { /* ignore */ }
        if (!de && f) {
          try {
            var keys = Object.keys(f);
            for (var i = 0; i < keys.length; i++) {
              var v = null;
              try { v = f[keys[i]]; } catch (e2) { continue; }
              if (isDe(v)) { de = v; break; }
            }
          } catch (e) { /* ignore */ }
        }
        if (de && (!or.wrappedDe[idx] || or.wrappedDe[idx].obj !== de)) {
          try {
            if (!de.__orPqb) {
              de.__orPqb = true;
              wrapPqb(de, side);
            }
            if (!de.__orIa) {
              de.__orIa = true;
              if (!wrapDeIa(de, side)) de.__orIa = false;
            }
            or.wrappedDe[idx] = { obj: de };
          } catch (e) { /* ignore */ }
        }
      } catch (e) { /* ignore */ }
    });
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
    var pqb = { Me: false, Enemy: false };
    var ia = { Me: false, Enemy: false };
    ["Me", "Enemy"].forEach(function (side) {
      try {
        var de = deOf(fight, side);
        if (de) {
          pqb[side] = !!de.__orPqb;
          ia[side] = !!de.__orIa;
        }
      } catch (e) { /* ignore */ }
    });
    emit({
      t: "oracle_header",
      js: "sf2.502f0946.js",
      harness: window.__oracleHarness,
      ai_side: aiSide(fight),
      hooked: {
        iPa: or.hookedPaths.iPa.slice(),
        N0a: or.hookedPaths.N0a.slice(),
        Pqb: pqb,
        "de.ia": ia
      }
    });
  }

  function oracleRecord(fight) {
    var side = aiSide(fight);
    var de = deOf(fight, side);
    var me = fighterAt(fight, "Me");
    var en = fighterAt(fight, "Enemy");
    var wMe = wcState(me);
    var wEn = wcState(en);
    var bMe = blockInfo(me);
    var bEn = blockInfo(en);
    var pqb = or.lastPqb[side] || null;
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
      round_timer_xU: num(fight.xU),
      round_timer_NF: num(fight.NF),
      block_state: { me: bMe.a, enemy: bEn.a },
      block_info: { me: bMe.n, enemy: bEn.n },
      camera: camOf(fight)
    };
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
      return fight;
    } catch (e) { return null; }
  }

  function oracleTick() {
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
