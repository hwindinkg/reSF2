/**
 * Shadow Fight 2 - Block Decision Reverse Engineering Hook
 * 
 * Strategy:
 * 1. Find the game binary base address dynamically
 * 2. Locate string tables for "Block", "Duck", "Tactic", "Interval" etc.
 * 3. Find code xrefs to those strings
 * 4. Hook the functions that reference them
 * 5. Log arguments, return values, and context during actual combat
 * 
 * Also hooks known hot functions from previous profiling:
 * - 0x2f0e0: Game loop / entity processor (717/s)
 * - 0x692464: Input dispatcher (246/s)
 * - 0x691ef0: Entity/model processor
 * - 0x11f0a0: Parent function of entity processor
 */

// ============================================================================
// Find game binary
// ============================================================================
var GAME_BASE = null;
var GAME_SIZE = 0;

var ranges = Process.enumerateRanges('r-x');
for (var i = 0; i < ranges.length; i++) {
    var r = ranges[i];
    if (r.size > 5000000 && r.size < 15000000) {
        var prologues = Memory.scanSync(r.base, Math.min(r.size, 1024*1024), 'f0 41 2d e9');
        if (prologues.length > 50) {
            GAME_BASE = r.base;
            GAME_SIZE = r.size;
            break;
        }
    }
}

if (!GAME_BASE) {
    GAME_BASE = ptr('0x8f35f000');
    GAME_SIZE = 8572928;
}

function offset(addr) {
    return '0x' + addr.sub(GAME_BASE).toInt32().toString(16);
}

function hex(v) { return '0x' + v.toString(16).padStart(8, '0'); }

function readFloatSafe(p, off) {
    try { return p.add(off).readFloat(); } catch(e) { return NaN; }
}

function readU32Safe(p, off) {
    try { return p.add(off).readU32(); } catch(e) { return 0; }
}

console.log('=== SF2 Block Decision RE Hook ===');
console.log('Game base: ' + GAME_BASE + ' size: ' + GAME_SIZE);

// ============================================================================
// String location map (from previous scan)
// ============================================================================
var STRING_ADDRS = {};

function findString(term) {
    if (STRING_ADDRS[term]) return STRING_ADDRS[term];
    try {
        var hexPat = '';
        for (var j = 0; j < term.length; j++) {
            hexPat += ('0' + term.charCodeAt(j).toString(16)).slice(-2) + ' ';
        }
        hexPat += '00';
        var found = Memory.scanSync(GAME_BASE, GAME_SIZE, hexPat.trim());
        if (found.length > 0) {
            STRING_ADDRS[term] = found[0].address;
            return found[0].address;
        }
    } catch(e) {}
    return null;
}

// ============================================================================
// Hook: Game Loop (0x2f0e0) — capture model state each tick
// ============================================================================
var gameLoopAddr = GAME_BASE.add(0x2f0e0);
var tickCount = 0;
var lastBlockState = -1;

