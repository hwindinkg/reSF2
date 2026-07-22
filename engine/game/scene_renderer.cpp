// engine/game/scene_renderer.cpp
//
// SceneRenderer implementation — location rendering and character rendering.

#include "scene_renderer.hpp"
#include "asset_manager.hpp"
#include "location_manager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ren = resf2::renderer;

// ---------- Static helpers ----------

static bool resolve_body_node_impl(
    const std::string& node_name,
    const resf2::game::BodyModel* body_model,
    const resf2::game::BodyModel* bag_model,
    const resf2::game::BodyModel* weapon_model,
    const std::unordered_map<std::string, resf2::game::SkelNode>& skeleton_nodes,
    const std::unordered_map<std::string, std::pair<float, float>>& anim_node_pos,
    float& x, float& y
) {
    using namespace resf2::game;
    if (node_name.rfind("WPN-", 0) == 0) {
        if (!weapon_model) return false;
        std::string base = node_name.substr(4);
        auto wn = weapon_model->nodes.find(base);
        if (wn == weapon_model->nodes.end()) return false;
        x = wn->second.x; y = wn->second.y;
        return true;
    }
    if (body_model) {
        auto bn = body_model->nodes.find(node_name);
        if (bn != body_model->nodes.end()) { x = bn->second.x; y = bn->second.y; return true; }
        auto mn = body_model->macro_nodes.find(node_name);
        if (mn != body_model->macro_nodes.end()) {
            float sum_lcc = 0, wx = 0, wy = 0;
            for (int i = 0; i < 4; ++i) {
                if (mn->second.children[i].empty()) continue;
                float cx, cy;
                resolve_body_node_impl(mn->second.children[i], body_model, bag_model, weapon_model, skeleton_nodes, anim_node_pos, cx, cy);
                wx += cx * mn->second.lcc[i];
                wy += cy * mn->second.lcc[i];
                sum_lcc += mn->second.lcc[i];
            }
            if (std::abs(sum_lcc) > 1e-6f) { x = wx / sum_lcc; y = wy / sum_lcc; return true; }
        }
    }
    auto sn = skeleton_nodes.find(node_name);
    if (sn != skeleton_nodes.end()) { x = sn->second.x; y = sn->second.y; return true; }
    if (bag_model) {
        auto bn = bag_model->nodes.find(node_name);
        if (bn != bag_model->nodes.end()) { x = bn->second.x; y = bn->second.y; return true; }
    }
    auto an = anim_node_pos.find(node_name);
    if (an != anim_node_pos.end()) { x = an->second.first; y = an->second.second; return true; }
    return false;
}

