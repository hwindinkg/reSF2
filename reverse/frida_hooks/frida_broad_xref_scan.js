/**
 * Shadow Fight 2 - Broad Xref Scanner
 * 
 * Scans entire game binary for 32-bit address references to key strings.
 * Also scans for Thumb code references (the binary might use Thumb mode).
 */

var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;

function off(addr) { return '0x' + addr.sub(GAME_BASE).toInt32().toString(16); }

console.log('=== SF2 Broad Xref Scanner ===');
console.log('');

var TARGETS = {
    'Block':           ptr('0x8fa9e8e0'),
    'UseDefense':      ptr('0x8fa9fa38'),
    'BlockChance':     ptr('0x8fa9fa64'),
    'IntervalAttack':  ptr('0x8faa136a'),
    'ConditionInterval': ptr('0x8faa0dae'),
    'CounterAttack':   ptr('0x8faa0620'),
    'TACTICS':         ptr('0x8fa96a24'),
    'Controlled':      ptr('0x8fa96eb0'),
    'BodyDefense':     ptr('0x8faa2928'),
};

// First, check if the code is Thumb mode by looking at prologues
console.log('=== Checking code mode ===');
var armPrologs = Memory.scanSync(GAME_BASE, Math.min(GAME_SIZE, 0x100000), 'f0 41 2d e9');
console.log('ARM prologues (f0 41 2d e9) in first 1MB: ' + armPrologs.length);

// Thumb prologue: push {r4-r7, lr} = b5 f0 (or variations)
var thumbPrologs = Memory.scanSync(GAME_BASE, Math.min(GAME_SIZE, 0x100000), 'f0 b5');
console.log('Thumb prologues (f0 b5) in first 1MB: ' + thumbPrologs.length);

// Try other Thumb prologues
var thumbPrologs2 = Memory.scanSync(GAME_BASE, Math.min(GAME_SIZE, 0x100000), 'f0 b5');
var thumbPrologs3 = Memory.scanSync(GAME_BASE, Math.min(GAME_SIZE, 0x100000), '30 b5');  // push {r4,r5,lr}
var thumbPrologs4 = Memory.scanSync(GAME_BASE, Math.min(GAME_SIZE, 0x100000), '08 b5');  // push {r3,lr}
console.log('Thumb prologues (30 b5) in first 1MB: ' + thumbPrologs3.length);
console.log('Thumb prologues (08 b5) in first 1MB: ' + thumbPrologs4.length);

console.log('');

// Now scan the ENTIRE binary for address references
console.log('=== Scanning entire binary for address xrefs ===');
console.log('');

var allFuncRefs = {};

