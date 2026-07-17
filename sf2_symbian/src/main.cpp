// Symbian entry point for reSF2
// Bootstrap: init platform, EGL, game loop (placeholder)

#include "engine/platform_symbian.h"
#include "engine/renderer_symbian.h"
#include "engine/input_symbian.h"

#include <e32std.h>
#include <e32base.h>
#include <e32cons.h>
#include <e32def.h>
#include <f32file.h>
#include <s32file.h>

// Log to file on C:\Data\resf2_log.txt
static void Log(const char* aMsg) {
    RFs fs;
    if (fs.Connect() != KErrNone) return;

    RFile f;
    TInt err = f.Open(fs, _L("C:\\Data\\resf2_log.txt"),
                      EFileWrite | EFileShareAny);
    if (err != KErrNone) {
        err = f.Create(fs, _L("C:\\Data\\resf2_log.txt"), EFileWrite);
    }
    if (err == KErrNone) {
        TInt pos = 0;
        f.Seek(ESeekEnd, pos);
        TBuf8<256> buf;
        buf.Copy((const TUint8*)aMsg);
        buf.Append((TUint8)'\n');
        f.Write(buf);
        f.Flush();
        f.Close();
    }
    fs.Close();
}

// Main game loop (simplified placeholder)
static void GameLoop() {
    SymbianPlatform platform;
    if (!platform.Init()) {
        Log("Platform init failed!");
        return;
    }
    Log("Platform initialized");

    SymbianRenderer renderer;
    if (!renderer.Init()) {
        Log("Renderer init failed!");
        platform.Shutdown();
        return;
    }
    Log("Renderer initialized");

    TouchInput touch;

    // Asset paths (Symbian format)
    _LIT(KAssetRoot, "C:\\Data\\reSF2\\");

    // Check if assets exist
    TBuf<256> testPath;
    testPath.Copy(KAssetRoot);
    testPath.Append(_L("params.xml"));
    if (!platform.FileExists(testPath)) {
        // Try memory card
        _LIT(KAssetRootE, "E:\\Data\\reSF2\\");
        Log("Assets not found on C:, trying E:");
        // In real impl, use E: path
    }

    Log("Starting main loop");

    // Main loop
    for (;;) {
        // Check for quit (simplified — no CAknAppUi yet)
        // Handle input
        touch.ClearFrame();

        // Render
        renderer.BeginFrame();
        // TODO: render dojo background, character, bag, HUD
        renderer.EndFrame();

        platform.SwapBuffers();
    }

    platform.Shutdown();
}

// E32Main — Symbian executable entry point
GLDEF_C TInt E32Main() {
    Log("reSF2 starting...");

    // Initialize cleanup stack
    CTrapCleanup* cleanup = CTrapCleanup::New();
    if (!cleanup) return KErrNoMemory;

    TRAPD(err, GameLoop());
    if (err != KErrNone) {
        TBuf8<64> errBuf;
        errBuf.Num((TUint)err);
        Log("GameLoop leave with error");
    }

    delete cleanup;
    return KErrNone;
}