namespace resf2::game {

bool SceneRenderer::resolve_body_node(
    const std::string& node_name,
    const std::unordered_map<std::string, BodyModel*>& models,
    const std::unordered_map<std::string, SkelNode>& skeleton_nodes,
    const std::unordered_map<std::string, SkelEdge>& /*skeleton_edges*/,
    const std::unordered_map<std::string, std::pair<float, float>>& anim_node_pos,
    float& x, float& y
) {
    auto body_it = models.find("body");
    auto bag_it = models.find("bag");
    auto weapon_it = models.find("weapon");
    return resolve_body_node_impl(node_name,
        body_it != models.end() ? body_it->second : nullptr,
        bag_it != models.end() ? bag_it->second : nullptr,
        weapon_it != models.end() ? weapon_it->second : nullptr,
        skeleton_nodes, anim_node_pos, x, y);
}

// ---------- Location rendering ----------

void SceneRenderer::render_location(ren::Renderer& renderer, float cam_x, float cam_y, float zoom) {
    auto* location = locations_.location();
    if (!location) return;
    float vw = 1280.0f, vh = 720.0f;

    for (auto& layer : location->layers) {
        float parallax_factor = layer.factor;
        if (parallax_factor <= 0.0f) parallax_factor = 1.0f;
        float parallax_shift = (1.0f - parallax_factor) * cam_x;

        for (auto& img : layer.images) {
            if (img.class_name == "pixel_1" && !img.color.empty()) {
                unsigned long col = std::stoul(img.color, nullptr, 16);
                ren::Color4B c{uint8_t((col>>16)&0xFF), uint8_t((col>>8)&0xFF), uint8_t(col&0xFF), 255};
                auto it = assets_.atlases().find(img.atlas_name);
                if (it == assets_.atlases().end()) {
                    float hw = vw / (2.0f * zoom), hh = vh / (2.0f * zoom);
                    float left = cam_x - hw, right = cam_x + hw;
                    float bottom = cam_y - hh, top = cam_y + hh;
                    float world_x = img.x - parallax_shift;
                    float world_y = -img.y;
                    float sx = (world_x - img.w/2.0f - left) / (right - left) * vw;
                    float sy = (1.0f - (world_y - img.h/2.0f - bottom) / (top - bottom)) * vh;
                    float ex = (world_x + img.w/2.0f - left) / (right - left) * vw;
                    float ey = (1.0f - (world_y + img.h/2.0f - bottom) / (top - bottom)) * vh;
                    renderer.draw_filled_rect_screen(std::min(sx,ex), std::min(sy,ey), std::abs(ex-sx), std::abs(ey-sy), c);
                }
                continue;
            }
            auto it = assets_.atlases().find(img.atlas_name);
            if (it == assets_.atlases().end()) {
                float world_y = -img.y;
                float world_x = img.x - parallax_shift;
                float left = world_x - img.w/2.0f;
                float bottom = world_y - img.h/2.0f;
                float hw2 = vw/(2.0f*zoom), hh2 = vh/(2.0f*zoom);
                float vis_left2 = cam_x - hw2, vis_right2 = cam_x + hw2;
                float vis_bottom2 = cam_y - hh2, vis_top2 = cam_y + hh2;
                if (left+img.w < vis_left2 || left > vis_right2 || bottom+img.h < vis_bottom2 || bottom > vis_top2) continue;
                float sx = (left-vis_left2)/(vis_right2-vis_left2)*vw;
                float sy = (1.0f-(bottom-vis_bottom2)/(vis_top2-vis_bottom2))*vh;
                float sw = img.w/(vis_right2-vis_left2)*vw;
                float sh = img.h/(vis_top2-vis_bottom2)*vh;
                uint8_t r=100,g=120,b=160;
                if (!img.color.empty()) { unsigned long col = std::stoul(img.color,nullptr,16); r=uint8_t((col>>16)&0xFF); g=uint8_t((col>>8)&0xFF); b=uint8_t(col&0xFF); }
                else if (location && !location->color.empty()) { unsigned long col = std::stoul(location->color,nullptr,16); r=uint8_t((col>>16)&0xFF); if(r<30)r=60; g=uint8_t((col>>8)&0xFF); if(g<30)g=80; b=uint8_t(col&0xFF); if(b<30)b=100; }
                renderer.draw_filled_rect_screen(sx,sy,sw,sh,ren::Color4B{r,g,b,200});
                ren::Color4B border{255,255,255,60};
                renderer.draw_filled_rect_screen(sx,sy,sw,2,border);
                renderer.draw_filled_rect_screen(sx,sy,2,sh,border);
                renderer.draw_filled_rect_screen(sx+sw-2,sy,2,sh,border);
                renderer.draw_filled_rect_screen(sx,sy+sh-2,sw,2,border);
                continue;
            }
            auto& atlas = it->second;
            if (!atlas.texture || !atlas.atlas) continue;
            auto fit = atlas.atlas->name_index.find(img.class_name+".png");
            if (fit == atlas.atlas->name_index.end()) { fit = atlas.atlas->name_index.find(img.class_name); if (fit==atlas.atlas->name_index.end()) continue; }
            auto& frame = atlas.atlas->frames[fit->second];
            float img_off_x = (float)frame.offset_x, img_off_y = (float)frame.offset_y;
            if (atlas.cropped.count(img.class_name)) {
                auto& ctex = atlas.cropped[img.class_name];
                float world_y = -img.y - img_off_y;
                float world_x = img.x + img_off_x - parallax_shift;
                renderer.draw_textured_quad(*ctex, world_x - img.w/2, world_y - img.h/2, img.w, img.h);
                continue;
            }
            float tw = (float)atlas.atlas->metadata.texture_w;
            float th = (float)atlas.atlas->metadata.texture_h;
            float u0 = frame.atlas_x/tw, v0 = frame.atlas_y/th;
            float u1 = (frame.atlas_x+frame.atlas_w)/tw, v1 = (frame.atlas_y+frame.atlas_h)/th;
            float world_y = -img.y, world_x = img.x + img_off_x - parallax_shift;
            if (frame.rotated)
                renderer.draw_textured_quad(*atlas.texture, world_x-img.w/2, world_y-img.h/2, img.w, img.h, u0,v1,u1,v0);
            else
                renderer.draw_textured_quad(*atlas.texture, world_x-img.w/2, world_y-img.h/2, img.w, img.h, u0,v0,u1,v1);
        }
    }
}

// ---------- Character rendering ----------

void SceneRenderer::render_character(
    ren::Renderer& renderer, float pos_x, float pos_y, bool facing_right,
    const std::unordered_map<std::string, std::pair<float, float>>& anim_node_pos,
    float y_adjust_smoothed, float hit_flash, bool is_player, float /*zoom*/
) {
    auto* body = assets_.body_model().get();
    if (!body) return;
    float face_scale = facing_right ? 1.0f : -1.0f;
    float ox = pos_x, oy = pos_y + y_adjust_smoothed;
    uint8_t r = is_player ? 180 : 160, g = is_player ? 160 : 120, b = is_player ? 140 : 100;
    if (hit_flash > 0) { r=255; g=100; b=100; }
    bool has_weapon = assets_.weapon_model() && !assets_.weapon_model()->nodes.empty();

    // Draw body model triangles using the rendering approach from original game.hpp
    if (!body->edges.empty()) {
        for (auto& e : body->edges) {
            // Use edge endpoints for capsule rendering
            auto n1 = body->nodes.find(e.end1);
            auto n2 = body->nodes.find(e.end2);
            if (n1 == body->nodes.end() || n2 == body->nodes.end()) continue;
            float nx1=n1->second.x, ny1=n1->second.y;
            float nx2=n2->second.x, ny2=n2->second.y;
            auto an1 = anim_node_pos.find(e.end1);
            auto an2 = anim_node_pos.find(e.end2);
            if (an1 != anim_node_pos.end()) { nx1=an1->second.first; ny1=an1->second.second; }
            if (an2 != anim_node_pos.end()) { nx2=an2->second.first; ny2=an2->second.second; }
            float wx1 = ox + nx1*face_scale, wy1 = oy + ny1;
            float wx2 = ox + nx2*face_scale, wy2 = oy + ny2;
            // Draw as capsule: two triangles forming a pill shape
            float dx = wx2 - wx1, dy = wy2 - wy1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.001f) continue;
            float px = -dy/len * 3.0f, py = dx/len * 3.0f;
            float ht = 4.0f;
            ren::Color4B col{r,g,b,150};
            renderer.draw_filled_triangle_world(wx1+px*ht, wy1+py*ht, wx2+px*ht, wy2+py*ht, wx1-px*ht, wy1-py*ht, col);
            renderer.draw_filled_triangle_world(wx1-px*ht, wy1-py*ht, wx2+px*ht, wy2+py*ht, wx2-px*ht, wy2-py*ht, col);
            renderer.draw_filled_circle_world(wx1, wy1, ht, col);
            renderer.draw_filled_circle_world(wx2, wy2, ht, col);
        }
    }

