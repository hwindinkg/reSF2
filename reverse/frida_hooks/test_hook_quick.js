'use strict';
// Minimal test: just attach to a known function and count
var gameBase = ptr("0x8f197000");

// Attach to s3eDeviceYield
Interceptor.attach(ptr("0x961df974"), {
    onEnter: function() {
        console.log("YIELD");
    }
});

// ALSO attach to a few game addresses near x86 offset to see if ANYTHING fires
var testOffsets = [0x2f0e0, 0x4422, 0xDA51, 0x4FF42, 0x47181];
for (var i = 0; i < testOffsets.length; i++) {
    var addr = gameBase.add(testOffsets[i]);
    try {
        Interceptor.attach(addr, {
            onEnter: function(args) {
                console.log("HIT 0x" + testOffsets[i].toString(16) + " this=" + args[0]);
            }
        });
    } catch (e) {
        console.log("FAIL 0x" + testOffsets[i].toString(16) + ": " + e.message);
    }
}

// Spin for 2 seconds
var t = Date.now();
while (Date.now() - t < 2000) {}
console.log("DONE");
