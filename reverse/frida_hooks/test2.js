console.log("=== SCRIPT_STARTED ===");
var rs = Process.enumerateRanges('r-x');
for (var i = 0; i < rs.length; i++) {
    var r = rs[i];
    if (r.size > 1024*1024) {
        console.log(r.base + " " + (r.size/1024/1024).toFixed(1) + "MB");
    }
}
console.log("=== SCRIPT_DONE ===");
