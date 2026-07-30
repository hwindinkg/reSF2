"""
SF2 Combat Function Profiler via Python Frida API
Uses socket connection (ADB forwarded port 27042).
Hooks known functions and tracks call counts during combat.
Also scans for medium-frequency functions that could be AI decision.
"""
import frida
import time
import sys
import json

def on_message(message, data):
    if message['type'] == 'send':
        payload = message['payload']
        if isinstance(payload, dict):
            ptype = payload.get('type', '')
            if ptype == 'log':
                print(payload.get('msg', ''))
            elif ptype == 'result':
                with open(r'E:\reSF2\reverse\analysis\profiler_results.json', 'w') as f:
                    json.dump(payload['data'], f, indent=2)
                print("[SAVED] Results to profiler_results.json")
            elif ptype == 'done':
                print("[DONE]")
    elif message['type'] == 'error':
        print(f"[ERROR] {message.get('description', message)}")

JS_CODE = r"""
var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;

function off(a) {
    return '0x' + a.sub(GAME_BASE).toInt32().toString(16);
}

// Known function offsets and their names
var KNOWN = {
    '0x2f0e0':  'GAME_LOOP',
    '0x692464': 'INPUT_DISPATCHER',
    '0x691ef0': 'ENTITY_PROC',
    '0x11f0a0': 'PARENT_ENTITY',
    '0x69251c': 'INPUT_PARENT',
    '0x68f194': 'REGION_1',
    '0x690e44': 'REGION_2',
    '0x691e8c': 'REGION_3',
    '0x693480': 'REGION_7',
    '0x694f18': 'REGION_8'
};

var counts = {};
var knownCounts = {};

// Initialize counters
Object.keys(KNOWN).forEach(function(k) { knownCounts[k] = 0; });

// Hook known functions
Object.keys(KNOWN).forEach(function(off) {
    try {
        var addr = GAME_BASE.add(parseInt(off));
        var name = KNOWN[off];
        Interceptor.attach(addr, {
            onEnter: function(args) {
                knownCounts[off]++;
            }
        });
    } catch(e) {}
});

// Also scan first 2MB for ALL function prologues and hook every 5th one
// This catches AI decision functions we don't know about yet
var prologs = Memory.scanSync(GAME_BASE, Math.min(GAME_SIZE, 0x200000), 'f0 41 2d e9');
var broadCounts = {};
var broadAddrs = {};
var hooked = 0;

for (var i = 5; i < prologs.length; i += 5) {
    var addr = prologs[i].address;
    var o = off(addr);
    broadCounts[o] = 0;
    broadAddrs[o] = addr;
    
    try {
        (function(a, k) {
            Interceptor.attach(a, {
                onEnter: function(args) {
                    broadCounts[k]++;
                }
            });
            hooked++;
        })(addr, o);
    } catch(e) {}
}

send({ type: 'log', msg: 'Hooked ' + hooked + ' broad-scan functions + ' + Object.keys(KNOWN).length + ' known' });
send({ type: 'log', msg: 'PROFILING FOR 15 SECONDS — FIGHT NOW!' });

// Report after 15 seconds
setTimeout(function() {
    // Combine all results
    var allFuncs = [];
    
    // Add known functions
    Object.keys(knownCounts).forEach(function(o) {
        allFuncs.push({
            offset: o,
            count: knownCounts[o],
            name: KNOWN[o],
            known: true
        });
    });
    
    // Add broad-scan functions
    Object.keys(broadCounts).forEach(function(o) {
        if (broadCounts[o] > 0) {
            allFuncs.push({
                offset: o,
                count: broadCounts[o],
                name: null,
                known: false
            });
        }
    });
    
    // Sort by count descending
    allFuncs.sort(function(a, b) { return b.count - a.count; });
    
    // Categorize
    var highFreq = allFuncs.filter(function(f) { return f.count >= 50; });
    var medFreq = allFuncs.filter(function(f) { return f.count >= 3 && f.count < 50; });
    var lowFreq = allFuncs.filter(function(f) { return f.count >= 1 && f.count < 3; });
    
    // Log results
    send({ type: 'log', msg: '' });
    send({ type: 'log', msg: '=== HIGH FREQ (>50 calls/15s = >3.3/s) ===' });
    highFreq.forEach(function(f) {
        var fps = (f.count / 15).toFixed(1);
        var name = f.name || '(unknown)';
        send({ type: 'log', msg: '  ' + f.offset + ': ' + f.count + ' (' + fps + '/s) ' + name });
    });
    
    send({ type: 'log', msg: '' });
    send({ type: 'log', msg: '=== MEDIUM FREQ (3-50 calls) — AI candidates ===' });
    medFreq.forEach(function(f) {
        var fps = (f.count / 15).toFixed(2);
        var name = f.name || '(unknown)';
        send({ type: 'log', msg: '  ' + f.offset + ': ' + f.count + ' (' + fps + '/s) ' + name });
    });
    
    send({ type: 'log', msg: '' });
    send({ type: 'log', msg: '=== LOW FREQ (1-2 calls) — event-triggered ===' });
    lowFreq.slice(0, 30).forEach(function(f) {
        send({ type: 'log', msg: '  ' + f.offset + ': ' + f.count });
    });
    
    send({ type: 'log', msg: '' });
    send({ type: 'log', msg: 'Total hooked: ' + hooked + ', with calls: ' + allFuncs.length });
    
    // Save structured result
    send({ type: 'result', data: {
        highFreq: highFreq,
        medFreq: medFreq,
        lowFreq: lowFreq,
        totalHooked: hooked,
        totalWithCalls: allFuncs.length,
        duration: 15
    }});
    
    send({ type: 'done' });
}, 15000);

// Keep alive
setTimeout(function(){}, 20000);
"""

print("=== SF2 Combat Function Profiler ===")
print("Connecting via socket (localhost:27042)...")

try:
    device = frida.get_device('socket')
    print(f"Connected to: {device.name}")
    
    session = device.attach(7821)
    print("Attached to PID 7821 (Shadow Fight 2)")
    
    script = session.create_script(JS_CODE)
    script.on('message', on_message)
    script.load()
    print("Script loaded. Waiting 18 seconds for profiling...")
    print(">>> START A FIGHT NOW! <<<")
    print("")
    
    time.sleep(18)
    
    session.detach()
    print("\nSession complete.")
    
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
