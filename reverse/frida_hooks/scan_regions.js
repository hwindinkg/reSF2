'use strict';
// Scan all executable memory regions > 1MB
var rs = Process.enumerateRanges('r-x');
for (var i = 0; i < rs.length; i++) {
    var r = rs[i];
    var mod = "?";
    try {
        var m = Process.findModuleByAddress(r.base);
        if (m) mod = m.name;
    } catch(e) {}
    if (r.size > 1024*1024) {
        console.log("r-x  " + r.base.toString(16).padStart(10) + "  " + (r.size/1024/1024).toFixed(1) + "MB  " + mod);
    }
}
// Also check rw- and rwx regions
var rws = Process.enumerateRanges('rw-');
for (var i = 0; i < rws.length; i++) {
    var r = rws[i];
    if (r.size > 1024*1024) {
        console.log("rw-  " + r.base.toString(16).padStart(10) + "  " + (r.size/1024/1024).toFixed(1) + "MB  (data)");
    }
}
console.log("---");
console.log("Game PID: " + Process.id);
