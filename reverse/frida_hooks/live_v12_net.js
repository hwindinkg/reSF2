/*
 * SF2 live interaction trace v8 вЂ” log-only instrumentation (no game-state mutation).
 * Extends live_boot_trace.js with:
 *  - s3ePointer* hooks: prove whether the logic loop READS input (touch counter,
 *    X/Y read on return) вЂ” the dojo-interaction mission hinges on this.
 *  - s3eSoundChannel* Stop/Pause/Resume + s3eSoundStopAllChannels (SFX lifecycle).
 *  - 1 Hz heartbeat: pointer-update counter vs touch-read counter (logic-loop alive?).
 *  - log-only hook at base+0x61dc (known SIGSEGV site) to name the caller.
 * Deferred attach at +3.5s (device quirk: pre-resume hooks never fire).
 */
'use strict';

var t0 = Date.now();
function ts() { return ((Date.now() - t0) / 1000).toFixed(2); }
function log(tag, m) { send({ tag: tag, t: ts(), m: m }); }
function readCStr(p) {
    try { return p.readCString(256); } catch (e) { return null; }
}

var hooksDone = false;
var pup = 0;          // s3ePointerUpdate calls (game loop heartbeat)
var touch = 0;        // s3ePointerGetTouch*/GetX/GetY/GetState calls (input actually read)
var soundPlays = 0;

