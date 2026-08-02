#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/platform/platform.hpp"
#include "engine/renderer/renderer.hpp"
#include "engine/reverse/plist_atlas.hpp"
#include "engine/reverse/bitmap_font.hpp"
#include "engine/reverse/dz_reader.hpp"
#include "engine/format/xml_doc.hpp"
#include "engine/format/stage_parser.hpp"
#include "engine/format/list_parser.hpp"
#include "engine/audio/audio.hpp"
#include "types.hpp"

namespace resf2::game {

namespace plat = resf2::platform;
namespace ren = resf2::renderer;
namespace plist = resf2::reverse::plist;
namespace font = resf2::reverse::font;
namespace fmt = resf2::format;
namespace aud = resf2::audio;

// ---------- Asset manager ----------
//
// Encapsulates all asset loading: textures, animations, skeletons,
// body models, moves, fonts, sounds, and location data.

class AssetManager {
public:
    AssetManager() = default;

    // Texture / atlas caches
    std::unordered_map<std::string, AtlasRef>& atlases() { return atlases_; }
    const std::unordered_map<std::string, AtlasRef>& atlases() const { return atlases_; }

    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& hud_textures() { return hud_textures_; }
    const std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& hud_textures() const { return hud_textures_; }

    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& menu_textures() { return menu_textures_; }
    const std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& menu_textures() const { return menu_textures_; }

    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& scroll_textures() { return scroll_textures_; }
    const std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& scroll_textures() const { return scroll_textures_; }

    std::unordered_map<int, std::unique_ptr<ren::Texture2D>>& zone_bg_textures() { return zone_bg_textures_; }
    const std::unordered_map<int, std::unique_ptr<ren::Texture2D>>& zone_bg_textures() const { return zone_bg_textures_; }

    // [ORIGINAL] Map-screen art. Battle nodes come from
    // assets/1536/image/battles/{base,active,locked}/batchBattles*.plist —
    // one frame per battle kind ("base_tournament", "active_lynx", ...). The
    // per-location photos next to them (arena.jpg, lynx.jpg, ...) are the
    // preview shown in the side scroll.
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& map_icon_textures() { return map_icon_textures_; }
    const std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& map_icon_textures() const { return map_icon_textures_; }
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>>& battle_preview_textures() { return battle_preview_textures_; }

    // Animation data cache
    std::unordered_map<std::string, resf2::game::AnimationData>& animations() { return animations_; }
    const std::unordered_map<std::string, resf2::game::AnimationData>& animations() const { return animations_; }

    // Move definitions
    std::unordered_map<std::string, resf2::game::MoveDef>& moves() { return moves_; }
    const std::unordered_map<std::string, resf2::game::MoveDef>& moves() const { return moves_; }

    // Skeleton
    std::unordered_map<std::string, resf2::game::SkelNode>& skeleton_nodes() { return skeleton_nodes_; }
    const std::unordered_map<std::string, resf2::game::SkelNode>& skeleton_nodes() const { return skeleton_nodes_; }
    std::unordered_map<std::string, resf2::game::SkelEdge>& skeleton_edges() { return skeleton_edges_; }
    const std::unordered_map<std::string, resf2::game::SkelEdge>& skeleton_edges() const { return skeleton_edges_; }
    std::vector<std::string>& ordered_node_names() { return ordered_node_names_; }
    const std::vector<std::string>& ordered_node_names() const { return ordered_node_names_; }

    // Body models
    std::unique_ptr<resf2::game::BodyModel>& body_model() { return body_model_; }
    const std::unique_ptr<resf2::game::BodyModel>& body_model() const { return body_model_; }
    std::unique_ptr<resf2::game::BodyModel>& bag_model() { return bag_model_; }
    const std::unique_ptr<resf2::game::BodyModel>& bag_model() const { return bag_model_; }
    std::unique_ptr<resf2::game::BodyModel>& weapon_model() { return weapon_model_; }
    const std::unique_ptr<resf2::game::BodyModel>& weapon_model() const { return weapon_model_; }
    std::unique_ptr<resf2::game::BodyModel>& enemy_weapon_model() { return enemy_weapon_model_; }
    const std::unique_ptr<resf2::game::BodyModel>& enemy_weapon_model() const { return enemy_weapon_model_; }
    std::unique_ptr<resf2::game::BodyModel>& armor_model() { return armor_model_; }
    const std::unique_ptr<resf2::game::BodyModel>& armor_model() const { return armor_model_; }
    std::unique_ptr<resf2::game::BodyModel>& helm_model() { return helm_model_; }
    const std::unique_ptr<resf2::game::BodyModel>& helm_model() const { return helm_model_; }

    // Loading screen images
    std::vector<resf2::game::LoadingImg>& loading_images() { return loading_images_; }
    const std::vector<resf2::game::LoadingImg>& loading_images() const { return loading_images_; }

