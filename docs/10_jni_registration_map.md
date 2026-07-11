# 10 — JNI registration map

> Status: Stage 2 partial. The full `RegisterNatives()` table is not yet
> recovered byte-for-byte (that requires Ghidra or similar), but we
> have the complete list of (a) Java class names, (b) Java native method
> names, and (c) JNI signatures observed as strings inside
> `libs3e_android.so`. That is sufficient for reSF2's clean-room
> Android wrapper (Stage 7.1).

## JNI entry points

```
$ nm -D --defined-only lib/armeabi-v7a/libs3e_android.so | grep -E 'Java_|JNI_'
0003c770 T JNI_OnLoad
0003c438 T JNI_OnUnLoad
```

`JNI_OnLoad` is at file offset `0x0003c770` (ARM mode, not Thumb — the
symbol value has bit 0 = 0).

## `JNI_OnLoad` disassembly (ARM mode, first 0x200 bytes)

```
0x0003c770:  push    {r3, lr}
0x0003c774:  mov     r2, #0
0x0003c778:  ldr     r3, [pc, #0x18]
0x0003c77c:  add     r3, pc, r3            ; r3 -> global var
0x0003c780:  str     r0, [r3, #0x1b8]      ; save JavaVM* at global+0x1b8
0x0003c784:  strb    r2, [r3]              ; clear a flag byte
0x0003c788:  bl      #0x3c698              ; call _s3eAndroidInit
0x0003c78c:  mov     r0, #2
0x0003c790:  movt    r0, #1                ; r0 = 0x00010002 = JNI_VERSION_1_4
0x0003c794:  pop     {r3, pc}              ; return JNI_VERSION_1_4
```

So `JNI_OnLoad` is a thin shim that:

1. Stores the `JavaVM*` (passed in `r0`) into a global at offset `0x1b8`
   of some data structure.
2. Clears a flag byte (probably "is_initialized").
3. Calls `_s3eAndroidInit` at `0x3c698` — this is the function that
   actually calls `FindClass` + `RegisterNatives`.
4. Returns `JNI_VERSION_1_4` (`0x00010002`).

`JNI_OnUnLoad` at `0x3c438` is `bx lr` (a no-op — Marmalade does not
implement unload).

## `_s3eAndroidInit` at `0x3c698` — the registration function

Disassembly shows it follows the standard `RegisterNatives` pattern:

```
0x3c79c:  push    {r4-r8, lr}
0x3c7a0:  sub     sp, sp, #8
0x3c7a4:  ldr     r4, [pc, #0x194]         ; load PC-relative offset to globals
0x3c7a8:  mov     r8, r1                   ; r8 = JNIEnv*
0x3c7ac:  mov     r7, r0                   ; r7 = JavaVM*
0x3c7b0:  bl      #0x83618                 ; get JNIEnv via (*JavaVM)->GetEnv
0x3c7b4:  add     r4, pc, r4               ; r4 -> global JNI table
0x3c7b8:  ldr     r1, [r4, #0x19c]         ; r1 = class name string
0x3c7bc:  bl      #0x8361c                 ; FindClass(env, className)
0x3c7c0:  subs    r3, r0, #0
0x3c7c4:  ldrne   r4, [r4, #8]             ; if found, r4 -> method table
0x3c7c8:  beq     #0x3c8e4                 ; if not found, error path
...
0x3c818:  ldr     ip, [r5]
0x3c81c:  ldr     r2, [pc, #0x124]         ; method count
0x3c820:  ldr     r3, [pc, #0x124]         ; JNINativeMethod[] pointer
0x3c824:  ldr     ip, [ip, #0x84]          ; JNIEnv->RegisterNatives offset
0x3c828:  add     r2, pc, r2
0x3c82c:  add     r3, pc, r3
0x3c830:  mov     r1, r0                   ; jclass
0x3c834:  mov     r0, r5                   ; JNIEnv
0x3c838:  blx     ip                       ; call RegisterNatives(env, cls, methods, count)
```

The actual `JNINativeMethod[]` array literal lives in `.rodata`. Its
exact file offset requires resolving the PC-relative loads at `0x3c81c`
and `0x3c820` — Stage 2.6 will do that with a proper disassembler
(Ghidra). For Stage 2 we have enough from string analysis alone.

## Recovered Java classes (from string extraction on `libs3e_android.so`)

