#include "button.hpp"
#include "../core/input.hpp"

namespace resf2::ui {

void Button::update(float dt, const core::InputState& input) {
    if (!enabled || !visible) {
        hovered = false;
        pressed = false;
        return;
    }

    // Check if mouse/touch is within bounds
    Vec2 pos = input.pointer_pos;  // should be screen coords
    hovered = bounds.contains(pos.x, pos.y);

    // Check for press
    bool any_press = false;
    for (int i = 0; i < input.pointer_count; i++) {
        if (input.pointers[i].just_pressed) any_press = true;
    }

    if (hovered && any_press) {
        pressed = true;
        if (on_click) on_click();
    }

    // Check for release
    if (pressed) {
        bool any_release = false;
        for (int i = 0; i < input.pointer_count; i++) {
            if (input.pointers[i].just_released) any_release = true;
        }
        if (any_release) pressed = false;
    }
}

void Button::render(core::Renderer2D& r) {
    if (!visible) return;

    uint32_t tex = disabled_tex;
    if (enabled) {
        if (pressed) tex = pressed_tex;
        else if (hovered) tex = hover_tex;
        else tex = normal_tex;
    }

    if (tex) {
        core::DrawQuad q;
        q.x = bounds.x; q.y = bounds.y;
        q.w = bounds.w; q.h = bounds.h;
        q.texture_id = tex;
        r.draw_quad(q);
    } else {
        // Fallback colored rectangle
        uint32_t color = enabled ? (pressed ? 0xFF888888 : hovered ? 0xFFAAAAAA : 0xFF666666) : 0xFF444444;
        r.draw_rect(bounds.x, bounds.y, bounds.w, bounds.h, color);
    }
}

void Layout::add(Button* btn, float weight, int margin) {
    items.push_back({btn, weight, margin});
}

void Layout::arrange() {
    if (items.empty()) return;

    if (dir == Direction::Vertical) {
        float total_weight = 0;
        for (auto& item : items) total_weight += item.weight;

        float available_h = bounds.h - (int)(items.size() - 1) * items[0].margin;
        float y = bounds.y;

        for (auto& item : items) {
            float h = available_h * (item.weight / total_weight);
            item.button->bounds = {bounds.x, y, bounds.w, h};
            y += h + item.margin;
        }
    } else {
        float total_weight = 0;
        for (auto& item : items) total_weight += item.weight;

        float available_w = bounds.w - (int)(items.size() - 1) * items[0].margin;
        float x = bounds.x;

        for (auto& item : items) {
            float w = available_w * (item.weight / total_weight);
            item.button->bounds = {x, bounds.y, w, bounds.h};
            x += w + item.margin;
        }
    }
}

void Layout::update(float dt, const core::InputState& input) {
    for (auto& item : items)
        if (item.button) item.button->update(dt, input);
}

void Layout::render(core::Renderer2D& r) {
    for (auto& item : items)
        if (item.button) item.button->render(r);
}

void Panel::render(core::Renderer2D& r) {
    if (!visible) return;
    r.draw_rect(bounds.x, bounds.y, bounds.w, bounds.h, bg_color);
}

} // namespace resf2::ui
