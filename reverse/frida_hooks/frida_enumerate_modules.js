/**
 * Shadow Fight 2 - Module Enumeration Script
 * 
 * Finds the game library and maps its memory layout.
 * The game binary is loaded at runtime from OBB via S3E engine.
 */

console.log('=== SF2 Module Enumeration ===');
console.log('PID: ' + Process.id);
console.log('Arch: ' + Process.arch);
console.log('Platform: ' + Process.platform);
console.log('PageSize: ' + Process.pageSize);
console.log('PointerSize: ' + Process.pointerSize);
console.log('');

// Enumerate all modules
var modules = Process.enumerateModules();
console.log('Total modules: ' + modules.length);
console.log('');

// Filter for game-related or large modules
console.log('=== GAME-RELATED MODULES ===');
modules.forEach(function(m) {
    var nameLower = m.name.toLowerCase();
    if (nameLower.includes('shadow') || 
        nameLower.includes('nekki') || 
        nameLower.includes('game') ||
        nameLower.includes('s3e') ||
        m.size > 5000000) {
        console.log('[+] ' + m.name);
        console.log('    Base: ' + m.base);
        console.log('    Size: ' + m.size + ' (' + (m.size / 1024 / 1024).toFixed(2) + ' MB)');
        console.log('    Path: ' + m.path);
        console.log('');
    }
});

console.log('=== ALL LARGE MODULES (>1MB) ===');
modules.forEach(function(m) {
    if (m.size > 1024 * 1024) {
        console.log('  ' + m.name + ' @ ' + m.base + ' size=' + m.size + ' path=' + m.path);
    }
});

// Find anonymous memory regions (game binary loaded from OBB is often anonymous)
console.log('');
console.log('=== LARGE ANONYMOUS MEMORY REGIONS ===');
var ranges = Process.enumerateRanges('r-x');
ranges.forEach(function(r) {
    if (r.size > 5000000) {
        console.log('  ' + r.base + ' size=' + r.size + ' (' + (r.size / 1024 / 1024).toFixed(2) + ' MB)');
    }
});

// Check specific known addresses
console.log('');
console.log('=== CHECKING KNOWN ADDRESSES ===');
var knownBases = [
    ptr('0x8f35f000'),  // Previous known base
    ptr('0x8f000000'),  // Common load address
    ptr('0x90000000'),  // Another common load address
];

knownBases.forEach(function(addr) {
    try {
        var firstBytes = addr.readByteArray(16);
        var hex = Array.from(new Uint8Array(firstBytes)).map(function(b) { 
            return ('0' + b.toString(16)).slice(-2); 
        }).join(' ');
        console.log('  ' + addr + ': ' + hex);
    } catch(e) {
        console.log('  ' + addr + ': NOT ACCESSIBLE');
    }
});

// Scan for the game binary by looking for ARM prologue pattern
console.log('');
console.log('=== SCANNING FOR GAME BINARY ===');
var scanRanges = Process.enumerateRanges('r-x');
for (var i = 0; i < scanRanges.length; i++) {
    var r = scanRanges[i];
    if (r.size > 5000000 && r.size < 15000000) {
        console.log('Scanning range: ' + r.base + ' size=' + r.size);
        // Look for ARM prologue: push {r4-r8, lr} = e9 2d 41 f0 (little-endian)
        // Actually ARM: E92D41F0 -> bytes: F0 41 2D E9 in little-endian
        var matches = Memory.scanSync(r.base, Math.min(r.size, 1024*1024), 'f0 41 2d e9');
        console.log('  Found ' + matches.length + ' ARM prologues in first 1MB');
        if (matches.length > 10) {
            console.log('  ** THIS LOOKS LIKE THE GAME BINARY **');
            console.log('  Base: ' + r.base);
            console.log('  Size: ' + r.size);
            
            // Try to read strings from this region
            var strRegion = r.base.add(r.size - 1024*1024);  // Last 1MB (where strings usually are)
            var strMatches = Memory.scanSync(strRegion, 1024*1024, '00');
            console.log('  Scanning last 1MB for strings...');
            
            // Search for specific strings
            var searchTerms = ['Block', 'Duck', 'Tactic', 'Attack', 'Model', 'Interval'];
            searchTerms.forEach(function(term) {
                try {
                    var termHex = '';
                    for (var j = 0; j < term.length; j++) {
                        termHex += ('0' + term.charCodeAt(j).toString(16)).slice(-2) + ' ';
                    }
                    termHex += '00';  // null terminator
                    var found = Memory.scanSync(r.base, r.size, termHex.trim());
                    if (found.length > 0) {
                        console.log('  Found "' + term + '" at ' + found[0].address + ' (' + found.length + ' matches)');
                    }
                } catch(e) {}
            });
        }
    }
}

console.log('');
console.log('=== DONE ===');