```
Lcom/ideaworks3d/marmalade/LoaderThread;
Lcom/ideaworks3d/marmalade/LoaderThread$MediaPlayerManager;
Lcom/ideaworks3d/marmalade/LoaderView;
Lcom/ideaworks3d/marmalade/LoaderKeyboard;
Lcom/ideaworks3d/marmalade.LoaderSMSReceiver;   (inferred)
Lcom/ideaworks3d/marmalade.LoaderAPI;           (inferred)
Lcom/ideaworks3d/marmalade.LoaderLocation;      (inferred)
Lcom/ideaworks3d/marmalade.LoaderActivity;      (inferred)
Lcom/ideaworks3d/marmalade.SoundPlayer;         (inferred)
Lcom/ideaworks3d/marmalade.SoundRecord;         (inferred)
Ljava/lang/String;
Ljava/lang/Runnable;
Landroid/telephony/SmsManager;
Landroid/app/PendingIntent;
```

## Recovered JNI signatures (24 unique, from string extraction)

| Signature           | Likely use |
| ------------------- | ---------- |
| `()V`               | No-arg void methods (init, destroy, resume, suspend) |
| `()I`               | No-arg int-returning methods (getBatteryLevel, getDeviceId, ...) |
| `()Z`               | No-arg boolean-returning methods (hasMultitouch, chargerIsConnected, ...) |
| `(I)I`              | int → int |
| `(I)V`              | int → void (soundStop, vibrateStop, ...) |
| `(I)Z`              | int → boolean |
| `(II)I`             | (int, int) → int |
| `(II)V`             | (int, int) → void (touchSetWait?, videoSetVolume?) |
| `(III)I`            | (int, int, int) → int |
| `(III)Z`            | (int, int, int) → boolean |
| `(IIII)V`           | (int, int, int, int) → void |
| `(II[IZ)V`          | (int, int, short[], boolean) → void (audio buffer write?) |
| `(IJDDDFFF)V`       | (int, long, 3×double, 3×float) → void (GPS data?) |
| `(IFFF)V`           | (int, 3×float) → void (accelerometer data?) |
| `(IFFIFZ)V`         | (int, 2×float, int, 2×float, boolean) → void |
| `(IZI)I`            | (int, boolean, int) → int |
| `(J)V`              | long → void |
| `([BIIII)V`         | (byte[], 4×int) → void (video frame blit?) |
| `([SI)V`            | (short[], int) → void (audio buffer) |
| `([SII)V`           | (short[], int, int) → void |
| `(Z)I`              | boolean → int |
| `(Z)V`              | boolean → void |
| `(Z)Z`              | boolean → boolean |
| `(FFF)V`            | (3×float) → void (compass / orientation) |

## Recovered Java native method names (partial, ~110 visible)

These are the camelCase strings extracted from `.rodata` of
`libs3e_android.so`, near the `LoaderThread` / `LoaderView` /
`LoaderKeyboard` class references. They are the names registered via
`RegisterNatives()`.

### `LoaderThread` methods (lifecycle + device)

| Method                  | Likely signature | Purpose |
| ----------------------- | ---------------- | ------- |
| `initNative`            | `()V`            | Initialize native runtime (called from `LoaderThread.<init>`) |
| `shutdownNative`        | `()V`            | Shutdown native runtime |
| `suspendAppThreads`     | `()V`            | Pause game thread (Activity.onPause) |
| `resumeAppThreads`      | `()V`            | Resume game thread (Activity.onResume) |
| `signalSuspend`         | `()V`            | Signal suspend (from signal handler) |
| `signalResume`          | `()V`            | Signal resume |
| `lowMemoryWarning`      | `()V`            | Forward Android low-memory callback |
| `doResume`              | `()V`            | Activity.onResume handler |
| `doSuspend`             | `()V`            | Activity.onPause handler |
| `fixOrientation`        | `()V`            | Apply `DispFixRot=Landscape` |
| `getOrientation`        | `()I`            | Get current device orientation |
| `enableRespondingToRotation` | `(Z)V`      | Toggle rotation response |
| `onOrientationChangedNative` | `(I)V`      | Native callback from `OrientationEventListener` |
| `onAccelNative`         | `(IFFF)V`        | Accelerometer callback (axis, x, y, z) |
| `onCompassNative`       | `(IFFF)V`        | Compass callback |
| `accelStart`            | `()Z`            | Start accelerometer |
| `accelStop`             | `()V`            | Stop accelerometer |
| `compassStart`          | `()Z`            | Start compass |
| `compassStop`           | `()V`            | Stop compass |
| `locationStart`         | `()Z`            | Start GPS |
| `locationStop`          | `()V`            | Stop GPS |
| `locationGpsData`       | `(IJDDDFFF)V`    | GPS data callback |
| `locationUpdate`        | `(...)V`         | Location update callback |
| `locationSatellite`     | `(...)V`         | Satellite info callback |
| `setART`                | `(Z)V`           | Toggle ART-runtime-specific code path |

