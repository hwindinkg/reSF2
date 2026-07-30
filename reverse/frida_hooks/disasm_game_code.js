'use strict';

var gameBase = ptr("0x8f197000");
var gameSize = 0x830000;

// Interesting offsets from battle stack walk
var targets = [
    { off: 0x1e45ec, name: "closest to s3e yield" },
    { off: 0x1e21e0, name: "another" },
    { off: 0x1b4e58, name: "another" },
    { off: 0x0a9124, name: "another" },
    { off: 0x2056f0, name: "Shadow Fight 2 string" },
];

console.log("=== Game Code Disassembly (Thumb2) ===");

for (var ti = 0; ti < targets.length; ti++) {
    var t = targets[ti];
    var retAddr = gameBase.add(t.off);
    console.log("\n--- game+0x" + t.off.toString(16) + " (" + t.name + ") ---");
    
    // Check what type of data this is by reading the bytes
    try {
        var dataType = "unknown";
        var b1 = retAddr.readU8();
        var b2 = retAddr.add(1).readU8();
        var b3 = retAddr.add(2).readU8();
        var b4 = retAddr.add(3).readU8();
        var val = b1 | (b2 << 8) | (b3 << 16) | (b4 << 24);
        console.log("  4 bytes: " + [b1,b2,b3,b4].map(function(x){return '0x'+x.toString(16)}).join(' ') + " = " + val);
        
        // Check if ASCII string
        var isAscii = (b1 >= 0x20 && b1 <= 0x7e) || b1 == 0;
        var isAddr = val > 0x80000000 && val < 0xFFFFFFFF; // potential memory address
        if (isAscii) {
            var s = "";
            for (var ci = 0; ci < 20; ci++) {
                var c = retAddr.add(ci).readU8();
                if (c >= 0x20 && c <= 0x7e) s += String.fromCharCode(c);
                else if (c == 0) break;
                else s += "?";
            }
            console.log("  Looks like ASCII: \"" + s + "\"");
        } else if (isAddr) {
            console.log("  Looks like pointer: " + ptr(val));
        }
    } catch(e) {
        console.log("  Error reading data: " + e.message);
    }
    
    // Try to disassemble at Thumb address (with bit 0 set)
    var thumbAddr = retAddr | 1; // Set Thumb bit
    try {
        // Try Instruction.parse at the return address (with Thumb bit)
        var candidate = ptr(retAddr.toString() | 1); // Hmm, can't do bitwise on NativePointer
    } catch(e) {}
    
    // Instead, try parsing at the address directly and see what ARM mode gives us
    // Then try to find proper Thumb by scanning
    try {
        // Dump a hex range
        console.log("  Hex dump (32 bytes starting at retAddr-8):");
        var hexLine = "";
        for (var hi = -8; hi < 24; hi++) {
            if (hi % 16 === 0) {
                if (hexLine) console.log("    " + hexLine);
                hexLine = "";
            }
            try {
                var b = retAddr.add(hi).readU8();
                hexLine += (hi === 0 ? " [" : " ") + b.toString(16).padStart(2, '0');
                if (hi === 0) hexLine += "]";
            } catch(e) { hexLine += " XX"; }
        }
        if (hexLine) console.log("    " + hexLine);
    } catch(e) {}
    
    // Try to find valid Thumb2 function by looking for BL instruction
    // BL in Thumb2: 32-bit instruction with first halfword F000-F7FF
    // and second halfword F800-FFFF (or F000-FFFF for BLX)
    try {
        // Scan from retAddr-32 to retAddr+32 to find BL to our offset
        console.log("  Scanning for BL instructions (F0..F7..):");
        for (var si = -32; si < 64; si += 2) {
            var addr = retAddr.add(si);
            var w1 = addr.readU16();
            // Check if this is a Thumb2 32-bit instruction starting with F0-F7
            if ((w1 & 0xF800) === 0xF000) {
                var w2 = addr.add(2).readU16();
                // BL: first halfword = 1111 0xxx, second = 1101 xxxx or 1111 xxxx
                if ((w2 & 0xD000) === 0xD000 || (w2 & 0xE000) === 0xE000) {
                    var disp = ((w1 & 0x7FF) << 12) | ((w2 & 0xFFFF) << 1);
                    // Sign extend
                    if (disp & 0x100000) disp |= 0xFFE00000;
                    var target = addr.add(4 + disp);
                    var gameOff = target.sub(gameBase).toInt32();
                    console.log("    " + (si >= 0 ? "+" : "") + si + ": BL -> game+" + gameOff.toString(16));
                }
            }
        }
    } catch(e) {}
}

console.log("\n=== Done ===");
