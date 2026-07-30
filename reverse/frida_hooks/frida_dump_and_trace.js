/**
 * SF2 - Binary Dumper + Stalker Combat Trace
 * Dumps first 1MB of code to /sdcard/ then traces execution during combat
 */

var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;

function off(a) { return '0x' + a.sub(GAME_BASE).toInt32().toString(16); }

console.log('=== SF2 Binary Dump + Stalker Trace ===');

// Part 1: Dump binary in chunks
var CHUNK = 0x40000;  // 256KB chunks
var totalChunks = Math.ceil(0x200000 / CHUNK);  // Dump first 2MB

function dumpChunk(idx) {
    if (idx >= totalChunks) {
        console.log('=== Binary dump complete! ===');
        console.log('Files: /sdcard/sf2_chunk_*.bin');
        console.log('Pull with: adb pull /sdcard/sf2_chunk_0.bin .');
        return;
    }
    
    try {
        var start = GAME_BASE.add(idx * CHUNK);
        var data = start.readByteArray(CHUNK);
        var fname = '/sdcard/sf2_chunk_' + idx + '.bin';
        var file = new File(fname, 'wb');
        file.write(data);
        file.close();
        console.log('Dumped chunk ' + idx + '/' + totalChunks + ' (' + CHUNK + ' bytes) -> ' + fname);
    } catch(e) {
        console.log('Chunk ' + idx + ' failed: ' + e);
    }
    
    setTimeout(function() { dumpChunk(idx + 1); }, 100);
}

// Start dump
dumpChunk(0);

// Part 2: After dump, set up Stalker trace
setTimeout(function() {
    console.log('');
    console.log('=== Setting up Stalker trace ===');
    console.log('Starting trace in 3 seconds... START FIGHTING NOW!');
    
    setTimeout(function() {
        var threadId = Process.getCurrentThreadId();
        console.log('Following thread ' + threadId);
        
        Stalker.follow(threadId, {
            events: { call: true, ret: false, exec: false },
            onCallSummary: function(summary) {
                console.log('');
                console.log('=== STALKER CALL SUMMARY (5 sec window) ===');
                
                var gameEntries = [];
                Object.keys(summary).forEach(function(target) {
                    var count = summary[target];
                    var targetAddr = ptr(target);
                    try {
                        if (targetAddr.compare(GAME_BASE) >= 0 && 
                            targetAddr.compare(GAME_BASE.add(GAME_SIZE)) < 0) {
                            gameEntries.push({ 
                                offset: off(targetAddr), 
                                count: count, 
                                addr: target 
                            });
                        }
                    } catch(e) {}
                });
                
                gameEntries.sort(function(a, b) { return b.count - a.count; });
                
                console.log('');
                console.log('Top 50 called game functions:');
                gameEntries.slice(0, 50).forEach(function(e) {
                    var fps = (e.count / 5).toFixed(1);
                    console.log('  ' + e.offset + ': ' + e.count + ' calls (' + fps + '/s)');
                });
                
                console.log('');
                console.log('Functions called 1-10 times (event-triggered):');
                gameEntries.filter(function(e) { return e.count >= 1 && e.count <= 10; })
                    .slice(0, 30)
                    .forEach(function(e) {
                        console.log('  ' + e.offset + ': ' + e.count + ' calls');
                    });
                
                console.log('');
                console.log('Total unique game functions: ' + gameEntries.length);
                console.log('');
                
                // Save results
                try {
                    var lines = [];
                    lines.push('=== Stalker Trace Results ===');
                    lines.push('Duration: 5 seconds');
                    lines.push('Total unique functions: ' + gameEntries.length);
                    lines.push('');
                    lines.push('Top 50:');
                    gameEntries.slice(0, 50).forEach(function(e) {
                        lines.push('  ' + e.offset + ': ' + e.count + ' calls');
                    });
                    lines.push('');
                    lines.push('Event-triggered (1-10 calls):');
                    gameEntries.filter(function(e) { return e.count >= 1 && e.count <= 10; })
                        .forEach(function(e) {
                            lines.push('  ' + e.offset + ': ' + e.count + ' calls');
                        });
                    
                    var logText = lines.join('\n');
                    var logFile = new File('/sdcard/sf2_stalker_trace.txt', 'w');
                    logFile.write(logText);
                    logFile.close();
                    console.log('Saved to /sdcard/sf2_stalker_trace.txt');
                } catch(e) {
                    console.log('Save failed: ' + e);
                }
            }
        });
        
        // Stop after 5 seconds
        setTimeout(function() {
            Stalker.unfollow(threadId);
            Stalker.flush();
            console.log('=== TRACE COMPLETE ===');
        }, 5000);
        
    }, 3000);
    
}, totalChunks * 150 + 1000);

var _keepAlive = setInterval(function(){}, 5000);