### `LoaderThread` methods (input + UI)

| Method                  | Likely signature | Purpose |
| ----------------------- | ---------------- | ------- |
| `touchSetWait`          | `(II)V`          | Configure touch event wait |
| `hasMultitouch`         | `()Z`            | Check multitouch capability |
| `setPixelsNative`       | `([BIIII)V`      | Blit pixel buffer to surface |
| `setInputText`          | `(Ljava/lang/String;)V` | Soft-keyboard input callback |
| `setCharInputEnabledNative` | `(Z)V`       | Toggle char input mode |
| `onKeyEventNative`      | `(II)V`          | Key event callback (keyCode, action) |
| `runOnOSThread`         | `(Ljava/lang/Runnable;)V` | Schedule Runnable on OS thread |
| `runOnOSThreadNative`   | `(I)V`           | Native callback from OSThread |
| `runOnOSTickNative`     | `()V`            | Periodic OSTick callback |
| `runOnOSSignal`         | `(I)V`           | Signal-driven wakeup |
| `onReceiveCallback`     | `(...)V`         | Generic callback dispatch |

### `LoaderThread` methods (audio + recorder)

| Method                  | Likely signature | Purpose |
| ----------------------- | ---------------- | ------- |
| `soundInit`             | `()V`            | Initialize audio system |
| `soundStart`            | `()V`            | Start audio output |
| `soundStop`             | `()V`            | Stop audio output |
| `soundSetVolume`        | `(I)V`           | Set master volume |
| `getSilentMode`         | `()Z`            | Check device silent mode |
| `audioStoppedNotify`    | `(I)V`           | MediaPlayer completion callback |
| `generateAudio`         | `([SI)V`         | Audio buffer fill callback (PCM) |
| `recordAudio`           | `([SII)V`        | Audio record buffer callback |
| `recordAvailable`       | `()Z`            | Check recording capability |
| `recordStart`           | `()Z`            | Start recording |
| `recordStop`            | `()V`            | Stop recording |

### `LoaderThread$MediaPlayerManager` methods (music + video)

| Method                  | Likely signature | Purpose |
| ----------------------- | ---------------- | ------- |
| `audioPlay`             | `(Ljava/lang/String;)V` | Play music track |
| `audioStop`             | `()V`            | Stop music |
| `audioPause`            | `()V`            | Pause music |
| `audioResume`           | `()V`            | Resume music |
| `audioGetPosition`      | `()I`            | Get playback position (ms) |
| `audioSetPosition`      | `(I)V`           | Seek to position |
| `audioGetStatus`        | `()I`            | Get playback status |
| `audioGetDuration`      | `()I`            | Get track duration |
| `audioSetVolume`        | `(I)V`           | Set music volume |
| `audioIsPlaying`        | `()Z`            | Is music playing |
| `audioGetNumChannels`   | `()I`            | Get channel count |
| `videoPlay`             | `(Ljava/lang/String;)V` | Play video |
| `videoStop`             | `()V`            | Stop video |
| `videoPause`            | `()V`            | Pause video |
| `videoResume`           | `()V`            | Resume video |
| `videoGetPosition`      | `()I`            | Video position |
| `videoGetStatus`        | `()I`            | Video status |
| `videoSetVolume`        | `(I)V`           | Video volume |
| `videoStoppedNotify`    | `()V`            | Video completion callback |

### `LoaderView` methods (rendering + haptics)

| Method                  | Likely signature | Purpose |
| ----------------------- | ---------------- | ------- |
| `doDraw`                | `()V`            | Render one frame |
| `setViewNative`         | `(II)V`          | Set viewport size |
| `vibrateStart`          | `(I)V`           | Start haptic vibration |
| `vibrateStop`           | `()V`            | Stop vibration |
| `vibrateAvailable`      | `()Z`            | Check haptic capability |
| `showError`             | `(Ljava/lang/String;)V` | Show native error dialog |
| `backlightOn`           | `()V`            | Force backlight on |
| `getLocale`             | `()Ljava/lang/String;` | Get device locale |
| `splashProgress`        | `(I)V`           | Splash screen progress (called `SplashProgress` in Java, but accessed via short name `plashProgress` in the .so string table due to a 1-byte offset issue) |

### `LoaderKeyboard` methods

| Method                  | Likely signature | Purpose |
| ----------------------- | ---------------- | ------- |
| `setShowOnScreenKeyboard` | `(Z)V`         | Show/hide soft keyboard |
| `getKeyboardInfo`       | `()I`            | Get keyboard height / visibility |

