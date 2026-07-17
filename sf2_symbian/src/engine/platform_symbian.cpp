#include "platform_symbian.h"
#include <e32math.h>
#include <f32file.h>
#include <s32file.h>

SymbianPlatform::SymbianPlatform()
    : iDisplay(EGL_NO_DISPLAY)
    , iSurface(EGL_NO_SURFACE)
    , iContext(EGL_NO_CONTEXT)
    , iInitialized(EFalse)
{
    Mem::FillZ(iKeys, sizeof(iKeys));
    Mem::FillZ(iKeysJustPressed, sizeof(iKeysJustPressed));
}

SymbianPlatform::~SymbianPlatform() {
    Shutdown();
}

TBool SymbianPlatform::Init() {
    // EGL initialization for Nokia N8 (OpenGL ES 2.0)
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) return EFalse;

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) return EFalse;

    EGLint attribList[] = {
        EGL_RED_SIZE,       5,
        EGL_GREEN_SIZE,     6,
        EGL_BLUE_SIZE,      5,
        EGL_ALPHA_SIZE,     0,
        EGL_DEPTH_SIZE,     0,
        EGL_STENCIL_SIZE,   0,
        EGL_SURFACE_TYPE,   EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfig;
    if (!eglChooseConfig(display, attribList, &config, 1, &numConfig)) {
        eglTerminate(display);
        return EFalse;
    }

    // Create window surface (simplified — requires CCoeControl in real impl)
    // For now, use EGL_DEFAULT_WINDOW (placeholder)
    EGLNativeWindowType window = (EGLNativeWindowType)0;
    EGLSurface surface = eglCreateWindowSurface(display, config, window, NULL);
    if (surface == EGL_NO_SURFACE) {
        eglTerminate(display);
        return EFalse;
    }

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return EFalse;
    }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return EFalse;
    }

    iDisplay = display;
    iSurface = surface;
    iContext = context;
    iInitialized = ETrue;

    // GL state
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    return ETrue;
}

void SymbianPlatform::Shutdown() {
    if (!iInitialized) return;
    eglMakeCurrent(iDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (iContext != EGL_NO_CONTEXT) eglDestroyContext(iDisplay, iContext);
    if (iSurface != EGL_NO_SURFACE) eglDestroySurface(iDisplay, iSurface);
    if (iDisplay != EGL_NO_DISPLAY) eglTerminate(iDisplay);
    iInitialized = EFalse;
}

void SymbianPlatform::SwapBuffers() {
    if (iInitialized) {
        eglSwapBuffers(iDisplay, iSurface);
    }
}

TInt SymbianPlatform::ReadFile(const TDesC& aPath, TDes8& aBuf) {
    RFs fs;
    TInt err = fs.Connect();
    if (err != KErrNone) return err;

    RFile file;
    err = file.Open(fs, aPath, EFileRead);
    if (err != KErrNone) {
        fs.Close();
        return err;
    }

    TInt size;
    err = file.Size(size);
    if (err != KErrNone) {
        file.Close();
        fs.Close();
        return err;
    }

    if (size > aBuf.MaxLength()) size = aBuf.MaxLength();
    err = file.Read(aBuf, size);

    file.Close();
    fs.Close();
    return err;
}

TBool SymbianPlatform::FileExists(const TDesC& aPath) {
    RFs fs;
    if (fs.Connect() != KErrNone) return EFalse;
    TBool exists = EFalse;
    TEntry entry;
    if (fs.Entry(aPath, entry) == KErrNone) exists = ETrue;
    fs.Close();
    return exists;
}

TUint32 SymbianPlatform::NowMs() const {
    return (TUint32)(User::TickCount() * 1000 / User::TickFrequency());
}

TBool SymbianPlatform::IsKeyDown(TInt aKeyCode) const {
    if (aKeyCode < 0 || aKeyCode >= 256) return EFalse;
    return iKeys[aKeyCode];
}

TBool SymbianPlatform::IsKeyJustPressed(TInt aKeyCode) const {
    if (aKeyCode < 0 || aKeyCode >= 256) return EFalse;
    return iKeysJustPressed[aKeyCode];
}

void SymbianPlatform::ClearKeys() {
    Mem::FillZ(iKeysJustPressed, sizeof(iKeysJustPressed));
}
