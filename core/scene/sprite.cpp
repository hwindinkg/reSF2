// Sprite render — draws through the active renderer/camera.

#include "scene/sprite.hpp"

#include "scene/renderer.hpp"

namespace sf2::scene {

void Sprite::render(sf2::render::Renderer& r) { r.draw_sprite(*this, r.current_camera()); }

} // namespace sf2::scene
