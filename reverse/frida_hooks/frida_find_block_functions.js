/**
 * Shadow Fight 2 - Targeted Block Decision Function Finder
 * 
 * Strategy: Find ARM literal pool entries pointing to key strings
 * ("Block", "BlockChance", "UseDefense", "IntervalAttack"),
 * then hook the nearest functions.
 * 
 * Key string addresses (from previous scan):
 *   "Block"              @ 0x8fa9e8e0 (offset 0x73f8e0)
 *   "UseDefense"         @ 0x8fa9fa38 (offset 0x740a38)
 *   "BlockChance"        @ 0x8fa9fa64 (offset 0x740a64)
 *   "CounterFactor"      @ 0x8fa9fb8c (offset 0x740b8c)
 *   "DamageFactor"       @ 0x8fa9fb9c (offset 0x740b9c)
 *   "HitFactor"          @ 0x8fa9fc24 (offset 0x740c24)
 *   "AnimationFramesFactor" @ 0x8fa9fbd0 (offset 0x740bd0)
 *   "IntervalAttack"     @ 0x8faa136a (offset 0x74236a)
 *   "ConditionInterval"  @ 0x8faa0dae (offset 0x741dae)
 *   "CounterAttack"      @ 0x8faa0620 (offset 0x741620)
 *   "TACTICS"            @ 0x8fa96a24 (offset 0x737a24)
 *   "Controlled"         @ 0x8fa96eb0 (offset 0x737eb0)
 */

var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;

function off(addr) { return '0x' + addr.sub(GAME_BASE).toInt32().toString(16); }
function hex(v) { return '0x' + v.toString(16).padStart(8, '0'); }

console.log('=== SF2 Targeted Block Decision Finder ===');
console.log('Game: ' + GAME_BASE + ' size=' + GAME_SIZE);

// Target strings and their addresses
var TARGETS = {
    'Block':                ptr('0x8fa9e8e0'),
    'UseDefense':           ptr('0x8fa9fa38'),
    'BlockChance':          ptr('0x8fa9fa64'),
    'CounterFactor':        ptr('0x8fa9fb8c'),
    'DamageFactor':         ptr('0x8fa9fb9c'),
    'HitFactor':            ptr('0x8fa9fc24'),
    'AnimFramesFactor':     ptr('0x8fa9fbd0'),
    'IntervalAttack':       ptr('0x8faa136a'),
    'ConditionInterval':    ptr('0x8faa0dae'),
    'CounterAttack':        ptr('0x8faa0620'),
    'TACTICS':              ptr('0x8fa96a24'),
    'Controlled':           ptr('0x8fa96eb0'),
    'BodyDefense':          ptr('0x8faa2928'),
    'BlockDamage':          ptr('0x8faac728'),
    'BlockDefense':         ptr('0x8faac884'),
};

// Code section: first 0x730000 bytes (before string data starts)
var CODE_SIZE = 0x730000;

console.log('');
console.log('=== Scanning code section for literal pool entries ===');
console.log('Code section: ' + GAME_BASE + ' to ' + GAME_BASE.add(CODE_SIZE));
console.log('');

var funcRefs = {};  // function_offset -> { addr, strings, refs }

Object.keys(TARGETS).forEach(function(name) {
    var strAddr = TARGETS[name];
    var strOffset = off(strAddr);
    
    // Build the 4-byte little-endian pattern for this address
    var addrVal = strAddr.toInt32();
    var b0 = addrVal & 0xFF;
    var b1 = (addrVal >> 8) & 0xFF;
    var b2 = (addrVal >> 16) & 0xFF;
    var b3 = (addrVal >> 24) & 0xFF;
    var pattern = ('0' + b0.toString(16)).slice(-2) + ' ' +
                  ('0' + b1.toString(16)).slice(-2) + ' ' +
                  ('0' + b2.toString(16)).slice(-2) + ' ' +
                  ('0' + b3.toString(16)).slice(-2);
    
    try {
        // Search in code section for this address as a literal pool entry
        var matches = Memory.scanSync(GAME_BASE, CODE_SIZE, pattern);
        
        if (matches.length > 0) {
            console.log('[*] "' + name + '" (' + strOffset + ') referenced ' + matches.length + ' times in code:');
            
            matches.forEach(function(m) {
                var refAddr = m.address;
                var refOffset = off(refAddr);
                console.log('  literal pool entry at ' + refAddr + ' (offset ' + refOffset + ')');
                
                // Dump surrounding bytes for analysis
                try {
                    var context = refAddr.sub(16).readByteArray(48);
                    var bytes = new Uint8Array(context);
                    var instrs = [];
                    for (var k = 0; k < 12; k++) {
                        var w = (bytes[k*4+3] << 24) | (bytes[k*4+2] << 16) | (bytes[k*4+1] << 8) | bytes[k*4];
                        instrs.push(('00000000' + w.toString(16)).slice(-8));
                    }
                    console.log('    instrs: ' + instrs.join(' '));
                } catch(e) {}
                
                // Find nearest function prologue before this ref
                var scanStart = refAddr.sub(8192);
                if (scanStart.compare(GAME_BASE) < 0) scanStart = GAME_BASE;
                var scanSize = refAddr.sub(scanStart).toInt32() + 4;
                
                var prologues = Memory.scanSync(scanStart, scanSize, 'f0 41 2d e9');
                if (prologues.length > 0) {
                    // Take the closest prologue before the ref
                    var funcAddr = prologues[prologues.length - 1].address;
                    var funcOff = off(funcAddr);
                    var dist = refAddr.sub(funcAddr).toInt32();
                    
                    console.log('    -> nearest func at ' + funcOff + ' (distance: ' + dist + ' bytes)');
                    
                    if (!funcRefs[funcOff]) {
                        funcRefs[funcOff] = { addr: funcAddr, strings: [], totalRefs: 0 };
                    }
                    funcRefs[funcOff].strings.push(name);
                    funcRefs[funcOff].totalRefs++;
                }
            });
            console.log('');
        } else {
            console.log('[-] "' + name + '" not referenced in code section (maybe Thumb or indirect)');
        }
    } catch(e) {
        console.log('[!] Error scanning for "' + name + '": ' + e);
    }
});

