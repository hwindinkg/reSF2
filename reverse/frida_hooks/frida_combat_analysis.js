/**
 * Shadow Fight 2 - Combat Block Analysis Script
 * 
 * Dumps a chunk of the game binary for offline Ghidra analysis,
 * then hooks known functions and traces execution during active combat.
 * 
 * Key known addresses (offsets from 0x8f35f000):
 *   0x2f0e0  - Game loop / entity processor (717/s)
 *   0x692464 - Input dispatcher (246/s)
 *   0x691ef0 - Entity/model processor
 *   0x11f0a0 - Parent of entity processor
 * 
 * Data table (interval types):
 *   0x7c2240 - Pointer table: Block, Uninterrupt, Invulnerable, etc.
 *   0x7c224c - "Block" entry (offset 0x73f8e0 from base)
 * 
 * String locations:
 *   0x73f8e0 - "Block"
 *   0x740a38 - "UseDefense"
 *   0x740a64 - "BlockChance"
 *   0x740b8c - "CounterFactor"
 *   0x740b9c - "DamageFactor"
 *   0x740c24 - "HitFactor"
 *   0x740bd0 - "AnimationFramesFactor"
 *   0x741620 - "CounterAttack"
 *   0x741648 - "Uninterrupt"
 *   0x74165c - "Invulnerable"
 *   0x74236a - "IntervalAttack"
 *   0x741dae - "ConditionInterval"
 *   0x737a24 - "TACTICS"
 *   0x737eb0 - "Controlled"
 */

var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;

function off(a) { return '0x' + a.sub(GAME_BASE).toInt32().toString(16); }
function hex(v) { return '0x' + (v >>> 0).toString(16).padStart(8, '0'); }
function readF(p, o) { try { return p.add(o).readFloat(); } catch(e) { return NaN; } }
function readU(p, o) { try { return p.add(o).readU32(); } catch(e) { return 0; } }

console.log('=== SF2 Combat Block Analysis ===');
console.log('Game: ' + GAME_BASE + ' (' + GAME_SIZE + ' bytes)');
console.log('');

// ============================================================================
// Part 1: Read the interval/data table to understand what's in it
// ============================================================================
console.log('=== INTERVAL DATA TABLE (offset 0x7c2200-0x7c2300) ===');
var tableBase = GAME_BASE.add(0x7c2200);

// Read strings at the string locations we found
var stringMap = {};
var stringOffsets = {
    'Block':       0x73f8e0,
    'UseDefense':  0x740a38,
    'BlockChance': 0x740a64,
    'CounterFactor': 0x740b8c,
    'DamageFactor':  0x740b9c,
    'HitFactor':     0x740c24,
    'AnimFramesFactor': 0x740bd0,
    'CounterAttack':  0x741620,
    'Uninterrupt':    0x741648,
    'Invulnerable':   0x74165c,
    'IntervalAttack': 0x74236a,
    'ConditionInterval': 0x741dae,
    'TACTICS':        0x737a24,
    'Controlled':     0x737eb0,
    'BodyDefense':    0x743928,
    'BlockDamage':    0x74d728,
    'BlockDefense':   0x74d884,
    'BlockDamageFactor': 0x74d870,
    'Model':          0x70c860,
    'ModelAnimation': 0x741722,
    'ShadowScale':    0x70ca90,
    'attack':         0x740935,
};

// Try to read each string
Object.keys(stringOffsets).forEach(function(name) {
    var strAddr = GAME_BASE.add(stringOffsets[name]);
    try {
        var s = strAddr.readUtf8String(64);
        stringMap[name] = s;
        // Only print if it differs from the key
        if (s !== name) {
            console.log('  "' + name + '" -> "' + s + '" @ ' + off(strAddr));
        }
    } catch(e) {
        // Read raw bytes instead
        try {
            var bytes = strAddr.readByteArray(32);
            var arr = new Uint8Array(bytes);
            var chars = '';
            for (var i = 0; i < 32 && arr[i] !== 0; i++) {
                chars += String.fromCharCode(arr[i]);
            }
            console.log('  "' + name + '" -> raw: "' + chars + '" @ ' + off(strAddr));
            stringMap[name] = chars;
        } catch(e2) {
            console.log('  "' + name + '" @ ' + off(strAddr) + ' UNREADABLE');
        }
    }
});