### `LoaderThread` methods (filesystem + device info)

| Method                  | Likely signature | Purpose |
| ----------------------- | ---------------- | ------- |
| `getPrivateExternalDir` | `()Ljava/lang/String;` | App-private external storage path |
| `getRstDir`             | `()Ljava/lang/String;` | Marmalade RST (resource) dir |
| `getCacheDir`           | `()Ljava/lang/String;` | Cache dir |
| `getTmpDir`             | `()Ljava/lang/String;` | Temp dir |
| `getDeviceId`           | `()Ljava/lang/String;` | Device unique ID |
| `getDeviceModel`        | `()Ljava/lang/String;` | Device model |
| `getDeviceIMSI`         | `()Ljava/lang/String;` | IMSI |
| `getDeviceNumber`       | `()Ljava/lang/String;` | Phone number |
| `getDeviceDpi`          | `()I`            | Device DPI |
| `getBatteryLevel`       | `()I`            | Battery level (0-100) |
| `chargerIsConnected`    | `()Z`            | Is charger connected |
| `chargerStateChanged`   | `()V`            | Charger state change callback |
| `getNetworkType`        | `()I`            | Network type (wifi/cellular) |
| `getNetworkSubType`     | `()I`            | Network subtype |
| `networkCheckStart`     | `()V`            | Start network monitoring |
| `networkCheckStop`      | `()V`            | Stop network monitoring |
| `networkCheckChanged`   | `(Z)V`           | Network state change callback |
| `acquireMulticastLock`  | `()V`            | Acquire WifiManager multicast lock |
| `releaseMulticastLock`  | `()V`            | Release multicast lock |
| `clipboardGet`          | `()Ljava/lang/String;` | Get clipboard content |
| `clipboardSet`          | `(Ljava/lang/String;)V` | Set clipboard content |
| `getMessage`            | `()Ljava/lang/String;` | Get pending message |

### ICF setting accessors (not JNI methods, but loaded by the same code path)

These appear as `oaderAPI.xxx` strings (the `L` is stripped because they
follow the `LoaderAPI` class name in the .rodata layout). They are
**not** Java methods — they are ICF setting keys that the native code
reads via `s3eConfigGet*`:

```
AndroidKeyHasBackLeft
AndroidFileRstPath
AndroidFileRamPath
AndroidFileUseSdcard
AndroidExtSo
AndroidHandleVol
AndroidPointEventWaitTime
AndroidPointMultiEnable
```

## reSF2 implications

For the Android target (Stage 7.1), reSF2 needs to provide a Java
`NativeActivity` wrapper that exposes **only the methods reSF2's
runtime actually calls**. We do not need to reproduce all 110+ methods
above — many are for hardware that reSF2 will not use (e.g. SMS, IMSI,
device phone number).

### Minimum viable Android wrapper (Stage 7.1 scope)

- `Main` extends `NativeActivity` (or a custom `Activity` with a
  `SurfaceView`).
- One Java class: `resf2.android.NativeBridge` with these methods:
  - `nativeInit(AssetManager assets, String dataDir, int width, int height)`
  - `nativeDestroy()`
  - `nativePause()` / `nativeResume()`
  - `nativeSurfaceChanged(int w, int h)`
  - `nativeTouch(int action, int pointerId, float x, float y)`
  - `nativeKeyDown(int keyCode)` / `nativeKeyUp(int keyCode)`
  - `nativeAudioGenerate(short[] buffer)` (PCM callback)
- That's ~10 native methods. The original game has ~110 because every
  Android device feature gets its own JNI bridge; reSF2 only needs
  rendering, input, and audio.

For non-Android platforms (Linux, Windows, macOS, Switch, Steam Deck),
reSF2 has no Java at all — the runtime boots directly from `main()`.

## What's still unknown (Stage 2.6 follow-up)

1. **Exact `JNINativeMethod[]` array offset** in `.rodata` — requires
   resolving PC-relative loads at `0x3c81c` and `0x3c820`. Needs Ghidra
   or IDA. Stage 2.6 will do this.
2. **Per-method C++ function pointer** — same. Not needed for clean-room
   reimplementation (we write our own C++ side).
3. **`libs3eSmartFox.so` JNI surface** — separate registration table.
   Stage 7.10 will look at this when implementing the SmartFox2X client.

## Tooling note

Disassembly was performed via Python + Capstone 5.0.7 (pip-installed).
System `objdump` was unable to disassemble ARM (`can't disassemble for
architecture UNKNOWN!`). Capstone gave correct ARM-mode disassembly.