Object.keys(TARGETS).forEach(function(name) {
    var strAddr = TARGETS[name];
    var addrVal = strAddr.toInt32();
    
    // Build LE pattern
    var b0 = addrVal & 0xFF;
    var b1 = (addrVal >> 8) & 0xFF;
    var b2 = (addrVal >> 16) & 0xFF;
    var b3 = (addrVal >> 24) & 0xFF;
    var pattern = ('0' + b0.toString(16)).slice(-2) + ' ' +
                  ('0' + b1.toString(16)).slice(-2) + ' ' +
                  ('0' + b2.toString(16)).slice(-2) + ' ' +
                  ('0' + b3.toString(16)).slice(-2);
    
    try {
        var matches = Memory.scanSync(GAME_BASE, GAME_SIZE, pattern);
        
        if (matches.length > 0) {
            console.log('[*] "' + name + '" @ ' + off(strAddr) + ' -> ' + matches.length + ' xrefs:');
            
            matches.forEach(function(m) {
                var refAddr = m.address;
                var refOff = off(refAddr);
                
                // Check what section this ref is in (code vs data)
                var refOffInt = refAddr.sub(GAME_BASE).toInt32();
                var section = refOffInt < 0x700000 ? 'CODE' : 'DATA';
                
                console.log('  ' + refOff + ' [' + section + ']');
                
                // Dump surrounding instructions/data
                try {
                    var context = refAddr.sub(16).readByteArray(48);
                    var bytes = new Uint8Array(context);
                    var words = [];
                    for (var k = 0; k < 12; k++) {
                        var w = (bytes[k*4+3] << 24) | (bytes[k*4+2] << 16) | (bytes[k*4+1] << 8) | bytes[k*4];
                        words.push(('00000000' + (w >>> 0).toString(16)).slice(-8));
                    }
                    console.log('    context: ' + words.join(' '));
                } catch(e) {}
                
                // Only track refs in code section
                if (refOffInt < 0x700000) {
                    // Find nearest function prologue
                    var scanStart = refAddr.sub(4096);
                    if (scanStart.compare(GAME_BASE) < 0) scanStart = GAME_BASE;
                    var scanSize = refAddr.sub(scanStart).toInt32() + 4;
                    
                    // Try ARM prologues
                    var prologues = Memory.scanSync(scanStart, scanSize, 'f0 41 2d e9');
                    if (prologues.length > 0) {
                        var funcAddr = prologues[prologues.length - 1].address;
                        var funcOff = off(funcAddr);
                        var dist = refAddr.sub(funcAddr).toInt32();
                        console.log('    ARM func: ' + funcOff + ' (dist=' + dist + ')');
                        
                        if (!allFuncRefs[funcOff]) {
                            allFuncRefs[funcOff] = { addr: funcAddr, strings: [], type: 'ARM' };
                        }
                        allFuncRefs[funcOff].strings.push(name);
                    }
                    
                    // Try Thumb prologues
                    var thumbMatches = Memory.scanSync(scanStart, scanSize, 'f0 b5');
                    if (thumbMatches.length > 0) {
                        var tFuncAddr = thumbMatches[thumbMatches.length - 1].address;
                        var tFuncOff = off(tFuncAddr);
                        var tDist = refAddr.sub(tFuncAddr).toInt32();
                        console.log('    Thumb func: ' + tFuncOff + ' (dist=' + tDist + ')');
                        
                        // Thumb addresses have bit 0 set
                        var thumbKey = tFuncOff + '_T';
                        if (!allFuncRefs[thumbKey]) {
                            allFuncRefs[thumbKey] = { addr: tFuncAddr, strings: [], type: 'Thumb' };
                        }
                        allFuncRefs[thumbKey].strings.push(name);
                    }
                }
            });
            console.log('');
        }
    } catch(e) {
        console.log('[!] Error for "' + name + '": ' + e);
    }
});

// Also look for pointer tables — addresses stored sequentially
console.log('=== Looking for pointer tables ===');
console.log('');

// A pointer table would contain multiple string addresses in sequence
// Check if any of our target addresses appear within 4-8 bytes of each other in memory
// (as part of a struct or table)

// Also scan for the string "Duck" specifically — if it exists with different casing
console.log('=== Searching for "Duck" variants ===');
var duckVariants = ['Duck', 'duck', 'DUCK', 'crouch', 'Crouch', 'guard', 'Guard', 'defend', 'Defend', 'shield', 'Shield'];
duckVariants.forEach(function(term) {
    var hex = '';
    for (var j = 0; j < term.length; j++) {
        hex += ('0' + term.charCodeAt(j).toString(16)).slice(-2) + ' ';
    }
    hex += '00';
    try {
        var found = Memory.scanSync(GAME_BASE, GAME_SIZE, hex.trim());
        if (found.length > 0) {
            console.log('  "' + term + '" found at:');
            found.forEach(function(f) { console.log('    ' + off(f.address)); });
        }
    } catch(e) {}
});

// Summary
console.log('');
console.log('=== FUNCTION SUMMARY ===');
console.log('');

Object.keys(allFuncRefs).sort().forEach(function(funcOff) {
    var info = allFuncRefs[funcOff];
    console.log('[' + info.type + ' ' + funcOff + '] strings: ' + info.strings.join(', '));
});

if (Object.keys(allFuncRefs).length === 0) {
    console.log('No function references found in code section.');
    console.log('The strings may be used via:');
    console.log('  1. String comparison functions (strcmp etc.)');
    console.log('  2. Parsed at load time into a data structure');
    console.log('  3. Referenced from a separate data/rodata segment');
    console.log('  4. The binary might be packed/encrypted');
}

console.log('');
console.log('=== DONE ===');