// Read the interval table entries and resolve to string names
console.log('');
console.log('=== INTERVAL TABLE RESOLUTION ===');
var intervalTableAddr = GAME_BASE.add(0x7c2240);
for (var i = 0; i < 12; i++) {
    var entryAddr = intervalTableAddr.add(i * 4);
    try {
        var ptrVal = entryAddr.readPointer();
        var ptrOff = ptrVal.sub(GAME_BASE).toInt32();
        var ptrOffHex = '0x' + ptrOff.toString(16);
        
        // Find matching string
        var found = false;
        Object.keys(stringOffsets).forEach(function(name) {
            if (stringOffsets[name] === ptrOff) {
                console.log('  table[' + i + '] @ 0x' + (0x7c2240 + i*4).toString(16) + 
                    ' = "' + name + '" (offset ' + ptrOffHex + ')');
                found = true;
            }
        });
        if (!found) {
            console.log('  table[' + i + '] @ 0x' + (0x7c2240 + i*4).toString(16) + 
                ' = offset ' + ptrOffHex);
        }
    } catch(e) {
        console.log('  table[' + i + '] unreadable');
    }
}

// ============================================================================
// Part 2: Hook known functions with detailed logging
// ============================================================================
console.log('');
console.log('=== HOOKING FUNCTIONS ===');

// Hook 1: Game Loop (0x2f0e0)
var gameLoopCount = 0;
try {
    Interceptor.attach(GAME_BASE.add(0x2f0e0), {
        onEnter: function(args) {
            gameLoopCount++;
            this.model = args[0];
            
            // Log every 300 ticks (~5 sec at 60fps*12x)
            if (gameLoopCount % 300 === 1) {
                console.log('[GAME_LOOP #' + gameLoopCount + '] r0=' + args[0] + ' r1=' + args[1]);
            }
        }
    });
    console.log('[+] Game loop @ 0x2f0e0');
} catch(e) { console.log('[-] Game loop: ' + e); }

// Hook 2: Entity processor (0x691ef0)
var entityCount = 0;
try {
    Interceptor.attach(GAME_BASE.add(0x691ef0), {
        onEnter: function(args) {
            entityCount++;
            if (entityCount % 300 === 1) {
                console.log('[ENTITY_PROC #' + entityCount + '] r0=' + hex(args[0].toInt32()) + 
                    ' r1=' + hex(args[1].toInt32()) + ' r2=' + hex(args[2].toInt32()) + 
                    ' r3=' + hex(args[3].toInt32()));
            }
        },
        onLeave: function(retval) {
            if (entityCount % 300 === 1) {
                console.log('[ENTITY_PROC leave] -> ' + hex(retval.toInt32()));
            }
        }
    });
    console.log('[+] Entity processor @ 0x691ef0');
} catch(e) { console.log('[-] Entity proc: ' + e); }

// Hook 3: Input dispatcher (0x692464) — log screen coordinates
var inputCount = 0;
try {
    Interceptor.attach(GAME_BASE.add(0x692464), {
        onEnter: function(args) {
            inputCount++;
            var x = args[0].toInt32();
            var y = args[1].toInt32();
            // Only log when coordinates change or periodically
            if (inputCount <= 5 || inputCount % 200 === 0) {
                console.log('[INPUT #' + inputCount + '] x=' + x + ' y=' + y + 
                    ' r2=' + hex(args[2].toInt32()) + ' r3=' + hex(args[3].toInt32()));
            }
        }
    });
    console.log('[+] Input dispatcher @ 0x692464');
} catch(e) { console.log('[-] Input: ' + e); }

// Hook 4: Parent function at 0x11f0a0 (calls entity processor)
var parentCount = 0;
try {
    Interceptor.attach(GAME_BASE.add(0x11f0a0), {
        onEnter: function(args) {
            parentCount++;
            if (parentCount % 100 === 1) {
                console.log('[PARENT_11f0a0 #' + parentCount + '] r0=' + hex(args[0].toInt32()));
                
                // Try reading model state from r0
                var r0 = args[0];
                if (!r0.isNull() && r0.compare(ptr('0x1000')) > 0) {
                    try {
                        var floats = [];
                        for (var i = 0; i < 16; i++) {
                            var f = readF(r0, i * 4);
                            if (isNaN(f)) break;
                            floats.push(f.toFixed(2));
                        }
                        if (floats.length > 0) {
                            console.log('  r0 floats: [' + floats.join(', ') + ']');
                        }
                    } catch(e) {}
                }
            }
        }
    });
    console.log('[+] Parent func @ 0x11f0a0');
} catch(e) { console.log('[-] Parent: ' + e); }

