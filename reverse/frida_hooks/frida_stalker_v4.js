/**
 * SF2 Stalker Combat Trace v4 - writes output to device file
 */
var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;
function off(a) { return '0x' + a.sub(GAME_BASE).toInt32().toString(16); }

var outputFile = '/sdcard/sf2_stalker_output.txt';
var lines = [];

function save(msg) {
    lines.push(msg);
    try {
        var f = new File(outputFile, 'w');
        f.write(lines.join('\n'));
        f.close();
    } catch(e) {}
}

save('=== Stalker Combat Trace v4 ===');
save('Starting trace in 3 seconds... FIGHT NOW!');
save('');

setTimeout(function() {
    save('=== TRACING (10 seconds) ===');
    save('');
    
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
            
            save('=== TOP 80 FUNCTIONS (10 sec) ===');
            gameEntries.slice(0, 80).forEach(function(e) {
                var fps = (e.count / 10).toFixed(1);
                save(e.offset + ': ' + e.count + ' (' + fps + '/s)');
            });
            
            save('');
            save('=== MEDIUM FREQ (5-50 calls) - AI candidates ===');
            gameEntries.filter(function(e) { return e.count >= 5 && e.count < 50; }).forEach(function(e) {
                save(e.offset + ': ' + e.count);
            });
            
            save('');
            save('=== EVENT-TRIGGERED (1-4 calls) ===');
            gameEntries.filter(function(e) { return e.count >= 1 && e.count <= 4; }).slice(0, 60).forEach(function(e) {
                save(e.offset + ': ' + e.count);
            });
            
            save('');
            save('Total unique game functions: ' + gameEntries.length);
            save('');
            save('=== TRACE COMPLETE ===');
        }
    });
    
    setTimeout(function() {
        Stalker.unfollow(threadId);
        Stalker.flush();
        save('=== DONE ===');
    }, 10000);
}, 3000);

// Keep alive for 20 seconds
setTimeout(function() {
    save('Session ending');
}, 20000);
