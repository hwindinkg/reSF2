/**
 * Shadow Fight 2 - Data Table Analysis
 * 
 * Dumps the pointer table at offset 0x7c2240 that contains
 * references to "Block" and other interval/defense strings.
 * Then finds code that references THIS table.
 */

var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;

function off(addr) { return '0x' + addr.sub(GAME_BASE).toInt32().toString(16); }
function offInt(val) { return '0x' + val.toString(16); }

console.log('=== SF2 Data Table Analysis ===');
console.log('');

// Dump the data table region at offset 0x7c2000-0x7c3000
var tableBase = GAME_BASE.add(0x7c2000);
var tableSize = 0x1000;  // 4KB

console.log('=== Pointer Table at offset 0x7c2000 ===');
console.log('');

// Read as 32-bit words and resolve each as a string pointer
for (var row = 0; row < 64; row++) {
    var rowAddr = tableBase.add(row * 16);
    var line = offInt(0x7c2000 + row * 16) + ': ';
    var strs = [];
    
    for (var col = 0; col < 4; col++) {
        var wordAddr = rowAddr.add(col * 4);
        try {
            var ptrVal = wordAddr.readPointer();
            var ptrInt = ptrVal.toInt32();
            
            // Check if this pointer points to a readable string in the binary
            if (ptrInt > GAME_BASE.toInt32() && ptrInt < GAME_BASE.toInt32() + GAME_SIZE) {
                try {
                    var str = ptrVal.readUtf8String(64);
                    if (str && str.length > 0 && str.length < 64) {
                        strs.push('"' + str + '"');
                    } else {
                        strs.push(off(ptrVal));
                    }
                } catch(e) {
                    strs.push(off(ptrVal));
                }
            } else {
                strs.push(off(ptrVal));
            }
        } catch(e) {
            strs.push('????????');
        }
    }
    console.log(line + strs.join(' | '));
}

console.log('');
console.log('=== Looking for what string pointers map to ===');
console.log('');

// Specifically resolve the strings near "Block" in the pointer table
var nearbyAddrs = [
    ptr('0x8fa9f930'), ptr('0x8fa9f93c'), ptr('0x8fa9f948'), 
    ptr('0x8fa9f580'), ptr('0x8fa9e8e0'), ptr('0x8fa89d70'),
    ptr('0x8faa0620'), ptr('0x8faa0630'), ptr('0x8faa0638'),
    ptr('0x8faa0644'), ptr('0x8faa0648'), ptr('0x8faa0654'),
    ptr('0x8faa065c'), ptr('0x8faa066c'),
    ptr('0x8faa0ff4'), ptr('0x8faa1000'), ptr('0x8faa100c'),
    ptr('0x8faa1018'), ptr('0x8faa107c'),
    ptr('0x8fa9fa38'), ptr('0x8fa9fa58'), ptr('0x8fa9fa64'),
    ptr('0x8fa9fb8c'), ptr('0x8fa9fb9c'), ptr('0x8fa9fc24'),
    ptr('0x8fa9fbd0'),
];

nearbyAddrs.forEach(function(addr) {
    try {
        var str = addr.readUtf8String(128);
        console.log('  ' + off(addr) + ' = "' + str + '"');
    } catch(e) {
        console.log('  ' + off(addr) + ' = (unreadable)');
    }
});

console.log('');
console.log('=== Now find CODE that references this data table ===');
console.log('');

// The data table itself is at offset 0x7c2240 (absolute: 0x8fb21240)
// Find what code references addresses in this table
var tableAddr = GAME_BASE.add(0x7c2240);
var tableAddrVal = tableAddr.toInt32();

// Build search patterns for the table address (the code might reference 
// the table itself, or specific entries within it)

// Search for references to the table start area (0x7c2xxx offsets)
// The code will likely use the table base address + offset

// Let's try to find the table base address in the code
var b0 = tableAddrVal & 0xFF;
var b1 = (tableAddrVal >> 8) & 0xFF;
var b2 = (tableAddrVal >> 16) & 0xFF;
var b3 = (tableAddrVal >> 24) & 0xFF;

// Try exact address
var pattern = ('0' + b0.toString(16)).slice(-2) + ' ' +
              ('0' + b1.toString(16)).slice(-2) + ' ' +
              ('0' + b2.toString(16)).slice(-2) + ' ' +
              ('0' + b3.toString(16)).slice(-2);

console.log('Searching for table base address pattern: ' + pattern);
try {
    var matches = Memory.scanSync(GAME_BASE, GAME_SIZE, pattern);
    console.log('Found ' + matches.length + ' matches for table base address');
    matches.forEach(function(m) {
        var refOff = off(m.address);
        var refOffInt = m.address.sub(GAME_BASE).toInt32();
        var section = refOffInt < 0x700000 ? 'CODE' : 'DATA';
        console.log('  ' + refOff + ' [' + section + ']');
        
        // Show context
        try {
            var ctx = m.address.sub(8).readByteArray(32);
            var bytes = new Uint8Array(ctx);
            var words = [];
            for (var k = 0; k < 8; k++) {
                var w = (bytes[k*4+3] << 24) | (bytes[k*4+2] << 16) | (bytes[k*4+1] << 8) | bytes[k*4];
                words.push(('00000000' + (w >>> 0).toString(16)).slice(-8));
            }
            console.log('    ' + words.join(' '));
        } catch(e) {}
    });
} catch(e) {
    console.log('Error: ' + e);
}

// Also try nearby addresses (table entries)
console.log('');
console.log('Searching for references to specific table entries...');
var entryOffsets = [0x7c2240, 0x7c2244, 0x7c2248, 0x7c224c, 0x7c2250, 0x7c2254, 0x7c2258, 0x7c225c, 0x7c2260, 0x7c2264, 0x7c2268, 0x7c226c, 0x7c2270, 0x7c2274, 0x7c2278, 0x7c227c];

entryOffsets.forEach(function(tblOff) {
    var entryAddr = GAME_BASE.add(tblOff);
    var ev = entryAddr.toInt32();
    var p0 = ev & 0xFF;
    var p1 = (ev >> 8) & 0xFF;
    var p2 = (ev >> 16) & 0xFF;
    var p3 = (ev >> 24) & 0xFF;
    var pat = ('0' + p0.toString(16)).slice(-2) + ' ' +
              ('0' + p1.toString(16)).slice(-2) + ' ' +
              ('0' + p2.toString(16)).slice(-2) + ' ' +
              ('0' + p3.toString(16)).slice(-2);
    
    try {
        var m = Memory.scanSync(GAME_BASE, 0x700000, pat);  // Only code section
        if (m.length > 0) {
            console.log('  Table entry @ ' + offInt(tblOff) + ': ' + m.length + ' code xrefs');
            m.forEach(function(match) {
                var mOff = offInt(match.address.sub(GAME_BASE).toInt32());
                console.log('    code ref at ' + mOff);
            });
        }
    } catch(e) {}
});

console.log('');
console.log('=== DONE ===');
