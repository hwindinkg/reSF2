#include "state.hpp"
#include "renderer2d.hpp"

namespace resf2::core {

void StateStack::push(std::unique_ptr<State> state) {
    state->on_enter();
    stack_.push_back(std::move(state));
}

std::unique_ptr<State> StateStack::pop() {
    if (stack_.empty()) return nullptr;
    auto top = std::move(stack_.back());
    stack_.pop_back();
    top->on_exit();
    return top;
}

void StateStack::replace(std::unique_ptr<State> state) {
    pop();
    push(std::move(state));
}

void StateStack::update(float dt) {
    // Update from top to bottom
    for (int i = (int)stack_.size() - 1; i >= 0; i--)
        stack_[i]->update(dt);
}

void StateStack::render(Renderer2D& r) {
    // Render from bottom to top (layered)
    for (auto& s : stack_)
        s->render(r);
}

State* StateStack::top() const {
    return stack_.empty() ? nullptr : stack_.back().get();
}

} // namespace resf2::core