function attachAll() {
    if (hooksDone) return;
    hooksDone = true;
    log('HOOK', 'attach phase begin');

    /* ---------- libc physical opens ---------- */
    ['open', 'openat'].forEach(function (fn) {
        try {
            var a = Module.findExportByName('libc.so', fn);
            Interceptor.attach(a, {
                onEnter: function (args) {
                    var p = readCStr(args[0]);
                    if (p && p.indexOf('/') !== -1) log('FILE', p);
                }
            });
            log('HOOK', 'hooked libc.so!' + fn + ' @ ' + a);
        } catch (e) { log('HOOK-ERR', fn + ': ' + e); }
    });
    ['fopen', 'fopen64'].forEach(function (fn) {
        try {
            var a = Module.findExportByName('libc.so', fn);
            Interceptor.attach(a, {
                onEnter: function (args) {
                    var p = readCStr(args[0]);
                    if (p && p.indexOf('/') !== -1) log('FILE', p);
                }
            });
            log('HOOK', 'hooked libc.so!' + fn + ' @ ' + a);
        } catch (e) { log('HOOK-ERR', fn + ': ' + e); }
    });

    /* ---------- S3E VFS / video / audio / sound ---------- */
    function hookS3e(name, cb) {
        try {
            var a = Module.findExportByName('libs3e_android.so', name);
            if (!a) { log('S3E', 'no export ' + name); return; }
            Interceptor.attach(a, { onEnter: cb });
            log('HOOK', 'hooked libs3e_android.so!' + name + ' @ ' + a);
        } catch (e) { log('S3E-ERR', name + ': ' + e); }
    }
    hookS3e('s3eFileOpen', function (args) {
        var p = readCStr(args[0]);
        if (p) log('VFS', p + ' access=' + args[1].toInt32());
    });
    hookS3e('s3eVideoPlay', function (args) {
        var p = readCStr(args[0]);
        log('VIDEO', 'PLAY ' + (p ? p : '(null)'));
    });
    hookS3e('s3eVideoIsPlaying', function () { log('VIDEO', 'IsPlaying?'); });
    hookS3e('s3eVideoStop', function () { log('VIDEO', 'STOP'); });
    hookS3e('s3eAudioPlay', function (args) {
        var p = readCStr(args[0]);
        log('AUDIO', 'PLAY ' + (p ? p : '(null)'));
    });
    hookS3e('s3eAudioPlayFromBuffer', function (args) {
        log('AUDIO', 'PLAY-FROM-BUFFER len=' + args[1].toInt32());
    });
    hookS3e('s3eAudioStop', function () { log('AUDIO', 'STOP'); });
    hookS3e('s3eSoundChannelPlay', function (args) {
        soundPlays++;
        log('SOUND', 'PLAY ch=' + args[0].toInt32() + ' snd=' + args[1].toInt32() + ' vol=' + args[2].toInt32());
    });
    hookS3e('s3eSoundChannelStop', function (args) {
        log('SOUND', 'STOP ch=' + args[0].toInt32());
    });
    hookS3e('s3eSoundChannelPause', function (args) {
        log('SOUND', 'PAUSE ch=' + args[0].toInt32());
    });
    hookS3e('s3eSoundChannelResume', function (args) {
        log('SOUND', 'RESUME ch=' + args[0].toInt32());
    });
    hookS3e('s3eSoundStopAllChannels', function () { log('SOUND', 'STOP-ALL'); });
    hookS3e('s3eSoundPauseAllChannels', function () { log('SOUND', 'PAUSE-ALL'); });
    hookS3e('s3eSoundResumeAllChannels', function () { log('SOUND', 'RESUME-ALL'); });
    hookS3e('s3eSoundGetFreeChannel', function () { log('SOUND', 'GET-FREE-CH'); });
    hookS3e('s3eDebugPrint', function (args) {
        var p = readCStr(args[0]);
        if (p && /[ -~]{3,}/.test(p)) log('TRACE', p.slice(0, 200));
    });

    /* ---------- Pointer: is input READ? (getters vs callback path) ---------- */
    hookS3e('s3ePointerRegister', function (args) {
        // s3ePointerRegister(callback, eventType, userData) вЂ” if the game
        // registers a touch callback, input flows THERE, not via getters.
        log('PREG', 'callback=' + args[0] + ' eventType=' + args[1].toInt32());
    });
    hookS3e('s3ePointerUnRegister', function (args) {
        log('PREG', 'unregister eventType=' + args[0].toInt32());
    });
    ['s3ePointerUpdate'].forEach(function (fn) {
        try {
            var a = Module.findExportByName('libs3e_android.so', fn);
            Interceptor.attach(a, { onEnter: function () { pup++; } });
            log('HOOK', 'hooked libs3e_android.so!' + fn + ' (counter) @ ' + a);
        } catch (e) { log('S3E-ERR', fn + ': ' + e); }
    });
    ['s3ePointerGetTouchState', 's3ePointerGetTouchX', 's3ePointerGetTouchY',
     's3ePointerGetState', 's3ePointerGetX', 's3ePointerGetY'].forEach(function (fn) {
        try {
            var a = Module.findExportByName('libs3e_android.so', fn);
            if (!a) { log('S3E', 'no export ' + fn); return; }
            Interceptor.attach(a, {
                onEnter: function (args) {
                    touch++;
                    log('PTOUCH', fn + '(' + args[0].toInt32() + ')');
                },
                onLeave: function (ret) {
                    if (fn.indexOf('X') !== -1 || fn.indexOf('Y') !== -1) {
                        log('PTOUCH', fn + ' -> ' + ret.toInt32());
                    }
                }
            });
            log('HOOK', 'hooked libs3e_android.so!' + fn + ' @ ' + a);
        } catch (e) { log('S3E-ERR', fn + ': ' + e); }
    });

    /* ---------- Network: name the stalled endpoint (log-only) ---------- */
    ['connect'].forEach(function (fn) {
        try {
            var a = Module.findExportByName('libc.so', fn);
            Interceptor.attach(a, {
                onEnter: function (args) {
                    // sockaddr: family(2) port(2) addr(4)
                    var sa = args[1];
                    if (sa.isNull()) return;
                    var fam = sa.readU16();
                    if (fam === 2) { // AF_INET
                        var port = (sa.add(2).readU16() & 0xffff);
                        var ip = sa.add(4).readU8() + '.' + sa.add(5).readU8() + '.' +
                                 sa.add(6).readU8() + '.' + sa.add(7).readU8();
                        log('NET', 'connect -> ' + ip + ':' + port + ' (fd=' + args[0].toInt32() + ')');
                    } else if (fam === 10) { // AF_INET6
                        log('NET', 'connect -> IPv6 fd=' + args[0].toInt32());
                    }
                }
            });
            log('HOOK', 'hooked libc.so!' + fn + ' @ ' + a);
        } catch (e) { log('HOOK-ERR', fn + ': ' + e); }
    });
    ['getaddrinfo'].forEach(function (fn) {
        try {
            var a = Module.findExportByName('libc.so', fn);
            Interceptor.attach(a, {
                onEnter: function (args) {
                    var n = readCStr(args[0]);
                    if (n) log('NET', 'getaddrinfo ' + n);
                }
            });
            log('HOOK', 'hooked libc.so!' + fn + ' @ ' + a);
        } catch (e) { log('HOOK-ERR', fn + ': ' + e); }
    });
    ['recv', 'recvfrom'].forEach(function (fn) {
        try {
            var a = Module.findExportByName('libc.so', fn);
            Interceptor.attach(a, {
                onEnter: function (args) { log('NET', fn + ' fd=' + args[0].toInt32()); }
            });
            log('HOOK', 'hooked libc.so!' + fn + ' @ ' + a);
        } catch (e) { log('HOOK-ERR', fn + ': ' + e); }
    });

    /* ---------- Crash-site (base+0x61dc, SIGSEGV pc from tombstones) ---------- */
    try {
        var target = ptr('0x8ef041dc');
        var mods = Process.enumerateModules();
        for (var i = 0; i < mods.length; i++) {
            var b = mods[i].base;
            if (target.compare(b) >= 0 && target.compare(b.add(mods[i].size)) < 0) {
                log('CRASH', 'base+0x61dc = ' + target + ' in ' + mods[i].name +
                    ' base=' + b + ' (module base)');
                Interceptor.attach(target, {
                    onEnter: function () {
                        log('CRASH-SITE', 'exec @ ' + target + ' caller(return)=' + this.returnAddress +
                            ' backtrace=' + Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 6).join(' <- '));
                    }
                });
                break;
            }
        }
    } catch (e) { log('CRASH-ERR', 'crash-site hook: ' + e); }

    /* ---------- Heartbeat: logic loop vs input read ---------- */
    setInterval(function () {
        log('PUP', 'updates=' + pup + ' touchReads=' + touch + ' soundPlays=' + soundPlays);
        pup = 0; touch = 0; soundPlays = 0;
    }, 1000);

    log('HOOK', 'attach phase done');
}

