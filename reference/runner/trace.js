/*
 * trace.js — per-frame state tracer for the SF2 web build.
 *
 * Loaded by the runner AFTER sf2.502f0946.js (see index.html GameInterface.init).
 * Hooks the game's fixed 60 Hz update pass and after each tick logs:
 *   - screen transitions: [TRACE] screen <id> <name>
 *   - fight frames:       [TRACE] F<frame>|<phase>|<round>|<timer>|P:<x>,<y>,<hp>,<anim>,<face>,<move>|E:<...>
 *
 * Exposes window.__sf2Trace.state() returning a JSON snapshot of the same data
 * for the shell to poll. Dependency-free.
 *
 * Hook strategy: the game script is wrapped in a UMD IIFE, so Pg/ca/mc/xn are
 * module-scoped and never appear on window (the only export is SF2.main).
 * We therefore try Pg.prototype.aa first (direct), and if Pg is unreachable we
 * hook Function.prototype.bind: the game binds every app method through its
 * w() helper (w(this, this.xeb) etc.), so the first bind with the app instance
 * (identifiable by aa+root+Sc) lets us hook the instance's own `aa` method —
 * functionally identical to hooking Pg.prototype.aa — and its `lbb` method,
 * which creates the screen manager (mc.K) and lets us read screen/fight state.
 */
(function () {
  "use strict";

  var state = {
    enabled: true,
    hooked: false,
    lastScreen: -1,
    hookPath: null,
    errored: false,
    mc: null /* screen manager (mc.K), captured from the app's lbb() */
  };

  window.__sf2Trace = {
    enabled: true,
    hooked: false,
    lastScreen: -1,
    hookPath: null,
    state: function () { return buildSnapshot(); }
  };

  /* Screen id -> name (replica of xn.iOa, JS_MAP §5.1 — xn is module-scoped). */
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

  /* Current screen id from the top of mc.K.stack (DQ on input screens, dJ on
   * screen states — JS_MAP §5.2 conflates the two; accept either). */
  function screenId() {
    var mgr = state.mc;
    if (mgr && mgr.stack && mgr.stack.length > 0) {
      var top = mgr.stack[mgr.stack.length - 1];
      if (top) {
        if (typeof top.DQ === "function") return top.DQ();
        if (typeof top.dJ === "function") return top.dJ();
      }
    }
    return -1;
  }

  /* Move id: f.jb is a fighter-like object (the current move script / target —
   wd.Naa pushes the opponent into HB and Zka defaults jb to HB[0]; wd.wI sets
   jb=a.jb when a move is active). Extract a readable identifier. */
  function moveId(v) {
    if (v == null) return null;
    if (typeof v === "object") {
      if (v.parameters && v.parameters.$s != null) return v.parameters.$s;
      if (v.name != null) return v.name;
      if (v.zP != null) return v.zP;
      return "[move]";
    }
    return v;
  }

  /* Animation name: f.zP is the JS_MAP-documented field, but in this build it is
   only updated by HUD-button animation events (ca.z3) and stays "" without
   input — the real fighter animation is f.da.Ua.name. Prefer zP, fall back. */
  function animName(f) {
    if (f.zP != null && f.zP !== "") return f.zP;
    try {
      if (f.da && f.da.Ua && f.da.Ua.name != null && f.da.Ua.name !== "") return f.da.Ua.name;
    } catch (e) { /* ignore */ }
    return f.zP != null ? f.zP : null;
  }

  /* Per-fighter fields: x/y = f.oa.Fe().ma, hp = f.parameters.gd,
   * anim = f.zP (fallback f.da.Ua.name), face = f.da.hd(), move = f.jb. */
  function fighterFields(f) {
    if (!f) return null;
    var x = null, y = null;
    try {
      if (f.oa && typeof f.oa.Fe === "function") {
        var fe = f.oa.Fe();
        if (fe && fe.ma) { x = fe.ma.x; y = fe.ma.y; }
      }
    } catch (e) { /* body not built yet */ }
    var params = f.parameters || null;
    var da = f.da || null;
    return {
      x: x,
      y: y,
      hp: params ? params.gd : null,
      anim: animName(f),
      face: da && typeof da.hd === "function" ? da.hd() : null,
      move: moveId(f.jb)
    };
  }

  /* Fight snapshot: the top screen's Ig field is the ca fight controller
   * (Tf/ai store it there; ca.h8 is set in the ca constructor). */
  function fightSnapshot() {
    var mgr = state.mc;
    if (!mgr || !mgr.stack || mgr.stack.length === 0) return null;
    var top = mgr.stack[mgr.stack.length - 1];
    if (!top || !top.Ig) return null;
    var fight = top.Ig;
    if (!fight || !fight.pb || !fight.yb || typeof fight.frame !== "number") return null;
    var round = fight.round || null;
    return {
      frame: fight.frame,
      phase: fight.eu,
      round: round ? round.round : null,
      timer: round ? round.time : null,
      player: fighterFields(fight.pb),
      enemy: fighterFields(fight.yb)
    };
  }

  function buildSnapshot() {
    var id = screenId();
    var snap = {
      enabled: state.enabled,
      hooked: state.hooked,
      hookPath: state.hookPath,
      screen: { id: id, name: screenName(id) }
    };
    var fight = fightSnapshot();
    if (fight) snap.fight = fight;
    return snap;
  }

  function fmt3(v) {
    return typeof v === "number" && isFinite(v) ? v.toFixed(3) : "?";
  }

  function fmt(v) {
    return v != null ? String(v) : "?";
  }

  function fighterLine(f) {
    if (!f) return "?,?,?,?,?,?";
    return fmt3(f.x) + "," + fmt3(f.y) + "," + fmt(f.hp) + "," +
      fmt(f.anim) + "," + fmt(f.face) + "," + fmt(f.move);
  }

  function afterFrame() {
    if (!state.enabled) return;

    var id = screenId();
    if (id !== state.lastScreen) {
      state.lastScreen = id;
      window.__sf2Trace.lastScreen = id;
      console.log("[TRACE] screen " + id + " " + screenName(id));
    }

    var fight = fightSnapshot();
    if (fight) {
      console.log(
        "[TRACE] F" + fmt(fight.frame) + "|" + fmt(fight.phase) + "|" +
        fmt(fight.round) + "|" + fmt(fight.timer) + "|P:" +
        fighterLine(fight.player) + "|E:" + fighterLine(fight.enemy)
      );
    }
  }

  function fail(err) {
    if (state.errored) return;
    state.errored = true;
    state.enabled = false;
    window.__sf2Trace.enabled = false;
    console.log("[TRACE] ERR " + (err && err.message ? err.message : String(err)));
  }

  function markHooked(path) {
    state.hooked = true;
    state.hookPath = path;
    window.__sf2Trace.hooked = true;
    window.__sf2Trace.hookPath = path;
    console.log("[TRACE] hooked " + path);
  }

  /* Wrap a per-frame method (the app's aa) so afterFrame runs after each tick. */
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

  /* Hook the app instance: own `aa` shadows Pg.prototype.aa; own `lbb` lets us
   * capture the screen manager (mc.K) it creates. */
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
            if (node.Mr && node.Mr.stack) { state.mc = node.Mr; break; }
            node = node.Ma;
          }
        } catch (e) { /* ignore */ }
        return r;
      };
    }
    return true;
  }

  /* Preferred path: wrap Pg.prototype.aa (the fixed 60 Hz update pass). */
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

  /* Fallback path: the game binds every app method via w() -> Function.prototype.bind,
   * so detect the app instance (aa+root+Sc) on the first bind and hook it. */
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