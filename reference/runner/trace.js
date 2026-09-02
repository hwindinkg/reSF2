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
  var MAX_CLIPS = 5; /* at most this many clip dumps per run */

  var tr = {
    enabled: true,
    hooked: false,
    lastScreen: -1,
    mc: null, /* screen manager (mc.K), captured from the app's lbb() */
    done: false,
    frameCount: 0,
    clips: 0,
    cam: null, /* {cx,cy,zoom} set by the Ut.Al hook */
    seenClips: {}, /* clip label -> true (dump once per clip per run) */
    lastClip: new WeakMap() /* fighter -> clip label seen last tick */
  };

  window.__sf2Trace = {
    enabled: true,
    hooked: false,
    lastScreen: -1,
    state: function () { return snapshot(); }
  };

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
   * native port use), as [x,y] pairs with z dropped, 3 decimals. */
  function bones2d(f) {
    var out = [];
    try {
      var all = f.oa && f.oa.Va && f.oa.Va.all;
      if (all && all.length) {
        for (var i = 0; i < all.length; i++) {
          var m = all[i] && all[i].ma;
          out.push([m ? r3(m.x) : 0, m ? r3(m.y) : 0]);
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
    out.bones = bones2d(f);
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

  /* ---------- per-tick driver ---------- */

  function afterFrame() {
    if (tr.done || !tr.enabled) return;
    var fight = fightObj();
    if (!fight) return;
    ensureHooks(fight);
    console.log(JSON.stringify(frameJson(fight)));
    tr.frameCount++;
    captureClips(fight);
    if (tr.frameCount >= MAX_FRAMES) finish();
  }

  function finish() {
    tr.done = true;
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

  if (!tryDirect()) {
    tryFallback();
  }
})();