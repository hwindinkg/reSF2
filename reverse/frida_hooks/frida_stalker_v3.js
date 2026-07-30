/**
 * SF2 Stalker Combat Trace v3
 * Uses -l flag with proper keep-alive
 */
var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;
function off(a) { return '0x' + a.sub(GAME_BASE).toInt32().toString(16); }

console.log('=== Stalker Combat Trace v3 ===');
console.log('10-second trace starting in 3 seconds...');
console.log('START FIGHTING NOW!');

setTimeout(function() {
    console.log('=== TRACING ===');
    var threadId = Process.getCurrentThreadId();
    
    Stalker.follow(threadId, {
        events: { call: true },
        onCallSummary: function(summary) {
            var gameEntries = [];
            Object.keys(summary).forEach(function(target) {
                var count = summary[target];
                var targetAddr = ptr(target);
                try {
                    if (targetAddr.compare(GAME_BASE) >= 0 && targetAddr.compare(GAME_BASE.add(GAME_SIZE)) < 0) {
                        gameEntries.push({ offset: off(targetAddr), count: count });
                    }
                } catch(e) {}
            });
            gameEntries.sort(function(a, b) { return b.count - a.count; });
            
            var lines = [];
            lines.push('=== TOP 60 FUNCTIONS (10 sec) ===');
            gameEntries.slice(0, 60).forEach(function(e) {
                var fps = (e.count / 10).toFixed(1);
                var line = e.offset + ': ' + e.count + ' (' + fps + '/s)';
                lines.push(line);
                console.log(line);
            });
            
            lines.push('');
            lines.push('=== EVENT-TRIGGERED (1-5 calls) ===');
            gameEntries.filter(function(e) { return e.count >= 1 && e.count <= 5; }).slice(0, 50).forEach(function(e) {
                var line = e.offset + ': ' + e.count;
                lines.push(line);
                console.log(line);
            });
            
            lines.push('');
            lines.push('Total unique: ' + gameEntries.length);
            console.log('Total unique: ' + gameEntries.length);
            
            try {
                var f = new File('/sdcard/sf2_stalker_result.txt', 'w');
                f.write(lines.join('\n'));
                f.close();
                console.log('Saved to /sdcard/sf2_stalker_result.txt');
            } catch(e) { console.log('Save error: ' + e); }
        }
    });
    
    setTimeout(function() {
        Stalker.unfollow(threadId);
        Stalker.flush();
        console.log('=== TRACE COMPLETE ===');
    }, 10000);
}, 3000);

// Keep session alive for 20 seconds
setTimeout(function() { console.log('Session ending...'); }, 20000);
