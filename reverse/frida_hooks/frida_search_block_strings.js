/**
 * Shadow Fight 2 - Block/AI String Scanner
 * 
 * Scans the game binary for strings related to:
 * - Block/defense mechanics
 * - AI decision making (tactics)
 * - Animation names (Duck, Block, etc.)
 * - Intervals system
 * 
 * Cross-references found strings to locate function offsets.
 */

// Find the game binary
var GAME_BASE = null;
var GAME_SIZE = 0;

// First, find the game binary by scanning for known base
var ranges = Process.enumerateRanges('r-x');
for (var i = 0; i < ranges.length; i++) {
    var r = ranges[i];
    if (r.size > 5000000 && r.size < 15000000) {
        // Check if it has ARM prologues
        var prologues = Memory.scanSync(r.base, Math.min(r.size, 1024*1024), 'f0 41 2d e9');
        if (prologues.length > 50) {
            GAME_BASE = r.base;
            GAME_SIZE = r.size;
            break;
        }
    }
}

if (!GAME_BASE) {
    console.log('[!] Could not find game binary!');
    console.log('[!] Trying known address 0x8f35f000...');
    GAME_BASE = ptr('0x8f35f000');
    GAME_SIZE = 8572928;
}

console.log('=== SF2 Block/AI String Scanner ===');
console.log('Game base: ' + GAME_BASE);
console.log('Game size: ' + GAME_SIZE);
console.log('');

// String search patterns related to block/AI logic
var searchPatterns = [
    // Block-related
    { term: 'Block', category: 'BLOCK' },
    { term: 'block', category: 'BLOCK' },
    { term: 'BodyDefense', category: 'BLOCK' },
    { term: 'BlockDamage', category: 'BLOCK' },
    { term: 'BlockDefense', category: 'BLOCK' },
    { term: 'BlockDamageFactor', category: 'BLOCK' },
    
    // Defense-related
    { term: 'UseDefense', category: 'DEFENSE' },
    { term: 'BlockChance', category: 'DEFENSE' },
    { term: 'CounterAttack', category: 'DEFENSE' },
    { term: 'CounterFactor', category: 'DEFENSE' },
    { term: 'DodgeChance', category: 'DEFENSE' },
    { term: 'DamageFactor', category: 'DEFENSE' },
    { term: 'HitFactor', category: 'DEFENSE' },
    { term: 'AnimationFramesFactor', category: 'DEFENSE' },
    
    // Animation names
    { term: 'Duck', category: 'ANIMATION' },
    { term: 'duck', category: 'ANIMATION' },
    { term: 'IdleStance', category: 'ANIMATION' },
    { term: 'Controlled', category: 'ANIMATION' },
    { term: 'ForwardStep', category: 'ANIMATION' },
    { term: 'BackStep', category: 'ANIMATION' },
    { term: 'ShortAttack', category: 'ANIMATION' },
    
    // Tactic/AI system
    { term: 'TACTICS', category: 'AI' },
    { term: 'Tactic', category: 'AI' },
    { term: 'tactic', category: 'AI' },
    { term: 'TacticWeight', category: 'AI' },
    { term: 'TacticContext', category: 'AI' },
    { term: 'tacticSettings', category: 'AI' },
    { term: 'ComputerSettings', category: 'AI' },
    { term: 'ShiftTables', category: 'AI' },
    { term: 'MovementsTables', category: 'AI' },
    { term: 'AttackTables', category: 'AI' },
    { term: 'OutcomeTables', category: 'AI' },
    { term: 'TablesReduction', category: 'AI' },
    
    // Interval system
    { term: 'Interval', category: 'INTERVAL' },
    { term: 'interval', category: 'INTERVAL' },
    { term: 'RemoveInterval', category: 'INTERVAL' },
    { term: 'Uninterrupt', category: 'INTERVAL' },
    { term: 'SemiUninterrupt', category: 'INTERVAL' },
    { term: 'Invulnerable', category: 'INTERVAL' },
    { term: 'Throwable', category: 'INTERVAL' },
    { term: 'IntervalAttack', category: 'INTERVAL' },
    
    // Model system
    { term: 'Model', category: 'MODEL' },
    { term: 'Model::step', category: 'MODEL' },
    { term: 'ModelAnimation', category: 'MODEL' },
    { term: 'startAction', category: 'MODEL' },
    { term: 'setNearestEnemy', category: 'MODEL' },
    { term: 'nearestEnemy', category: 'MODEL' },
    
    // Condition system
    { term: 'Condition', category: 'CONDITION' },
    { term: 'ConditionInterval', category: 'CONDITION' },
    { term: 'ConditionAnimation', category: 'CONDITION' },
    
    // Other game terms
    { term: 'ShadowScale', category: 'ENGINE' },
    { term: 'attack', category: 'COMBAT' },
    { term: 'damage', category: 'COMBAT' },
    { term: 'health', category: 'COMBAT' },
];

