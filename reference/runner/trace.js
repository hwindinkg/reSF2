/*
 * trace.js v2 — JSONL pose/clip tracer for the SF2 web build (oracle side of
 * the 1:1 port). Loaded by the runner AFTER sf2.502f0946.js (index.html
 * GameInterface.init). Hooks the game's fixed 60 Hz update pass and, while a
 * fight is active, logs ONE JSON object per frame to the console (captured by
 * OracleShell.exe into reference/traces/console.log):
 *
 *   {"t":"frame","f":123,"phase":2,"round":1,"timer":45,
 *    "cam":{"cx":..,"cy":..,"zoom":..},
 *    "fighters":[{"id":"Me","x":..,"y":..,"fx":1,"clip":"..","cf":..,
 *                  "sub":..,"subn":..,"bones":[[x,y],...]},
 *                {"id":"Enemy", ...}]}
 *
 * Plus one {"t":"clip",...} dump per played clip (raw i16/16 source data,
 * capped at MAX_CLIPS per run).
 *
 * Hook strategy (same as v1): the game script is a UMD IIFE, so the engine
 * classes (Te/wd/Ut/ca/...) are module-scoped and unreachable by name. We
 * detect the app instance via Function.prototype.bind (w() binds every app
 * method), hook its `aa` update pass, and capture the screen manager from its
 * `lbb`. Fight classes are discovered at runtime from the fight controller
 * instance (top screen's Ig) and patched once on their prototype:
 *   - Ut.prototype.Al (camera framing, L826) -> records focus + zoom.
 * Frame data itself is read directly from live instances at the end of `aa`
 * (gameplay-authoritative: oa.Va.all[i].ma = final posed world bones; the
 * counter fight.frame is the ca.prototype.ia counter; camera via Ut hook).
 * Every read is try/catch-guarded so a wrong field can never break the game.
 */
