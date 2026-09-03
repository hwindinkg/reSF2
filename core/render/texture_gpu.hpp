#pragma once

// GPU texture upload: pushes a decoded core/data Texture (RGBA8) to the
// GPU as a GL_TEXTURE_2D, and draws a SpriteBatch quad covering a region of
// it. One GL texture per core/data texture; the renderer keeps a
// name->texture cache so each atlas is uploaded once.

#include <cstdint>

#include "render/gl_types.hpp"

namespace sf2::data {
struct Texture;
}

namespace sf2::render {

// Uploads `tex` as a GL_TEXTURE_2D (RGBA8, clamped, linear filtered).
// Returns 0 on failure (no current context / empty texture).
GLuint upload_texture_rgba(const sf2::data::Texture& tex);

// Frees a texture uploaded by upload_texture_rgba.
void delete_texture(GLuint texture);

} // namespace sf2::render
