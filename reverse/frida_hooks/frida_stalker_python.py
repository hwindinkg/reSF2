"""
SF2 Stalker Combat Trace via Python Frida API
Keeps the session alive properly and captures Stalker output.
"""
import frida
import time
import sys

DEVICE_ID = "socket"
PID = 7821

JS_CODE = """
var GAME_BASE = ptr('0x8f35f000');
var GAME_SIZE = 8572928;
function off(a) { return '0x' + a.sub(GAME_BASE).toInt32().toString(16); }

send({ type: 'status', msg: 'Script loaded. Starting trace in 3 seconds... FIGHT NOW!' });

setTimeout(function() {
    send({ type: 'status', msg: 'TRACING (10 seconds)...' });
    
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
            
            send({ type: 'result', entries: gameEntries });
        }
    });
    
    setTimeout(function() {
        Stalker.unfollow(threadId);
        Stalker.flush();
        send({ type: 'status', msg: 'TRACE COMPLETE' });
    }, 10000);
}, 3000);
"""

def on_message(message, data):
    if message['type'] == 'send':
        payload = message['payload']
        if payload.get('type') == 'status':
            print(f"[STATUS] {payload['msg']}")
        elif payload.get('type') == 'result':
            entries = payload['entries']
            print(f"\n=== STALKER RESULTS ({len(entries)} unique functions) ===\n")
            
            print("--- TOP 60 FUNCTIONS ---")
            for e in entries[:60]:
                fps = e['count'] / 10.0
                print(f"  {e['offset']}: {e['count']} ({fps:.1f}/s)")
            
            print("\n--- MEDIUM FREQ (5-50 calls) - AI candidates ---")
            for e in entries:
                if 5 <= e['count'] < 50:
                    print(f"  {e['offset']}: {e['count']}")
            
            print("\n--- EVENT-TRIGGERED (1-4 calls) ---")
            count = 0
            for e in entries:
                if 1 <= e['count'] <= 4:
                    print(f"  {e['offset']}: {e['count']}")
                    count += 1
                    if count >= 50:
                        break
            
            # Save to file
            with open(r'E:\reSF2\reverse\analysis\stalker_combat_results.txt', 'w') as f:
                f.write("=== Stalker Combat Trace Results ===\n")
                f.write(f"Total unique functions: {len(entries)}\n\n")
                f.write("TOP 80:\n")
                for e in entries[:80]:
                    fps = e['count'] / 10.0
                    f.write(f"  {e['offset']}: {e['count']} ({fps:.1f}/s)\n")
                f.write("\nMEDIUM FREQ (5-50) - AI candidates:\n")
                for e in entries:
                    if 5 <= e['count'] < 50:
                        f.write(f"  {e['offset']}: {e['count']}\n")
                f.write("\nEVENT-TRIGGERED (1-4):\n")
                for e in entries:
                    if 1 <= e['count'] <= 4:
                        f.write(f"  {e['offset']}: {e['count']}\n")
            print(f"\nSaved to reverse/analysis/stalker_combat_results.txt")
    elif message['type'] == 'error':
        print(f"[ERROR] {message['description']}")

print("=== SF2 Stalker Combat Trace (Python) ===")
print(f"Connecting to device {DEVICE_ID}, PID {PID}...")

device = frida.get_device(DEVICE_ID)
session = device.attach(PID)
script = session.create_script(JS_CODE)
script.on('message', on_message)
script.load()

print("Script loaded. Waiting for trace to complete (15 seconds)...")
print("START FIGHTING NOW!")
print("")

# Keep alive for 18 seconds (3s delay + 10s trace + buffer)
time.sleep(18)

print("\nDone!")
session.detach()
