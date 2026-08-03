/*
 * SF2 live boot trace v3 — log-only instrumentation (no game-state mutation).
 * - All Interceptor.attach calls run ~3.5s AFTER resume (frida quirk: hooks
 *   attached before resume never fire on this device; heartbeat proves JS
 *   runtime keeps running, hook counter stays 0 until re-attached).
 * - readCString() for paths (readUtf8String(200) breaks on short strings).
 */
'use strict';

var t0 = Date.now();
function ts() { return ((Date.now() - t0) / 1000).toFixed(2); }
function log(tag, m) { send({ tag: tag, t: ts(), m: m }); }

function readCStr(p) {
    try { return p.readCString(256); } catch (e) { return null; }
}

var hooksDone = false;

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

    /* ---------- S3E VFS / video / audio / trace ---------- */
    var m = Process.findModuleByName('libs3e_android.so');
    if (!m) { log('S3E', 'libs3e_android.so NOT loaded'); }
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
    hookS3e('s3eAudioStop', function () { log('AUDIO', 'STOP'); });
    hookS3e('s3eSoundChannelPlay', function (args) {
        log('SOUND', 'ch=' + args[0].toInt32() + ' snd=' + args[1].toInt32() + ' vol=' + args[2].toInt32());
    });
    hookS3e('s3eDebugPrint', function (args) {
        var p = readCStr(args[0]);
        if (p && /[ -~]{3,}/.test(p)) log('TRACE', p.slice(0, 200));
    });

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
        log('JAVA', 'java hooks installed');
    });
}

log('INIT', 'instrument loaded');