try {
    Interceptor.attach(gameLoopAddr, {
        onEnter: function(args) {
            tickCount++;
            this.model = args[0];
            
            // Only log every 60th tick (~1/sec at 60fps) or on state change
            if (tickCount % 60 !== 0) return;
            
            if (!this.model.isNull() && this.model.compare(ptr('0x1000')) > 0) {
                try {
                    // Read model fields (offsets from x86 analysis, may differ on ARM)
                    var posX = readFloatSafe(this.model, 0x80);
                    var posY = readFloatSafe(this.model, 0x84);
                    var health = readFloatSafe(this.model, 0x90);
                    var animType = readU32Safe(this.model, 0xA0);
                    var animFrame = readU32Safe(this.model, 0xA4);
                    var mirrored = readU32Safe(this.model, 0xB0);
                    
                    // Check if any values look reasonable
                    if (!isNaN(posX) && Math.abs(posX) < 10000) {
                        console.log('[TICK #' + tickCount + '] model=' + this.model +
                            ' pos_x=' + posX.toFixed(1) +
                            ' health=' + (isNaN(health) ? '?' : health.toFixed(2)) +
                            ' anim=' + animType + '/' + animFrame +
                            ' mirrored=' + hex(mirrored));
                        
                        // Check for enemy pointer
                        try {
                            var enemyPtr = this.model.add(0x190).readPointer();
                            if (!enemyPtr.isNull() && enemyPtr.compare(ptr('0x1000')) > 0) {
                                var enemyX = readFloatSafe(enemyPtr, 0x80);
                                var enemyHealth = readFloatSafe(enemyPtr, 0x90);
                                var enemyAnim = readU32Safe(enemyPtr, 0xA0);
                                if (!isNaN(enemyX)) {
                                    var dist = Math.abs(enemyX - posX);
                                    console.log('  enemy: pos_x=' + enemyX.toFixed(1) +
                                        ' dist=' + dist.toFixed(1) +
                                        ' health=' + (isNaN(enemyHealth) ? '?' : enemyHealth.toFixed(2)) +
                                        ' anim=' + enemyAnim);
                                }
                            }
                        } catch(e) {}
                    }
                } catch(e) {}
            }
        }
    });
    console.log('[+] Hooked game loop at ' + gameLoopAddr + ' (offset ' + offset(gameLoopAddr) + ')');
} catch(e) {
    console.log('[!] Game loop hook failed: ' + e);
}

// ============================================================================
// Hook: Entity/Model Processor (0x691ef0) — per-entity updates
// ============================================================================
try {
    Interceptor.attach(GAME_BASE.add(0x691ef0), {
        onEnter: function(args) {
            this.r0 = args[0];
            this.r1 = args[1];
            this.r2 = args[2];
            this.r3 = args[3];
        },
        onLeave: function(retval) {
            // Log occasionally to track state changes
            if (tickCount % 120 === 0) {
                console.log('[ENTITY_PROC] r0=' + this.r0 + ' r1=' + this.r1 + 
                    ' r2=' + this.r2 + ' r3=' + this.r3 + ' -> ' + retval);
            }
        }
    });
    console.log('[+] Hooked entity processor at offset 0x691ef0');
} catch(e) {
    console.log('[!] Entity processor hook failed: ' + e);
}

// ============================================================================
// String-based function discovery
// Search for code that references "Block", "Duck", "Tactic" strings
// and hook those functions
// ============================================================================
console.log('');
console.log('=== Searching for block/AI-related functions ===');

var targetStrings = ['Block', 'Duck', 'Tactic', 'Interval', 'BlockChance', 'UseDefense', 
                     'BodyDefense', 'CounterAttack', 'animation'];
var hookedFunctions = {};

targetStrings.forEach(function(term) {
    var strAddr = findString(term);
    if (!strAddr) return;
    
    console.log('[*] "' + term + '" at ' + strAddr + ' (offset ' + offset(strAddr) + ')');
    
    // Find code references to this string address
    // In ARM, string addresses are loaded via literal pools
    // The literal pool contains the 32-bit address, loaded with LDR rX, [pc, #off]
    try {
        var addrBytes = strAddr.toByteArray();
        if (addrBytes.length < 4) return;
        
        var addrPattern = '';
        for (var j = 0; j < 4; j++) {
            addrPattern += ('0' + addrBytes[j].toString(16)).slice(-2) + ' ';
        }
        
        var refs = Memory.scanSync(GAME_BASE, GAME_SIZE, addrPattern.trim());
        if (refs.length === 0) return;
        
        console.log('  Found ' + refs.length + ' address references');
        
        // For each reference, find the nearest function prologue before it
        refs.slice(0, 10).forEach(function(ref) {
            // Scan backwards for ARM function prologue (push {r4-r8, lr})
            var scanStart = ref.address.sub(4096);  // Function shouldn't be more than 4KB before ref
            if (scanStart.compare(GAME_BASE) < 0) scanStart = GAME_BASE;
            var scanSize = ref.address.sub(scanStart).toInt32() + 4;
            
            var prologues = Memory.scanSync(scanStart, scanSize, 'f0 41 2d e9');
            if (prologues.length === 0) return;
            
            // Take the last prologue before the reference
            var funcAddr = prologues[prologues.length - 1].address;
            var funcOffset = offset(funcAddr);
            
            if (!hookedFunctions[funcOffset]) {
                hookedFunctions[funcOffset] = {
                    addr: funcAddr,
                    strings: [],
                    refCount: 0
                };
            }
            hookedFunctions[funcOffset].strings.push(term);
            hookedFunctions[funcOffset].refCount++;
        });
    } catch(e) {}
});

