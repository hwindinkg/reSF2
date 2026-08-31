#pragma once

// Minimal scene graph for the SF2 native port's 2D location rendering.
//
// Mirrors the game's `Db`/`O` scene nodes (JS_MAP §1.5): a node has an
// update pass (`oja`) and a render pass (`nja`), children, and a 2D
// transform (position + scale + anchor). Only what the location renderer
// needs — nothing more.

#include <memory>
#include <vector>

namespace sf2::render {
class Renderer;
}

namespace sf2::scene {

// 2D transform: world position, uniform/horizontal scale, and an anchor
// (normalized 0..1 point in the sprite's own box, matching the game's
// `ik(.5,.5)` — sprites are centered on their position).
struct Transform {
    float x = 0.0f;
    float y = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float anchor_x = 0.5f;
    float anchor_y = 0.5f;

    void set_pos(float px, float py) {
        x = px;
        y = py;
    }
    void set_scale(float sx, float sy) {
        scale_x = sx;
        scale_y = sy;
    }
};

// Scene node base class. Children are drawn after (on top of) the node's
// own render. The game's update pass runs `aa()` then recurses; the render
// pass runs `Ea()` then recurses — same shape here.
class Node {
public:
    virtual ~Node() = default;

    // Update pass — called once per fixed tick (60 Hz in the game). The
    // default does nothing.
    virtual void update(float dt) { (void)dt; }

    // Render pass — draws the node into `batch` (via the renderer).
    virtual void render(sf2::render::Renderer& r) { (void)r; }

    void add_child(std::shared_ptr<Node> child) { children_.push_back(std::move(child)); }

    const std::vector<std::shared_ptr<Node>>& children() const { return children_; }

private:
    std::vector<std::shared_ptr<Node>> children_;
};

} // namespace sf2::scene