(function () {
  "use strict";

  var MAX_FRAMES = 350; /* stop emitting after this many frame lines */
  /* SCENARIO SELECTOR (state harness): `?maxFrames=N` raises the frame budget
   * so the trace survives the hub navigation and covers a REAL fight. The
   * default (350) keeps the legacy dojo/training pose window unchanged. */
  try {
    var qm = new URLSearchParams(window.location.search).get("maxFrames");
    if (qm != null) {
      var qn = parseInt(qm, 10);
      if (isFinite(qn) && qn > 0) MAX_FRAMES = qn;
    }
  } catch (e) { /* no query string */ }
  /* `?skipFightNone=1`: suppress emission while the live fight is the
   * `FightNone` hub/dojo viewer (`fight.Da.type == "FightNone"`, JS `dl.type`
   * L59254). A navigating scenario (hub -> Map -> battle) would otherwise
   * interleave the viewer's frames with the real fight's; skipping them makes
   * the state trace start exactly at the real fight, like the native
   * `--fight` dump. Opt-in, so the dojo-demo (`postTutorial=0`) scenario —
   * which IS the viewer — is unchanged. */
  var SKIP_FIGHT_NONE = false;
  try {
    SKIP_FIGHT_NONE =
      new URLSearchParams(window.location.search).get("skipFightNone") === "1";
  } catch (e) { /* no query string */ }
  var MAX_CLIPS = 5; /* at most this many clip dumps per run */

  /* STATE-HARNESS profile probe (`?probe=save`): the NON-fight layers
   * (save/profile/economy + the persisted quest + shop rows) live in the
   * game's own savegame blob, which the browser build keeps in
   * `localStorage["shadow-fight-2:savegame"]` under `data.SF2User` — the
   * `Aa.save` (L34782-area) `SF2User` storage form: a u32-LE UTF-8 byte
   * length + the save XML, base64-encoded (the same bytes the port's
   * `SaveSystem::envelope_decode` consumes). Emitting that blob gives the
   * non-fight differential a REAL artifact from the JS side without
   * inventing any field. Emitted as `{"t":"savegame","b64":".."}`; the
   * extractor keeps the LAST one (the page may re-save during the run). */
  var PROBE_SAVE = false;
  try {
    PROBE_SAVE =
      new URLSearchParams(window.location.search).get("probe") === "save";
  } catch (e) { /* no query string */ }

  /* ================= DETERMINISTIC Math.random =================
   * [FIX missing harness pin] The port mirrors `mulberry32(0xC0FFEE)` for every
   * JS `Math.random` consumer (`shared_rng.hpp` `kFightMathRandomSeed`,
   * `reseed_shared_math_random` at the first live frame; the documented pin is
   * `reference/traces/README.md` `harness.mathRandom`). This tracer - the one
   * the gate runs actually load - had NO pin at all, so on the oracle side the
   * stream was the browser's REAL random: every consumer diverged run to run.
   * The visible symptom was the audio gate: `<RandomSound Event="Strike">`
   * picks `snd_hit1..6` through `uf.sja(n) = floor(Math.random()*n)` and the
   * port chose a different variant on 2-3 hits per fight (tournament f=250
   * snd_hit5 vs snd_hit4, f=270 snd_hit1 vs snd_hit3, f=330 snd_hit3 vs
   * snd_hit2) while all seeded `Da.pg` state matched exactly.
   *
   * Ported verbatim from the older `reference/tools/trace_oracle.js`
   * (`MR_SEED`, the mulberry32 body and the re-pin at `eu == 1`). */
  var T0_MR = 1720000000000;
  var MR_SEED = 0xC0FFEE;
  var mrState = MR_SEED;
  var mrDraws = 0;   /* diagnostic: draws since load */
  /* `?mrtrace=1` (harness-only): one line per draw, in the SAME shape the port
   * prints with `SF2_TRACE_MR=1` (`[mr] f=<frame> n=<counter> v=<value>`), so
   * the two sequences can be diffed draw by draw and the first consumer that
   * over/under-draws can be named. */
  var MR_TRACE = false;
  try {
    MR_TRACE =
      new URLSearchParams(window.location.search).get("mrtrace") === "1";
  } catch (eMRt) { /* ignore */ }
  window.__mrDraws = 0;
  Math.random = function () {
    mrDraws++;
    mrState |= 0;
    mrState = (mrState + 0x6D2B79F5) | 0;
    var t = Math.imul(mrState ^ (mrState >>> 15), 1 | mrState);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    var v = ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    if (MR_TRACE) {
      try {
        var fr = -1;
        var f0 = null;
        try { f0 = fightObj(); } catch (eMRf) { f0 = null; }
        if (f0 && typeof f0.frame === "number") fr = f0.frame;
        console.log("[mr] f=" + fr + " n=" + mrDraws + " v=" + v.toFixed(9));
      } catch (eMRl) { /* ignore */ }
    }
    return v;
  };
  try {
    window.__oracleHarness = {
      dateFixed: T0_MR,
      mathRandom: "mulberry32(0xC0FFEE)"
    };
  } catch (e0) { /* ignore */ }

  /* Re-pin at the FIRST live fight frame, like the port's `enter_start_stance`:
   * hub/dojo draws before that must not shift the fight's own sequence. */
  var mrFightReseeded = false;
  function reseedMathRandomForFight(fight) {
    if (mrFightReseeded || !fight) return;
    try {
      if (fight.Da && fight.Da.type === "FightNone") return;
      if (Number(fight.eu) !== 1) return;
    } catch (e1) { return; }
    mrFightReseeded = true;
    try {
      console.log("[ORACLE] math.random reseeded for the fight: drawsBefore=" +
                  mrDraws + " seed=0x" + MR_SEED.toString(16));
    } catch (e2) { /* ignore */ }
    mrState = MR_SEED;
  }

  function readSavegameB64() {
    try {
      var keys = ["shadow-fight-2:savegame", "SF2User", "savegame"];
      for (var i = 0; i < keys.length; i++) {
        var raw = window.localStorage.getItem(keys[i]);
        if (!raw) continue;
        var obj = null;
        try { obj = JSON.parse(raw); } catch (e) { obj = null; }
        if (obj && obj.data && typeof obj.data.SF2User === "string") {
          return obj.data.SF2User;
        }
        if (obj && typeof obj.SF2User === "string") return obj.SF2User;
      }
      /* Fallback: any localStorage value that carries the envelope. */
      for (var k = 0; k < window.localStorage.length; k++) {
        var key = window.localStorage.key(k);
        var v = window.localStorage.getItem(key);
        if (!v || v.indexOf("SF2User") < 0) continue;
        try {
          var o = JSON.parse(v);
          if (o && o.data && typeof o.data.SF2User === "string") {
            return o.data.SF2User;
          }
        } catch (e) { /* not this key */ }
      }
    } catch (e) { /* ignore */ }
    return null;
  }

  function emitSavegame() {
    if (!PROBE_SAVE) return;
    var b64 = readSavegameB64();
    if (!b64) return;
    console.log('{"t":"savegame","b64":"' + b64 + '"}');
  }

  /* MOVE-LIST probe (pack-availability verification). Once per run, on the
   * first LIVE fight frame, dump each fighter's OWN move list (`wd.me`,
   * sf2.readable.js L20295 — filled by the fighter build at L28904-28913:
   * iterate the global registry `ra.Lk` and push every move whose conditions
   * `f.nw(d, b)` pass and that is not locked by `<Skeleton>`; the registry
   * itself is built by `ra.load()` -> `Fa.parse(ra.Lk, ...)` (L28790-28801),
   * which drops pack-gated moves via `Mc.F().yEa(name)` (L14602 ->
   * `T1` L39736). The native side prints the SAME lists as
   * `--enemy-move-audit`'s `[emaudit] enemy moves:` digest / `move_list_digest`
   * so the two pack models can be compared name-for-name. Also reports how
   * many `<Pack>` records the SF2Packs seed carries, to prove the seed
   * actually landed in `GameInterface.storage`. */
  function emitMovesOnce(fight) {
    if (tr.movesEmitted) return;
    var names = function (f) {
      var out = [];
      try {
        if (f && f.me && f.me.length) {
          for (var i = 0; i < f.me.length; i++) {
            var nm = f.me[i] && f.me[i].name != null ? String(f.me[i].name) : "";
            if (nm) out.push(nm);
          }
        }
      } catch (e) { /* ignore */ }
      return out;
    };
    var packs = 0;
    try {
      var raw = window.localStorage.getItem("shadow-fight-2:savegame");
      var env = raw ? JSON.parse(raw) : null;
      var xml = env && env.data ? env.data.SF2Packs : null;
      if (typeof xml === "string") {
        packs = xml.split("<Pack ").length - 1;
      }
    } catch (e) { /* ignore */ }
    tr.movesEmitted = true;
    console.log(JSON.stringify({
      t: "moves",
      packs: packs,
      p: names(fight.pb),
      e: names(fight.yb)
    }));
  }

  /* Run marker: OracleShell's console.log ACCUMULATES every run (a fresh page
   * per run appends to the same file), so a state extractor must be able to
   * keep ONLY the last run — otherwise the mixed frames make
   * `compare_state.py --align auto` pick a wrong offset. Emitted once per page
   * load, before any `{"t":"state"}` line. */
  console.log('{"t":"run_start","maxFrames":' + MAX_FRAMES + '}');

  var tr = {
    enabled: true,
    hooked: false,
    lastScreen: -1,
    mc: null, /* screen manager (mc.K), captured from the app's lbb() */
    done: false,
    frameCount: 0,
    tick: 0,
    clips: 0,
    cam: null, /* {cx,cy,zoom} set by the Ut.Al hook */
    seenClips: {}, /* clip label -> true (dump once per clip per run) */
    lastFightFrame: -1, /* last emitted `fight.frame` (one line per fight frame) */
    lastClip: new WeakMap(), /* fighter -> clip label seen last tick */
    movesEmitted: false /* one-shot `{"t":"moves"}` line per run */,
    drawHooked: false /* `?drawTrace=1`: the `Da.pg.jf` wrapper is installed */
  };

  window.__sf2Trace = {
    enabled: true,
    hooked: false,
    lastScreen: -1,
    state: function () { return snapshot(); }
  };

  /* ---------- AUDIO hook (Js platform mixer `Ss`, app.$f) ----------------
   * Every cue the JS plays goes through ONE point: `Ss.play(id, loop)`
   * (`Ss` = `new Ss()` at L1950, reached as `L.K.$f.play(a,b)` from `ta.ak`
   * L52930 for SFX and `ta.Ut` L52954 for music). Hooking its prototype
   * captures the exact (asset id, loop) stream, the per-cue replays, and the
   * two bus writes (`Ss.uF`/`Ss.cMa`, the `ta.VT`/`ta.WT` sources L52982/
   * L52986) without touching game code. The asset NAME is resolved from the
   * player's OWN dict (`Ss.kWa` L100054 `this.names.v[e.id]=e.name`), so the
   * trace carries the same `snd_*`/music key the native sfx table uses. */
  var AU = {
    hooked: false, plays: [], stops: [], sfx: 1, music: 1,
    seen: {}, diag: []
  };
  function auName(player, id) {
    try {
      if (player && player.names && player.names.v &&
          player.names.v[id] != null) {
        return String(player.names.v[id]);
      }
    } catch (e) { /* ignore */ }
    return null;
  }
  function hookAudio(app) {
    try {
      var snd = app && app.$f;
      if (!snd) return;
      var P = Object.getPrototypeOf(snd);
      if (!P || P.__sf2Au) return;
      P.__sf2Au = true;
      var oPlay = P.play, oStop = P.stop, oUF = P.uF, oCMa = P.cMa;
      if (typeof oPlay === "function") {
        P.play = function (id, loop) {
          var r;
          try { r = oPlay.apply(this, arguments); } catch (e) { r = -1; }
          try {
            if (AU.hooked && r !== -1) {
              var nm = auName(this, id);
              var key = nm == null ? ("#" + id) : nm;
              if (auIsMusic(key)) {
                AU.musicTrack = auMusicTrack(key);
              } else {
                AU.plays.push([key, loop ? 1 : 0]);
              }
              if (!AU.seen[key]) {
                AU.seen[key] = 1;
                if (AU.diag.length < 80) AU.diag.push([id, nm, loop ? 1 : 0]);
              }
            }
          } catch (e) { /* ignore */ }
          return r;
        };
      }
      if (typeof oStop === "function") {
        P.stop = function (id) {
          var r;
          try { r = oStop.apply(this, arguments); } catch (e) { /* ignore */ }
          try {
            if (AU.hooked) {
              var nm = auName(this, id);
              AU.stops.push(nm == null ? ("#" + id) : nm);
            }
          } catch (e) { /* ignore */ }
          return r;
        };
      }
      if (typeof oUF === "function") {
        P.uF = function (g) {
          try { if (AU.hooked) AU.music = g; } catch (e) { /* ignore */ }
          return oUF.apply(this, arguments);
        };
      }
      if (typeof oCMa === "function") {
        P.cMa = function (g) {
          try { if (AU.hooked) AU.sfx = g; } catch (e) { /* ignore */ }
          return oCMa.apply(this, arguments);
        };
      }
      AU.hooked = true;
      console.log("[TRACE] audio hooked (Ss.prototype.play/stop/uF/cMa)");
    } catch (e) {
      console.log("[TRACE] audio hook ERR " + (e && e.message ? e.message : e));
    }
  }
  function auIsMusic(name) {
    return name.indexOf("res/audio/") === 0 || /\.(m4a|ogg|mp3)$/i.test(name);
  }
  function auMusicTrack(name) {
    var b = name;
    var i = b.lastIndexOf("/");
    if (i >= 0) b = b.substring(i + 1);
    b = b.replace(/\.(m4a|ogg|mp3)$/i, "");
    b = b.replace(/_music$/i, "");
    return b;
  }
  function auClear() { AU.plays.length = 0; AU.stops.length = 0; }
  function emitAudio(fight) {
    if (!AU.hooked) return;
    if (AU.diag.length > (AU.diagSent || 0)) {
      AU.diagSent = AU.diag.length;
      console.log(JSON.stringify({ t: "audiodiag", d: AU.diag, hooked: AU.hooked }));
    }
    var plays = AU.plays; AU.plays = [];
    var stops = AU.stops; AU.stops = [];
    console.log(JSON.stringify({
      t: "audio",
      f: typeof fight.frame === "number" ? fight.frame : 0,
      phase: typeof fight.eu === "number" ? fight.eu : 0,
      plays: plays, stops: stops, sfx: AU.sfx, music: AU.music,
      music_track: AU.musicTrack
    }));
  }

  /* ---------- UI SCREEN-STATE (the `mc` screen manager stack) ------------
   * `mc.K` (L4831) is the singleton screen manager; `tr.mc` is captured from
   * the app's `lbb`. Every screen entry `d` (`$d` base) carries `state` (the
   * `Te(n)` machine), `active`, `time`, and its id (`DQ`/`dJ`). One line per
   * STACK CHANGE, so the trace is the screen-transition timeline the port's
   * ScreenManager stack reproduces. */
  var UI = { last: null, lines: 0 };
  function uiStack() {
    var out = [];
    try {
      var mgr = tr.mc;
      if (mgr && mgr.stack) {
        for (var i = 0; i < mgr.stack.length; i++) {
          var d = mgr.stack[i];
          if (!d) continue;
          var sid = -1;
          if (typeof d.DQ === "function") sid = d.DQ();
          else if (typeof d.dJ === "function") sid = d.dJ();
          out.push({
            id: sid,
            name: screenName(sid),
            state: typeof d.state === "number" ? d.state : -1,
            active: d.active ? 1 : 0,
            t: r3(typeof d.time === "number" ? d.time : 0)
          });
        }
      }
    } catch (e) { /* ignore */ }
    return out;
  }
  function emitUi() {
    var st = uiStack();
    var sig = JSON.stringify(st.map(function (e) {
      return [e.id, e.state, e.active];
    }));
    if (sig === UI.last) return;
    UI.last = sig;
    UI.lines++;
    console.log(JSON.stringify({ t: "ui", tick: tr.tick, stack: st }));
  }

  /* ---------- helpers ---------- */

  function r3(v) {
    return typeof v === "number" && isFinite(v) ? Math.round(v * 1000) / 1000 : 0;
  }

  function screenName(id) {
    switch (id) {
      case 0: return "Preloader";
      case 2: return "Loader";
      case 3: return "Dojo";
      case 4: return "Shop";
      case 5: return "Map";
      case 6: return "Fight";
      case 7: return "Profile";
      case 8: return "GeneralMenu";
      case 9: return "Pvp";
      default: return "";
    }
  }

  function screenId() {
    var mgr = tr.mc;
    if (mgr && mgr.stack && mgr.stack.length > 0) {
      var top = mgr.stack[mgr.stack.length - 1];
      if (top) {
        if (typeof top.DQ === "function") return top.DQ();
        if (typeof top.dJ === "function") return top.dJ();
      }
    }
    return -1;
  }

  /* The ca fight controller: top screen's Ig field (set by the fight screen). */
  function fightObj() {
    var mgr = tr.mc;
    if (!mgr || !mgr.stack || mgr.stack.length === 0) return null;
    var top = mgr.stack[mgr.stack.length - 1];
    if (!top || !top.Ig) return null;
    var fight = top.Ig;
    if (!fight || typeof fight.frame !== "number") return null;
    return fight;
  }

  /* ---------- prototype patches (discovered classes, patched once) ---------- */

  /* Ut.prototype.Al — camera framing. args[0] = world focus point (Ut.Al is
   * called with `focus, fighterA_COM, fighterB_COM, scale, zoom`), this.Bj =
   * current zoom. Records the camera AFTER the fight tick computed it. */
  /* ---------------------------------------------------------------------------
 * DRAW TRACE (`?drawTrace=1`, harness-only): log every `Da.pg.jf()` draw with
 * its 1-based index, value, frame and caller frames. This is the only way to
 * see WHERE the shared `Rk` stream sits when a setup routine (e.g. the
 * survival warrior pick `efa`/`y8a`) consumes it - the port must replay the
 * same draws. Off unless the query asks for it, so the state trace is
 * unchanged.
 * ------------------------------------------------------------------------- */
var DRAW_TRACE = new URLSearchParams(window.location.search).get("drawTrace") === "1";

function ensureDrawHook(fight) {
  if (!DRAW_TRACE || tr.drawHooked) return;
  var pg = null;
  try {
    if (fight && fight.Da && fight.Da.constructor && fight.Da.constructor.pg) {
      pg = fight.Da.constructor.pg;
    }
  } catch (e0) { /* ignore */ }
  if (!pg) {
    try {
      var ks = Object.keys(fight);
      for (var ki = 0; ki < ks.length && !pg; ki++) {
        var vv = null;
        try { vv = fight[ks[ki]]; } catch (e1) { continue; }
        if (vv && vv.constructor && vv.constructor.pg) pg = vv.constructor.pg;
      }
    } catch (e2) { /* ignore */ }
  }
  tr.drawHooked = true;
  if (!pg || typeof pg.jf !== "function") {
    var diag = { t: "drawprobe", ok: 0 };
    try {
      diag.da = typeof fight.Da;
      diag.daKeys = fight.Da ? Object.keys(fight.Da).slice(0, 40).join(",") : "";
      diag.ctorKeys = (fight.Da && fight.Da.constructor)
          ? Object.keys(fight.Da.constructor).join(",") : "";
      diag.pgInst = !!(fight.Da && fight.Da.pg);
    } catch (eD) { diag.err = String(eD); }
    console.log(JSON.stringify(diag));
    return;
  }
  var n = 0;
  var orig = pg.jf;
  pg.jf = function () {
    var v = orig.apply(this, arguments);
    try {
      n++;
      var st = "";
      try { st = (new Error().stack || "").split("\n").slice(2, 5).join(" | "); }
      catch (e3) { /* ignore */ }
      console.log('{"t":"draw","i":' + n + ',"v":' + v + ',"f":' +
                  (fight.frame | 0) + ',"c":' + JSON.stringify(st) + '}');
    } catch (e4) { /* ignore */ }
    return v;
  };
  console.log('{"t":"drawprobe","ok":1}');
}

function ensureHooks(fight) {
    try {
      var ql = fight.Ta;
      if (ql && ql.ia) {
        var Ut = ql.ia.constructor;
        var P = Ut && Ut.prototype;
        if (P && !P.__sf2trAl && typeof P.Al === "function") {
          P.__sf2trAl = true;
          var origAl = P.Al;
          P.Al = function (focus) {
            var r = origAl.apply(this, arguments);
            try {
              if (focus && focus.x != null) {
                tr.cam = {
                  cx: r3(focus.x),
                  cy: r3(focus.y),
                  zoom: typeof this.Bj === "number" ? r3(this.Bj) : 1
                };
              }
            } catch (e) { /* ignore */ }
            return r;
          };
        }
      }
    } catch (e) { /* ignore */ }
  }

  /* Fallback camera read (if the Ut.Al hook never fires): ql.Go.ma is the
   * smoothed focus point, ql.ia.Bj the zoom. */
  function camFallback(fight) {
    var cx = 0, cy = 0, zoom = 1;
    try {
      if (fight.Ta && fight.Ta.Go && fight.Ta.Go.ma) {
        cx = r3(fight.Ta.Go.ma.x);
        cy = r3(fight.Ta.Go.ma.y);
      }
    } catch (e) { /* ignore */ }
    try {
      if (fight.Ta && fight.Ta.ia && typeof fight.Ta.ia.Bj === "number") {
        zoom = r3(fight.Ta.ia.Bj);
      }
    } catch (e) { /* ignore */ }
    return { cx: cx, cy: cy, zoom: zoom };
  }

  /* ---------- frame data ---------- */

  /* Final per-bone positions in oa.Va.all order (the bone order the model /
   * native port use), as [x,y,z] triples, 3 decimals. The z component is
   * INCLUDED: the cloth chains hang almost along z, so an xy-only trace hides
   * the coordinate that decides whether two chains are actually the same. */
  /* Per-bone VELOCITY (`ma - mf`) in oa.Va.all order, 3 decimals. Positions
   * alone cannot separate "same dynamics, different phase" from a real
   * per-frame difference: `mf` is NOT always the previous `ma` (Te.eda's f4()
   * and Bl.s2a's midpoint smoothing rewrite it), so the true velocity has to
   * be dumped directly. */
  function vels3d(f) {
    var out = [];
    try {
      var all = f.oa && f.oa.Va && f.oa.Va.all;
      if (all && all.length) {
        for (var i = 0; i < all.length; i++) {
          var m = all[i];
          if (!m || !m.ma || !m.mf) { out.push([0, 0, 0]); continue; }
          out.push([r3(m.ma.x - m.mf.x), r3(m.ma.y - m.mf.y), r3(m.ma.z - m.mf.z)]);
        }
      }
    } catch (e) { /* ignore */ }
    return out;
  }

  function bones3d(f) {
    var out = [];
    try {
      var all = f.oa && f.oa.Va && f.oa.Va.all;
      if (all && all.length) {
        for (var i = 0; i < all.length; i++) {
          var m = all[i] && all[i].ma;
          out.push([m ? r3(m.x) : 0, m ? r3(m.y) : 0, m ? r3(m.z) : 0]);
        }
      }
    } catch (e) { /* ignore */ }
    return out;
  }

  function fighterJson(f, id) {
    var out = { id: id, x: 0, y: 0, fx: 1, clip: null, cf: 0, sub: 0, subn: null, bones: [] };
    if (!f) return out;
    try {
      var fe = f.oa && typeof f.oa.Fe === "function" ? f.oa.Fe() : null;
      if (fe && fe.ma) { out.x = r3(fe.ma.x); out.y = r3(fe.ma.y); }
    } catch (e) { /* body not posed yet */ }
    var da = f.da;
    if (da) {
      out.fx = typeof da.hd === "function" ? (da.hd() < 0 ? -1 : 1) : 1;
      if (da.Ua) {
        out.clip = da.Ua.name != null ? String(da.Ua.name) : null;
        out.cf = typeof da.Xh === "number" ? da.Xh : 0;
      }
      out.sub = r3(da.mo);
      try {
        if (da.fq && da.fq[0]) out.subn = da.fq[0].length;
      } catch (e) { /* ignore */ }
    }
    out.bones = bones3d(f);
    out.vel = vels3d(f);
    return out;
  }

  function frameJson(fight) {
    var round = fight.round || null;
    return {
      t: "frame",
      f: typeof fight.frame === "number" ? fight.frame : 0,
      phase: typeof fight.eu === "number" ? fight.eu : 0,
      round: round ? round.round : null,
      timer: round ? round.time : null,
      cam: tr.cam || camFallback(fight),
      fighters: [fighterJson(fight.pb, "Me"), fighterJson(fight.yb, "Enemy")]
    };
  }

  /* ---------- full fight-state dump (mechanical differential harness) ------ */

  /* Per-fighter LIVE combat state, read from the game objects:
   *   hp/mhp   `parameters.gd` / `parameters.Zn`
   *   combo    `Vx.tf`        (combo ladder)
   *   style    `dz`           (style level)
   *   unhit    `parameters.cE`(per-round "not hit yet" latch)
   *   shock    `vc`           (shock/disarm latch)
   *   block    `da.yD(5)`     (a Block interval active)
   *   invuln   `da.yD(6)`     (an Invulnerable interval active)
   *   move     `da.Ua.name`   (current move name)
   *   af       `da.Xh`        (clip playhead)
   *   iv       `da.xj`        (active intervals [name,type])
   *   rag/ragf `Nd.nk` / `Nd.frameCount` (ragdoll solver)
   * Every read is try/catch-guarded; the field NAMES/ORDER mirror the native
   * `FightController::dump_state_frame` line exactly. */
  function fighterState(f, id) {
    var o = {
      id: id, x: null, y: null, w: null,
      hp: null, mhp: null, combo: null, style: null, unhit: null,
      shock: 0, block: 0, invuln: 0, move: null, af: null, rag: 0, ragf: 0,
      iv: []
    };
    if (!f) return o;
    try {
      // World position of the posed body (`fighterJson`'s `f.oa.Fe().ma`,
      // the same source the visual trace uses).
      var fe = f.oa && typeof f.oa.Fe === "function" ? f.oa.Fe() : null;
      if (fe && fe.ma) {
        o.x = Math.round(fe.ma.x * 1e4) / 1e4;
        o.y = Math.round(fe.ma.y * 1e4) / 1e4;
      }
    } catch (e) { /* body not posed yet */ }
    try {
      // The wielded weapon = the `Weapon`-slot item (`xc.hk("Weapon",item)`
      // L34259-34261 stores it on `Hd`; `I.vg = "Weapon"` L102807).
      o.w = f.parameters && f.parameters.Hd ? String(f.parameters.Hd.name) : null;
    } catch (e) { /* no params yet */ }
    try {
      if (f.parameters) {
        o.hp = r3(f.parameters.gd);
        o.mhp = r3(f.parameters.Zn);
        o.unhit = f.parameters.cE ? 1 : 0;
      }
    } catch (e) { /* ignore */ }
    try { if (f.Vx) o.combo = f.Vx.tf | 0; } catch (e) { /* ignore */ }
    try { o.style = f.dz | 0; } catch (e) { /* ignore */ }
    try { o.shock = f.vc ? 1 : 0; } catch (e) { /* ignore */ }
    try {
      if (f.da) {
        o.move = f.da.Ua && f.da.Ua.name != null ? String(f.da.Ua.name) : null;
        o.af = typeof f.da.Xh === "number" ? f.da.Xh : null;
        o.block = f.da.yD(5) != null ? 1 : 0;
        o.invuln = f.da.yD(6) != null ? 1 : 0;
        var xj = f.da.xj;
        if (xj && xj.length) {
          for (var i = 0; i < xj.length; i++) {
            var it = xj[i];
            o.iv.push([it && it.name != null ? String(it.name) : "",
                       it && typeof it.type === "number" ? it.type : 0]);
          }
          o.iv.sort(function (a, b) {
            if (a[0] !== b[0]) return a[0] < b[0] ? -1 : 1;
            return a[1] - b[1];
          });
        }
      }
    } catch (e) { /* ignore */ }
    try {
      if (f.Nd) {
        o.rag = f.Nd.nk ? 1 : 0;
        o.ragf = typeof f.Nd.frameCount === "number" ? f.Nd.frameCount | 0 : 0;
      }
    } catch (e) { /* ignore */ }
    return o;
  }

  /* Fight-level state:
   *   nf       `Sf.NF`      (the HUD countdown seconds — `fight.ha.mb` is the
   *                           `Sf` HUD, `Ar.E1` L83904; `Sf.iPa` L84739
   *                           `NF = xU/60|0`, ticked while `round.Vt`)
   *   timer    `Sf.NF`      (the raw countdown; the JS `$t.time` is
   *                           write-once `0` (`ca.tx` L16643) so it is NOT a
   *                           comparable quantity — the live countdown is)
   *   result   `ey`        (the round result code)
   *   rules    [n, n_active] over `fight.Da.jh()` (L59361: `p.o.Yh ? CV :
   *            Ae`, the eclipse-filtered rule collection the port's
   *            `rules_begin_round` mirrors), falling back to `ud.Ae`
   *   callouts [n_me, n_en]  (`Sf.Id.Jh.Gu` / `Sf.je.Jh.Gu` — the live `Gr`
   *                           display lists, ticked by `Gr.azb` inside
   *                           `ca.ia`)
   *   counters `m$` / `lW` */
  function hudSf(fight) {
    try {
      if (fight.ha && fight.ha.mb) return fight.ha.mb;      // `Ar.mb` (Sf)
      if (fight.mb && typeof fight.mb.NF === "number") return fight.mb;
    } catch (e) { /* ignore */ }
    return null;
  }

  function fightState(fight) {
    var round = fight.round || null;
    var sf = hudSf(fight);
    var nf = null;
    if (sf && typeof sf.NF === "number") nf = sf.NF | 0;
    var result = null;
    try { if (typeof fight.ey === "number") result = fight.ey | 0; } catch (e) { /* ignore */ }
    var rules = [0, 0];
    try {
      var src = null;
      try { if (fight.Da && typeof fight.Da.jh === "function") src = fight.Da.jh(); }
      catch (e) { /* ignore */ }
      if (!src) {
        var ud = fight.ud;
        if (ud && ud.Ae) src = ud.Ae;
      }
      if (src) {
        rules[0] = src.length | 0;
        var act = 0;
        for (var i = 0; i < src.length; i++) {
          if (src[i] && src[i].active) ++act;
        }
        rules[1] = act;
      }
    } catch (e) { /* ignore */ }
    var callouts = [0, 0];
    try {
      var mb = sf || (fight.ha && fight.ha.mb);
      if (mb) {
        if (mb.Id && mb.Id.Jh && mb.Id.Jh.Gu) callouts[0] = mb.Id.Jh.Gu.length | 0;
        if (mb.je && mb.je.Jh && mb.je.Jh.Gu) callouts[1] = mb.je.Jh.Gu.length | 0;
      }
    } catch (e) { /* ignore */ }
    var counters = { "m$": 0, "lW": 0 };
    try { counters["m$"] = fight.m$ ? 1 : 0; } catch (e) { /* ignore */ }
    try { counters["lW"] = fight.lW ? 1 : 0; } catch (e) { /* ignore */ }
    return {
      t: "state",
      f: typeof fight.frame === "number" ? fight.frame : 0,
      phase: typeof fight.eu === "number" ? fight.eu : 0,
      round: round ? round.round : null,
      timer: nf,
      nf: nf,
      result: result,
      cam: tr.cam || camFallback(fight),
      counters: counters,
      rules: rules,
      callouts: callouts,
      fighters: [fighterState(fight.pb, "Me"), fighterState(fight.yb, "Enemy")]
    };
  }

  /* ---------- clip dump ---------- */

  /* Once per played clip (throttled: first occurrence of each label per run,
   * capped at MAX_CLIPS). Reads the clip ASSET (Te.Ua.Kk — raw parsed frames,
   * never mirrored; mirroring happens on the per-Te playback buffer, Te.jc).
   * Each frame = flat array of [x16,y16,z16] per bone, recovered from the
   * i16/16 fixed point. JS stores H(x16/16, -y16/16, z16/16), and the native
   * dump stores y already source-signed (verified against
   * native_clip_fists1_stance_idle.json: native y16 == Math.round(H.y*16)),
   * so y is NOT re-negated here. */
  function dumpClipAsset(Ua) {
    if (!Ua || tr.clips >= MAX_CLIPS) return;
    var name = Ua.name != null ? String(Ua.name) : "";
    if (!name || tr.seenClips[name]) return;
    var frames = [];
    try {
      var Kk = Ua.Kk;
      if (Kk && Kk.length) {
        for (var i = 0; i < Kk.length; i++) {
          var fr = Kk[i];
          var flat = [];
          if (fr && fr.length) {
            for (var b = 0; b < fr.length; b++) {
              var h = fr[b];
              flat.push(h && h.x != null
                ? [Math.round(h.x * 16), Math.round(h.y * 16), Math.round(h.z * 16)]
                : [0, 0, 0]);
            }
          }
          frames.push(flat);
        }
      }
    } catch (e) { return; }
    if (!frames.length || !frames[0].length) return;
    tr.seenClips[name] = true;
    tr.clips++;
    console.log(JSON.stringify({
      t: "clip",
      name: name,
      frames: frames.length,
      bones: frames[0].length,
      data: frames
    }));
  }

  function captureClips(fight) {
    var fighters = [fight.pb, fight.yb];
    for (var i = 0; i < fighters.length; i++) {
      var f = fighters[i];
      try {
        if (!f || !f.da || !f.da.Ua) continue;
        var name = f.da.Ua.name != null ? String(f.da.Ua.name) : "";
        if (!name) continue;
        var prev = tr.lastClip.get(f);
        if (prev === name) continue; /* same clip as last tick */
        tr.lastClip.set(f, name);
        dumpClipAsset(f.da.Ua); /* first sight OR clip change */
        if (tr.clips >= MAX_CLIPS) return;
      } catch (e) { /* ignore */ }
    }
  }

  /* ---------- frame-exact stimulus (page side, SHARED script) ----------
   * OracleShell forwards every `atframe` line once as window.__oracleStimulus
   * (MainForm.cs: {f,t:'key',c,d} / tap / drag / press / release). The native
   * side reads THE SAME FILE through `--replay` (parse_replay_file) and routes
   * the key edges through FightScreen::on_key with the same numeric codes, so
   * one script drives both runs. Key edges fire BEFORE the tick that advances
   * the fight past frame f — the native applies `edges[ei].frame <=
   * fight_frames` right before `run_one_frame()`, so both sides gate on the
   * identical comparison. */
  var STIM = { items: null, next: 0 };

  function pollStimulus() {
    try {
      if (!STIM.items && window.__oracleStimulus && window.__oracleStimulus.length) {
        STIM.items = window.__oracleStimulus.slice();
        STIM.items.sort(function (a, b) { return a.f - b.f; });
        console.log("[STIM] armed: " + STIM.items.length + " items");
      }
    } catch (e) { /* ignore */ }
  }

  function stimTarget() {
    try {
      return document.getElementById("gfx") || document.body || document;
    } catch (e) { return null; }
  }

  function keyCodeToCode(c) {
    /* The game's input manager resolves the DOM `code` string first
     * (`ey.hi(a.code)` = `Os.v[code]`, L100859 + `Gz` L834-859: "KeyA".."KeyZ"
     * -> 65..90, "Digit0".."Digit9" -> 48..57) and only falls back to
     * `a.which`. Feeding the real `code` keeps the release path (`gFa`)
     * resolving the same key as the press. */
    if (c >= 65 && c <= 90) return "Key" + String.fromCharCode(c);
    if (c >= 48 && c <= 57) return "Digit" + String.fromCharCode(c);
    if (c === 32) return "Space";
    if (c >= 37 && c <= 40) return ["ArrowLeft", "ArrowUp", "ArrowRight", "ArrowDown"][c - 37];
    return "";
  }

  function fireKey(item, down) {
    try {
      var target = stimTarget();
      if (!target) return;
      var code = keyCodeToCode(item.c);
      var init = {
        bubbles: true, cancelable: true, keyCode: item.c, which: item.c
      };
      if (code) init.code = code;
      var ev = new KeyboardEvent(down ? "keydown" : "keyup", init);
      console.log("[STIM] " + (down ? "keydown" : "keyup") + " " + item.c +
        " at f=" + item.f);
      target.dispatchEvent(ev);
    } catch (e) { /* ignore */ }
  }

  /* Stimulus self-check: a window-level listener that reports what the engine's
   * own handlers see (the events bubble from the canvas), so a dropped or
   * mis-resolved key shows up in the trace instead of silently doing nothing. */
  function hookStimCheck() {
    try {
      ["keydown", "keyup"].forEach(function (type) {
        window.addEventListener(type, function (e) {
          try {
            console.log("[STIMCHK] " + type + " code=" + e.code + " keyCode=" +
              e.keyCode + " which=" + e.which + " repeat=" + e.repeat +
              " f=" + tr.lastFightFrame + " target=" +
              (e.target && e.target.id ? e.target.id : (e.target && e.target.tagName)));
          } catch (e2) { /* ignore */ }
        }, true);
      });
    } catch (e) { /* ignore */ }
  }

  /* RELAX TRACE (`?relaxTrace=1`, harness-only), read-only variant: snapshot
   * every edge length BEFORE the app's per-frame update (`aa`, which runs
   * `Nd.ia()` = sk + jE) and compare it AFTER. No game method is patched - an
   * earlier attempt that wrapped `Al.prototype.jE` broke the trace after ~24
   * frames, and per-edge corrections can be read from the same positions
   * without touching the engine. `fight.pb.Nd` is the 23-edge dummy in the
   * dojo, `fight.yb.Nd` the 277-edge player, so both are sampled. */
  function relaxSnapshot(fight) {
    var out = [];
    try {
      if (new URLSearchParams(window.location.search).get("relaxTrace") !== "1") {
        return out;
      }
      var sides = ["pb", "yb"];
      for (var si = 0; si < sides.length; si++) {
        var ftr = fight[sides[si]];
        var sol = ftr && ftr.Nd;
        var edges = sol && sol.oa && sol.oa.Nl && sol.oa.Nl.all;
        if (!edges || !edges.length) continue;
        // Index-keyed and CLOTH-AGNOSTIC: node names are not reliably exposed on
        // the edge objects, so every edge of a solver with more than 30 edges
        // (the player: 277; the dummy has 23) is sampled, capped to bound output.
        if (edges.length < 30) continue;
        var row = {};
        // The merged order is skeleton (193) then body (36) then head (48), so
        // the BODY cloth chain lives at indices ~193..228: sample that band.
        var lo = 185, hi = Math.min(edges.length, 245);
        for (var i = lo; i < hi; i++) {
          var e = edges[i];
          var a = e && e.sx ? e.sx.ma : null;
          var b = e && e.Zs ? e.Zs.ma : null;
          if (!a || !b) continue;
          var dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
          row["i" + i] = { pre: Math.sqrt(dx * dx + dy * dy + dz * dz),
                            len: typeof e.length === "number" ? e.length : 0,
                            side: sides[si], n: i };
        }
        out.push({ side: sides[si], row: row, sol: sol });
      }
    } catch (e0) { /* ignore */ }
    return out;
  }

  function beforeFrame() {
    if (tr.done || !tr.enabled) return;
    try {
      reseedMathRandomForFight(fightObj());
    } catch (eMR) { /* ignore */ }
    pollStimulus();
    if (!STIM.items || STIM.next >= STIM.items.length) return;
    var fight = fightObj();
    if (!fight) return;
    var f = null;
    try { f = typeof fight.frame === "number" ? fight.frame : null; } catch (e) { f = null; }
    if (f == null) return;
    while (STIM.next < STIM.items.length && STIM.items[STIM.next].f <= f) {
      var it = STIM.items[STIM.next++];
      if (it.t === "key") fireKey(it, it.d !== 0);
    }
  }

  /* ---------- debug overlay (oracle side) ----------
   * The port's `core/app/debug_overlay.hpp` panels, toggled by the same digit
   * keys (1..9, 0 = clear). Each panel is its OWN absolutely positioned box in
   * one of four anchored flows - left, right, bottom-left, bottom-right - so
   * panels can never cover each other, exactly like the native layout.
   *   2 quests/screens (bottom-left)  3 collision (block + intervals)
   *   5 damage (HP)                   6 fight (frame/phase/round/timer)
   *   8 screen stack (bottom-right)   9 fps
   * The AI panel (1) lives in `reference/tools/trace_oracle.js` (this tracer
   * does not collect the AI branch/zone). Display-only: every call is wrapped
   * in try/catch by the caller and never touches the trace stream. */
  var DBG = { on: {}, boxes: null, frames: 0, t0: 0 };

  function dbgBox(id, css) {
    var el = document.createElement("div");
    el.id = id;
    el.style.cssText = "position:fixed;z-index:2147483647;" +
      "font:12px/15px monospace;color:#cfe;text-shadow:0 0 2px #000,1px 1px 0 #000;" +
      "pointer-events:none;white-space:pre;padding:3px 5px;margin:6px;" +
      "background:rgba(5,5,8,0.74);border:1px solid rgba(90,107,128,0.9);" + css;
    document.body.appendChild(el);
    return el;
  }

  function dbgInit() {
    if (DBG.boxes) return;
    if (typeof document === "undefined" || !document.body) return;
    DBG.boxes = {
      left: dbgBox("__trDbgL", "left:0;top:0;"),
      right: dbgBox("__trDbgR", "right:0;top:0;text-align:left;"),
      bl: dbgBox("__trDbgBL", "left:0;bottom:0;"),
      br: dbgBox("__trDbgBR", "right:0;bottom:0;")
    };
    window.addEventListener("keydown", function (ev) {
      var k = ev.key;
      if (k >= "0" && k <= "9") {
        var d = k.charCodeAt(0) - 48;
        if (d === 0) DBG.on = {};
        else DBG.on[d] = !DBG.on[d];
        ev.preventDefault();
      }
    });
  }

  function dbgTick(st, fight) {
    dbgInit();
    if (!DBG.boxes) return;
    var any = false;
    for (var i = 1; i < 10; i++) if (DBG.on[i]) any = true;
    var keys = ["left", "right", "bl", "br"];
    if (!any) {
      for (var z = 0; z < keys.length; z++) {
        if (DBG.boxes[keys[z]].textContent !== "")
          DBG.boxes[keys[z]].textContent = "";
      }
      return;
    }
    var L = [], R = [], BL = [], BR = [];
    var me = st.fighters[0], en = st.fighters[1];
    if (DBG.on[2]) {
      var stack = [];
      try { stack = uiStack() || []; } catch (e) { /* ignore */ }
      var names = stack.map(function (s) { return s.name || s.id; });
      BL.push("[2] Quest/screens");
      BL.push("  " + names.join(" > "));
    }
    if (DBG.on[3]) {
      L.push("[3] Collision");
      L.push("  P block=" + me.block + " iv=" + JSON.stringify(me.iv));
      L.push("  E block=" + en.block + " iv=" + JSON.stringify(en.iv));
    }
    if (DBG.on[5]) {
      R.push("[5] Damage");
      R.push("  P hp=" + me.hp + "/" + me.mhp + " dealt=" +
             (me.mhp - me.hp).toFixed(2));
      R.push("  E hp=" + en.hp + "/" + en.mhp + " dealt=" +
             (en.mhp - en.hp).toFixed(2));
    }
    if (DBG.on[6]) {
      R.push("[6] Fight f=" + st.f + " phase=" + st.phase + " round=" + st.round +
             " timer=" + st.timer);
      R.push("  P move=" + me.move + " af=" + me.af + " rag=" + me.rag);
      R.push("  E move=" + en.move + " af=" + en.af + " rag=" + en.rag);
    }
    if (DBG.on[8]) {
      var stack2 = [];
      try { stack2 = uiStack() || []; } catch (e) { /* ignore */ }
      BR.push("[8] Screens");
      BR.push("  " + stack2.map(function (s) {
        return (s.name || "?") + "/" + s.state;
      }).join(","));
    }
    if (DBG.on[9]) {
      DBG.frames++;
      var now = Date.now();
      if (!DBG.t0) DBG.t0 = now;
      var dt = (now - DBG.t0) / 1000;
      if (dt > 0) R.push("[9] " + (DBG.frames / dt).toFixed(1) + " fps");
    }
    DBG.boxes.left.textContent = L.join("\n");
    DBG.boxes.right.textContent = R.join("\n");
    DBG.boxes.bl.textContent = BL.join("\n");
    DBG.boxes.br.textContent = BR.join("\n");
  }

  /* ---------- per-tick driver ---------- */

  function afterFrame() {
    if (tr.done || !tr.enabled) return;
    tr.tick++;
    emitUi();
    var fight = fightObj();
    if (!fight) { auClear(); return; }
    if (SKIP_FIGHT_NONE) {
      try {
        if (fight.Da && fight.Da.type === "FightNone") return;
      } catch (e) { /* ignore */ }
    }
    ensureHooks(fight);
    ensureDrawHook(fight);
    // One line per FIGHT frame, from the first LIVE frame — exactly like the
    // native `dump_state_frame` (`if (phase_ == fight_phase::idle) return;`
    // plus its once-per-update call). The JS `eu == 0` pre-round intro holds
    // `fight.frame` at 0 for many app ticks (the VS overlay / round plate), so
    // without this the trace would carry dozens of duplicate `f=0,phase=0`
    // lines and the two-pointer alignment would stall.
    try {
      if (fight.eu === 0) { auClear(); return; }
      if (typeof fight.frame === "number" && fight.frame === tr.lastFightFrame)
        return;
      tr.lastFightFrame = fight.frame;
    } catch (e) { /* ignore */ }
    try {
      if (tr.relaxPre && tr.relaxPre.length) {
        var frNow = (typeof fight.frame === "number") ? fight.frame : 0;
        // Sample from the FIRST live frame: the separation of i210/i224 is
        // already present at the old window start (f=100), so the onset has to
        // be found below it.
        if (frNow >= 0 && frNow <= 140) {
          for (var pi = 0; pi < tr.relaxPre.length; pi++) {
            var snap = tr.relaxPre[pi];
            var edges2 = snap.sol && snap.sol.oa && snap.sol.oa.Nl && snap.sol.oa.Nl.all;
            if (!edges2) continue;
            for (var key in snap.row) {
              if (!Object.prototype.hasOwnProperty.call(snap.row, key)) continue;
              var rec = snap.row[key];
              var e2 = edges2[rec.n];
              if (!e2 || !e2.sx || !e2.Zs) continue;
              var a2 = e2.sx.ma, b2 = e2.Zs.ma;
              var ddx = a2.x - b2.x, ddy = a2.y - b2.y, ddz = a2.z - b2.z;
              console.log(JSON.stringify({
                t: "relax", f: frNow, side: rec.side, e: key,
                pre: r3(rec.pre),
                post: r3(Math.sqrt(ddx * ddx + ddy * ddy + ddz * ddz)),
                len: r3(rec.len)
              }));
            }
          }
        }
        tr.relaxPre = null;
      }
    } catch (eR2) { /* ignore */ }
    // Snapshot the CURRENT lengths as the PRE of the next frame: fterFrame
    // runs after a, so this is exactly the post state of this frame.
    try {
      tr.relaxPre = relaxSnapshot(fight);
      if (!tr.relaxDbg || (tr.frameCount % 200) === 0) {
        tr.relaxDbg = 1;
        var dbgSides = [];
        try {
          var pSol = fight.pb && fight.pb.Nd;
          var ySol = fight.yb && fight.yb.Nd;
          dbgSides.push(pSol && pSol.oa && pSol.oa.Nl && pSol.oa.Nl.all ? pSol.oa.Nl.all.length : -1);
          dbgSides.push(ySol && ySol.oa && ySol.oa.Nl && ySol.oa.Nl.all ? ySol.oa.Nl.all.length : -1);
        } catch (eD) { dbgSides.push("err"); }
        console.log('{"t":"relaxdbg","pre":' + (tr.relaxPre ? tr.relaxPre.length : -1) +
                    ',"sides":' + JSON.stringify(dbgSides) + '}');
      }
    } catch (eR3) {
      console.log('{"t":"relaxdbg","err":' + JSON.stringify(String(eR3)) + '}');
    }
    /* PRE-JE PROBE (`?preje=1`, harness-only): the earlier end-of-frame
     * snapshot measures the state AFTER jE, so it cannot separate "the port
     * arrives more distorted" from "we sampled a different stage". The excess
     * measured per edge sits on cloth<->SKELETON edges (top 10: +0.20..+0.41),
     * which is exactly the stage-sensitive class, so read the oracle's TRUE
     * post-sk/pre-jE state by wrapping ONE instance's `jE` (not the prototype -
     * that broke the trace) with every step guarded. */
    try {
      if (new URLSearchParams(window.location.search).get("preje") === "1" &&
          !tr.prejeHooked) {
        var meF = fight.yb;
        var sol2 = meF && meF.Nd;
        if (sol2 && typeof sol2.jE === "function") {
          var orig2 = sol2.jE;
          sol2.jE = function () {
            var pre = null;
            try {
              var edges3 = this.oa && this.oa.Nl && this.oa.Nl.all;
              if (edges3 && edges3.length > 30) {
                pre = new Array(edges3.length);
                for (var i3 = 0; i3 < edges3.length; i3++) {
                  var e3 = edges3[i3];
                  var a3 = e3 && e3.sx ? e3.sx.ma : null;
                  var b3 = e3 && e3.Zs ? e3.Zs.ma : null;
                  if (!a3 || !b3) continue;
                  var dx3 = a3.x - b3.x, dy3 = a3.y - b3.y, dz3 = a3.z - b3.z;
                  pre[i3] = Math.sqrt(dx3 * dx3 + dy3 * dy3 + dz3 * dz3);
                }
              }
            } catch (e0) { pre = null; }
            var res = orig2.apply(this, arguments);
            try {
              if (pre) {
                var f3 = (typeof fight.frame === "number") ? fight.frame : -1;
                // Whole-run window: the stage-matched comparison is only
                // useful if it can find the FIRST frame at which the solver
                // input states separate, not just a fixed early slice.
                if (f3 >= 0 && f3 <= 600) {
                  for (var k3 = 185; k3 < Math.min(pre.length, 245); k3++) {
                    if (pre[k3] == null) continue;
                    console.log(JSON.stringify({
                      t: "preje", f: f3, i: k3,
                      d: r3(pre[k3]),
                      len: r3(typeof this.oa.Nl.all[k3].length === "number"
                              ? this.oa.Nl.all[k3].length : 0)
                    }));
                  }
                }
              }
            } catch (e1) { /* ignore */ }
            return res;
          };
          tr.prejeHooked = 1;
          console.log('{"t":"prejehooked"}');
        }
      }
    } catch (eh) { /* ignore */ }
    /* SOLVER-CADENCE PROBE (`?iacount=1`, harness-only): count how many times the
     * CLOTH SOLVER runs per fight frame. JS `Te.ia` (L20591) calls
     * `this.Nd.ia()` (the `Al` solver = `sk(); jE();`) ONCE per call, but `Te.ia`
     * itself may run more than once per rendered frame - if it does, the cloth
     * integrates more steps than the port's one-per-frame and the chain rotates
     * at a different rate (the measured residual is almost purely TANGENTIAL).
     * Logs a counter line per fight frame and the total per frame. */
    try {
      if (new URLSearchParams(window.location.search).get("iacount") === "1") {
        if (!tr.iaHooked) {
          tr.iaHooked = 1;
          tr.iaTot = 0;
          var wrapNd = function (m) {
            var da = m && m.da;
            if (!da || typeof da.ia !== "function" || da.__wrapped) return;
            // Count the ANIMATOR tick (`Te.ia`), which calls the cloth solver
            // (`this.Nd.ia()`) exactly once - so the count is the solver cadence.
            var orig = da.ia;
            da.ia = function () {
              tr.iaTot++;
              return orig.apply(this, arguments);
            };
            da.__wrapped = 1;
          };
          try {
            var d0 = fight && fight.yb && fight.yb.da;
            console.log(JSON.stringify({ t: "iakeys", n: d0 ? Object.keys(d0).length : -1,
                                         hasNd: !!(d0 && d0.Nd),
                                         hasIa: !!(d0 && typeof d0.ia === "function") }));
          } catch (e00) { /* ignore */ }
          try { wrapNd(fight.yb); } catch (e0) { /* ignore */ }
          try { wrapNd(fight.pb); } catch (e1) { /* ignore */ }
          console.log('{"t":"iahooked"}');
          tr.iaTot = 0;
        }
        var fr2 = -1;
        try { fr2 = fight.frame; } catch (e2) { fr2 = -1; }
        console.log(JSON.stringify({ t: "ia", f: fr2, tot: tr.iaTot }));
      }
    } catch (e3) { /* ignore */ }
    /* CLIP-SAMPLE PROBE (`?clipsamples=1`, harness-only): snapshot the chain
     * nodes' `ma` immediately BEFORE the cloth solver runs. `Te.ia` writes the
     * clip pose into every node (`e.ma = clip`) via `eda`, and only then calls
     * `this.Nd.ia()` = `sk(); jE();`. Wrapping `Nd.ia` and reading
     * `Nd.oa.Va.all[i].ma` first therefore yields exactly the pre-solver input
     * the port's `[clipsample]` line logs (same band 76..81, 91, 92). */
    try {
      if (new URLSearchParams(window.location.search).get("clipsamples") === "1") {
        if (!tr.csHooked) {
          tr.csHooked = 1;
          var wrapSol = function (ftr, side) {
            var sol = ftr && ftr.Nd;
            if (!sol || typeof sol.ia !== "function" || sol.__csWrapped) return;
            var orig = sol.ia;
            var BAND = [76, 77, 78, 79, 80, 81, 91, 92];
            sol.ia = function () {
              try {
                var nodes = sol.oa && sol.oa.Va && sol.oa.Va.all;
                if (nodes && nodes.length > 92) {
                  var fr = -1;
                  try { fr = fight.frame; } catch (e0) { fr = -1; }
                  // The same key compare_pose aligns by: (clip, cf) = the JS
                  // `da.Ua.name` and `da.Xh` (the port dumps `playhead_xh()`).
                  var clipName = "", cfv = -1;
                  try {
                    var anim = ftr && ftr.da;
                    if (anim) {
                      if (anim.Ua && anim.Ua.name != null) {
                        clipName = String(anim.Ua.name);
                      }
                      if (typeof anim.Xh === "number") cfv = anim.Xh;
                    }
                  } catch (e9) { /* ignore */ }
                  for (var bi = 0; bi < BAND.length; bi++) {
                    var ix = BAND[bi];
                    var p = nodes[ix] && nodes[ix].ma;
                    if (!p) continue;
                    console.log("[clipsample] s=" + side + " f=" + fr +
                                " clip=" + clipName + " cf=" + cfv + " i=" + ix +
                                " x=" + p.x.toFixed(5) +
                                " y=" + p.y.toFixed(5) +
                                " z=" + p.z.toFixed(5));
                  }
                }
              } catch (e1) { /* ignore */ }
              return orig.apply(this, arguments);
            };
            sol.__csWrapped = 1;
          };
          try { wrapSol(fight.pb, "Enemy"); } catch (e2) { /* ignore */ }
          try { wrapSol(fight.yb, "Me"); } catch (e3) { /* ignore */ }
          console.log('{"t":"cshooked"}');
        }
      }
    } catch (e4) { /* ignore */ }
    /* TU TRACE (`?tutrace=1`, harness-only): the Gc bus widgets that frame
     * (`Gc.Tu`), sampled INSIDE `Gc.Gnb` - the function CONSUMES and clears
     * `Tu` (`Gnb`: `... a = this.Tu.length = 0`), so reading it after `Bg.ia`
     * only ever showed leftovers. The wrapper prints the list the pump really
     * had, then calls the original. Read-only apart from the wrapper. */
    try {
      if (new URLSearchParams(window.location.search).get("tutrace") === "1") {
        if (!tr.tuHooked && fight.Bg && typeof fight.Bg.Gnb === "function") {
          tr.tuHooked = 1;
          var origGnb = fight.Bg.Gnb;
          fight.Bg.Gnb = function () {
            try {
              var tu = this.Tu || [];
              var tv = [];
              for (var ti = 0; ti < tu.length; ti++) {
                var el = tu[ti];
                var who = "?";
                try {
                  if (el.model && el.model === fight.yb) {
                    who = "Me";
                  } else if (el.model && el.model === fight.pb) {
                    who = "Enemy";
                  }
                } catch (eW) { who = "?"; }
                tv.push(el.type + ":" + who);
              }
              console.log("[tub] f=" +
                          (typeof fight.frame === "number" ? fight.frame : -1) +
                          " n=" + tv.length + " [" + tv.join(" ") + "]");
            } catch (eB) { /* ignore */ }
            return origGnb.apply(this, arguments);
          };
        }
      }
    } catch (eTU) { /* ignore */ }
    emitMovesOnce(fight);
    console.log(JSON.stringify(frameJson(fight)));
    var stDbg = fightState(fight);
    console.log(JSON.stringify(stDbg));
    try { dbgTick(stDbg, fight); } catch (eDbg) { /* overlay is display-only */ }
    emitAudio(fight);
    tr.frameCount++;
    captureClips(fight);
    if (tr.frameCount >= MAX_FRAMES) finish();
  }

  function finish() {
    tr.done = true;
    console.log(JSON.stringify({ t: "audiodiag", d: AU.diag, hooked: AU.hooked }));
    console.log("[TRACE] done " + tr.frameCount + " frames, " + tr.clips + " clips");
    /* Best effort: request the browser to close the tab/runtime. WebView2
     * ignores window.close() for host-embedded pages, so the shell process is
     * expected to be terminated externally; the console.log stream is flushed
     * line-by-line (File.AppendAllText), so no data is lost. */
    try { window.close(); } catch (e) { /* ignore */ }
    tr.enabled = false;
  }

  function snapshot() {
    var fight = fightObj();
    var snap = {
      enabled: tr.enabled,
      hooked: tr.hooked,
      done: tr.done,
      frames: tr.frameCount,
      clips: tr.clips,
      screen: { id: screenId(), name: screenName(screenId()) }
    };
    if (fight) {
      snap.fight = {
        frame: fight.frame,
        phase: fight.eu,
        round: fight.round ? fight.round.round : null,
        timer: fight.round ? fight.round.time : null,
        cam: tr.cam || camFallback(fight),
        player: fighterJson(fight.pb, "Me"),
        enemy: fighterJson(fight.yb, "Enemy")
      };
    }
    return snap;
  }

  /* ---------- hook scaffolding (v1, unchanged) ---------- */

  function fail(err) {
    if (!tr.enabled) return;
    tr.enabled = false;
    console.log("[TRACE] ERR " + (err && err.message ? err.message : String(err)));
  }

  function markHooked(path) {
    tr.hooked = true;
    window.__sf2Trace.hooked = true;
    console.log("[TRACE] hooked " + path);
  }

  function wrapFrameMethod(fn) {
    return function (a) {
      try {
        beforeFrame();
      } catch (err) {
        fail(err);
      }
      var result = fn.apply(this, arguments);
      try {
        afterFrame();
      } catch (err) {
        fail(err);
      }
      return result;
    };
  }

  function hookApp(app) {
    if (!app || app.__sf2Hooked) return false;
    if (typeof app.aa !== "function" || !app.root || !app.Sc) return false;
    app.__sf2Hooked = true;

    app.aa = wrapFrameMethod(app.aa);
    hookAudio(app);
    hookStimCheck();

    if (typeof app.lbb === "function") {
      var origLbb = app.lbb;
      app.lbb = function (a) {
        var r = origLbb.apply(this, arguments);
        try {
          var node = this.root && this.root.af;
          while (node) {
            if (node.Mr && node.Mr.stack) { tr.mc = node.Mr; break; }
            node = node.Ma;
          }
        } catch (e) { /* ignore */ }
        return r;
      };
    }
    return true;
  }

  function tryDirect() {
    var Pg = null;
    if (typeof window !== "undefined" && window.Pg != null) {
      Pg = window.Pg;
    } else {
      try {
        /* eslint-disable-next-line no-eval */
        Pg = eval("Pg");
      } catch (e) {
        Pg = undefined;
      }
    }
    if (Pg && Pg.prototype && typeof Pg.prototype.aa === "function") {
      Pg.prototype.aa = wrapFrameMethod(Pg.prototype.aa);
      markHooked("Pg.prototype.aa (direct)");
      hookStimCheck();
      return true;
    }
    return false;
  }

  function tryFallback() {
    console.log("[TRACE] Pg not accessible (module-scoped), hooking Function.prototype.bind to detect the app instance");
    if (typeof Function.prototype.bind !== "function") {
      console.log("[TRACE] ERR Function.prototype.bind unavailable");
      return;
    }
    var origBind = Function.prototype.bind;
    Function.prototype.bind = function (thisArg) {
      var bound = origBind.apply(this, arguments);
      try {
        if (thisArg && typeof thisArg.aa === "function" && thisArg.root && thisArg.Sc) {
          if (hookApp(thisArg)) {
            markHooked("app instance aa via bind detection");
          }
        }
      } catch (e) { /* ignore */ }
      return bound;
    };
  }

  /* STATE-HARNESS: arm the profile probe (bounded periodic re-emit so a
   * save the game writes later in the run is captured; the extractor keeps
   * the last line). */
  if (PROBE_SAVE) {
    emitSavegame();
    var probeTicks = 0;
    var probeTimer = setInterval(function () {
      emitSavegame();
      if (++probeTicks >= 40) clearInterval(probeTimer);
    }, 1000);
  }

  if (!tryDirect()) {
    tryFallback();
  }
})();