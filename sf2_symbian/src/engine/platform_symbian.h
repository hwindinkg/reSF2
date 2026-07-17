#pragma once

#include <e32std.h>
#include <e32def.h>
#include <gles\gl.h>
#include <egl\egl.h>

// Platform abstraction for Symbian S60 5th Ed (Nokia N8)
class SymbianPlatform {
public:
    SymbianPlatform();
    ~SymbianPlatform();

    TBool Init();
    void Shutdown();

    // EGL
    EGLDisplay GetDisplay() const { return iDisplay; }
    EGLSurface GetSurface() const { return iSurface; }
    EGLContext GetContext() const { return iContext; }
    void SwapBuffers();

    // Window
    TInt Width() const { return 640; }
    TInt Height() const { return 360; }

    // Input
    TBool IsKeyDown(TInt aKeyCode) const;
    TBool IsKeyJustPressed(TInt aKeyCode) const;
    void ClearKeys();

    // File I/O
    TInt ReadFile(const TDesC& aPath, TDes8& aBuf);
    TBool FileExists(const TDesC& aPath);

    // Time
    TUint32 NowMs() const;

private:
    EGLDisplay iDisplay;
    EGLSurface iSurface;
    EGLContext iContext;
    TBool iInitialized;

    // Input state
    TBool iKeys[256];
    TBool iKeysJustPressed[256];
};
