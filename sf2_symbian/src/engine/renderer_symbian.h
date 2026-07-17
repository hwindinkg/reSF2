#pragma once

#include <gles\gl.h>
#include <egl\egl.h>

struct Color4B {
    unsigned char r, g, b, a;
    Color4B() : r(255), g(255), b(255), a(255) {}
    Color4B(unsigned char cr, unsigned char cg, unsigned char cb, unsigned char ca)
        : r(cr), g(cg), b(cb), a(ca) {}
};

class SymbianRenderer {
public:
    SymbianRenderer();
    ~SymbianRenderer();

    TBool Init();
    void BeginFrame();
    void EndFrame();

    // Drawing primitives (matching desktop renderer interface)
    void DrawFilledRectScreen(float x, float y, float w, float h, const Color4B& col);
    void DrawFilledTriangleWorld(float x1, float y1, float x2, float y2,
                                  float x3, float y3, const Color4B& col);
    void DrawFilledCircleWorld(float cx, float cy, float r, const Color4B& col);

private:
    TBool iInitialized;
};
