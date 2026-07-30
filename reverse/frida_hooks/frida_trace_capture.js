// reverse/frida_hooks/frida_trace_capture.js
//
// Capture input→state→output traces from the ORIGINAL Shadow Fight 2 game.
//
// The recorded traces are the source of truth our reverse-engineered engine
// must reproduce. Run this script against the original Android APK on a device
// with frida-server, perform a short gameplay scenario, then pull the JSON
// trace out via the RPC export and save it under reverse/traces/.
//
// Usage:
//   frida -U -n com.nekki.shadowfight -l frida_trace_capture.js
//   # perform scenario in-game
//   # in the frida REPL:
//     > traces = rpc.exports.getTraces()
//     > saveTraces("block_when_idle.json", traces)
//
// The script hooks three layers:
//   1. Input processing — keyDown / keyUp events as the game sees them
//   2. Fighter state    — move_state_, pos_x_, health, blocking, animation
//   3. AI decisions     — block_decision / tactic_choice results
//
// All timestamps are milliseconds relative to session start.

'use strict';

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

var GAME_SO      = "libgame.so";  // Not used in Marmalade — game is loaded from OBB
var S3E_SO       = "libs3e_android.so";
var FRAME_MS     = 16;   // ~60 Hz gameplay tick
var MAX_TRACES   = 8192; // ring-buffer cap; prevents unbounded growth

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

var traces        = [];
var sessionStart  = Date.now();
var gameBase      = null;  // Will be set to main game memory region
var gameSize      = 0;
var s3eBase       = null;

// [MARMALADE] Game binary is not loaded as .so — it's mapped from OBB by S3E engine.
// From previous Frida profiling, main game region is ~8.2MB at runtime address.
// We'll detect it by scanning for large executable memory regions.

