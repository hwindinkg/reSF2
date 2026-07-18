#pragma once

#include <memory>
#include <vector>
#include <string>

namespace resf2::core {

class State {
public:
    virtual ~State() = default;
    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void update(float dt) {}
    virtual void render(class Renderer2D& r) {}
    virtual std::string name() const = 0;
};

// Pushdown state stack — matches JS `PD[]` pattern.
// States are layered: each state can consume input and render.
class StateStack {
public:
    void push(std::unique_ptr<State> state);
    std::unique_ptr<State> pop();
    void replace(std::unique_ptr<State> state);

    void update(float dt);
    void render(Renderer2D& r);

    State* top() const;
    size_t size() const { return stack_.size(); }
    bool empty() const { return stack_.empty(); }

    template<typename T>
    T* find() {
        for (auto& s : stack_)
            if (auto* t = dynamic_cast<T*>(s.get())) return t;
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<State>> stack_;
};

} // namespace resf2::core