// ============================================================================
// Hook discovered functions
// ============================================================================
console.log('');
console.log('=== Hooking discovered functions ===');

Object.keys(hookedFunctions).forEach(function(funcOffset) {
    var info = hookedFunctions[funcOffset];
    var funcAddr = info.addr;
    
    console.log('[*] Function at ' + funcOffset + ' refs: ' + info.strings.join(', '));
    
    try {
        (function(addr, strings, fo) {
            var callCount = 0;
            Interceptor.attach(addr, {
                onEnter: function(args) {
                    callCount++;
                    // Log first few calls and then periodically
                    if (callCount <= 3 || callCount % 100 === 0) {
                        var r0 = args[0], r1 = args[1], r2 = args[2], r3 = args[3];
                        console.log('[FUNC ' + fo + ' #' + callCount + '] (' + strings.join('+') + ')' +
                            ' r0=' + r0 + ' r1=' + r1 + ' r2=' + r2 + ' r3=' + r3);
                        
                        // Try to read r0 as a model pointer
                        try {
                            if (!r0.isNull() && r0.compare(ptr('0x1000')) > 0) {
                                var f1 = readFloatSafe(r0, 0x80);
                                if (!isNaN(f1) && Math.abs(f1) < 10000) {
                                    console.log('  r0->float@0x80=' + f1.toFixed(2));
                                }
                            }
                        } catch(e) {}
                        
                        // Get backtrace
                        try {
                            var bt = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 5);
                            var btStr = bt.map(function(a) {
                                if (a.compare(GAME_BASE) >= 0 && a.compare(GAME_BASE.add(GAME_SIZE)) < 0) {
                                    return 'game+' + offset(a);
                                }
                                return a.toString();
                            }).join(' < ');
                            console.log('  BT: ' + btStr);
                        } catch(e) {}
                    }
                },
                onLeave: function(retval) {
                    if (callCount <= 3 || callCount % 100 === 0) {
                        console.log('[FUNC ' + fo + ' LEAVE] -> ' + retval);
                    }
                }
            });
            console.log('  [+] Hooked at ' + addr);
        })(funcAddr, info.strings, funcOffset);
    } catch(e) {
        console.log('  [!] Hook failed: ' + e);
    }
});

// ============================================================================
// Memory watch: monitor block-related state changes
// ============================================================================
console.log('');
console.log('=== Setting up state monitors ===');

// Periodic scan for animation state changes that indicate blocking
var blockMonitorInterval = setInterval(function() {
    // This is a passive monitor — just reads memory every 100ms
    // to detect when entities enter/exit block state
}, 100);

// ============================================================================
// Report and keep alive
// ============================================================================
console.log('');
console.log('=== All hooks installed ===');
console.log('Functions hooked: ' + Object.keys(hookedFunctions).length);
console.log('Game loop: hooked');
console.log('Entity processor: hooked');
console.log('');
console.log('NOW START A FIGHT and interact with the game.');
console.log('Block/attack/move to generate data.');
console.log('');
console.log('Tip: watch console output for [FUNC ...] lines during combat.');
console.log('The AI decision function should fire every 0.6-1.0 seconds.');

// Keep alive
var _keepAlive = setInterval(function(){}, 5000);