// ============================================================================
// Part 3: Stalker trace — trace all calls during a short window
// ============================================================================
console.log('');
console.log('=== STALKER TRACE SETUP ===');
console.log('Will trace all execution for 3 seconds after "TRACE_START" command');
console.log('Type: rpc exports.startTrace() in Frida console');

// Export functions for interactive use
rpc.exports = {
    startTrace: function() {
        console.log('');
        console.log('=== STALKER TRACE STARTING ===');
        console.log('Tracing for 5 seconds... PLAY THE GAME NOW!');
        console.log('');
        
        var traceCalls = {};
        var traceCount = 0;
        
        // Use Stalker to trace the current thread
        var threadId = Process.getCurrentThreadId();
        
        Stalker.follow(threadId, {
            events: {
                call: true,
                ret: false,
                exec: false
            },
            onCallSummary: function(summary) {
                console.log('');
                console.log('=== STALKER CALL SUMMARY ===');
                var entries = [];
                Object.keys(summary).forEach(function(target) {
                    var count = summary[target];
                    var targetAddr = ptr(target);
                    if (targetAddr.compare(GAME_BASE) >= 0 && 
                        targetAddr.compare(GAME_BASE.add(GAME_SIZE)) < 0) {
                        var targetOff = off(targetAddr);
                        entries.push({ offset: targetOff, count: count, addr: target });
                    }
                });
                
                entries.sort(function(a, b) { return b.count - a.count; });
                
                console.log('Top 30 called game functions:');
                entries.slice(0, 30).forEach(function(e) {
                    console.log('  ' + e.offset + ': ' + e.count + ' calls');
                });
                
                console.log('');
                console.log('Total unique game functions called: ' + entries.length);
            }
        });
        
        setTimeout(function() {
            Stalker.unfollow(threadId);
            Stalker.flush();
            console.log('=== TRACE COMPLETE ===');
        }, 5000);
    },
    
    dumpBinary: function() {
        console.log('=== Dumping game binary chunk (0x0-0x100000) ===');
        try {
            var data = GAME_BASE.readByteArray(0x100000);  // First 1MB
            // Save to device
            var file = new File('/sdcard/sf2_game_dump_1mb.bin', 'wb');
            file.write(data);
            file.close();
            console.log('Saved 1MB to /sdcard/sf2_game_dump_1mb.bin');
        } catch(e) {
            console.log('Dump failed: ' + e);
        }
    },
    
    dumpDataSection: function() {
        console.log('=== Dumping data section (0x700000-0x830000) ===');
        try {
            var data = GAME_BASE.add(0x700000).readByteArray(0x130000);
            var file = new File('/sdcard/sf2_data_section.bin', 'wb');
            file.write(data);
            file.close();
            console.log('Saved 1.2MB data section to /sdcard/sf2_data_section.bin');
        } catch(e) {
            console.log('Dump failed: ' + e);
        }
    },
    
    readModel: function(addr) {
        var model = ptr(addr);
        console.log('=== Model at ' + model + ' ===');
        for (var i = 0; i < 64; i++) {
            try {
                var val = model.add(i * 4).readU32();
                var f = model.add(i * 4).readFloat();
                var off_hex = '0x' + (i * 4).toString(16).padStart(3, '0');
                if (!isNaN(f) && f !== 0 && Math.abs(f) < 100000) {
                    console.log('  ' + off_hex + ': ' + hex(val) + ' (float: ' + f.toFixed(4) + ')');
                } else {
                    console.log('  ' + off_hex + ': ' + hex(val));
                }
            } catch(e) {
                console.log('  ' + off_hex + ': UNREADABLE');
            }
        }
    }
};

console.log('');
console.log('=== All hooks installed ===');
console.log('Available commands:');
console.log('  rpc exports.startTrace()  - Trace all calls for 5 seconds');
console.log('  rpc exports.dumpBinary()  - Dump first 1MB to /sdcard/');
console.log('  rpc exports.dumpDataSection() - Dump data section to /sdcard/');
console.log('  rpc exports.readModel("0xADDR") - Dump model struct fields');
console.log('');
console.log('START A FIGHT NOW! Then type: rpc exports.startTrace()');

// Keep alive
var _keepAlive = setInterval(function(){}, 5000);