// ============================================================================
// Summary of functions found
// ============================================================================
console.log('');
console.log('=== FUNCTIONS REFERENCING BLOCK/AI STRINGS ===');
console.log('');

var sortedFuncs = Object.keys(funcRefs).sort(function(a, b) {
    return funcRefs[b].totalRefs - funcRefs[a].totalRefs;
});

sortedFuncs.forEach(function(funcOff) {
    var info = funcRefs[funcOff];
    console.log('[FUNC ' + funcOff + '] refs=' + info.totalRefs + ' strings: ' + info.strings.join(', '));
});

// ============================================================================
// Hook the most promising functions
// ============================================================================
console.log('');
console.log('=== HOOKING TOP FUNCTIONS ===');
console.log('');

var hookTargets = sortedFuncs.slice(0, 15);  // Hook up to 15 functions

hookTargets.forEach(function(funcOff) {
    var info = funcRefs[funcOff];
    var funcAddr = info.addr;
    
    try {
        (function(addr, strings, fo) {
            var callCount = 0;
            Interceptor.attach(addr, {
                onEnter: function(args) {
                    callCount++;
                    // Log first 5 calls, then every 50th
                    if (callCount <= 5 || callCount % 50 === 0) {
                        var r0 = args[0], r1 = args[1], r2 = args[2], r3 = args[3];
                        console.log('[ENTER ' + fo + ' #' + callCount + '] (' + strings.join('+') + ')');
                        console.log('  r0=' + r0 + ' r1=' + r1 + ' r2=' + r2 + ' r3=' + r3);
                        
                        // Read r0 as potential model pointer
                        try {
                            if (!r0.isNull() && r0.compare(ptr('0x1000')) > 0) {
                                var f0x80 = NaN, f0x84 = NaN, f0x90 = NaN;
                                try { f0x80 = r0.add(0x80).readFloat(); } catch(e) {}
                                try { f0x84 = r0.add(0x84).readFloat(); } catch(e) {}
                                try { f0x90 = r0.add(0x90).readFloat(); } catch(e) {}
                                
                                if (!isNaN(f0x80) && Math.abs(f0x80) < 10000) {
                                    console.log('  r0: pos_x=' + f0x80.toFixed(2) + 
                                        ' pos_y=' + (isNaN(f0x84) ? '?' : f0x84.toFixed(2)) +
                                        ' health=' + (isNaN(f0x90) ? '?' : f0x90.toFixed(2)));
                                }
                                
                                // Try reading as float array (position, velocity, etc.)
                                try {
                                    var floats = [];
                                    for (var i = 0; i < 8; i++) {
                                        var f = r0.add(i * 4).readFloat();
                                        if (isNaN(f) || Math.abs(f) > 100000) break;
                                        floats.push(f.toFixed(3));
                                    }
                                    if (floats.length > 2) {
                                        console.log('  r0 floats: [' + floats.join(', ') + ']');
                                    }
                                } catch(e) {}
                            }
                        } catch(e) {}
                        
                        // Backtrace
                        try {
                            var bt = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 6);
                            console.log('  BT: ' + bt.map(function(a) {
                                if (a.compare(GAME_BASE) >= 0 && a.compare(GAME_BASE.add(GAME_SIZE)) < 0) {
                                    return off(a);
                                }
                                return a.toString();
                            }).join(' < '));
                        } catch(e) {}
                    }
                },
                onLeave: function(retval) {
                    if (callCount <= 5 || callCount % 50 === 0) {
                        var rv = retval.toInt32();
                        var rvFloat = NaN;
                        try { rvFloat = retval.readFloat(); } catch(e) {}
                        
                        console.log('[LEAVE ' + fo + '] -> int=' + rv + 
                            (isNaN(rvFloat) ? '' : ' float=' + rvFloat.toFixed(4)) +
                            ' raw=' + retval);
                    }
                }
            });
            console.log('[+] Hooked ' + fo + ' (strings: ' + strings.join(', ') + ')');
        })(funcAddr, info.strings, funcOff);
    } catch(e) {
        console.log('[!] Failed to hook ' + funcOff + ': ' + e);
    }
});

// ============================================================================
// Also hook the known game loop
// ============================================================================
try {
    var gameLoop = GAME_BASE.add(0x2f0e0);
    var loopCount = 0;
    Interceptor.attach(gameLoop, {
        onEnter: function(args) {
            loopCount++;
            if (loopCount % 600 === 0) {  // Every ~1 second at 60fps*12
                console.log('[GAME_LOOP] tick #' + loopCount + ' r0=' + args[0]);
            }
        }
    });
    console.log('[+] Hooked game loop at 0x2f0e0');
} catch(e) {
    console.log('[!] Game loop hook failed: ' + e);
}

console.log('');
console.log('=== All hooks installed (' + hookTargets.length + ' functions + game loop) ===');
console.log('NOW START A FIGHT! Attack, block, and move to generate data.');
console.log('');

// Keep alive
var _keepAlive = setInterval(function(){}, 5000);
