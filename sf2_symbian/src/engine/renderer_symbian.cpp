#include "renderer_symbian.h"
#include <e32std.h>

SymbianRenderer::SymbianRenderer() : iInitialized(EFalse) {}
SymbianRenderer::~SymbianRenderer() {}

TBool SymbianRenderer::Init() {
    // GL state is set up by platform
    iInitialized = ETrue;
    return ETrue;
}

void SymbianRenderer::BeginFrame() {
    glClear(GL_COLOR_BUFFER_BIT);
}

void SymbianRenderer::EndFrame() {
    // Swap is handled by platform
}

void SymbianRenderer::DrawFilledRectScreen(float x, float y, float w, float h,
                                            const Color4B& col) {
    float r = col.r / 255.0f;
    float g = col.g / 255.0f;
    float b = col.b / 255.0f;
    float a = col.a / 255.0f;

    GLfloat verts[] = {
        x,   y,
        x+w, y,
        x,   y+h,
        x+w, y+h
    };

    glVertexPointer(2, GL_FLOAT, 0, verts);
    glEnableClientState(GL_VERTEX_ARRAY);
    glColor4f(r, g, b, a);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void SymbianRenderer::DrawFilledTriangleWorld(float x1, float y1,
                                               float x2, float y2,
                                               float x3, float y3,
                                               const Color4B& col) {
    float r = col.r / 255.0f;
    float g = col.g / 255.0f;
    float b = col.b / 255.0f;
    float a = col.a / 255.0f;

    GLfloat verts[] = { x1, y1, x2, y2, x3, y3 };
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glEnableClientState(GL_VERTEX_ARRAY);
    glColor4f(r, g, b, a);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void SymbianRenderer::DrawFilledCircleWorld(float cx, float cy, float r,
                                             const Color4B& col) {
    // Simple hexagon approximation for circles
    float cr = col.r / 255.0f;
    float cg = col.g / 255.0f;
    float cb = col.b / 255.0f;
    float ca = col.a / 255.0f;

    GLfloat verts[12]; // 6 triangles = 12 vertices
    for (int i = 0; i < 6; i++) {
        double angle = 3.14159 * 2.0 * i / 6.0;
        verts[i*2]     = cx + r * (float)cos(angle);
        verts[i*2 + 1] = cy + r * (float)sin(angle);
    }

    // Re-center at cx, cy as first vertex (fan)
    GLfloat fanVerts[15] = { cx, cy };
    for (int i = 0; i < 6; i++) {
        fanVerts[(i+1)*2]     = verts[i*2];
        fanVerts[(i+1)*2 + 1] = verts[i*2 + 1];
    }
    // Close fan
    fanVerts[14] = verts[0];
    fanVerts[15] = verts[1];

    glVertexPointer(2, GL_FLOAT, 0, fanVerts);
    glEnableClientState(GL_VERTEX_ARRAY);
    glColor4f(cr, cg, cb, ca);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 8);
    glDisableClientState(GL_VERTEX_ARRAY);
}
