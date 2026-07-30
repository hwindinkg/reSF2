/**
 * SF2 Stalker Combat Trace
 * Traces all function calls for 10 seconds during active combat.
 * Identifies the AI decision function by call frequency pattern:
 *   - Game loop: ~717/s (12x 60fps)
 *   - AI decision: ~1-2/s (every 0.6-1.0 sec)
 *   - Input handler: ~246/s
 */

var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;
function off(a) { return '0x' + a.sub(GAME_BASE).toInt32().toString(16); }

console.log('=== SF2 Stalker Combat Trace ===');
console.log('START A FIGHT NOW! Trace begins in 5 seconds...');
console.log('Attack, block, move — do everything!');
console.log('');

// Count calls to known functions during trace
var knownCounts = {
    '0x2f0e0': { name: 'GAME_LOOP', count: 0 },
    '0x692464': { name: 'INPUT_DISPATCHER', count: 0 },
    '0x691ef0': { name: 'ENTITY_PROC', count: 0 },
    '0x11f0a0': { name: 'PARENT_11f', count: 0 },
};

// Hook known functions to count calls during trace
Object.keys(knownCounts).forEach(function(off) {
    try {
        var addr = GAME_BASE.add(parseInt(off));
        var info = knownCounts[off];
        Interceptor.attach(addr, {
            onEnter: function(args) {
                info.count++;
            }
        });
    } catch(e) {}
});

setTimeout(function() {
    console.log('=== TRACE STARTING NOW ===');
    console.log('FIGHT! 10 seconds...');
    console.log('');
    
    var threadId = Process.getCurrentThreadId();
    
    Stalker.follow(threadId, {
        events: { call: true, ret: false, exec: false },
        onCallSummary: function(summary) {
            console.log('');
            console.log('=== STALKER CALL SUMMARY ===');
            console.log('');
            
            // Separate game functions from non-game
            var gameEntries = [];
            var totalCount = 0;
            
            Object.keys(summary).forEach(function(target) {
                var count = summary[target];
                totalCount += count;
                var targetAddr = ptr(target);
                try {
                    if (targetAddr.compare(GAME_BASE) >= 0 && 
                        targetAddr.compare(GAME_BASE.add(GAME_SIZE)) < 0) {
                        gameEntries.push({ offset: off(targetAddr), count: count });
                    }
                } catch(e) {}
            });
            
            gameEntries.sort(function(a, b) { return b.count - a.count; });
            
            // Classify by frequency
            var highFreq = gameEntries.filter(function(e) { return e.count >= 50; });
            var medFreq = gameEntries.filter(function(e) { return e.count >= 5 && e.count < 50; });
            var lowFreq = gameEntries.filter(function(e) { return e.count >= 2 && e.count < 5; });
            var onceFuncs = gameEntries.filter(function(e) { return e.count === 1; });
            
            console.log('--- HIGH FREQUENCY (>50 calls/10s = >5/s) ---');
            highFreq.forEach(function(e) {
                var fps = (e.count / 10).toFixed(1);
                var note = '';
                Object.keys(knownCounts).forEach(function(k) {
                    if (e.offset === k) note = ' [' + knownCounts[k].name + ']';
                });
                console.log('  ' + e.offset + ': ' + e.count + ' calls (' + fps + '/s)' + note);
            });
            
            console.log('');
            console.log('--- MEDIUM FREQUENCY (5-50 calls/10s = 0.5-5/s) ---');
            console.log('*** These are likely AI decision functions! ***');
            medFreq.forEach(function(e) {
                var fps = (e.count / 10).toFixed(1);
                var note = '';
                Object.keys(knownCounts).forEach(function(k) {
                    if (e.offset === k) note = ' [' + knownCounts[k].name + ']';
                });
                console.log('  ' + e.offset + ': ' + e.count + ' calls (' + fps + '/s)' + note);
            });
            
            console.log('');
            console.log('--- LOW FREQUENCY (2-4 calls/10s) ---');
            console.log('*** These could be event-triggered (block, attack) ***');
            lowFreq.forEach(function(e) {
                console.log('  ' + e.offset + ': ' + e.count + ' calls');
            });
            
            console.log('');
            console.log('--- CALLED EXACTLY ONCE ---');
            console.log('  ' + onceFuncs.length + ' functions called once');
            onceFuncs.slice(0, 30).forEach(function(e) {
                console.log('  ' + e.offset);
            });
            
            console.log('');
            console.log('--- KNOWN FUNCTION CALL COUNTS ---');
            Object.keys(knownCounts).forEach(function(k) {
                var info = knownCounts[k];
                console.log('  ' + k + ' (' + info.name + '): ' + info.count + ' calls');
            });
            
            console.log('');
            console.log('--- STATS ---');
            console.log('Total calls: ' + totalCount);
            console.log('Unique game functions: ' + gameEntries.length);
            console.log('High-freq (>5/s): ' + highFreq.length);
            console.log('Medium-freq (0.5-5/s): ' + medFreq.length);
            console.log('Low-freq (2-4): ' + lowFreq.length);
            console.log('Once: ' + onceFuncs.length);
            
            // Save to file
            try {
                var lines = [];
                lines.push('=== Stalker Combat Trace ===');
                lines.push('Total calls: ' + totalCount);
                lines.push('Unique game functions: ' + gameEntries.length);
                lines.push('');
                lines.push('HIGH FREQ (>5/s):');
                highFreq.forEach(function(e) { lines.push('  ' + e.offset + ': ' + e.count); });
                lines.push('');
                lines.push('MEDIUM FREQ (0.5-5/s) — AI candidates:');
                medFreq.forEach(function(e) { lines.push('  ' + e.offset + ': ' + e.count); });
                lines.push('');
                lines.push('LOW FREQ (event-triggered):');
                lowFreq.forEach(function(e) { lines.push('  ' + e.offset + ': ' + e.count); });
                lines.push('');
                lines.push('ONCE:');
                onceFuncs.forEach(function(e) { lines.push('  ' + e.offset); });
                
                var text = lines.join('\n');
                var f = new File('/sdcard/sf2_stalker_combat.txt', 'w');
                f.write(text);
                f.close();
                console.log('Saved to /sdcard/sf2_stalker_combat.txt');
            } catch(e) {}
        }
    });
    
    // Stop after 10 seconds
    setTimeout(function() {
        Stalker.unfollow(threadId);
        Stalker.flush();
        console.log('');
        console.log('=== TRACE COMPLETE ===');
    }, 10000);
    
}, 5000);

var _keepAlive = setInterval(function(){}, 15000);
