#pragma once

#include <functional>
#include <string>
#include "../core/math.hpp"
#include "../core/renderer2d.hpp"
#include "../core/input.hpp"

namespace resf2::ui {
using core::Vec2;
using core::Rect;
using core::Color;

// Button widget — uses sprite atlases for normal/hover/pressed states.
class Button {
public:
    std::string name;
    Rect bounds;

    std::function<void()> on_click;
    std::function<void()> on_hover;

    // Texture IDs from asset manager
    uint32_t normal_tex = 0;
    uint32_t hover_tex = 0;
    uint32_t pressed_tex = 0;
    uint32_t disabled_tex = 0;

    bool enabled = true;
    bool visible = true;
    bool hovered = false;
    bool pressed = false;

    void update(float dt, const core::InputState& input);
    void render(core::Renderer2D& r);
};

// Horizontal/vertical layout with anchor support
class Layout {
public:
    enum class Direction { Horizontal, Vertical };

    struct Item {
        Button* button = nullptr;
        float weight = 1.0f;
        int margin = 4;
    };

    Direction dir = Direction::Vertical;
    Rect bounds;
    std::vector<Item> items;

    void add(Button* btn, float weight = 1.0f, int margin = 4);
    void arrange();
    void update(float dt, const core::InputState& input);
    void render(core::Renderer2D& r);
};

// Simple panel (container with background)
class Panel {
public:
    Rect bounds;
    uint32_t bg_texture = 0;
    uint32_t bg_color = 0xFF000000;
    bool visible = true;

    void render(core::Renderer2D& r);
};

} // namespace resf2::ui
