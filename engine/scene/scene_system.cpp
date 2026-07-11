// engine/scene/scene_system.cpp
//
// SceneManager implementation.

#include "scene_system.hpp"

#include <cstdio>
#include <optional>
#include <utility>

namespace resf2::scene {

// ---------- SceneId helpers ----------

const char* scene_name(SceneId id) noexcept {
    switch (id) {
        case SceneId::Boot:     return "Boot";
        case SceneId::Loading:  return "Loading";
        case SceneId::MainMenu: return "MainMenu";
        case SceneId::Map:      return "Map";
        case SceneId::Shop:     return "Shop";
        case SceneId::Settings: return "Settings";
        case SceneId::Dialogue: return "Dialogue";
        case SceneId::Battle:   return "Battle";
        case SceneId::Results:  return "Results";
    }
    return "?";
}

// ---------- SceneManager ----------

SceneManager::SceneManager() = default;

void SceneManager::register_scene(SceneId id, SceneFactory factory) {
    factories_[id] = std::move(factory);
}

void SceneManager::start(SceneId initial, SceneContext& ctx) {
    if (current_) {
        std::fprintf(stderr, "SceneManager: start() called but a scene is already active\n");
        return;
    }
    auto it = factories_.find(initial);
    if (it == factories_.end()) {
        std::fprintf(stderr, "SceneManager: no factory registered for scene %s\n",
                     scene_name(initial));
        return;
    }
    current_ = it->second();
    current_id_ = initial;
    std::printf("[scene] enter %s\n", scene_name(current_id_));
    current_->on_enter(ctx);
}

void SceneManager::transition_to(SceneId to) {
    if (to == current_id_ && !pending_) {
        return;  // already there, no-op
    }
    pending_ = to;
    std::printf("[scene] transition requested: %s -> %s\n",
                scene_name(current_id_), scene_name(to));
}

void SceneManager::update(SceneContext& ctx) {
    if (current_) {
        current_->on_update(ctx);
    }
    if (pending_) {
        apply_transition(ctx);
    }
}

void SceneManager::render(SceneContext& ctx) {
    if (current_) {
        current_->on_render(ctx);
    }
}

bool SceneManager::handle_quit_request(SceneContext& ctx) {
    if (current_) {
        return current_->on_quit_request(ctx);
    }
    return true;
}

void SceneManager::apply_transition(SceneContext& ctx) {
    if (!pending_) return;
    SceneId to = *pending_;
    pending_.reset();

    if (current_) {
        std::printf("[scene] exit %s\n", scene_name(current_id_));
        current_->on_exit(ctx);
        current_.reset();
    }

    auto it = factories_.find(to);
    if (it == factories_.end()) {
        std::fprintf(stderr, "[scene] no factory for %s, falling back to MainMenu\n",
                     scene_name(to));
        it = factories_.find(SceneId::MainMenu);
        if (it == factories_.end()) {
            std::fprintf(stderr, "[scene] FATAL: no MainMenu factory\n");
            return;
        }
        to = SceneId::MainMenu;
    }

    current_ = it->second();
    current_id_ = to;
    std::printf("[scene] enter %s\n", scene_name(current_id_));
    current_->on_enter(ctx);
}

}  // namespace resf2::scene
