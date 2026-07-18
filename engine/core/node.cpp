#include "node.hpp"
#include "renderer2d.hpp"

namespace resf2::core {

// ---- Node ----

Node::Node(std::string name) : name_(std::move(name)) {}
Node::~Node() = default;

void Node::append_child(std::unique_ptr<Node> child) {
    child->parent_ = this;
    children_.push_back(std::move(child));
}

void Node::remove_child(Node* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
        [child](auto& p) { return p.get() == child; });
    if (it != children_.end()) {
        (*it)->parent_ = nullptr;
        children_.erase(it);
    }
}

void Node::remove_from_parent() {
    if (parent_) parent_->remove_child(this);
}

Node* Node::find(const std::string& name) {
    if (name_ == name) return this;
    for (auto& c : children_) {
        if (auto* found = c->find(name)) return found;
    }
    return nullptr;
}

void Node::update(float dt) {}
void Node::render(Renderer2D& r) {}

void Node::update_all(float dt) {
    if (!active) return;
    update(dt);
    time += dt;
    for (auto& c : children_) c->update_all(dt);
}

void Node::render_all(Renderer2D& r) {
    if (!active) return;
    render(r);
    for (auto& c : children_) c->render_all(r);
}

// ---- TransformNode ----

Mat4 TransformNode::local_transform() const {
    return Mat4::translate(position.x, position.y)
         * Mat4::rotate(rotation)
         * Mat4::scale(scale.x, scale.y);
}

void TransformNode::render(Renderer2D& r) {
    r.push_transform(local_transform());
    Node::render(r);
    r.pop_transform();
}

// ---- SpriteNode ----

void SpriteNode::render(Renderer2D& r) {
    r.push_transform(local_transform());
    DrawQuad q;
    q.x = dest_rect.x; q.y = dest_rect.y;
    q.w = dest_rect.w; q.h = dest_rect.h;
    q.u0 = source_rect.x; q.v0 = source_rect.y;
    q.u1 = source_rect.x + source_rect.w;
    q.v1 = source_rect.y + source_rect.h;
    q.texture_id = texture_id;
    q.color = color.to_rgba();
    q.flip_x = flip_x;
    r.draw_quad(q);
    Node::render(r);
    r.pop_transform();
}

// ---- ButtonNode ----

void ButtonNode::render(Renderer2D& r) {
    r.push_transform(local_transform());
    uint32_t tex = pressed ? pressed_tex : (hovered ? hover_tex : normal_tex);
    if (tex) {
        DrawQuad q;
        q.x = bounds.x; q.y = bounds.y;
        q.w = bounds.w; q.h = bounds.h;
        q.texture_id = tex;
        r.draw_quad(q);
    }
    Node::render(r);
    r.pop_transform();
}

// ---- LabelNode ----

void LabelNode::render(Renderer2D& r) {
    r.push_transform(local_transform());
    // TODO: bitmap font rendering
    Node::render(r);
    r.pop_transform();
}

} // namespace resf2::core
