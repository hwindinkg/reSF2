'use strict';
// Dump memory regions to file
var out = "";
out += "PID: " + Process.id + "\n";
out += "Arch: " + Process.arch + "\n";
out += "Platform: " + Process.platform + "\n";
out += "PageSize: " + Process.pageSize + "\n\n";

// Large executable regions
out += "=== r-x (>1MB) ===\n";
var rs = Process.enumerateRanges('r-x');
for (var i = 0; i < rs.length; i++) {
    var r = rs[i];
    if (r.size > 1024*1024) {
        var mod = "?";
        try { var m = Process.findModuleByAddress(r.base); if(m) mod = m.name; } catch(e) {}
        out += r.base + "  " + (r.size/1024/1024).toFixed(1) + "MB  " + mod + "\n";
    }
}

// Data regions
out += "\n=== rw- (>1MB) ===\n";
var rws = Process.enumerateRanges('rw-');
for (var i = 0; i < rws.length; i++) {
    var r = rws[i];
    if (r.size > 1024*1024) {
        out += r.base + "  " + (r.size/1024/1024).toFixed(1) + "MB\n";
    }
}

// rwX regions
out += "\n=== rwx (>64KB) ===\n";
var rwx = Process.enumerateRanges('rwx');
for (var i = 0; i < rwx.length; i++) {
    var r = rwx[i];
    if (r.size > 64*1024) {
        var mod = "?";
        try { var m = Process.findModuleByAddress(r.base); if(m) mod = m.name; } catch(e) {}
        out += r.base + "  " + (r.size/1024/1024).toFixed(1) + "MB  " + mod + "\n";
    }
}

try {
    var f = new File("/data/local/tmp/memory_map.txt", "w");
    f.write(out);
    f.flush();
    f.close();
    console.log("[OK] Written to /data/local/tmp/memory_map.txt");
} catch(e) {
    console.log("[FAIL] " + e);
}