    // Draw weapon model on top
    if (has_weapon) {
        render_body_model(renderer, *assets_.weapon_model(), ox, oy, facing_right, anim_node_pos, 1.0f);
    }
}

// ---------- Body model rendering (capsule/triangle mesh) ----------

void SceneRenderer::render_body_model(
    ren::Renderer& renderer, const BodyModel& model,
    float offset_x, float offset_y, bool facing_right,
    const std::unordered_map<std::string, std::pair<float, float>>& node_pos,
    float /*zoom*/
) {
    float face_scale = facing_right ? 1.0f : -1.0f;
    auto get_pos = [&](const std::string& name) -> std::pair<float,float> {
        auto np = node_pos.find(name);
        if (np != node_pos.end()) return np->second;
        auto mn = model.nodes.find(name);
        if (mn != model.nodes.end()) return {mn->second.x, mn->second.y};
        return {0,0};
    };
    for (auto& tri : model.triangles) {
        auto p1 = get_pos(tri.n1), p2 = get_pos(tri.n2), p3 = get_pos(tri.n3);
        renderer.draw_filled_triangle_world(
            offset_x + p1.first*face_scale, offset_y + p1.second,
            offset_x + p2.first*face_scale, offset_y + p2.second,
            offset_x + p3.first*face_scale, offset_y + p3.second,
            ren::Color4B{160,140,120,80});
    }
    for (auto& cap : model.capsules) {
        // Find the skeleton edge for this capsule
        // Simplified: just draw a line at the capsule position
        auto p1 = get_pos(cap.edge_name);
        (void)p1; // Capsule rendering simplified
    }
    for (auto& e : model.edges) {
        auto p1 = get_pos(e.end1), p2 = get_pos(e.end2);
        float x1 = offset_x + p1.first*face_scale, y1 = offset_y + p1.second;
        float x2 = offset_x + p2.first*face_scale, y2 = offset_y + p2.second;
        float dx = x2-x1, dy = y2-y1;
        float len = std::sqrt(dx*dx+dy*dy);
        if (len < 0.001f) continue;
        float px = -dy/len * 4.0f, py = dx/len * 4.0f;
        ren::Color4B col = e.collisible ? ren::Color4B{200,80,80,150} : ren::Color4B{120,120,200,100};
        renderer.draw_filled_triangle_world(x1+px, y1+py, x2+px, y2+py, x1-px, y1-py, col);
        renderer.draw_filled_triangle_world(x1-px, y1-py, x2+px, y2+py, x2-px, y2-py, col);
    }
}

// ---------- Enemy fighter rendering ----------