var results = {};

searchPatterns.forEach(function(p) {
    try {
        // Convert string to hex pattern
        var hex = '';
        for (var j = 0; j < p.term.length; j++) {
            hex += ('0' + p.term.charCodeAt(j).toString(16)).slice(-2) + ' ';
        }
        hex += '00';  // null terminator
        
        var found = Memory.scanSync(GAME_BASE, GAME_SIZE, hex.trim());
        if (found.length > 0) {
            if (!results[p.category]) results[p.category] = [];
            found.forEach(function(f) {
                results[p.category].push({
                    term: p.term,
                    address: f.address,
                    offset: '0x' + f.address.sub(GAME_BASE).toInt32().toString(16)
                });
            });
        }
    } catch(e) {
        // Skip errors (unreadable pages etc.)
    }
});

// Print results grouped by category
console.log('=== STRING SCAN RESULTS ===');
console.log('');

Object.keys(results).sort().forEach(function(cat) {
    console.log('--- ' + cat + ' ---');
    var seen = {};
    results[cat].forEach(function(r) {
        var key = r.term + '@' + r.offset;
        if (!seen[key]) {
            console.log('  "' + r.term + '" at ' + r.address + ' (offset ' + r.offset + ')');
            seen[key] = true;
        }
    });
    console.log('');
});

// Now find xrefs to key strings
console.log('=== KEY STRING XREF ANALYSIS ===');
console.log('');

// We want to find code that references these strings
// In ARM, string refs are typically: ldr rX, [pc, #offset]
// where the PC-relative load points to the string address

var keyStrings = ['Block', 'Duck', 'Tactic', 'Interval', 'BlockChance', 'UseDefense'];
keyStrings.forEach(function(term) {
    try {
        var hex = '';
        for (var j = 0; j < term.length; j++) {
            hex += ('0' + term.charCodeAt(j).toString(16)).slice(-2) + ' ';
        }
        hex += '00';
        var found = Memory.scanSync(GAME_BASE, GAME_SIZE, hex.trim());
        if (found.length > 0) {
            var strAddr = found[0].address;
            console.log('[*] "' + term + '" at ' + strAddr);
            
            // Scan for ARM LDR instructions that might reference this address
            // ARM LDR (literal): loads from PC-relative address
            // The literal pool typically contains the address of the string
            // We look for the address in the binary (as a 32-bit word)
            var addrBytes = strAddr.toByteArray();
            if (addrBytes.length >= 4) {
                var addrPattern = '';
                for (var j = 0; j < 4; j++) {
                    addrPattern += ('0' + addrBytes[j].toString(16)).slice(-2) + ' ';
                }
                try {
                    var refs = Memory.scanSync(GAME_BASE, GAME_SIZE, addrPattern.trim());
                    if (refs.length > 0) {
                        console.log('  Address referenced ' + refs.length + ' times:');
                        refs.slice(0, 5).forEach(function(ref) {
                            var refOffset = '0x' + ref.address.sub(GAME_BASE).toInt32().toString(16);
                            console.log('    ref at ' + ref.address + ' (offset ' + refOffset + ')');
                            
                            // Read nearby instructions
                            try {
                                // ARM instructions are 4 bytes, read 5 before and after
                                var start = ref.address.sub(20);
                                var codeBytes = start.readByteArray(44);
                                var bytes = new Uint8Array(codeBytes);
                                var instrs = [];
                                for (var k = 0; k < 11; k++) {
                                    var instrHex = '';
                                    for (var b = 3; b >= 0; b--) {
                                        instrHex += ('0' + bytes[k*4 + b].toString(16)).slice(-2);
                                    }
                                    instrs.push(instrHex);
                                }
                                console.log('      context: ' + instrs.join(' '));
                            } catch(e) {}
                        });
                    }
                } catch(e) {}
            }
            console.log('');
        }
    } catch(e) {}
});

console.log('=== DONE ===');
