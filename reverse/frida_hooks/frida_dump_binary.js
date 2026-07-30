/**
 * SF2 Binary Dumper - dumps first 2MB in chunks
 */
var GAME_BASE = ptr('0x8f35f000');
var CHUNK = 0x40000;  // 256KB
var TOTAL = 8;

console.log('Starting dump of 2MB in ' + TOTAL + ' chunks...');

var done = 0;
for (var i = 0; i < TOTAL; i++) {
    (function(idx) {
        try {
            var data = GAME_BASE.add(idx * CHUNK).readByteArray(CHUNK);
            var fname = '/sdcard/sf2_chunk_' + idx + '.bin';
            var f = new File(fname, 'wb');
            f.write(data);
            f.close();
            done++;
            console.log('[' + done + '/' + TOTAL + '] Dumped ' + fname);
        } catch(e) {
            console.log('Error chunk ' + idx + ': ' + e);
        }
    })(i);
}

console.log('Dump complete: ' + done + '/' + TOTAL + ' chunks');
console.log('Pull with: adb pull /sdcard/sf2_chunk_N.bin .');
console.log('Then concat: copy /b sf2_chunk_0.bin+sf2_chunk_1.bin+... sf2_binary.bin');