void SceneRenderer::render_enemy_fighter(
    ren::Renderer& renderer, float enemy_pos_x, float enemy_pos_y,
    bool enemy_facing_right, float enemy_y_adjust, float enemy_hit_flash,
    const std::string& enemy_anim, float enemy_anim_time,
    const std::unordered_map<std::string, AnimationData>& animations,
    const std::unordered_map<std::string, SkelNode>& skeleton_nodes,
    const std::unordered_map<std::string, SkelEdge>& skeleton_edges,
    const std::vector<std::string>& ordered_node_names,
    const std::unique_ptr<BodyModel>& enemy_weapon_model,
    bool show_enemy, float /*zoom*/
) {
    if (!show_enemy) return;
    float npivot_x=0, npivot_y=0;
    std::unordered_map<std::string, std::pair<float,float>> enemy_node_pos;
    auto eit = animations.find(enemy_anim);
    if (eit != animations.end() && eit->second.frame_count > 0) {
        auto& eanim = eit->second;
        int frame = (int)(enemy_anim_time * 20.0f) % eanim.frame_count;
        int npivot_idx = -1;
        for (int i = 0; i < (int)ordered_node_names.size(); ++i) {
            if (ordered_node_names[i] == "NPivot") { npivot_idx = i; break; }
        }
        if (npivot_idx >= 0) {
            float npx,npy,npz; eanim.get_node_pos(frame, npivot_idx, npx,npy,npz);
            npivot_x=npx; npivot_y=npy;
        }
        for (int ni = 0; ni < (int)ordered_node_names.size() && ni < (int)eanim.node_positions[frame].size(); ++ni) {
            auto [nx,ny,nz] = eanim.node_positions[frame][ni];
            enemy_node_pos[ordered_node_names[ni]] = {nx-npivot_x, ny-npivot_y};
        }
    }
    float face_scale = enemy_facing_right ? 1.0f : -1.0f;
    float ox = enemy_pos_x, oy = enemy_pos_y + enemy_y_adjust;
    uint8_t r = enemy_hit_flash>0?255:180, g=enemy_hit_flash>0?80:80, b=enemy_hit_flash>0?80:80;

    // Render enemy skeleton using capsule/triangle approach matching original game
    for (auto& [en, edge] : skeleton_edges) {
        auto p1 = skeleton_nodes.find(edge.end1);
        auto p2 = skeleton_nodes.find(edge.end2);
        if (p1==skeleton_nodes.end() || p2==skeleton_nodes.end()) continue;
        float nx1=p1->second.x, ny1=p1->second.y;
        float nx2=p2->second.x, ny2=p2->second.y;
        auto an1 = enemy_node_pos.find(edge.end1);
        auto an2 = enemy_node_pos.find(edge.end2);
        if (an1 != enemy_node_pos.end()) { nx1=an1->second.first; ny1=an1->second.second; }
        if (an2 != enemy_node_pos.end()) { nx2=an2->second.first; ny2=an2->second.second; }
        float wx1 = ox + nx1*face_scale, wy1 = oy + ny1;
        float wx2 = ox + nx2*face_scale, wy2 = oy + ny2;
        float dx = wx2-wx1, dy = wy2-wy1;
        float len = std::sqrt(dx*dx+dy*dy);
        if (len < 0.001f) continue;
        float px = -dy/len * 3.0f, py = dx/len * 3.0f;
        float ht = 4.0f;
        ren::Color4B col{r,g,b,100};
        renderer.draw_filled_triangle_world(wx1+px*ht, wy1+py*ht, wx2+px*ht, wy2+py*ht, wx1-px*ht, wy1-py*ht, col);
        renderer.draw_filled_triangle_world(wx1-px*ht, wy1-py*ht, wx2+px*ht, wy2+py*ht, wx2-px*ht, wy2-py*ht, col);
        renderer.draw_filled_circle_world(wx1, wy1, ht, col);
        renderer.draw_filled_circle_world(wx2, wy2, ht, col);
    }

    if (enemy_weapon_model) {
        render_body_model(renderer, *enemy_weapon_model, ox, oy, enemy_facing_right, enemy_node_pos, 1.0f);
    }
}

// ---------- Punching bag rendering ----------

void SceneRenderer::render_punching_bag(
    ren::Renderer& renderer,
    const std::unique_ptr<BodyModel>& bag_model,
    const std::unordered_map<std::string, VerletNode>& bag_verlet,
    float enemy_pos_x, float enemy_pos_y,
    float /*zoom*/
) {
    if (!bag_model) return;
    // Verlet nodes as circles
    for (auto& [name, vn] : bag_verlet) {
        float wx = enemy_pos_x + vn.x, wy = enemy_pos_y + vn.y;
        ren::Color4B col = vn.fixed ? ren::Color4B{200,200,200,255} : ren::Color4B{100,180,100,200};
        renderer.draw_filled_circle_world(wx, wy, vn.fixed ? 6.0f : 3.0f, col);
    }
}

} // namespace resf2::game