    // HUD font
    std::shared_ptr<font::ParsedFont>& hud_font() { return hud_font_; }
    const std::shared_ptr<font::ParsedFont>& hud_font() const { return hud_font_; }
    std::unique_ptr<ren::Texture2D>& hud_font_tex() { return hud_font_tex_; }
    const std::unique_ptr<ren::Texture2D>& hud_font_tex() const { return hud_font_tex_; }

    // NPivot Y baseline
    float stance_npivot_y() const { return stance_npivot_y_; }
    void set_stance_npivot_y(float y) { stance_npivot_y_ = y; }

    // Damage settings (from internalSettings.xml)
    DamageSettings& damage_settings() { return damage_settings_; }
    const DamageSettings& damage_settings() const { return damage_settings_; }

    // Stage data
    fmt::StageData& stage_data() { return stage_data_; }
    const fmt::StageData& stage_data() const { return stage_data_; }
    bool stages_loaded() const { return stages_loaded_; }
    void set_stages_loaded(bool l) { stages_loaded_ = l; }

    // Loading methods
    void load_atlas(const std::string& name, const std::string& location, const std::string& asset_root);
    void load_hud_textures(const std::string& asset_root);
    void load_menu_textures(const std::string& asset_root);
    void load_loading_screen(const std::string& asset_root, int window_w, int window_h);
    void load_skeleton(const std::string& asset_root, const std::string& location);
    void load_body_model(const std::string& asset_root, const std::string& location, bool is_bag = false);
    void load_punching_bag_model(const std::string& asset_root);
    void load_enemy_weapon(const std::string& weapon_name, const std::string& asset_root);
    void load_player_weapon(const std::string& tactic, const std::string& asset_root);
    void load_armor_model(const std::string& model_file, const std::string& asset_root);
    void load_helm_model(const std::string& model_file, const std::string& asset_root);
    void load_animations(const std::string& asset_root);
    void load_moves(const std::string& asset_root);
    void load_hud_font(const std::string& asset_root);
    void load_stages(const std::string& asset_root);
    void load_sounds(const std::string& asset_root);
    void load_internal_settings(const std::string& asset_root);

    // Animation loading (single .bin file)
    void load_animation(const std::string& anim_name, const std::string& asset_root, const std::string& search_dir = "");

    // Sound loading
    static void load_sound(const std::string& name, const std::string& asset_root);

    // Render loading screen
    void render_loading_screen(ren::Renderer& renderer, plat::Platform& platform, float progress, float load_scale);

    // Clear atlases for location reload
    void clear_atlases() { atlases_.clear(); }

    // Map weapon tactic name to model file
    std::string weapon_tactic_to_model_file(const std::string& tactic) const;

    // Parse one moves.xml document into moves_ (load_moves reads the file
    // and delegates; the tests feed the device reference file directly).
    void parse_moves_xml(const std::string& xml);

public:
    // Parse body/head model XML into a BodyModel
    void parse_body_model_xml(const std::string& xml, resf2::game::BodyModel* model, const std::string& tag_prefix);

    // Load a single texture atlas into hud_textures_ (used by load_hud_textures)
    void load_texture_atlas_to_hud(const std::filesystem::path& dir, const std::string& atlas_name);

    // Texture/atlas caches
    std::unordered_map<std::string, AtlasRef> atlases_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> hud_textures_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> menu_textures_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> scroll_textures_;
    std::unordered_map<int, std::unique_ptr<ren::Texture2D>> zone_bg_textures_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> map_icon_textures_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> battle_preview_textures_;

    // Animation cache
    std::unordered_map<std::string, resf2::game::AnimationData> animations_;

    // Move definitions
    std::unordered_map<std::string, resf2::game::MoveDef> moves_;

    // Skeleton
    std::unordered_map<std::string, resf2::game::SkelNode> skeleton_nodes_;
    std::unordered_map<std::string, resf2::game::SkelEdge> skeleton_edges_;
    std::vector<std::string> ordered_node_names_;

    // Body models
    std::unique_ptr<resf2::game::BodyModel> body_model_;
    std::unique_ptr<resf2::game::BodyModel> bag_model_;
    std::unique_ptr<resf2::game::BodyModel> weapon_model_;
    std::unique_ptr<resf2::game::BodyModel> enemy_weapon_model_;
    std::unique_ptr<resf2::game::BodyModel> armor_model_;
    std::unique_ptr<resf2::game::BodyModel> helm_model_;

    // Loading screen images
    std::vector<resf2::game::LoadingImg> loading_images_;

    // HUD font
    std::shared_ptr<font::ParsedFont> hud_font_;
    std::unique_ptr<ren::Texture2D> hud_font_tex_;

    // Stage data
    fmt::StageData stage_data_;
    bool stages_loaded_ = false;

    // NPivot Y from skeleton rest-pose (set by load_skeleton)
    float stance_npivot_y_ = 106.0f;

    // Damage settings from internalSettings.xml
    DamageSettings damage_settings_;
};

} // namespace resf2::game
