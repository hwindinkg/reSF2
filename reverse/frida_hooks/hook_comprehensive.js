/**
 * Shadow Fight 2 - Comprehensive ARM Hook Script (v2)
 * 
 * Based on profiling results:
 * - Game binary at 0x8f35f000 (ARM, ~8.5MB)
 * - Hot function at offset 0x692464 (246 calls/sec - likely input/event handler)
 * - 8 functions in nearby region to investigate
 * 
 * Hooks:
 * 1. All 8 functions in hot region with backtrace
 * 2. Java-side input dispatch (MotionEvent)
 * 3. Broader scan of game binary for additional hotspots
 */

var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;

var logs = [];
function log(msg) {
    var ts = Date.now();
    var line = '[' + ts + '] ' + msg;
    console.log(line);
    logs.push(line);
    if (logs.length > 5000) logs.shift(); // Keep last 5000 lines
}

log('=== SF2 Comprehensive Hook v2 ===');
log('Game base: ' + GAME_BASE);

// ============================================================================
// Part 1: Hook all 8 functions in the hot region
// ============================================================================
var hotRegion = [
    { offset: 0x68f194, name: 'hot_region_1' },
    { offset: 0x690e44, name: 'hot_region_2' },
    { offset: 0x691e8c, name: 'hot_region_3' },
    { offset: 0x691ef0, name: 'hot_region_4' },
    { offset: 0x692464, name: 'INPUT_DISPATCHER' }, // Known hot function
    { offset: 0x69251c, name: 'hot_region_6' },
    { offset: 0x693480, name: 'hot_region_7' },
    { offset: 0x694f18, name: 'hot_region_8' },
];

hotRegion.forEach(function(entry) {
    var addr = GAME_BASE.add(entry.offset);
    try {
        Interceptor.attach(addr, {
            onEnter: function(args) {
                var r0 = this.context.r0;
                var r1 = this.context.r1;
                var r2 = this.context.r2;
                var r3 = this.context.r3;
                
                log('[ENTER ' + entry.name + ' @' + entry.offset.toString(16) + ']' +
                    ' r0=' + r0 + ' r1=' + r1 + ' r2=' + r2 + ' r3=' + r3);
                
                // Get backtrace (top 5 frames)
                var bt = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 5);
                log('  BT: ' + bt.map(a => {
                    if (a.compare(GAME_BASE) >= 0 && a.compare(GAME_BASE.add(GAME_SIZE)) < 0) {
                        return 'game+0x' + a.sub(GAME_BASE).toInt32().toString(16);
                    }
                    return a.toString();
                }).join(' < '));
                
                // Save args for onLeave
                this.savedR0 = r0;
                this.name = entry.name;
            },
            onLeave: function(retval) {
                log('[LEAVE ' + this.name + '] -> ' + retval);
            }
        });
        log('Hooked ' + entry.name + ' at ' + addr);
    } catch(e) {
        log('Failed to hook ' + entry.name + ': ' + e);
    }
});

// ============================================================================
// Part 2: Hook broader game binary - find game loop
// Do a denser scan around the first 2MB of code (most likely game logic area)
// ============================================================================
var scanSize = Math.min(2 * 1024 * 1024, GAME_SIZE);
var prologs = Memory.scanSync(GAME_BASE, scanSize, 'f0 41 2d e9');
log('Found ' + prologs.length + ' prologues in first 2MB');

// Hook every 20th function prologue in the first 2MB (should be ~50 hooks)
var broadHooks = 0;
var broadCounts = {};
for (var i = 10; i < prologs.length; i += 20) {
    var addr = prologs[i].address;
    var offset = addr.sub(GAME_BASE).toInt32();
    var key = 'broad_0x' + offset.toString(16);
    broadCounts[key] = 0;
    
    try {
        (function(a, k, o) {
            Interceptor.attach(a, {
                onEnter: function(args) {
                    broadCounts[k] = (broadCounts[k] || 0) + 1;
                }
            });
            broadHooks++;
        })(addr, key, offset);
    } catch(e) {}
}
log('Broad-hooked ' + broadHooks + ' functions in first 2MB');

// ============================================================================
// Part 3: Hook Java input system to detect player actions
// ============================================================================
try {
    var MotionEvent = Java.use('android.view.MotionEvent');
    var View = Java.use('android.view.View');
    
    // Hook dispatchTouchEvent on the game's SurfaceView
    View.dispatchTouchEvent.overload('android.view.MotionEvent').implementation = function(event) {
        var action = event.getAction();
        var x = event.getX();
        var y = event.getY();
        var actionStr = 'UNKNOWN';
        switch(action & 0xFF) {
            case 0: actionStr = 'DOWN'; break;
            case 1: actionStr = 'UP'; break;
            case 2: actionStr = 'MOVE'; break;
            case 3: actionStr = 'CANCEL'; break;
            case 5: actionStr = 'POINTER_DOWN'; break;
            case 6: actionStr = 'POINTER_UP'; break;
        }
        log('[TOUCH] action=' + actionStr + ' x=' + x.toFixed(1) + ' y=' + y.toFixed(1));
        return this.dispatchTouchEvent(event);
    };
    log('Hooked View.dispatchTouchEvent');
} catch(e) {
    log('Java input hook failed (may need Java VM attach): ' + e);
    // Try attaching to Java VM
    try {
        Java.perform(function() {
            var View = Java.use('android.view.View');
            View.dispatchTouchEvent.overload('android.view.MotionEvent').implementation = function(event) {
                var action = event.getAction();
                var x = event.getX();
                var y = event.getY();
                log('[TOUCH] action=' + action + ' x=' + x.toFixed(1) + ' y=' + y.toFixed(1));
                return this.dispatchTouchEvent(event);
            };
            log('Hooked View.dispatchTouchEvent (via Java.perform)');
        });
    } catch(e2) {
        log('Java perform also failed: ' + e2);
    }
}

// ============================================================================
// Part 4: Report after 20 seconds
// ============================================================================
log('');
log('=== All hooks installed. Playing for 20 seconds... ===');
log('=== Interact with the game NOW! ===');

setTimeout(function() {
    log('');
    log('=== PROFILING REPORT (after 20s) ===');
    log('');
    
    log('--- Broad scan results (first 2MB, every 20th function): ---');
    var sorted = Object.keys(broadCounts).map(k => ({key: k, count: broadCounts[k]}));
    sorted.sort((a,b) => b.count - a.count);
    
    sorted.filter(x => x.count > 0).forEach(f => {
        var fps = (f.count / 20).toFixed(1);
        log('  ' + f.key + ': ' + f.count + ' calls (' + fps + '/s)');
    });
    
    var neverCalled = sorted.filter(x => x.count === 0);
    log('  ' + neverCalled.length + ' functions never called');
    
    log('');
    log('=== Full log saved (' + logs.length + ' lines) ===');
    
    // Save logs to device
    try {
        var logText = logs.join('\n');
        var file = new File('/sdcard/frida_sf2_logs.txt', 'w');
        file.write(logText);
        file.close();
        log('Logs saved to /sdcard/frida_sf2_logs.txt');
    } catch(e) {
        log('Could not save to device: ' + e);
    }
    
}, 20000);