// Known offsets (ARM, APK 1.9.21 / 2.46). Update when porting to another
// build. Values were established during the RE sessions captured in
// reverse/analysis/.
var OFF = {
    // Fighter state (read from `this` of Fighter::tick / Player::tick)
    fighter_move_state:   0x0B4,  // int  — 0 idle, 1 walk_back, 2 walk_fwd, ...
    fighter_pos_x:        0x0C8,  // float
    fighter_pos_y:        0x0CC,  // float
    fighter_facing_right: 0x0D0,  // bool (u32)
    fighter_is_blocking:  0x120,  // bool
    fighter_health_cur:   0x140,  // float
    fighter_health_max:   0x144,  // float
    fighter_anim_id:      0x160,  // int  — index into animation table

    // AI / tactic
    tactic_block_choice:  0x018,  // int  — 0 none, 1 high, 2 low, 3 duck
    tactic_score:         0x01C,  // float
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function nowMs() { return Date.now() - sessionStart; }

function hex(a) {
    if (!a) return "0x0";
    return "0x" + a.toString(16);
}

function readFloatSafe(p, off) {
    try { return p.add(off).readFloat(); } catch (e) { return NaN; }
}
function readU32Safe(p, off) {
    try { return p.add(off).readU32(); } catch (e) { return 0; }
}

function pushTrace(evt) {
    if (traces.length >= MAX_TRACES) {
        // Drop oldest — keeps the buffer useful for long sessions
        traces.splice(0, traces.length - MAX_TRACES + 256);
    }
    evt.t = nowMs();
    traces.push(evt);
}

function resolveModules() {
    // [MARMALADE] Game binary is not loaded as .so — it's mapped from OBB by S3E engine.
    // We need to find the main game memory region by scanning for large executable ranges.
    if (!gameBase) {
        try {
            // Scan for large executable memory regions (>5MB) that are not system libraries
            var ranges = Process.enumerateRanges('r-x');
            for (var i = 0; i < ranges.length; i++) {
                var r = ranges[i];
                // Game region is typically 8-9MB, not a named module
                if (r.size > 5 * 1024 * 1024 && r.size < 10 * 1024 * 1024) {
                    // Check if it's not a known system library
                    var isSystem = false;
                    try {
                        var mod = Process.findModuleByAddress(r.base);
                        if (mod && (mod.name.includes("libandroid") || mod.name.includes("libc.") || 
                                    mod.name.includes("libart") || mod.name.includes("boot."))) {
                            isSystem = true;
                        }
                    } catch (e) {}
                    
                    if (!isSystem) {
                        gameBase = r.base;
                        gameSize = r.size;
                        console.log("[trace] Found game region @ " + hex(gameBase) + 
                                    " size=" + (gameSize / 1024 / 1024).toFixed(1) + "MB");
                        break;
                    }
                }
            }
            if (!gameBase) {
                console.log("[trace] Game region not found yet (scanning...)");
            }
        } catch (e) { 
            console.log("[trace] resolve game region: " + e); 
        }
    }

    // Also find libs3e_android.so for input hooks
    if (!s3eBase) {
        try {
            var s = Module.findBaseAddress(S3E_SO);
            if (s) {
                s3eBase = s;
                console.log("[trace] " + S3E_SO + " @ " + hex(s3eBase));
            }
        } catch (e) { console.log("[trace] resolve s3e: " + e); }
    }
}

// ---------------------------------------------------------------------------
// Hook: input processing (s3e pointer / key callbacks)
// ---------------------------------------------------------------------------

function hookInput() {
    if (!s3eBase) return;

    // [FIXED] Previous hardcoded offsets (0x0004a820, 0x0004a9c0) were wrong
    // for this build — they pointed to non-input code that produced garbage.
    // Instead, we hook through S3E exports which are stable across builds.
    
    // Capture only real input events - filter out the 60fps spam from wrong offsets
    // We do this by checking if the hook address actually is a callback-style function.
    // For now, rely on Model::tick state changes to infer what happened.

    // Hook s3ePointer callback registration to find the game's pointer handler
    try {
        var setPointerCb = Module.findExportByName("libs3e_android.so", "s3ePointerSetCallback");
        if (setPointerCb) {
            Interceptor.attach(setPointerCb, {
                onEnter: function (args) {
                    console.log("[trace] s3ePointerSetCallback registered: " + args[0]);
                }
            });
        }
    } catch (e) {}

    // Hook s3eKeyboard callback registration  
    try {
        var setKeyCb = Module.findExportByName("libs3e_android.so", "s3eKeyboardSetCallback");
        if (setKeyCb) {
            Interceptor.attach(setKeyCb, {
                onEnter: function (args) {
                    console.log("[trace] s3eKeyboardSetCallback registered: " + args[0]);
                }
            });
        }
    } catch (e) {}
}

// ---------------------------------------------------------------------------
// Hook: fighter state — read on every main tick
// ---------------------------------------------------------------------------

function hookFighterTick() {
    if (!gameBase) return;

    // Main gameplay tick. Calls Player::tick and Enemy::tick.
    // We hook the entry so we can snapshot both fighters after they update.
    var mainTick = gameBase.add(0x0002f0e0); // Model::tick (see analysis/)
    try {
        Interceptor.attach(mainTick, {
            onEnter: function (args) {
                // ARM: args[0] = r0 = this pointer (first arg = this for C++ methods)
                // Save it for onLeave — r0 gets clobbered with return value
                this.selfPtr = args[0];
            },
            onLeave: function (retval) {
                try {
                    var self = this.selfPtr;
                    if (!self) return;
                    var player = self.add(0x08).readPointer();
                    var enemy  = self.add(0x0C).readPointer();

                    pushTrace({
                        type: "state",
                        data: {
                            player: readFighter(player),
                            enemy:  readFighter(enemy)
                        }
                    });
                } catch (e) {}
            }
        });
        console.log("[trace] hooked Model::tick @ " + hex(mainTick));
    } catch (e) {
        console.log("[trace] tick hook failed: " + e);
    }
}

function readFighter(p) {
    if (p.isNull() || p.compare(ptr(0x1000)) < 0) return null;
    return {
        move_state:   readU32Safe(p, OFF.fighter_move_state),
        pos_x:        readFloatSafe(p, OFF.fighter_pos_x),
        pos_y:        readFloatSafe(p, OFF.fighter_pos_y),
        facing_right: readU32Safe(p, OFF.fighter_facing_right) !== 0,
        is_blocking:  readU32Safe(p, OFF.fighter_is_blocking) !== 0,
        hp_cur:       readFloatSafe(p, OFF.fighter_health_cur),
        hp_max:       readFloatSafe(p, OFF.fighter_health_max),
        anim_id:      readU32Safe(p, OFF.fighter_anim_id)
    };
}

// ---------------------------------------------------------------------------
// Hook: AI / block decision
// ---------------------------------------------------------------------------

function hookBlockDecision() {
    if (!gameBase) return;

    // BlockBrain::decide — returns a tactic enum (0..3). The score is
    // written to *(this+0x1C). Address from frida_hook_block_decision.js.
    var decide = gameBase.add(0x0005c740);
    try {
        Interceptor.attach(decide, {
            onEnter: function (args) {
                // Save this pointer — r0 gets clobbered by return value in onLeave
                this.selfPtr = args[0];
            },
            onLeave: function (retval) {
                try {
                    var self = this.selfPtr;
                    if (!self) return;
                    pushTrace({
                        type: "ai_decision",
                        data: {
                            choice: retval.toInt32(),
                            score:  readFloatSafe(self, OFF.tactic_score),
                            ctx: {
                                dist:       readFloatSafe(self, 0x004),
                                enemy_anim: readU32Safe (self, 0x008),
                                my_hp_frac: readFloatSafe(self, 0x00C)
                            }
                        }
                    });
                } catch (e) {}
            }
        });
        console.log("[trace] hooked BlockBrain::decide @ " + hex(decide));
    } catch (e) {
        console.log("[trace] block-decision hook failed: " + e);
    }
}

// ---------------------------------------------------------------------------
// RPC exports — let the operator pull traces out of the running process
// ---------------------------------------------------------------------------

rpc.exports = {
    gettraces: function () {
        return JSON.stringify(traces);
    },
    getTraces: function () {      // camelCase alias
        return JSON.stringify(traces);
    },
    cleartraces: function () {
        var n = traces.length;
        traces = [];
        return n;
    },
    clearTraces: function () {    // camelCase alias
        var n = traces.length;
        traces = [];
        return n;
    },
    tracecount: function () {
        return traces.length;
    },
    traceCount: function () {     // camelCase alias
        return traces.length;
    }
};

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

function attachAll() {
    resolveModules();
    if (gameBase) {
        hookInput();
        hookFighterTick();
        hookBlockDecision();
    } else {
        // Game hasn't loaded yet — retry in 1 s.
        setTimeout(attachAll, 1000);
    }
}

console.log("[trace] frida_trace_capture.js loaded");
console.log("[trace] scanning for game memory region...");
attachAll();
