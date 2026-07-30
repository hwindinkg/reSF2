/**
 * Shadow Fight 2 ARM Profiling Script
 * 
 * Finds and hooks hot functions in the game binary (loaded at 0x8f35f000).
 * Identifies:
 *   - Game loop (~60fps)
 *   - Input handling
 *   - AI decision making
 *   - Block/attack processing
 */

var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;  // ~8.5MB
var callCounts = {};
var hookCount = 0;

console.log('=== SF2 ARM Profiling ===');
console.log('Game base: ' + GAME_BASE);
console.log('Scanning for function prologues...');

// Find all ARM function prologues (push {r4-r8,lr})
var prologs = Memory.scanSync(GAME_BASE, GAME_SIZE, 'f0 41 2d e9');
console.log('Found ' + prologs.length + ' function prologues');

// Sample evenly across the binary
var SAMPLE_COUNT = 50;
var step = Math.max(1, Math.floor(prologs.length / SAMPLE_COUNT));

for (var i = 0; i < prologs.length; i += step) {
    var funcAddr = prologs[i].address;
    var offset = funcAddr.sub(GAME_BASE).toInt32();
    var key = '0x' + offset.toString(16);
    callCounts[key] = 0;
    
    try {
        (function(addr, k) {
            Interceptor.attach(addr, {
                onEnter: function(args) {
                    callCounts[k] = (callCounts[k] || 0) + 1;
                }
            });
            hookCount++;
        })(funcAddr, key);
    } catch(e) {
        // Skip problematic addresses
    }
}
console.log('Hooked ' + hookCount + ' functions');
console.log('');
console.log('=== Profiling for 15 seconds... ===');
console.log('Play the game now! Attack, block, move...');
console.log('');

// Report after 15 seconds
setTimeout(function() {
    console.log('');
    console.log('=== PROFILING RESULTS ===');
    console.log('Top called functions (>10 calls in 15s):');
    
    var sorted = Object.keys(callCounts).map(k => ({key: k, count: callCounts[k]}));
    sorted.sort((a,b) => b.count - a.count);
    
    var highFreq = sorted.filter(x => x.count >= 10);
    var medFreq = sorted.filter(x => x.count >= 2 && x.count < 10);
    var lowFreq = sorted.filter(x => x.count === 1);
    var neverCalled = sorted.filter(x => x.count === 0);
    
    console.log('\n--- HIGH FREQUENCY (>10 calls / 15s) ---');
    highFreq.forEach(f => {
        var addr = GAME_BASE.add(parseInt(f.key));
        var fps = (f.count / 15).toFixed(1);
        console.log('  ' + f.key + ' (' + addr + '): ' + f.count + ' calls (' + fps + '/s)');
    });
    
    console.log('\n--- MEDIUM FREQUENCY (2-10 calls) ---');
    medFreq.forEach(f => {
        var addr = GAME_BASE.add(parseInt(f.key));
        console.log('  ' + f.key + ' (' + addr + '): ' + f.count + ' calls');
    });
    
    console.log('\n--- LOW FREQUENCY (1 call) ---');
    console.log('  ' + lowFreq.length + ' functions called exactly once');
    
    console.log('\n--- NEVER CALLED ---');
    console.log('  ' + neverCalled.length + ' functions not called at all');
    
    console.log('\n=== INTERPRETATION ===');
    console.log('High-freq functions (~60/s) = likely game loop or rendering');
    console.log('Medium-freq functions (~1-5/s) = likely AI decisions or input processing');
    console.log('Low-freq functions = triggered by specific events (attack, block)');
    
    // Now hook the top high-freq functions with more detail
    console.log('\n=== Detailed logging of top 5 functions (5 more seconds) ===');
    
    var top5 = highFreq.slice(0, 5);
    top5.forEach(function(f) {
        var addr = GAME_BASE.add(parseInt(f.key));
        try {
            Interceptor.attach(addr, {
                onEnter: function(args) {
                    var r0 = this.context.r0;
                    var r1 = this.context.r1;
                    var r2 = this.context.r2;
                    var r3 = this.context.r3;
                    console.log('[FUNC ' + f.key + '] r0=' + r0 + ' r1=' + r1 + ' r2=' + r2 + ' r3=' + r3);
                    
                    // Try to read r0 as model pointer (float at offset 0x80)
                    try {
                        if (!r0.isNull() && r0.compare(ptr('0x1000')) > 0) {
                            var f1 = r0.add(0x80).readFloat();
                            var f2 = r0.add(0x84).readFloat();
                            if (f1 > -10000 && f1 < 10000 && !isNaN(f1)) {
                                console.log('  r0->float@0x80=' + f1.toFixed(2) + ' float@0x84=' + f2.toFixed(2));
                            }
                        }
                    } catch(e) {}
                }
            });
        } catch(e) {
            console.log('  Could not re-hook ' + f.key + ': ' + e);
        }
    });
    
    setTimeout(function() {
        console.log('\n=== DONE ===');
    }, 5000);
    
}, 15000);