setTimeout(attachAll, 3500);

/* ---------- Java: activity + dialogs ---------- */
if (Java.available) {
    Java.perform(function () {
        try {
            var Activity = Java.use('android.app.Activity');
            Activity.onResume.implementation = function () {
                log('JAVA', 'Activity.onResume: ' + this.getClass().getName());
                return this.onResume();
            };
        } catch (e) { log('JAVA-ERR', 'Activity hook: ' + e); }
        try {
            var Dialog = Java.use('android.app.Dialog');
            Dialog.show.implementation = function () {
                var cls = 'unknown';
                try { cls = this.getClass().getName(); } catch (e2) {}
                log('JAVA-DIALOG', 'Dialog.show: ' + cls);
                return this.show();
            };
        } catch (e) { log('JAVA-ERR', 'Dialog hook: ' + e); }
        try {
            var AlertDialog = Java.use('android.app.AlertDialog');
            AlertDialog.setMessage.implementation = function (msg) {
                try { log('JAVA-DIALOG', 'AlertDialog.setMessage: ' + msg); } catch (e3) {}
                return this.setMessage(msg);
            };
        } catch (e) { log('JAVA-ERR', 'AlertDialog hook: ' + e); }
        try {
            var Act = Java.use('android.app.Activity');
            Act.dispatchTouchEvent.implementation = function (ev) {
                var out = this.dispatchTouchEvent(ev);
                try {
                    var act = ev.getAction() & 255;
                    log('JAVA-TOUCH', 'action=' + act + ' x=' + ev.getX() + ' y=' + ev.getY() +
                        ' handled=' + out + ' cls=' + this.getClass().getName());
                } catch (e4) {}
                return out;
            };
        } catch (e) { log('JAVA-ERR', 'dispatchTouchEvent hook: ' + e); }
        log('JAVA', 'java hooks installed');
    });
}

log('INIT', 'instrument v8 loaded');

