#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "math.hpp"

namespace resf2::core {

// Base display tree node — matches JS `Db` class.
// Tree structure: parent/child via sibling linked list (af/Ma pointers in JS).
// We use a simpler vector-of-children for clarity.
class Node {
public:
    explicit Node(std::string name = {});
    virtual ~Node();

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // Tree ops
    void append_child(std::unique_ptr<Node> child);
    void remove_child(Node* child);
    void remove_from_parent();

    Node* parent() const { return parent_; }
    const std::vector<std::unique_ptr<Node>>& children() const { return children_; }
    Node* find(const std::string& name);

    // Lifecycle
    bool active = true;
    float time = 0;

    virtual void update(float dt);
    virtual void render(class Renderer2D& r);

    // Walk helpers
    void update_all(float dt);
    void render_all(class Renderer2D& r);

    const std::string& name() const { return name_; }
    void set_name(const std::string& n) { name_ = n; }

private:
    std::string name_;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
};

// Transform node — matches JS `O` class (has position/rotation/scale).
class TransformNode : public Node {
public:
    using Node::Node;

    Vec2 position;
    Vec2 scale{1, 1};
    float rotation = 0;

    Mat4 local_transform() const;

    void render(Renderer2D& r) override;
};

// Sprite node — renders a textured quad.
class SpriteNode : public TransformNode {
public:
    using TransformNode::TransformNode;

    // Texture handle (set by renderer)
    uint32_t texture_id = 0;
    Rect source_rect;  // UV in texture (pixel coords for atlas)
    Rect dest_rect;    // Screen position/size
    Color color;
    bool flip_x = false;

    void render(Renderer2D& r) override;
};

// Button node — interactive with hover/press states.
class ButtonNode : public TransformNode {
public:
    using TransformNode::TransformNode;

    std::function<void()> on_click;
    Rect bounds;
    uint32_t normal_tex = 0;
    uint32_t hover_tex = 0;
    uint32_t pressed_tex = 0;
    bool hovered = false;
    bool pressed = false;

    void render(Renderer2D& r) override;
};

// Label node — bitmap text.
class LabelNode : public TransformNode {
public:
    using TransformNode::TransformNode;

    std::string text;
    uint32_t font_id = 0;
    Color text_color;
    float font_size = 1;

    void render(Renderer2D& r) override;
};

} // namespace resf2::core
