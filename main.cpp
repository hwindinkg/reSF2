// main.cpp
//
// Interactive GLFW-based reSF2 engine driver. Implements the full game
// boot sequence for the Dojo location:
//
//   1. Loading screen (startLoading.xml assets)
//   2. Dojo location (params.xml + parallax background layers)
//   3. Player character (skeletal stick figure from skeleton.xml)
//   4. Punching bag at the enemy position
//   5. HUD overlay (money, energy, level, menu button)
//   6. Menu overlay (Map / Shop / Settings / Save / Exit)
//   7. Story dialog overlay (intro line from Sensei)
//
// Controls:
//   A / D  or  Left / Right   Move player
//   W / S  or  Up   / Down    Move camera (debug)
//   Space                     Hit (visual feedback on punching bag)
//   M  or  click menu button  Toggle menu overlay
//   T                         Toggle dialog overlay
//   1 / 2 / 3                 Zoom presets
//   Esc                       Quit (or close menu if open)

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>

#include "engine/platform/platform.hpp"
#include "engine/platform/glfw_platform.hpp"
#include "engine/runtime/loop.hpp"
#include "engine/renderer/renderer.hpp"
#include "engine/reverse/plist_atlas.hpp"
#include "engine/reverse/bitmap_font.hpp"
#include "engine/reverse/dz_reader.hpp"
#include "engine/scene/scene_system.hpp"
#include "engine/scene/scenes.hpp"
#include "engine/renderer/stb_image.h"
#include "engine/format/xml_doc.hpp"
#include "engine/audio/audio.hpp"

namespace plat = resf2::platform;
namespace rt = resf2::runtime;
namespace ren = resf2::renderer;
namespace fmt = resf2::format;
namespace aud = resf2::audio;
namespace plist = resf2::reverse::plist;
namespace font = resf2::reverse::font;
namespace scene = resf2::scene;

// ---------- Small helpers ----------

static std::vector<std::byte> read_file(const std::string& path) {
    // Try filesystem first
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (f) {
        auto sz = (size_t)f.tellg(); if (!sz) return {};
        f.seekg(0); std::vector<std::byte> d(sz);
        f.read((char*)d.data(), (std::streamsize)sz); return d;
    }
    // Try DZ archive
    auto& dz = resf2::dz::DzRegistry::instance();
    // Extract basename for DZ lookup
    auto p = std::filesystem::path(path);
    std::string basename = p.filename().string();
    if (dz.has_file(basename)) {
        return dz.read_file(basename);
    }
    return {};
}

static std::string read_text(const std::string& path) {
    auto d = read_file(path); return std::string((const char*)d.data(), d.size());
}

static std::string xml_attr(const std::string& tag, const std::string& attr) {
    auto pos = tag.find(attr + "=\""); if (pos == std::string::npos) return "";
    pos += attr.size() + 2; auto end = tag.find('"', pos);
    return tag.substr(pos, end - pos);
}

static float tof(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    try { return std::stof(s); } catch (...) { return def; }
}

static int toi(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

// Get the directory containing the executable. Used to find model XML files
// that are shipped with the repo (assets/models/) but may not be in the
// game's asset directory (they're inside files.dz which we can't read yet).
static std::filesystem::path get_exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

// Build a list of search paths for model XML files.
// Searches: asset_root/models/, asset_root/assets/models/,
//           exe_dir/../../assets/models/ (repo-local)
static std::vector<std::filesystem::path> model_paths(const std::string& asset_root, const char* filename) {
    namespace fs = std::filesystem;
    auto root = fs::path(asset_root);
    return {
        root / "models" / filename,
        root / "assets" / "models" / filename,
        root / "assets" / "assets" / "models" / filename,
        root / "assets" / "assets" / "assets" / "models" / filename,
    };
}

// ---------- Asset types ----------

struct AtlasRef {
    std::unique_ptr<ren::Texture2D> texture;
    std::shared_ptr<plist::ParsedAtlas> atlas;
    // Pre-cropped textures for rotated frames (un-rotated during crop)
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> cropped;
};

struct LayerImage {
    std::string atlas_name, class_name;
    float x = 0, y = 0, w = 0, h = 0;
    std::string color;
};

struct LocationLayer {
    int type = 0;
    float factor = 1.0f;
    std::string atlas_name;
    std::vector<LayerImage> images;
};

struct GameLocation {
    std::string color;
    float width = 0, height = 0;
    float wall = 0;      // Wall position from params.xml (distance from center to wall)
    float floor = 0;     // Floor position from params.xml
    float player_x = 0, player_y = 0;
    float enemy_x = 0, enemy_y = 0;
    std::vector<LocationLayer> layers;
};

struct SkelNode {
    std::string name;
    float x = 0, y = 0, z = 0;
};

struct SkelEdge {
    std::string name;
    std::string end1, end2;
    float radius = 0;
};

// ---------- Animation system ----------
// .bin format (VERIFIED from Gymnast-Tool-Suite Blender plugin source):
//
// File structure:
//   u32 frame_count              (LITTLE-ENDIAN)
//   frame_count * variable bytes (one record per frame)
//
// Each frame:
//   byte 0     : skip byte (type flag: 1=keyframe, 5=interframe)
//   bytes 1..4 : u32 node_count (LITTLE-ENDIAN)
//   bytes 5..  : node_count × 3 floats (X, Y, -Z), each LITTLE-ENDIAN f32
//
// Coordinate mapping (from plugin: struct.pack("fff", pos.x, pos.z, -pos.y)):
//   bin stores (blender.x, blender.z, -blender.y)
//   blender.x = game.X, blender.y = game.Z, blender.z = game.Y
//   So bin stores: (game.X, game.Y, -game.Z)
//
// Node order: ALL skeleton.xml nodes in XML order (54 Node + 1 COM + 12 MacroNode = 67)
// Body.xml nodes are NOT stored in .bin — they're derived from skeleton nodes at runtime.
//
// Positions are ABSOLUTE (world space). To get LOCAL positions, subtract
// NPivot's world position (NPivot is node index 18 in XML order).
// ---------- Move definition (from moves.xml) ----------
struct MoveDef {
    std::string name;
    std::string filename;
    std::string template_name;
    int first_frame = 0;
    int end_frame = 0;
    int priority = 0;
    
    // Attack interval (frames where hit detection is active)
    int attack_start = -1;
    int attack_end = -1;
    
    // Attack edges (body parts that deal damage)
    std::vector<std::string> attack_edges;
    
    // Damage value
    float damage = 0.0f;

    // Impulse (from <Impulse X="..." Y="..."/> in Attack interval)
    // Applied to the target on hit. X = horizontal push, Y = vertical lift.
    float impulse_x = 0.0f;
    float impulse_y = 0.0f;
    
    // Block interval (can block during these frames)
    int block_start = -1;
    
    // Uninterrupt interval (can't be interrupted)
    int uninterrupt_start = -1;
    int uninterrupt_end = -1;
    
    // Key combination
    std::vector<std::string> key_types;  // "Punch", "Kick", "Forward", etc.
    
    // Parsed from Template string
    int key_count = 0;           // 1, 2, or 3 (from "1key", "2key", "3key")
    std::string direction;        // "Central", "Forward", "Back", "Up", "Down", 
                                  // "UpForward", "UpBack", "DownForward", "DownBack", ""
    std::string move_type;        // "Punch", "Kick", "Jump", "Move", "Retreat", "Step", etc.
    std::string weapon_filter;    // "Unarmed", "Weapon", "" (empty = any)
    bool is_unarmed = false;      // Template contains "Unarmed"
    bool is_jump = false;         // Template contains "Jump"
    bool is_retreat = false;      // Template contains "Retreat"
    bool is_step = false;         // Template contains "Step"
    bool is_double_step = false;  // Template contains "DoubleStep"
    bool is_block = false;        // Template contains "Block"
    bool is_stance = false;       // Template contains "Stance"
    bool is_idle = false;         // Template contains "Idle"
    bool is_not_titan = false;    // Template contains "NotTitan"
    std::string tactic_weapon;    // TacticWeapon attribute (e.g. "Fists")

    // Distance condition (from <Distance Min=".." Max=".." Axis="X">)
    // Used to pick correct move based on enemy distance.
    // Example: LowKick (Max=100) vs Sweep (Max=300) — pick by distance.
    float distance_min = 0.0f;
    float distance_max = 0.0f;  // 0 = no limit
    bool has_distance_cond = false;

    // Locks (from <Locks> section)
    // Perk required (e.g. PERK_DOUBLE_SWEEP). Empty = no perk required.
    std::string required_perk;
    // Weapon subtype required (e.g. "Fists"). Empty = any weapon.
    std::string required_weapon_subtype;

    // [ORIGINAL] MoveInside pivot alignment (from <Align><Pivot .../></Align>
    // in moves.xml). Binary-verified: fcn.10165c10 reads
    //   Model[0x20] -> animationInfo
    //   animationInfo[0x94] -> moveInside
    //   moveInside[0x70] -> align.pivotID
    //   Model[0x5c] <- node_array[pivotID].Y   (align_y)
    //   Model[0x58] <- pivotID                 (cached)
    // When pivotID == -1 (Pivot Object="Animation"), align_y = 0.
    // moves.xml <Pivot Object="Nodes" Part="NHeel_2"/> names the node;
    // <Pivot Object="Animation"/> means no node alignment.
    // [HEURISTIC-TODO] the exact formula that consumes align_y (Model[0x5c])
    // to produce render Y is NOT yet byte-confirmed end-to-end; the per-frame
    // y_adjust below is an approximation (ground the named pivot node to
    // floor_y). See docs/s3e_reverse_engineering.md "MoveInside".
    std::string moveinside_pivot_node;  // e.g. "NHeel_2"; empty = Object="Animation"
    bool moveinside_is_animation = false;  // <Pivot Object="Animation"/>

    // [ORIGINAL] CurrentAnimation condition from moves.xml <Conditions>.
    // For 3key combos: <CurrentAnimation Name="HeavyPunch"/> means this move
    // can ONLY trigger when HeavyPunch is the currently playing animation.
    // Empty = no CurrentAnimation condition (1key/2key moves).
    std::string required_current_animation;

    // [ORIGINAL] Type from moves.xml Type="ATTACK" or Type="MOVE".
    // ATTACK = combat move (punch/kick), MOVE = non-combat (duck/stance/step).
    // Used to distinguish in_basic_attack (only ATTACK moves block 1key/2key).
    bool is_attack = false;

    // [ORIGINAL] Full interval lists parsed via engine/format/xml_doc.hpp
    // (replaces the lossy string-based scan that only captured the FIRST
    // Attack/Uninterrupt interval via `break`). The scalar fields above
    // (attack_start/end, uninterrupt_start/end, block_start) are kept for
    // backward compatibility and populated from the first element of these
    // vectors. Combat logic should migrate to these vectors for 1:1 timing.
    struct Interval {
        std::string type;   // "Attack", "Block", "Uninterrupt", "Complex", ...
        std::string name;   // Name attribute (e.g. "Attack", "Uninterrupt")
        float start = 0;
        float end = 0;
        int damage = 0;
        float impulse_x = 0;
        float impulse_y = 0;
        std::string hit_type;
        std::vector<std::string> edges;
        // ComplexInterval conditions (Type="Complex")
        std::string condition_anim;     // CurrentAnimation condition inside interval
    };
    std::vector<Interval> intervals;
};

struct AnimationData {
    std::string name;
    int frame_count = 0;
    // frames[fi][i] = (X, Y, Z) in game coords for node i at frame fi
    // Z is already converted from -Z to Z
    std::vector<std::vector<std::tuple<float,float,float>>> node_positions;

    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return false;
        auto sz = (size_t)f.tellg();
        if (sz < 4) return false;
        f.seekg(0);
        std::vector<uint8_t> data(sz);
        f.read((char*)data.data(), sz);

        frame_count = read_u32_le(data.data(), 0);
        if (frame_count <= 0 || frame_count > 10000) return false;

        node_positions.resize(frame_count);
        size_t offset = 4;
        for (int fi = 0; fi < frame_count; ++fi) {
            if (offset + 5 > sz) break;
            uint8_t skip = data[offset];
            uint32_t nc = read_u32_le(data.data(), offset + 1);
            offset += 5;
            
            auto& nodes = node_positions[fi];
            nodes.reserve(nc);
            for (uint32_t i = 0; i < nc; ++i) {
                if (offset + 12 > sz) break;
                float fx, fy, fneg_z;
                memcpy(&fx, &data[offset], 4);
                memcpy(&fy, &data[offset + 4], 4);
                memcpy(&fneg_z, &data[offset + 8], 4);
                // bin stores (game.X, game.Y, -game.Z)
                nodes.push_back({fx, fy, -fneg_z});
                offset += 12;
            }
        }
        return !node_positions.empty();
    }

    // Get animated (X, Y, Z) for node `idx` at frame `fi` (absolute world coords)
    bool get_node_pos(int fi, int idx, float& x, float& y, float& z) const {
        if (fi < 0 || fi >= (int)node_positions.size()) return false;
        const auto& nodes = node_positions[fi];
        if (idx < 0 || idx >= (int)nodes.size()) return false;
        auto [nx, ny, nz] = nodes[idx];
        x = nx; y = ny; z = nz;
        return true;
    }

    static uint32_t read_u32_le(const uint8_t* p, size_t off) {
        return (uint32_t)p[off] | ((uint32_t)p[off+1] << 8) |
               ((uint32_t)p[off+2] << 16) | ((uint32_t)p[off+3] << 24);
    }
};

struct BodyNode {
    std::string name;
    float x = 0, y = 0, z = 0;
    float mass = 1.0f;
    bool fixed = false;
    float attenuation = 0.02f;  // damping coefficient
    bool cloth = false;
};

struct BodyEdge {
    std::string name;
    std::string end1, end2;
    float length = 0;
    float radius = 0;
    bool collisible = false;
};

// Verlet physics state for a single node.
// Verlet integration: pos_new = 2*pos - pos_prev + acc * dt^2
// No explicit velocity — velocity is implicit (pos - pos_prev).
struct VerletNode {
    float x = 0, y = 0;       // current position
    float px = 0, py = 0;     // previous position (for Verlet integration)
    float mass = 1.0f;
    float inv_mass = 1.0f;    // 1/mass (0 if fixed)
    bool fixed = false;
    float attenuation = 0.02f;
};

struct VerletConstraint {
    std::string n1, n2;
    float length = 0;     // rest length
    float stiffness = 1.0f;  // 1.0 = rigid, <1.0 = soft
};

struct BodyCapsule {
    std::string edge_name;
    float radius1 = 0, radius2 = 0;
    float margin1 = 0, margin2 = 0;
};

struct BodyMacroNode {
    std::string name;
    std::string children[4];
    float lcc[4] = {};
};

struct BodyTriangle {
    std::string n1, n2, n3;
};

struct BodyModel {
    std::unordered_map<std::string, BodyNode> nodes;
    std::unordered_map<std::string, BodyMacroNode> macro_nodes;
    std::vector<BodyEdge> edges;
    std::vector<BodyCapsule> capsules;
    std::vector<BodyTriangle> triangles;
};

struct LoadingImg {
    std::unique_ptr<ren::Texture2D> texture;
    float x = 0, y = 0;
};

// ---------- Game states ----------
//
// The old monolithic GameState { Loading, Location } has been replaced by
// the scene::SceneManager (see engine/scene/scene_system.hpp). The enum is
// kept only for the Overlay system, which is a sub-state within the
// MainMenu/Battle scenes.

enum class Overlay { None, Menu, Dialog };

// ---------- Game ----------
//
// The Game class is the SceneHost — it owns the SceneManager and implements
// the scene::SceneHost interface. Individual scenes (MainMenu, Battle, Map,
// etc.) call back into Game via the host interface to load assets, render
// the dojo, save progress, etc.

class Game final : public rt::IGame, public scene::SceneHost {
public:
    explicit Game(std::string asset_root, bool replay_mode = false, bool dump_state = false)
        : asset_root_(std::move(asset_root)), replay_mode_(replay_mode), dump_state_(dump_state) {}

    void on_init(plat::Platform& platform) override {
        platform_ = &platform;
        std::printf("reSF2 initialized.\n");
        std::printf("Controls (original SF2 layout):\n");
        std::printf("  W/A/S/D     - Up / Left / Down / Right (movement + attack direction)\n");
        std::printf("  O           - Punch (W=upper, S=low, D=heavy, A=spinning, S+A=elbow)\n");
        std::printf("  P           - Kick (S=sweep, D=front, A=back, S+D=dodge reverse)\n");
        std::printf("  W           - Jump (W+D=front flip, W+A=back flip)\n");
        std::printf("  S+D / S+A   - Forward roll / Back roll\n");
        std::printf("  S (hold)    - Duck (crouch)\n");
        std::printf("  Block       - AUTOMATIC (when idle, not attacking)\n");
        std::printf("  M or click menu - Toggle menu\n");
        std::printf("  T           - Toggle dialog\n");
        std::printf("  N           - New game (go to Map)\n");
        std::printf("  Y/L         - Declare victory/defeat (debug, in Battle)\n");
        std::printf("  1/2/3       - Zoom presets\n");
        std::printf("  Esc         - Quit / close overlay / back\n\n");

        renderer_ = std::make_unique<ren::Renderer>();
        if (!renderer_->init(platform.window_width(), platform.window_height())) {
            renderer_.reset(); return;
        }
        renderer_->set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

        // Load loading screen textures (used by LoadingScene via render_loading_screen)
        if (!asset_root_.empty()) load_loading_screen();

        // Register all scenes with the SceneManager
        scene_manager_.register_scene(scene::SceneId::Boot,
            [] { return std::make_unique<scene::BootScene>(); });
        scene_manager_.register_scene(scene::SceneId::Loading,
            [] { return std::make_unique<scene::LoadingScene>(); });
        scene_manager_.register_scene(scene::SceneId::MainMenu,
            [] { return std::make_unique<scene::MainMenuScene>(); });
        scene_manager_.register_scene(scene::SceneId::Map,
            [] { return std::make_unique<scene::MapScene>(); });
        scene_manager_.register_scene(scene::SceneId::Shop,
            [] { return std::make_unique<scene::ShopScene>(); });
        scene_manager_.register_scene(scene::SceneId::Settings,
            [] { return std::make_unique<scene::SettingsScene>(); });
        scene_manager_.register_scene(scene::SceneId::Dialogue,
            [] { return std::make_unique<scene::DialogueScene>(); });
        scene_manager_.register_scene(scene::SceneId::Battle,
            [] { return std::make_unique<scene::BattleScene>(); });
        scene_manager_.register_scene(scene::SceneId::Results,
            [] { return std::make_unique<scene::ResultsScene>(); });

        // Start the scene flow at Boot
        scene::SceneContext ctx{*this, platform, *renderer_, 0};
        scene_manager_.start(scene::SceneId::Boot, ctx);
    }

    void on_update(plat::Platform& platform, uint32_t dt) override {
        if (!renderer_) return;

        // Build the scene context and delegate to the SceneManager.
        // The current scene's on_update handles all input and game logic.
        // For MainMenu/Battle, the scene calls host_update_gameplay() which
        // contains the movement, combat, and animation code.
        scene::SceneContext ctx{*this, platform, *renderer_, dt};
        scene_manager_.update(ctx);
    }

    void on_render(plat::Platform& platform) override {
        if (!renderer_) return;
        renderer_->begin_frame();
        scene::SceneContext ctx{*this, platform, *renderer_, 0};
        scene_manager_.render(ctx);
        renderer_->end_frame();
    }

    void on_shutdown(plat::Platform&) override {
        if (renderer_) renderer_->shutdown();
    }

    bool quit_requested() const noexcept { return quit_requested_; }

    // ---------- scene::SceneHost implementation ----------
    //
    // These methods are called by the scenes (MainMenu, Battle, etc.) via
    // the SceneHost interface to interact with the game state.

    void request_scene_transition(scene::SceneId to) override {
        // In replay mode, skip the menu flow: Loading → direct to Battle
        if (replay_mode_ && to == scene::SceneId::MainMenu) {
            to = scene::SceneId::Battle;
            std::printf("[REPLAY] Skipping menus, entering Battle directly\n");
        }
        scene_manager_.transition_to(to);
    }

    void host_load_location() override {
        if (!location_loaded_) {
            init_location();
        }
    }

    bool host_location_loaded() const noexcept override {
        return location_loaded_;
    }

    bool host_save_progress() override {
        // Minimal save: write a JSON file next to the executable with
        // completed levels and currency. This is a stub — the real save
        // format will be determined once the story/progress asset format
        // is decoded from DZ archives.
        try {
            auto save_path = std::filesystem::temp_directory_path() / "resf2_save.json";
            std::ofstream f(save_path);
            if (!f) return false;
            f << "{\n";
            f << "  \"version\": 1,\n";
            f << "  \"current_level\": \"" << current_level_ << "\",\n";
            f << "  \"battle_result\": \"" << battle_result_ << "\",\n";
            f << "  \"completed_levels\": ["; 
            for (size_t i = 0; i < completed_levels_.size(); ++i) {
                if (i) f << ", ";
                f << "\"" << completed_levels_[i] << "\"";
            }
            f << "],\n";
            f << "  \"currency\": " << currency_ << "\n";
            f << "}\n";
            std::printf("[save] wrote %s\n", save_path.string().c_str());
            return true;
        } catch (...) {
            return false;
        }
    }

    bool host_load_progress() override {
        try {
            auto save_path = std::filesystem::temp_directory_path() / "resf2_save.json";
            if (!std::filesystem::exists(save_path)) return false;
            std::printf("[save] found %s (loading not yet implemented)\n",
                        save_path.string().c_str());
            // TODO: parse JSON and restore state
            return true;
        } catch (...) {
            return false;
        }
    }

    void host_set_dialogue(std::vector<std::pair<std::string, std::string>> lines) override {
        dialogue_lines_ = std::move(lines);
        dialogue_index_ = 0;
    }

    void host_set_current_level(std::string level_id) override {
        current_level_ = std::move(level_id);
    }

    std::string host_get_battle_result() const override {
        return battle_result_;
    }

    // Called by MainMenuScene and BattleScene to update the dojo gameplay
    // (movement, combat, animation, physics, overlays).
    void host_update_gameplay(uint32_t dt) {
        // [DIAGNOSTIC] Advance input-script frame counter and apply events
        // scheduled for this frame BEFORE reading input. This keeps script
        // frame N aligned with gameplay frame N (Boot/Loading don't count).
        platform_->tick_input_script();
        const auto& input = platform_->input();
        float dt_sec = (float)dt / 1000.0f;

        // [ORIGINAL] Combat state update: decay hit flash, hit stun, invuln.
        if (player_hit_flash_ > 0) player_hit_flash_ = std::max(0.0f, player_hit_flash_ - dt_sec);
        if (enemy_hit_flash_ > 0) enemy_hit_flash_ = std::max(0.0f, enemy_hit_flash_ - dt_sec);
        if (player_fighter_.hit_stun_time > 0) player_fighter_.hit_stun_time = std::max(0.0f, player_fighter_.hit_stun_time - dt_sec);
        if (enemy_fighter_.hit_stun_time > 0) enemy_fighter_.hit_stun_time = std::max(0.0f, enemy_fighter_.hit_stun_time - dt_sec);
        if (player_fighter_.invuln_time > 0) player_fighter_.invuln_time = std::max(0.0f, player_fighter_.invuln_time - dt_sec);
        if (enemy_fighter_.invuln_time > 0) enemy_fighter_.invuln_time = std::max(0.0f, enemy_fighter_.invuln_time - dt_sec);
        // Update audio engine (mix + write to backend)
        aud::AudioEngine::instance().update(dt_sec);

        // [ORIGINAL] Player block: automatic when idle (not attacking, not moving).
        // Original SF2: block is automatic when standing still and not attacking.
        // key_down (S) = duck (low block); standing = high block.
        if (!player_fighter_.is_dead) {
            bool player_idle = (hit_anim_ == 0 && move_state_ == 0 &&
                                !start_stance_playing_);
            player_fighter_.is_blocking = player_idle;
        }

        // [ORIGINAL] Enemy AI: simple state machine.
        // States: 0=idle, 1=approach, 2=attack, 3=retreat, 4=block
        // Decisions every 0.8s: based on distance to player + randomness.
        if (!enemy_fighter_.is_dead && !player_fighter_.is_dead) {
            enemy_ai_timer_ += dt_sec;
            enemy_attack_cooldown_ = std::max(0.0f, enemy_attack_cooldown_ - dt_sec);
            if (enemy_fighter_.hit_stun_time > 0) {
                // Stunned — can't act
                enemy_anim_ = "fists_hit";
            } else if (enemy_ai_timer_ >= enemy_ai_decision_interval_) {
                enemy_ai_timer_ = 0;
                float dist = std::abs(enemy_pos_x_ - player_pos_x_);
                int r = std::rand() % 100;
                if (dist > 250) {
                    enemy_ai_state_ = 1;  // approach
                } else if (dist < 120) {
                    if (r < 30) enemy_ai_state_ = 2;  // attack
                    else if (r < 50) enemy_ai_state_ = 3;  // retreat
                    else if (r < 70) enemy_ai_state_ = 4;  // block
                    else enemy_ai_state_ = 0;  // idle
                } else {
                    if (r < 40) enemy_ai_state_ = 2;  // attack
                    else if (r < 60) enemy_ai_state_ = 1;  // approach
                    else enemy_ai_state_ = 0;  // idle
                }
            }
            // Execute current AI state
            enemy_fighter_.is_blocking = (enemy_ai_state_ == 4);
            float enemy_speed = 90.0f;
            if (enemy_ai_state_ == 1) {  // approach
                if (enemy_pos_x_ > player_pos_x_) enemy_pos_x_ -= enemy_speed * dt_sec;
                else enemy_pos_x_ += enemy_speed * dt_sec;
                enemy_anim_ = "step_forward";
                enemy_facing_right_ = (player_pos_x_ > enemy_pos_x_);
            } else if (enemy_ai_state_ == 3) {  // retreat
                if (enemy_pos_x_ < player_pos_x_) enemy_pos_x_ -= enemy_speed * dt_sec;
                else enemy_pos_x_ += enemy_speed * dt_sec;
                enemy_anim_ = "step_back";
            } else if (enemy_ai_state_ == 2 && enemy_attack_cooldown_ <= 0) {  // attack
                enemy_anim_ = "high_punch";
                enemy_attacking_ = true;
                enemy_attack_duration_ = 0.4f;
                enemy_attack_cooldown_ = 1.5f;
                play_sound("f_pl_attack2", 0.4f);
                // [ORIGINAL] Enemy attack hits player if in range and player not blocking/invuln
                float dist = std::abs(enemy_pos_x_ - player_pos_x_);
                if (dist < 180 && player_fighter_.invuln_time <= 0 && !player_fighter_.is_dead) {
                    float dmg = 6.0f;
                    if (player_fighter_.is_blocking) {
                        dmg *= 0.25f;
                    } else {
                        player_hit_flash_ = 0.3f;
                    }
                    player_fighter_.health -= dmg;
                    player_fighter_.is_hit = true;
                    player_fighter_.hit_stun_time = 0.25f;
                    player_fighter_.invuln_time = 0.3f;
                    player_fighter_.hits_taken++;
                    enemy_fighter_.hits_landed++;
                    enemy_fighter_.energy = std::min(enemy_fighter_.max_energy,
                        enemy_fighter_.energy + dmg * 0.5f);
                    play_sound("armor", 0.5f);
                    if (player_fighter_.health <= 0) {
                        player_fighter_.health = 0;
                        player_fighter_.is_dead = true;
                        battle_result_ = "defeat";
                        play_sound("bodyfall3", 0.9f);
                        std::printf("[COMBAT] Player defeated! battle_result=defeat\n");
                    }
                }
            } else if (enemy_ai_state_ == 4) {  // block
                enemy_anim_ = "fists_block";
            } else {  // idle
                enemy_anim_ = "fists_idle";
            }
            if (enemy_attacking_) {
                enemy_attack_duration_ -= dt_sec;
                if (enemy_attack_duration_ <= 0) enemy_attacking_ = false;
            }
            enemy_anim_time_ += dt_sec;
            // Face the player
            enemy_facing_right_ = (player_pos_x_ > enemy_pos_x_);
        }

        // R: restart battle (after victory/defeat)
        if (input.keys_just_pressed[(size_t)plat::Key::R]) {
            if (player_fighter_.is_dead || enemy_fighter_.is_dead) {
                player_fighter_ = FighterState{};
                enemy_fighter_ = FighterState{};
                battle_result_.clear();
                std::printf("[COMBAT] Battle restarted\n");
            }
        }

        // Esc: close overlay if open, else request quit (handled by scene)
        if (input.keys_just_pressed[(size_t)plat::Key::Escape]) {
            if (overlay_ != Overlay::None) overlay_ = Overlay::None;
            else quit_requested_ = true;
        }
        // M: toggle menu
        if (input.keys_just_pressed[(size_t)plat::Key::M]) {
            overlay_ = (overlay_ == Overlay::Menu) ? Overlay::None : Overlay::Menu;
        }
        // T: toggle dialog
        if (input.keys_just_pressed[(size_t)plat::Key::T]) {
            overlay_ = (overlay_ == Overlay::Dialog) ? Overlay::None : Overlay::Dialog;
        }

        // Click: check menu button (left side)
        for (const auto& p : input.pointers) {
            if (p.just_pressed) {
                float btn_x = 10.0f, btn_y = 58.0f, btn_w = 130.0f, btn_h = 40.0f;
                if (p.x >= btn_x && p.x <= btn_x + btn_w &&
                    p.y >= btn_y && p.y <= btn_y + btn_h) {
                    overlay_ = (overlay_ == Overlay::Menu) ? Overlay::None : Overlay::Menu;
                }
            }
        }

        // Animate menu expand/collapse (300ms transition)
        float target_progress = (overlay_ == Overlay::Menu) ? 1.0f : 0.0f;
        float anim_speed = 1000.0f / 300.0f;
        if (menu_anim_progress_ < target_progress) {
            menu_anim_progress_ += (float)dt / anim_speed;
            if (menu_anim_progress_ > target_progress) menu_anim_progress_ = target_progress;
        } else if (menu_anim_progress_ > target_progress) {
            menu_anim_progress_ -= (float)dt / anim_speed;
            if (menu_anim_progress_ < target_progress) menu_anim_progress_ = target_progress;
        }

        // === DYNAMIC FACING ===
        // Character faces the enemy. Update facing DYNAMICALLY during step
        // [ORIGINAL] PC source: sf2.js — facing is locked during root-motion
        // moves (roll, jump, flip, attack). The original game only updates
        // facing during idle/step states, not during special moves.
        // Our root-motion whitelist (is_root_motion_anim) determines which
        // animations lock facing. This prevents instant flip during roll.
        bool facing_locked = hit_anim_ > 0 ||
            current_anim_ == "forward_roll" || current_anim_ == "back_roll" ||
            current_anim_ == "jump" || current_anim_ == "jump_away" ||
            current_anim_ == "front_flip" || current_anim_ == "back_flip" ||
            current_anim_ == "back_handflip";
        if (location_ && !facing_locked) {
            float bag_x = location_->enemy_x - 983.0f;
            bool should_face_right = (bag_x >= player_pos_x_);
            float dist_to_enemy = std::abs(bag_x - player_pos_x_);
            if (dist_to_enemy > 30.0f) {
                facing_right_ = should_face_right;
            }
        }

        // === INPUT: original SF2 controls ===
        // W=up, A=left, S=down, D=right (absolute directions)
        // O=punch, P=kick
        // Direction keys are interpreted RELATIVE to facing:
        //   If facing right: D=Forward, A=Back
        //   If facing left:  A=Forward, D=Back
        bool key_up = input.keys_down[(size_t)plat::Key::W] ||
                      input.keys_down[(size_t)plat::Key::ArrowUp];
        bool key_down = input.keys_down[(size_t)plat::Key::S] ||
                        input.keys_down[(size_t)plat::Key::ArrowDown];
        bool key_left = input.keys_down[(size_t)plat::Key::A] ||
                        input.keys_down[(size_t)plat::Key::ArrowLeft];
        bool key_right = input.keys_down[(size_t)plat::Key::D] ||
                         input.keys_down[(size_t)plat::Key::ArrowRight];

        // Convert absolute directions to relative (Forward/Back)
        bool key_forward = facing_right_ ? key_right : key_left;
        bool key_back = facing_right_ ? key_left : key_right;

        bool punch_pressed = input.keys_just_pressed[(size_t)plat::Key::O];
        bool kick_pressed = input.keys_just_pressed[(size_t)plat::Key::P];
        // Also keep Space/K as fallback for testing
        if (input.keys_just_pressed[(size_t)plat::Key::Space]) punch_pressed = true;
        if (input.keys_just_pressed[(size_t)plat::Key::K]) kick_pressed = true;

        // Debug: log key state and what blocks input
        if (punch_pressed || kick_pressed) {
            std::printf("[KEY] %s%s pressed — hit_anim=%u is_uninterrupt=%d move_state=%d current_move='%s'\n",
                        punch_pressed ? "O" : "", kick_pressed ? "P" : "",
                        hit_anim_, is_uninterrupt_ ? 1 : 0, move_state_, current_move_.c_str());
        }
        // Note: Removed sticky key buffer — it caused unwanted repeat attacks.
        // GetAsyncKeyState is reliable; the original issue was elsewhere.

        // [HEURISTIC-TODO] step_min_played: invented 400ms threshold to prevent
        // tap-to-cancel of step animations. The original engine gates move
        // transitions via the Uninterrupt interval in moves.xml (each Move's
        // <Interval Name="Uninterrupt" Start=".." End=".."/>). Once combat
        // logic migrates to use MoveDef::intervals (the full interval vector
        // populated by the xml_doc pass), this 400ms heuristic should be
        // replaced by: `is_in_uninterrupt(current_move_, anim_time_)`.
        if (move_state_ == 1 || move_state_ == 2) {
            step_play_time_ += dt;
        } else {
            step_play_time_ = 0;
        }
        bool step_min_played = step_play_time_ >= 400;

        // [HEURISTIC-TODO] fwd_held_ms_/back_held_ms_: invented 200ms latch
        // for direction keys. The original engine reads key state per-frame
        // via the Marmalade keypad (dz_keypad_update_decompiled.c) with no
        // latch — combos are gated by CurrentAnimation conditions, not key
        // history. Remove this latch once combo logic uses MoveQuery with
        // required_current_animation from moves.xml <Conditions>.
        if (key_forward) fwd_held_ms_ = 200;
        else if (fwd_held_ms_ > 0) fwd_held_ms_ -= (int)dt;
        if (key_back) back_held_ms_ = 200;
        else if (back_held_ms_ > 0) back_held_ms_ -= (int)dt;

        // === DYNAMIC MOVE SELECTION (from moves.xml) ===
        // The engine reads ALL moves from moves.xml at load time, including
        // their Template strings (key_count, direction, move_type, etc.).
        // Here we match the current key state against loaded moves and
        // select the one with the highest priority.
        //
        // Key state:
        //   key_forward (D relative to facing) → Forward
        //   key_back (A relative to facing) → Back
        //   key_up (W) → Up
        //   key_down (S) → Down
        //   Combinations: Up+Forward=UpForward, Down+Back=DownBack, etc.
        //   punch_pressed (O) → Punch
        //   kick_pressed (P) → Kick
        //
        // Move type detection (from moves.xml templates):
        //   1key: Just tap Punch/Kick (no direction hold)
        //   2key: Hold direction + tap Punch/Kick (e.g. Sweep = S+P)
        //   3key: Combos — triggered when CURRENT animation is from a 1key
        //         or 2key basic attack. This is the "second tap" combo.
        //         Example: HeavyPunch (1key) → tap Punch again → DoublePunch (3key)
        //
        // Selection: find ALL moves matching (direction, move_type, key_count,
        // is_unarmed, TacticWeapon=Fists or empty). Pick highest priority.
        // For moves with <Distance> condition, only match if enemy is in range.

        // [ORIGINAL] PC source: sf2.js tKa() — player input is NOT gated by
        // Uninterrupt (which is AI-only, ocb/pcb at line 18770).
        // BUT: 1key/2key attacks CANNOT interrupt another 1key/2key attack.
        // Only 3key combos (which require CurrentAnimation=<specific attack>
        // in moves.xml) can interrupt. This is the REAL cancel window:
        //   - During attack animation (hit_anim_ > 0, current_anim_ is attack):
        //     → Only 3key combos allowed (in_basic_attack = true)
        //   - Outside attack (hit_anim_ == 0 or current_anim_ is idle/step):
//     → 1key/2key attacks allowed
        // Previous fix (5f392b0) removed is_uninterrupt_ entirely, allowing
        // ANY attack to interrupt ANY animation - regression.
        // This gate replaces the old is_uninterrupt_ check with the correct
        // logic: in_basic_attack blocks 1key/2key, allows only 3key.
        // [ORIGINAL] PC source: Pqb() (line 18769-18810) - move selection
        // During attack animation (elapsed < total_len):
        //   If in Uninterrupt interval: allow 3key chain attacks (YAa, Gea)
        //   Else: return 0 (NO moves available - not even movement)
        // After attack ends: normal move selection (1key, 2key, movement)
        //
        // In our code: move_state_ == 10 means in attack/special.
        // hit_anim_ > 0 means attack animation playing.
        // current_move_ tracks the move name.
        // is_uninterrupt_ is true only during attack animation's Uninterrupt interval.
        bool in_attack = (move_state_ == 10 && current_move_ != "StartStance" && hit_anim_ > 0);
        {
        std::string cur_direction;
        std::string cur_move_type;
        float dist_to_enemy;

        if (punch_pressed || kick_pressed) {
            // Determine direction from key state
            cur_direction = "Central";  // default: no direction
            if (key_up && key_forward) cur_direction = "UpForward";
            else if (key_up && key_back) cur_direction = "UpBack";
            else if (key_down && key_forward) cur_direction = "DownForward";
            else if (key_down && key_back) cur_direction = "DownBack";
            else if (key_up) cur_direction = "Up";
            else if (key_down) cur_direction = "Down";
            else if (key_forward) cur_direction = "Forward";
            else if (key_back) cur_direction = "Back";

            // Determine move type
            if (punch_pressed) cur_move_type = "Punch";
            else if (kick_pressed) cur_move_type = "Kick";

            // Compute distance to enemy (bag) for distance-based move selection
            dist_to_enemy = 1000.0f;  // default: far
            if (location_ && bag_model_) {

            // If in attack and NOT in Uninterrupt: block ALL combat input (Pqb returns 0)
            // Original: if (this.$x < b.kJ() && !de.Ycb(b)) if (!this.ds.pcb(this.Fl)) return 0;
            bool block_all_combat = in_attack && !is_uninterrupt_;
            const MoveDef* best_move = nullptr;
            int candidate_count = 0;  // [DIAGNOSTIC] for [INPUT_DECISION] log
            for (auto& [name, move] : moves_) {
                // Skip moves with no filename or no template
                if (move.filename.empty() || move.template_name.empty()) continue;
                // Skip Titan moves (player is not a Titan)
                {
                    size_t titan_pos = move.template_name.find("Titan");
                    if (titan_pos != std::string::npos) {
                        if (titan_pos < 3 || move.template_name.substr(titan_pos - 3, 3) != "Not") {
                            continue;
                        }
                    }
                }
                // Match move_type (Punch or Kick)
                if (move.move_type != cur_move_type) continue;

                if (block_all_combat) {
                    // During attack, not in Uninterrupt: NO combat moves at all
                    continue;
                } else if (in_attack && is_uninterrupt_) {
                    // During attack, IN Uninterrupt: only 3key chain combos allowed
                    if (move.key_count != 3) continue;
                    // 3key combos require CurrentAnimation match
                    if (!move.required_current_animation.empty()) {
                        if (current_move_ != move.required_current_animation) continue;
                    }
                } else {
                    // Not in attack: normal 1key/2key selection, skip 3key
                    if (move.key_count == 3) continue;
                }
                // Match direction
                if (move.direction != cur_direction) continue;
                // Match weapon (Unarmed or Fists or empty)
                if (!move.tactic_weapon.empty() && move.tactic_weapon != "Fists" &&
                    move.tactic_weapon.find("Fists") == std::string::npos) continue;
                // Check distance condition (only from main <Conditions>, not <Tactics>)
                // Note: <Tactics><Distance> is for AI move selection, not player.
                // We skip distance check entirely — player can attack at any distance.
                // (The original game uses distance only for AI tactic selection.)
                // Check weapon subtype lock (e.g. DoublePunch requires Fists)
                // For now, we assume player has Fists equipped, so Fists-locked moves pass.
                // Other weapon subtypes are not yet supported.
                if (!move.required_weapon_subtype.empty() &&
                    move.required_weapon_subtype != "Fists") continue;
                // [ORIGINAL] CurrentAnimation condition check.
                // PC source: sf2.js np.isEqual() (line 42544) — 3key combos
                // require the current animation to match a specific name.
                // e.g., DoublePunch requires CurrentAnimation="HeavyPunch".
                // The Name in moves.xml matches the Move Name (not filename).
                if (!move.required_current_animation.empty()) {
                    if (current_move_ != move.required_current_animation) continue;
                }
                // Check if animation exists
                std::string anim_name = move.filename;
                if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                    anim_name = anim_name.substr(0, anim_name.size()-4);
                if (!animations_.count(anim_name)) continue;
                // [DIAGNOSTIC] This move passed all filters — count it.
                ++candidate_count;
                // Select by highest priority
                if (!best_move || move.priority > best_move->priority) {
                    best_move = &move;
                }
            }

            if (best_move) {
                std::string anim_name = best_move->filename;
                if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                    anim_name = anim_name.substr(0, anim_name.size()-4);
                std::printf("[COMBAT] %s%s -> %s (anim '%s', prio=%d, tmpl='%s', dist=%.1f)\n",
                            (in_attack && !is_uninterrupt_) ? "[COMBO] " : "",
                            cur_move_type.c_str(), best_move->name.c_str(),
                            anim_name.c_str(), best_move->priority,
                            best_move->template_name.c_str(), dist_to_enemy);
                // [DIAGNOSTIC] Structured O/P decision log for input diagnosis.
                std::printf("[INPUT_DECISION] f=%llu btn=%s keys_down=%s%s%s%s just=%s%s "
                            "face=%d dir=%s ms=%d anim='%s' move='%s' hit=%u unint=%d "
                            "basic=%d cand=%d sel='%s' reject=none\n",
                            (unsigned long long)total_frame_count_,
                            punch_pressed ? "O" : "P",
                            key_up?"W":"", key_down?"S":"", key_left?"A":"", key_right?"D":"",
                            punch_pressed?"O":"", kick_pressed?"P":"",
                            (int)facing_right_, cur_direction.c_str(), move_state_,
                            current_anim_.c_str(), current_move_.c_str(),
                            hit_anim_, is_uninterrupt_?1:0, (int)(in_attack && !is_uninterrupt_),
                            candidate_count, best_move->name.c_str());
                play_animation(anim_name, false);
                current_move_ = best_move->name;
                int fc = animations_[anim_name].frame_count;
                hit_anim_ = (uint32_t)(fc * 1000.0f / 20.0f);
                move_state_ = 10;
                need_switch_to_idle_ = false;
                // [ORIGINAL] Play attack swing sound at attack start.
                // Original SF2 plays f_pl_attack*.wav on the first attack frame.
                int snd = (best_move->name.length() % 4) + 1;
                play_sound("f_pl_attack" + std::to_string(snd), 0.5f);
                goto after_combat;
            } else if (punch_pressed || kick_pressed) {
                // [DIAGNOSTIC] No candidate found — log structured reject.
                std::printf("[INPUT_DECISION] f=%llu btn=%s keys_down=%s%s%s%s just=%s%s "
                            "face=%d dir=%s ms=%d anim='%s' move='%s' hit=%u unint=%d "
                            "basic=%d cand=%d sel='' reject=no_candidate\n",
                            (unsigned long long)total_frame_count_,
                            punch_pressed ? "O" : "P",
                            key_up?"W":"", key_down?"S":"", key_left?"A":"", key_right?"D":"",
                            punch_pressed?"O":"", kick_pressed?"P":"",
                            (int)facing_right_, cur_direction.c_str(), move_state_,
                            current_anim_.c_str(), current_move_.c_str(),
                            hit_anim_, is_uninterrupt_?1:0, (int)(in_attack && !is_uninterrupt_),
                            candidate_count);
                // Debug: no move found — log why
                static int no_move_log = 0;
                if (no_move_log < 3) {
                    std::printf("[COMBAT] NO MOVE for %s dir='%s' basic=%d — candidates:\n",
                                cur_move_type.c_str(), cur_direction.c_str(), (in_attack && !is_uninterrupt_) ? 1 : 0);
                    for (auto& [name, move] : moves_) {
                        if (move.move_type != cur_move_type) continue;
                        if (move.direction != cur_direction) continue;
                        if (move.key_count == 3 && !(in_attack && !is_uninterrupt_)) continue;
                        std::string anim_name = move.filename;
                        if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                            anim_name = anim_name.substr(0, anim_name.size()-4);
                        bool anim_exists = animations_.count(anim_name) > 0;
                        std::printf("  %s tmpl='%s' kc=%d tw='%s' titan=%d anim=%d\n",
                                    name.c_str(), move.template_name.c_str(), move.key_count,
                                    move.tactic_weapon.c_str(),
                                    move.template_name.find("Titan") != std::string::npos ? 1 : 0,
                                    anim_exists ? 1 : 0);
                    }
                    no_move_log++;
                }
            }
        } else if ((punch_pressed || kick_pressed) && is_uninterrupt_) {
            // [DIAGNOSTIC] O/P pressed but blocked by Uninterrupt interval.
            std::printf("[INPUT_DECISION] f=%llu btn=%s keys_down=%s%s%s%s just=%s%s "
                        "face=%d dir=? ms=%d anim='%s' move='%s' hit=%u unint=%d "
                        "basic=? cand=? sel='' reject=uninterrupt\n",
                        (unsigned long long)total_frame_count_,
                        punch_pressed ? "O" : "P",
                        key_up?"W":"", key_down?"S":"", key_left?"A":"", key_right?"D":"",
                        punch_pressed?"O":"", kick_pressed?"P":"",
                        (int)facing_right_, move_state_,
                        current_anim_.c_str(), current_move_.c_str(),
                        hit_anim_, is_uninterrupt_?1:0);
        }
    }
    }

// === SPECIAL MOVES (jumps, rolls, duck) — from moves.xml ===
        // Match 1key moves with Jump, Step, or no type (Duck, Roll)
        // [ORIGINAL] PC source: sf2.js Pqb() (line 18769-18810) -- move selector
        // During attack (elapsed < total_len):
        //   If in Uninterrupt: Pqb allows chain attacks (YAa/Gea) -- movement specials NOT included
        //   If NOT in Uninterrupt: Pqb returns 0 -- NO moves available at all
        // After attack ends: normal move selection (1key, 2key, movement specials, steps)
        // movement specials (jump, duck, roll) are NEVER available during attack.
        // Exclude StartStance -- player should act immediately after intro.
        do {
        if (!in_attack) {
            // Trigger when ANY direction key is just pressed; use HELD keys for full direction.
            // This correctly handles W+left (A just pressed while W held) → "UpBack"
            bool any_dir_just_pressed =
                input.keys_just_pressed[(size_t)plat::Key::W] ||
                input.keys_just_pressed[(size_t)plat::Key::ArrowUp] ||
                input.keys_just_pressed[(size_t)plat::Key::A] ||
                input.keys_just_pressed[(size_t)plat::Key::ArrowLeft] ||
                input.keys_just_pressed[(size_t)plat::Key::S] ||
                input.keys_just_pressed[(size_t)plat::Key::ArrowDown] ||
                input.keys_just_pressed[(size_t)plat::Key::D] ||
                input.keys_just_pressed[(size_t)plat::Key::ArrowRight];

            if (!any_dir_just_pressed) break;

            // Determine direction from HELD key state
            std::string cur_direction;
            bool up_held = input.keys_down[(size_t)plat::Key::W] ||
                           input.keys_down[(size_t)plat::Key::ArrowUp];
            bool down_held = input.keys_down[(size_t)plat::Key::S] ||
                             input.keys_down[(size_t)plat::Key::ArrowDown];
            if (up_held && key_forward) cur_direction = "UpForward";
            else if (up_held && key_back) cur_direction = "UpBack";
            else if (up_held) cur_direction = "Up";
            else if (down_held && key_forward) cur_direction = "DownForward";
            else if (down_held && key_back) cur_direction = "DownBack";
            else if (down_held) cur_direction = "Down";

            if (!cur_direction.empty()) {
                // Find best matching move (Jump, or MOVE type with 1key)
                const MoveDef* best_move = nullptr;
                for (auto& [name, move] : moves_) {
                    if (move.filename.empty() || move.template_name.empty()) continue;
                    if (move.key_count != 1) continue;
                    if (move.direction != cur_direction) continue;
                    // Skip Titan moves (player is not a Titan)
                    if (move.template_name.find("Titan") != std::string::npos &&
                        move.template_name.find("NotTitan") == std::string::npos) continue;
                    // Match Jump moves or MOVE type (not Wall, not Punch/Kick)
                    if (move.template_name.find("Wall") != std::string::npos) continue;
                    if (!move.is_jump && move.move_type != "Jump" &&
                        move.move_type != "MOVE" && !move.move_type.empty()) continue;
                    // Weapon filter
                    if (!move.tactic_weapon.empty() && move.tactic_weapon != "Fists" &&
                        move.tactic_weapon.find("Fists") == std::string::npos) continue;
                    // Check animation
                    std::string anim_name = move.filename;
                    if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                        anim_name = anim_name.substr(0, anim_name.size()-4);
                    if (!animations_.count(anim_name)) continue;
                    if (!best_move || move.priority > best_move->priority) {
                        best_move = &move;
                    }
                }

                if (best_move) {
                    std::string anim_name = best_move->filename;
                    if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                        anim_name = anim_name.substr(0, anim_name.size()-4);
                    std::printf("[MOVE] %s (anim '%s', prio=%d)\n",
                                best_move->name.c_str(), anim_name.c_str(),
                                best_move->priority);
                    play_animation(anim_name, false);
                    current_move_ = best_move->name;
                    int fc = animations_[anim_name].frame_count;
                    hit_anim_ = (uint32_t)(fc * 1000.0f / 20.0f);
                    move_state_ = 10;
                    goto after_combat;
                }
            }

            // Duck: S held (or just pressed) with no direction
            // Original game: holding S keeps you ducking. If you were attacking
            // and the attack ends while S is held, you immediately duck again.
            bool duck_input = key_down && !key_forward && !key_back;
            if (duck_input && (move_state_ == 0 || move_state_ == 11)) {
                // Debug: log duck attempt
                static int duck_log_count = 0;
                if (duck_log_count < 5) {
                    std::printf("[DUCK] attempt — move_state=%d key_down=%d\n", move_state_, key_down ? 1 : 0);
                    duck_log_count++;
                }
                // Find Duck move (1key|Down|NotTitan, Type=MOVE)
                const MoveDef* duck_move = nullptr;
                for (auto& [name, move] : moves_) {
                    if (move.filename.empty()) continue;
                    if (move.key_count != 1) continue;
                    if (move.direction != "Down") continue;
                    if (move.is_jump) continue;
                    if (move.move_type == "Punch" || move.move_type == "Kick") continue;
                    // Skip Titan moves (they require TitanGiantSword weapon)
                    if (move.template_name.find("Titan") != std::string::npos &&
                        move.template_name.find("NotTitan") == std::string::npos) continue;
                    // Weapon filter — only allow Fists or empty
                    if (!move.tactic_weapon.empty() && move.tactic_weapon != "Fists" &&
                        move.tactic_weapon.find("Fists") == std::string::npos) continue;
                    // Require NotTitan template (player is not a Titan)
                    if (!move.is_not_titan) continue;
                    std::string anim_name = move.filename;
                    if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                        anim_name = anim_name.substr(0, anim_name.size()-4);
                    if (!animations_.count(anim_name)) continue;
                    if (!duck_move || move.priority > duck_move->priority) {
                        duck_move = &move;
                    }
                }
                if (duck_move) {
                    std::string duck_anim_name = duck_move->filename;
                    if (duck_anim_name.size() > 4 && duck_anim_name.substr(duck_anim_name.size()-4) == ".bin")
                        duck_anim_name = duck_anim_name.substr(0, duck_anim_name.size()-4);
                    // Only switch animation if not already ducking
                    if (move_state_ != 11 || current_anim_ != duck_anim_name) {
                        std::printf("[DUCK] found: %s (anim '%s')\n", duck_move->name.c_str(), duck_anim_name.c_str());
                        play_animation(duck_anim_name, true);
                        current_move_ = duck_move->name;
                        move_state_ = 11;
                        duck_play_time_ = 0;
                    }
                } else {
                    static int no_duck_log = 0;
                    if (no_duck_log < 2) {
                        std::printf("[DUCK] no duck move found! Searching 1key|Down moves:\n");
                        for (auto& [name, move] : moves_) {
                            if (move.key_count != 1 || move.direction != "Down") continue;
                            std::printf("  %s tmpl='%s' mt='%s' titan=%d not_titan=%d jump=%d\n",
                                        name.c_str(), move.template_name.c_str(),
                                        move.move_type.c_str(),
                                        move.template_name.find("Titan") != std::string::npos ? 1 : 0,
                                        move.is_not_titan ? 1 : 0,
                                        move.is_jump ? 1 : 0);
                        }
                        no_duck_log++;
                    }
                }
            }
        }
        } while(0);

        // === STEP MOVEMENT (from moves.xml: StepForward/StepBack) ===
        // FIX: don't override attack animation with step when in an attack.
        // hit_anim_ > 0 also applies during start_stance (stance_2), which
        // should NOT block steps. Use is_in_attack to distinguish.
        {
        bool is_in_attack = false;
        if (!current_move_.empty()) {
            auto mit = moves_.find(current_move_);
            if (mit != moves_.end() && mit->second.is_attack) is_in_attack = true;
        }
        if (!is_in_attack) {
            bool fwd_latched = fwd_held_ms_ > 0;
            bool back_latched = back_held_ms_ > 0;

            // Find step animation names from moves.xml
            std::string step_fwd_anim, step_back_anim;
            int step_fwd_prio = -1, step_back_prio = -1;
            for (auto& [name, move] : moves_) {
                if (move.filename.empty()) continue;
                // Skip Titan moves (player is not a Titan)
                if (move.template_name.find("Titan") != std::string::npos) continue;
                if (!move.is_step || move.is_double_step) continue;
                std::string anim_name = move.filename;
                if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                    anim_name = anim_name.substr(0, anim_name.size()-4);
                if (!animations_.count(anim_name)) continue;
                if (move.direction == "Forward" && move.priority > step_fwd_prio) {
                    step_fwd_anim = anim_name;
                    step_fwd_prio = move.priority;
                } else if (move.direction == "Back" && move.priority > step_back_prio) {
                    step_back_anim = anim_name;
                    step_back_prio = move.priority;
                }
            }
            // Fallback if not found
            if (step_fwd_anim.empty() && animations_.count("step_forward")) step_fwd_anim = "step_forward";
            if (step_back_anim.empty() && animations_.count("step_back")) step_back_anim = "step_back";

            if (move_state_ == 0 || start_stance_playing_) {  // IDLE (or start stance)
                // [STEP_DEBUG] Log step conditions around start-stance end
                if (total_frame_count_ >= 245 && total_frame_count_ <= 265) {
                    std::printf("[STEPDBG] f=%llu ms=%d ss=%d ha=%u anim='%s' kf=%d kb=%d kd=%d sf='%s'\n",
                                (unsigned long long)total_frame_count_,
                                move_state_, (int)start_stance_playing_, hit_anim_,
                                current_anim_.c_str(),
                                (int)key_forward, (int)key_back, (int)key_down,
                                step_fwd_anim.c_str());
                }
                if (key_forward && !key_back && !key_down && !step_fwd_anim.empty()) {
                    move_state_ = 2;
                    start_stance_playing_ = false;
                    play_animation(step_fwd_anim, true);
                } else if (key_back && !key_forward && !key_down && !step_back_anim.empty()) {
                    move_state_ = 1;
                    start_stance_playing_ = false;
                    play_animation(step_back_anim, true);
                }
            } else if (move_state_ == 1) {  // MOVING_BACK
                if (!back_latched && step_min_played) {
                    move_state_ = 0; need_switch_to_idle_ = true;
                } else if (fwd_latched && !back_latched && step_min_played && !step_fwd_anim.empty()) {
                    move_state_ = 2;
                    play_animation(step_fwd_anim, true);
                }
            } else if (move_state_ == 2) {  // MOVING_FORWARD
                if (!fwd_latched && step_min_played) {
                    move_state_ = 0; need_switch_to_idle_ = true;
                } else if (back_latched && !fwd_latched && step_min_played && !step_back_anim.empty()) {
                    move_state_ = 1;
                    play_animation(step_back_anim, true);
                }
            }
        }
        }

        // === HIT ANIM COUNTDOWN ===
        if (hit_anim_ > 0) {
            hit_anim_ -= std::min<uint32_t>(hit_anim_, dt);
        }

        // is_uninterrupt_ will be computed after update_animation()
        // (we need the updated anim_time_ to check the current frame)
        is_uninterrupt_ = false;

        // Exit special move state when animation finishes
        if (move_state_ == 10 && hit_anim_ == 0) {
            std::printf("[STATE] move_state 10->0 (special ended), current_move='%s'\n",
                        current_move_.c_str());
            move_state_ = 0;
            need_switch_to_idle_ = true;
            // Clear current_move_ so 3key combos don't trigger on next key press
            current_move_.clear();
        }
        // Exit duck state when Down released
        // No minimum duration — original game allows immediate release
        if (move_state_ == 11) {
            duck_play_time_ += dt;
            if (!key_down && duck_play_time_ >= 100) {
                move_state_ = 0;
                need_switch_to_idle_ = true;
            }
        }

        after_combat:
        // Camera follows player (always update, even after attack)
        cam_x_ = player_pos_x_ + 200.0f;
        renderer_->camera().set_target(cam_x_, cam_y_);
        renderer_->camera().set_zoom(zoom_);

        // === UPDATE ANIMATION ===
        // MUST run BEFORE any play_animation calls so the final frame's
        // root motion is applied before switching to idle.
        update_animation(dt);

        // === UNINTERRUPT CHECK (after update_animation) ===
        // [ORIGINAL] PC source: sf2.js ocb() — checks if current animation frame
        // is within any <Interval Name="Uninterrupt"> start..finish range.
        // Intervals are fired as events when the animation passes their start frame.
        // Uninterrupt is NOT a global input lock — it only blocks during specific
        // frames of the ATTACK animation (not stance_idle or other anims).
        // FIX: only check is_uninterrupt_ when current_anim_ IS the attack animation.
        // Previous code checked current_move_ (which stays set after anim ends),
        // using stance_idle's frame count against the attack's Uninterrupt interval.
        is_uninterrupt_ = false;
        if (hit_anim_ > 0 && !current_move_.empty()) {
            auto move_it = moves_.find(current_move_);
            if (move_it != moves_.end() && move_it->second.uninterrupt_start >= 0) {
                // [ORIGINAL] Only check Uninterrupt when the ATTACK animation is
                // currently playing, not when we've switched to stance_idle.
                std::string expected_anim = move_it->second.filename;
                if (expected_anim.size() > 4 && expected_anim.substr(expected_anim.size()-4) == ".bin")
                    expected_anim = expected_anim.substr(0, expected_anim.size()-4);
                if (expected_anim == current_anim_) {
                    int current_frame = (int)(anim_time_ * 20.0f);
                    int start = move_it->second.uninterrupt_start - 1;
                    int end = move_it->second.uninterrupt_end > 0 ?
                              move_it->second.uninterrupt_end - 1 : 9999;
                    if (current_frame >= start && current_frame <= end) {
                        is_uninterrupt_ = true;
                    }
                }
            }
        }

        // After update_animation, switch to idle if requested.
        // This ensures the previous animation's final displacement is applied.
        if (need_switch_to_idle_) {
            need_switch_to_idle_ = false;
            if (start_stance_playing_) {
                start_stance_playing_ = false;
            }
            play_animation("stance_idle", true);
        }

        // === HIT DETECTION ===
        // hit_anim_ countdown already done above (before special move exit).
        // Only do hit detection here if hit_anim_ is still > 0.
        // Reset bag_hit_ at the START of each attack animation (when current_move_
        // changes), not just when hit_anim_ reaches 0.
        // === HIT DETECTION (from moves.xml intervals) ===
        // Original game logic (from moves.xml + s3e disassembly):
        //
        // 1. Each move has an Attack interval: <Interval Type="Attack" Start="4" End="5">
        //    This defines WHICH FRAMES the attack is active.
        //
        // 2. During Attack interval, the game checks collision between
        //    AttackingParts (edges like EForearm_2, EHand_2, EFingers_2)
        //    and the enemy's Collisible edges (edges with Collisible="1").
        //
        // 3. Collision = distance between edge endpoints < threshold.
        //    Each attacking edge has two endpoints (End1, End2 from skeleton).
        //    Both endpoints are checked against ALL enemy collisible edges.
        //
        // 4. On collision: apply Damage, Impulse, and play Hit effect.
        //    The hit is PER-FRAME — each frame in the Attack interval can
        //    register a separate hit. There is NO "bag_hit_" flag in the
        //    original. The original checks collision every frame during
        //    the attack interval.
        //
        // 5. To prevent multiple hits per frame, the original uses
        //    Invulnerable interval on the TARGET (not the attacker).
        //    After being hit, the target becomes Invulnerable for N frames.
        //
        // We implement: check collision EVERY frame during Attack interval.
        // Apply impulse only if the bag wasn't already hit THIS FRAME.
        // Reset hit state when leaving the Attack interval (so the next
        // attack interval frame can hit again).

        // IMPORTANT: Clear current_move_ when attack ends, regardless of
        // whether bag_model_ exists. Otherwise current_move_ stays set
        // from a previous attack, causing 3key combos to trigger on the
        // next key press (e.g., DoubleSweep after a single P press).
        if (hit_anim_ > 0 && bag_model_ && location_) {
            auto anim_it = animations_.find(current_anim_);
            if (anim_it != animations_.end()) {
                int fc = anim_it->second.frame_count;
                int current_frame = (int)(anim_time_ * 20.0f);
                auto move_it = moves_.find(current_move_);
                // [DIAGNOSTIC] Per-frame hit-detection state log for
                // "hit without animation" diagnosis (Task 2).
                {
                    std::string expected_anim;
                    bool anim_match = false;
                    if (move_it != moves_.end()) {
                        expected_anim = move_it->second.filename;
                        if (expected_anim.size() > 4 && expected_anim.substr(expected_anim.size()-4) == ".bin")
                            expected_anim = expected_anim.substr(0, expected_anim.size()-4);
                        anim_match = (expected_anim == current_anim_);
                    }
                    std::printf("[HIT_CHECK] f=%llu move='%s' anim='%s' exp_anim='%s' match=%d frame=%d/%d hit_anim=%u atk=%d-%d bag_hit=%d\n",
                                (unsigned long long)total_frame_count_,
                                current_move_.c_str(), current_anim_.c_str(),
                                expected_anim.c_str(), (int)anim_match,
                                current_frame, fc, hit_anim_,
                                move_it != moves_.end() ? move_it->second.attack_start : -1,
                                move_it != moves_.end() ? move_it->second.attack_end : -1,
                                (int)bag_hit_);
                }
                if (move_it != moves_.end() && move_it->second.attack_start > 0) {
                    int attack_start = move_it->second.attack_start;
                    int attack_end = move_it->second.attack_end > 0 ?
                                   move_it->second.attack_end : attack_start;
                    int frame_start = attack_start - 1;
                    int frame_end = attack_end - 1;
                    // Check if we're in the attack interval
                    bool in_attack_interval = (current_frame >= frame_start && current_frame <= frame_end);

                    // Reset bag_hit_ when NOT in attack interval (allows re-hit
                    // when entering the interval again, e.g., for multi-hit moves)
                    if (!in_attack_interval) {
                        bag_hit_ = false;
                    }

                    if (in_attack_interval && !bag_hit_) {
                        // [ORIGINAL] Distance-based hit detection on enemy fighter.
                        // If the player's attack limb is within hit range of the
                        // enemy fighter (enemy_pos_x_), register a hit. This works
                        // alongside the bag-collision detection (bag stays at the
                        // original spawn point as a visual punching bag; the enemy
                        // fighter moves via AI and is hit by distance check).
                        if (!enemy_fighter_.is_dead && enemy_fighter_.invuln_time <= 0) {
                            float dist_to_enemy = std::abs(enemy_pos_x_ - player_pos_x_);
                            // Hit range: 180px (covers punch/kick reach)
                            if (dist_to_enemy < 180.0f) {
                                float dmg = move_it->second.damage;
                                if (dmg <= 0) dmg = 8.0f;
                                if (enemy_fighter_.is_blocking) dmg *= 0.25f;
                                enemy_fighter_.health -= dmg;
                                enemy_fighter_.is_hit = true;
                                enemy_fighter_.hit_stun_time = 0.3f;
                                enemy_fighter_.invuln_time = 0.4f;
                                enemy_fighter_.hits_taken++;
                                player_fighter_.hits_landed++;
                                player_fighter_.energy = std::min(player_fighter_.max_energy,
                                    player_fighter_.energy + dmg * 0.5f);
                                enemy_hit_flash_ = 0.25f;
                                int snd_idx = (current_frame + (int)current_move_[0]) % 4 + 1;
                                play_sound("f_pl_attack" + std::to_string(snd_idx), 0.7f);
                                play_sound("armor", 0.5f);
                                bag_hit_ = true;  // prevent multi-hit per frame
                                if (enemy_fighter_.health <= 0) {
                                    enemy_fighter_.health = 0;
                                    enemy_fighter_.is_dead = true;
                                    battle_result_ = "victory";
                                    play_sound("bodyfall1", 0.9f);
                                    std::printf("[COMBAT] Enemy defeated! battle_result=victory\n");
                                }
                                std::printf("[COMBAT] Player hit enemy: move=%s dist=%.1f dmg=%.1f enemy_hp=%.1f\n",
                                    current_move_.c_str(), dist_to_enemy, dmg, enemy_fighter_.health);
                            }
                        }
                        // Determine attacking limb from AttackingParts in moves.xml
                        // Each AttackingParts Edge has End1 and End2 in skeleton.xml
                        // We check ALL attacking edges, not just one
                        bool hit_registered = false;
                        for (auto& edge_name : move_it->second.attack_edges) {
                            if (edge_name.empty()) continue;
                            // Look up edge in skeleton to get End1/End2
                            auto skel_edge = skeleton_edges_.find(edge_name);
                            std::string node1, node2;
                            if (skel_edge != skeleton_edges_.end()) {
                                node1 = skel_edge->second.end1;
                                node2 = skel_edge->second.end2;
                            } else {
                                // Fallback: guess from edge name
                                if (edge_name.find("Foot") != std::string::npos ||
                                    edge_name.find("Calf") != std::string::npos ||
                                    edge_name.find("Leg") != std::string::npos) {
                                    node1 = "NToe_1"; node2 = "NAnkle_1";
                                } else {
                                    node1 = "NWrist_1"; node2 = "NKnuckles_1";
                                }
                            }

                            // Check collision for both endpoints of this edge
                            for (int endpoint = 0; endpoint < 2; endpoint++) {
                                std::string& limb_node = (endpoint == 0) ? node1 : node2;
                                if (limb_node.empty()) continue;
                                auto ait = anim_node_pos_.find(limb_node);
                                if (ait == anim_node_pos_.end()) continue;

                                float limb_lx = ait->second.first;
                                float limb_ly = ait->second.second;
                                auto pivot_it = skeleton_nodes_.find("NPivot");
                                float pivot_ly = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 169.48f;
                                float limb_wx = player_pos_x_ + (facing_right_ ? limb_lx : -limb_lx);
                                float limb_wy = player_pos_y_ + y_adjust_smoothed_ + (limb_ly - pivot_ly);

                                // Get attacking edge radius from skeleton
                                float atk_radius = 0;
                                auto skel_it = skeleton_edges_.find(edge_name);
                                if (skel_it != skeleton_edges_.end()) {
                                    atk_radius = skel_it->second.radius;
                                }

                                // Get world-space positions of both attacking edge endpoints
                                auto ait2 = anim_node_pos_.find(node2);
                                if (ait2 == anim_node_pos_.end()) continue;

                                float limb2_lx = ait2->second.first;
                                float limb2_ly = ait2->second.second;
                                float limb2_wx = player_pos_x_ + (facing_right_ ? limb2_lx : -limb2_lx);
                                float limb2_wy = player_pos_y_ + y_adjust_smoothed_ + (limb2_ly - pivot_ly);

                                // Check against ALL collisible bag edges
                                bool bag_hit_this_frame = false;
                                for (auto& be : bag_model_->edges) {
                                    if (!be.collisible) continue;
                                    float bag_r = be.radius;
                                    if (bag_r <= 0) continue;
                                    if (be.end1.empty() || be.end2.empty()) continue;

                                    // Get bag edge endpoints from Verlet
                                    auto bv1 = bag_verlet_.find(be.end1);
                                    auto bv2 = bag_verlet_.find(be.end2);
                                    if (bv1 == bag_verlet_.end() || bv2 == bag_verlet_.end()) continue;

                                    float be1x = bv1->second.x, be1y = bv1->second.y;
                                    float be2x = bv2->second.x, be2y = bv2->second.y;

                                    // Segment-segment closest distance, also returns hit ratio t along bag edge
                                    float ex = limb2_wx - limb_wx, ey = limb2_wy - limb_wy;
                                    float fx = be2x - be1x, fy = be2y - be1y;
                                    float gx = limb_wx - be1x, gy = limb_wy - be1y;
                                    float a = ex*ex + ey*ey;
                                    float b = ex*fx + ey*fy;
                                    float c = fx*fx + fy*fy;
                                    float d = ex*gx + ey*gy;
                                    float e = fx*gx + fy*gy;
                                    float det = a*c - b*b;
                                    float s, t;
                                    if (det < 1e-12f) {
                                        s = 0.0f;
                                        t = (b > c) ? d / b : e / c;
                                        t = std::max(0.0f, std::min(1.0f, t));
                                    } else {
                                        s = (b*e - c*d) / det;
                                        t = (a*e - b*d) / det;
                                        if (s < 0) { s = 0; t = e / c; t = std::max(0.0f, std::min(1.0f, t)); }
                                        else if (s > 1) { s = 1; t = (b + e) / c; t = std::max(0.0f, std::min(1.0f, t)); }
                                        else if (t < 0) { t = 0; s = -d / a; s = std::max(0.0f, std::min(1.0f, s)); }
                                        else if (t > 1) { t = 1; s = (b - d) / a; s = std::max(0.0f, std::min(1.0f, s)); }
                                    }
                                    float px = limb_wx + s*ex, py = limb_wy + s*ey;
                                    float qx = be1x + t*fx, qy = be1y + t*fy;
                                    float rx = px - qx, ry = py - qy;
                                    float sq_dist = rx*rx + ry*ry;
                                    float threshold = atk_radius + bag_r;
                                    if (sq_dist < threshold * threshold) {
                                        std::printf("[COMBAT] HIT! move=%s frame=%d/%d [%d-%d] atk_edge=%s bag_edge=%s sq_dist=%.1f thresh=%.1f (atk_r=%.1f bag_r=%.1f)\n",
                                                    current_move_.c_str(), current_frame, fc,
                                                    frame_start, frame_end,
                                                    edge_name.c_str(), be.name.c_str(),
                                                    sq_dist, threshold*threshold, atk_radius, bag_r);
                                        // [ORIGINAL] JS: Kwb() line 15467 creates impulse H(kw,gR,hR,1)
                                        // from XML <Impulse X/Y/Z>, then strike() applies to defender physics.
                                        // Apply the impulse to the bag edge's Verlet nodes.
                                        float imp_x = move_it->second.impulse_x;
                                        float imp_y = move_it->second.impulse_y;
                                        if (imp_x != 0 || imp_y != 0) {
                                            float dir = facing_right_ ? 1.0f : -1.0f;
                                            // Distribute impulse by hit position along edge (original Bl.strike)
                                            float hit_ratio = std::max(0.0f, std::min(1.0f, t));
                                            float dist1 = 1.0f - hit_ratio;
                                            float dist2 = hit_ratio;
                                            apply_bag_impulse(be.end1, dir * imp_x * dist1, imp_y * dist1);
                                            apply_bag_impulse(be.end2, dir * imp_x * dist2, imp_y * dist2);
                                        }
                                        bag_hit_ = true;
                                        bag_hit_this_frame = true;
                                        hit_registered = true;
                                        // [ORIGINAL] Play hit sound + apply damage to enemy fighter.
                                        // Original SF2: on hit, plays armor/body sound + damage from
                                        // MoveDef::damage (parsed from <Damage Value=".."/>).
                                        // Pick a random attack sound for variety.
                                        int snd_idx = (current_frame + (int)current_move_[0]) % 4 + 1;
                                        play_sound("f_pl_attack" + std::to_string(snd_idx), 0.8f);
                                        play_sound("armor", 0.6f);
                                        // Apply damage to enemy (punching bag = enemy proxy)
                                        if (!enemy_fighter_.is_dead && enemy_fighter_.invuln_time <= 0) {
                                            float dmg = move_it->second.damage;
                                            if (dmg <= 0) dmg = 8.0f;  // default per hit if not specified
                                            // Blocking reduces damage by 75%
                                            if (enemy_fighter_.is_blocking) dmg *= 0.25f;
                                            enemy_fighter_.health -= dmg;
                                            enemy_fighter_.is_hit = true;
                                            enemy_fighter_.hit_stun_time = 0.3f;
                                            enemy_fighter_.invuln_time = 0.2f;
                                            enemy_fighter_.hits_taken++;
                                            player_fighter_.hits_landed++;
                                            player_fighter_.energy = std::min(player_fighter_.max_energy,
                                                player_fighter_.energy + dmg * 0.5f);
                                            enemy_hit_flash_ = 0.2f;
                                            if (enemy_fighter_.health <= 0) {
                                                enemy_fighter_.health = 0;
                                                enemy_fighter_.is_dead = true;
                                                battle_result_ = "victory";
                                                play_sound("bodyfall1", 0.9f);
                                                std::printf("[COMBAT] Enemy defeated! battle_result=victory\n");
                                            }
                                        }
                                        break;
                                    }
                                }
                                if (bag_hit_this_frame) break;
                            }
                            if (hit_registered) break;
                        }
                    }
                }
            }
            if (hit_anim_ == 0 && move_state_ == 0) {
                need_switch_to_idle_ = true;
                current_move_.clear();
                bag_hit_ = false;  // reset for next attack
            }

            // IMPORTANT: DON'T reset move_state_ here — hit_anim_ expiring
            // (start stance countdown) should NOT interrupt step movement.
            // Allow the MOVING_FORWARD/MOVING_BACK state machine to keep
            // stepping. Only clear current_move_ so the next key press can
            // trigger a new move.
            if (hit_anim_ == 0 && move_state_ != 0 && !current_move_.empty()) {
                current_move_.clear();
                bag_hit_ = false;
            }
        }

        // IMPORTANT: Also clear current_move_ when hit_anim_ reaches 0
        // even if bag_model_ doesn't exist (no character loaded).
        // This prevents 3key combos from triggering on the next key press
        // when current_move_ is still set from a previous attack.
        // Only force idle switch if not currently stepping.
        if (hit_anim_ == 0 && !current_move_.empty() && move_state_ == 0) {
            need_switch_to_idle_ = true;
            current_move_.clear();
            bag_hit_ = false;
        } else if (hit_anim_ == 0 && !current_move_.empty()) {
            // Stepping — just clear the move name, don't interrupt the step
            current_move_.clear();
            bag_hit_ = false;
        }

        // Update bag Verlet physics
        update_bag_verlet(dt / 1000.0f);

        // Zoom presets
        if (input.keys_just_pressed[(size_t)plat::Key::Num1]) zoom_ = 1.0f;
        if (input.keys_just_pressed[(size_t)plat::Key::Num2]) zoom_ = 0.7f;
        if (input.keys_just_pressed[(size_t)plat::Key::Num3]) zoom_ = 1.5f;

        // [DIAGNOSTIC] Structured frame state dump (--dump-state)
        if (dump_state_) {
            std::printf("[STATE] f=%llu ms=%d ha=%u anim='%s' move='%s' px=%.1f py=%.1f "
                        "bag_hit=%d bag_angle=%.3f nv=%zu\n",
                        (unsigned long long)total_frame_count_, move_state_, hit_anim_,
                        current_anim_.c_str(), current_move_.c_str(),
                        player_pos_x_, player_pos_y_,
                        (int)bag_hit_, bag_angle_, bag_verlet_.size());
        }
    }

    // Called by MainMenuScene and BattleScene to render the dojo scene
    // (background, character, bag, HUD, menu/dialog overlays).
    void host_render_scene() {
        if (!location_loaded_) return;
        render_location();
        render_punching_bag();
        render_enemy_fighter();
        render_character();
        render_hud(*platform_);
        if (menu_anim_progress_ > 0.01f) render_menu_expanded(*platform_);
        if (overlay_ == Overlay::Dialog) render_dialog_overlay(*platform_);
    }

    // [ORIGINAL] Render the enemy skeleton fighter as a simplified silhouette.
    // Draws a dark figure at enemy_pos_x_ with the current AI animation pose.
    // Uses the same skeleton_nodes_/body model as the player, but with a red
    // tint to distinguish from the player's black silhouette.
    // [HEURISTIC-TODO] This is a simplified render: it draws the rest-pose
    // skeleton (no per-node .bin animation for the enemy yet). A full second-
    // model render requires factoring render_body_model() to accept a
    // FighterState + position + facing parameter.
    void render_enemy_fighter() {
        if (enemy_fighter_.is_dead) return;  // don't render dead enemy
        // Use skeleton rest pose to draw a simplified humanoid silhouette
        if (skeleton_nodes_.empty()) return;
        // Key nodes for a stick-figure silhouette
        auto get_node = [&](const std::string& n) -> const SkelNode* {
            auto it = skeleton_nodes_.find(n);
            return it != skeleton_nodes_.end() ? &it->second : nullptr;
        };
        const SkelNode* np = get_node("NPivot");
        const SkelNode* head = get_node("NHead");
        const SkelNode* neck = get_node("NNeck");
        const SkelNode* chest = get_node("NChest");
        const SkelNode* waist = get_node("NWaist");
        const SkelNode* lshoulder = get_node("NShoulder_1");
        const SkelNode* rshoulder = get_node("NShoulder_2");
        const SkelNode* lelbow = get_node("NElbow_1");
        const SkelNode* relbow = get_node("NElbow_2");
        const SkelNode* lwrist = get_node("NWrist_1");
        const SkelNode* rwrist = get_node("NWrist_2");
        const SkelNode* lhip = get_node("NHip_1");
        const SkelNode* rhip = get_node("NHip_2");
        const SkelNode* lknee = get_node("NKnee_1");
        const SkelNode* rknee = get_node("NKnee_2");
        const SkelNode* lfoot = get_node("NToe_1");
        const SkelNode* rfoot = get_node("NToe_2");
        if (!np) return;
        // World position: enemy_pos_x_, enemy_pos_y_ + y_adjust
        float wx = enemy_pos_x_;
        float wy = enemy_pos_y_ + enemy_y_adjust_;
        // NPivot offset (skeleton is relative to NPivot)
        float npy = np->y, npx = np->x;
        // Enemy color: dark red silhouette (vs player's black)
        ren::Color4B enemy_col = (enemy_hit_flash_ > 0) ?
            ren::Color4B{255, 100, 100, 255} : ren::Color4B{90, 30, 30, 255};
        if (enemy_fighter_.is_blocking) enemy_col = ren::Color4B{60, 60, 120, 255};
        // Convert skeleton-local to world: world = (wx + (lx - npx) * facing, wy + (ly - npy))
        auto to_world = [&](const SkelNode* n, float& ox, float& oy) {
            if (!n) { ox = wx; oy = wy; return; }
            float dx = n->x - npx;
            float dy = n->y - npy;
            ox = wx + (enemy_facing_right_ ? dx : -dx);
            oy = wy + dy;
        };
        // Draw limbs as thick capsules (lines with circle caps)
        auto draw_limb = [&](const SkelNode* a, const SkelNode* b, float thickness) {
            if (!a || !b) return;
            float ax, ay, bx, by;
            to_world(a, ax, ay);
            to_world(b, bx, by);
            // Simple thick line via filled rect rotated
            float dx = bx - ax, dy = by - ay;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) return;
            // Draw a series of overlapping circles along the segment
            int steps = std::max(2, (int)(len / (thickness * 0.5f)));
            for (int i = 0; i <= steps; ++i) {
                float t = (float)i / steps;
                float cx = ax + dx * t, cy = ay + dy * t;
                renderer_->draw_filled_circle_world(cx, cy, thickness, enemy_col);
            }
        };
        // Animate: add a simple bob/attack offset based on enemy_anim_
        float anim_phase = enemy_anim_time_ * 8.0f;  // ~8 Hz bob
        float bob = (enemy_anim_ == "fists_idle") ? std::sin(anim_phase) * 1.5f : 0;
        wy += bob;
        // During attack, extend arm forward
        bool attacking = (enemy_anim_ == "high_punch" || enemy_attacking_);
        // Draw body: spine (waist->chest->neck->head)
        draw_limb(waist, chest, 8.0f);
        draw_limb(chest, neck, 6.0f);
        if (head) {
            float hx, hy;
            to_world(head, hx, hy);
            renderer_->draw_filled_circle_world(hx, hy, 12.0f, enemy_col);
        }
        // Arms
        if (attacking) {
            // Extend attacking arm toward player
            float dir = enemy_facing_right_ ? 1.0f : -1.0f;
            if (lshoulder && lwrist) {
                float sx, sy, ex, ey;
                to_world(lshoulder, sx, sy);
                ex = sx + dir * 50.0f; ey = sy - 5.0f;
                // Draw extended arm via circles
                int steps = 6;
                for (int i = 0; i <= steps; ++i) {
                    float t = (float)i / steps;
                    float cx = sx + (ex - sx) * t, cy = sy + (ey - sy) * t;
                    renderer_->draw_filled_circle_world(cx, cy, 5.0f, enemy_col);
                }
                // Fist
                renderer_->draw_filled_circle_world(ex, ey, 7.0f, enemy_col);
            }
            // Other arm tucked
            draw_limb(rshoulder, relbow, 5.0f);
            draw_limb(relbow, rwrist, 4.0f);
        } else {
            draw_limb(lshoulder, lelbow, 5.0f);
            draw_limb(lelbow, lwrist, 4.0f);
            draw_limb(rshoulder, relbow, 5.0f);
            draw_limb(relbow, rwrist, 4.0f);
        }
        // Legs
        draw_limb(waist, lhip, 7.0f);
        draw_limb(waist, rhip, 7.0f);
        draw_limb(lhip, lknee, 6.0f);
        draw_limb(lknee, lfoot, 6.0f);
        draw_limb(rhip, rknee, 6.0f);
        draw_limb(rknee, rfoot, 6.0f);
        // Health bar above enemy (small, floating) — drawn as 2 triangles (rect)
        float hb_w = 60.0f, hb_h = 5.0f;
        float hb_x = wx - hb_w / 2.0f;
        float hb_y = wy + 80.0f;
        // Background (dark)
        renderer_->draw_filled_triangle_world(hb_x, hb_y, hb_x + hb_w, hb_y,
            hb_x + hb_w, hb_y + hb_h, ren::Color4B{40, 40, 40, 255});
        renderer_->draw_filled_triangle_world(hb_x, hb_y, hb_x + hb_w, hb_y + hb_h,
            hb_x, hb_y + hb_h, ren::Color4B{40, 40, 40, 255});
        float pct = enemy_fighter_.health / enemy_fighter_.max_health;
        if (pct > 0) {
            ren::Color4B c = (pct > 0.5f) ? ren::Color4B{60, 180, 70, 255} :
                (pct > 0.25f ? ren::Color4B{200, 160, 40, 255} : ren::Color4B{180, 30, 30, 255});
            float fw = hb_w * pct;
            renderer_->draw_filled_triangle_world(hb_x, hb_y, hb_x + fw, hb_y,
                hb_x + fw, hb_y + hb_h, c);
            renderer_->draw_filled_triangle_world(hb_x, hb_y, hb_x + fw, hb_y + hb_h,
                hb_x, hb_y + hb_h, c);
        }
    }

    // Called by LoadingScene to render the loading screen.
    void host_render_loading() {
        render_loading_screen(*platform_);
    }

private:
    // ---------- Loading screen ----------
    void load_loading_screen() {
        auto root = std::filesystem::path(asset_root_);
        std::string xml_path;
        // Search for startLoading.xml in multiple possible paths
        for (const auto& dir : {root/"assets"/"1536"/"textures"/"fullscreen",
                                 root/"1536"/"textures"/"fullscreen",
                                 root/"assets"/"1536"/"fullscreen",
                                 root/"1536"/"fullscreen"}) {
            auto p = dir/"startLoading.xml";
            if (std::filesystem::exists(p)) { xml_path = p.string(); break; }
        }
        if (xml_path.empty()) {
            std::printf("  startLoading.xml not found, loading screen will be blank\n");
            return;
        }
        auto xml = read_text(xml_path);
        load_scale_ = std::min(
            (float)platform_->window_width() / 1820.0f,
            (float)platform_->window_height() / 1024.0f);
        size_t pos = 0;
        while ((pos = xml.find("<Image", pos)) != std::string::npos) {
            auto end = xml.find("/>", pos);
            auto tag = xml.substr(pos, end - pos);
            auto file = xml_attr(tag, "File");
            auto x = tof(xml_attr(tag, "X"));
            auto y = tof(xml_attr(tag, "Y"));
            // Search for the image file in multiple paths
            std::filesystem::path img_path;
            for (const auto& base : {root/"assets"/"1536", root/"1536", root/"assets", root}) {
                auto p = base / file;
                if (std::filesystem::exists(p)) { img_path = p; break; }
            }
            if (!img_path.empty()) {
                auto data = read_file(img_path.string());
                int w, h, ch;
                auto* px = stbi_load_from_memory(
                    (const stbi_uc*)data.data(), (int)data.size(), &w, &h, &ch, 4);
                if (px) {
                    auto tex = std::make_unique<ren::Texture2D>();
                    tex->init_rgba(w, h, px);
                    stbi_image_free(px);
                    loading_images_.push_back({std::move(tex), x, y});
                }
            }
            pos = end + 2;
        }
        if (loading_images_.empty()) {
            std::printf("  No loading images found, loading screen will be blank\n");
        }
    }

    // Initialize the dojo location: load all assets and set up the scene.
    // Called by host_load_location() (SceneHost interface) when entering
    // MainMenu or Battle scene.
    void init_location() {
        // Open DZ archives (for reading files that are only in .dz)
        auto root = std::filesystem::path(asset_root_);
        auto& dz = resf2::dz::DzRegistry::instance();
        for (const auto& base : {root, root/"assets", root/"assets"/"assets"}) {
            for (const auto& dz_name : {"files.dz", "animations.dz"}) {
                auto dz_path = base / dz_name;
                if (std::filesystem::exists(dz_path)) {
                    dz.open_archive(dz_path.string());
                }
            }
        }
        // Register fallback directories for extracted DZ contents.
        // When a file can't be decompressed from .dz (e.g. type=4 DZ custom
        // compression), the registry looks for the file in these directories.
        // This allows the engine to work with pre-extracted assets while
        // still supporting .dz archives for files that can be decompressed
        // (gzip type=8).
        for (const auto& base : {root, root/"assets", root/"assets"/"assets"}) {
            dz.add_fallback_dir(base.string());
        }
        
        load_location("dojo");
        location_loaded_ = true;
        if (location_ && !location_->color.empty()) {
            auto c = std::stoul(location_->color, nullptr, 16);
            renderer_->set_clear_color(
                ((c>>16)&0xFF)/255.0f,
                ((c>>8)&0xFF)/255.0f,
                (c&0xFF)/255.0f, 1.0f);
        }
        load_skeleton();
        load_body_model();
        load_punching_bag_model();
        load_animations();
        load_moves();
        load_hud_textures();
        load_menu_textures();
        load_hud_font();
        load_sounds();
        if (location_) {
            // Player/enemy positions in params.xml use Y-DOWN, same as image
            // coordinates. Location images are Y-inverted in render_location
            // (world_y = -img.y). But player/enemy Y is used directly (NOT
            // inverted) because the skeleton model space already has Y-UP
            // with NPivot at Y=169 and feet at Y=73 (difference = 96).
            //
            // Floor (layer_3) at params y=225 → world_y = -225 (inverted image).
            // Floor top surface at -225 + 32 = -193.
            // Player NPivot at params y=-93 → world_y = -93 (direct).
            // Player feet at -93 - 96 = -189. Floor at -193. Gap = 4. ✓
            //
            // Bag: enemy_y = -105. Bag NPivot at -105.
            // Node12 (ceiling attachment) at -105 + 226 = 121.
            // Ceiling (layer_5) at params y=-202 → world_y = +202.
            // Need Node12 at ceiling: bag_cy + 226 = 202 → bag_cy = -24.
            // Offset from enemy_y: -24 - (-105) = 81.
            // bag_cy = enemy_y + 81.
            //
            // X offset: align bag with holder (layer_5 at x=-10).
            // bag_cx = enemy_x - offset = -10 → offset = enemy_x + 10 = 983.
            const float X_OFFSET = 983.0f;  // aligns bag with ceiling holder
            player_pos_x_ = location_->player_x - X_OFFSET;
            player_pos_y_ = location_->player_y;  // no invert (matches location rendering)
            // [ORIGINAL] Enemy fighter position: same as the punching bag/enemy
            // spawn point from params.xml (enemy_x - X_OFFSET). The enemy
            // skeleton stands here and AI controls its behavior.
            enemy_pos_x_ = location_->enemy_x - 983.0f;
            enemy_pos_y_ = location_->player_y;  // same Y as player (on floor)
            enemy_facing_right_ = false;  // faces left toward player
        }
        // Camera: follow player but keep a proper Y that shows the floor.
        // The dojo floor (layer_3) is at world Y ≈ -193. Player at Y ≈ -93.
        // Camera Y should be around -50 to show player + floor + ceiling.
        cam_x_ = player_pos_x_ + 200.0f;
        cam_y_ = -50.0f;  // shows floor and character properly
        zoom_ = 1.0f;

        // Play start stance animation (from moves.xml: FistsStartStance-Right)
        // This is the intro animation before the fight begins.
        // stance_2.bin = right-facing start stance.
        // Cannot be interrupted — plays once, then transitions to stance_idle.
        if (animations_.count("stance_2")) {
            play_animation("stance_2", false);
            current_move_ = "StartStance";
            int fc = animations_["stance_2"].frame_count;
            hit_anim_ = (uint32_t)(fc * 1000.0f / 20.0f);
            move_state_ = 10;  // special move state (non-interruptible)
            start_stance_playing_ = true;
            std::printf("[STANCE] Playing start stance (stance_2, %d frames)\n", fc);
        } else if (animations_.count("stance_idle")) {
            play_animation("stance_idle", true);
        }
    }

    void render_loading_screen(plat::Platform& platform) {
        float tw = 1820.0f * load_scale_, th = 1024.0f * load_scale_;
        float ox = (platform.window_width() - tw) / 2.0f;
        float oy = (platform.window_height() - th) / 2.0f;
        for (auto& img : loading_images_) {
            if (!img.texture) continue;
            float w = img.texture->width() * load_scale_;
            float h = img.texture->height() * load_scale_;
            float x = ox + (img.x + 910.0f) * load_scale_;
            float y = oy + (img.y + 512.0f) * load_scale_;
            renderer_->draw_textured_quad_screen(*img.texture, x, y, w, h);
        }
    }

    // ---------- Location ----------
    void load_location(const std::string& name) {
        auto root = std::filesystem::path(asset_root_);
        std::string params_path;
        for (const auto& dir : {root/"assets"/"locations"/name,
                                 root/"locations"/name,
                                 root/"assets"/"1536"/"locations"/name}) {
            auto p = dir/"params.xml";
            if (std::filesystem::exists(p)) { params_path = p.string(); break; }
        }
        if (params_path.empty()) {
            std::printf("Location '%s' not found!\n", name.c_str()); return;
        }
        std::printf("Loading location: %s\n", params_path.c_str());
        auto xml = read_text(params_path);
        location_ = std::make_unique<GameLocation>(parse_location(xml));
        std::printf("  Player: (%.0f, %.0f)  Enemy: (%.0f, %.0f)\n",
                    location_->player_x, location_->player_y,
                    location_->enemy_x, location_->enemy_y);
        for (auto& layer : location_->layers) {
            if (layer.atlas_name.empty()) continue;
            if (atlases_.count(layer.atlas_name)) continue;
            load_atlas(layer.atlas_name, name);
        }
    }

    GameLocation parse_location(const std::string& xml) {
        GameLocation loc;
        auto rp = xml.find("<Root");
        if (rp != std::string::npos) {
            auto end = xml.find('>', rp);
            auto tag = xml.substr(rp, end - rp);
            loc.color = xml_attr(tag, "Color");
            loc.width = tof(xml_attr(tag, "Width"));
            loc.height = tof(xml_attr(tag, "Height"));
            loc.wall = tof(xml_attr(tag, "Wall"));
            loc.floor = tof(xml_attr(tag, "Floor"));
        }
        size_t pos = 0;
        while ((pos = xml.find("<Layer", pos)) != std::string::npos) {
            auto end = xml.find('>', pos);
            auto tag = xml.substr(pos, end - pos);
            LocationLayer layer;
            layer.type = toi(xml_attr(tag, "Type"));
            layer.factor = tof(xml_attr(tag, "Factor"), 1.0f);
            layer.atlas_name = xml_attr(tag, "Atlas");
            auto le = xml.find("</Layer>", pos);
            if (le == std::string::npos) le = xml.size();
            size_t ip = pos;
            while ((ip = xml.find("<Image", ip)) != std::string::npos && ip < le) {
                auto ie = xml.find("/>", ip);
                if (ie == std::string::npos) break;
                auto itag = xml.substr(ip, ie - ip);
                LayerImage img;
                img.atlas_name = layer.atlas_name;
                img.class_name = xml_attr(itag, "ClassName");
                img.x = tof(xml_attr(itag, "X"));
                img.y = tof(xml_attr(itag, "Y"));
                img.w = tof(xml_attr(itag, "Width"));
                img.h = tof(xml_attr(itag, "Height"));
                img.color = xml_attr(itag, "Color");
                layer.images.push_back(img);
                ip = ie + 2;
            }
            ip = pos;
            while ((ip = xml.find("<SimpleEffect", ip)) != std::string::npos && ip < le) {
                auto ie = xml.find(">", ip);
                if (ie == std::string::npos) break;
                auto ee = xml.find("</SimpleEffect>", ip);
                auto itag = xml.substr(ip, ie - ip);
                LayerImage img;
                img.atlas_name = layer.atlas_name;
                img.class_name = xml_attr(itag, "ClassName");
                img.x = tof(xml_attr(itag, "X"));
                img.y = tof(xml_attr(itag, "Y"));
                img.w = tof(xml_attr(itag, "Width"));
                img.h = tof(xml_attr(itag, "Height"));
                img.color = xml_attr(itag, "Color");
                layer.images.push_back(img);
                ip = ee != std::string::npos ? ee + 15 : ie + 1;
            }
            auto mv = xml.find("ModelsViewer", pos);
            if (mv != std::string::npos && mv < le) {
                auto me = xml.find("/>", mv);
                auto mtag = xml.substr(mv, me - mv);
                loc.player_x = tof(xml_attr(mtag, "PlayerPositionX"));
                loc.player_y = tof(xml_attr(mtag, "PlayerPositionY"));
                loc.enemy_x = tof(xml_attr(mtag, "EnemyPositionX"));
                loc.enemy_y = tof(xml_attr(mtag, "EnemyPositionY"));
            }
            loc.layers.push_back(layer);
            pos = le + 8;
        }
        return loc;
    }

    void load_atlas(const std::string& name, const std::string& loc) {
        auto root = std::filesystem::path(asset_root_);
        for (const auto& dir : {root/"assets"/"1536"/"locations"/loc,
                                 root/"assets"/"1536"/"textures",
                                 root/"assets"/"1536",
                                 root/"1536"/"locations"/loc,
                                 root/"1536"/"textures",
                                 root/"1536",
                                 root/"assets",
                                 root}) {
            auto pp = dir/(name+".plist"), pn = dir/(name+".png");
            if (std::filesystem::exists(pp) && std::filesystem::exists(pn)) {
                auto result = plist::parse(read_text(pp.string()));
                if (!result) continue;
                auto png_data = read_file(pn.string());
                // Decode atlas PNG for pre-cropping rotated frames
                int aw, ah, ach;
                auto* atlas_px = stbi_load_from_memory(
                    (const stbi_uc*)png_data.data(), (int)png_data.size(),
                    &aw, &ah, &ach, 4);
                auto tex = std::make_unique<ren::Texture2D>();
                if (!tex->init_from_png((const uint8_t*)png_data.data(),
                                         png_data.size())) {
                    if (atlas_px) stbi_image_free(atlas_px);
                    continue;
                }
                AtlasRef a;
                a.texture = std::move(tex);
                a.atlas = std::make_shared<plist::ParsedAtlas>(std::move(*result));
                // Pre-crop rotated frames into individual un-rotated textures
                if (atlas_px) {
                    for (auto& [fname, idx] : a.atlas->name_index) {
                        auto& frame = a.atlas->frames[idx];
                        if (!frame.rotated) continue;
                        // For rotated frames, atlas_w/atlas_h are ATLAS (post-rotation) dimensions.
                        // Original sprite dimensions are swapped.
                        int fw = frame.atlas_h;  // original width (swapped)
                        int fh = frame.atlas_w;  // original height (swapped)
                        auto ctex = std::make_unique<ren::Texture2D>();
                        std::vector<std::uint8_t> px((size_t)fw * fh * 4);
                        for (int y = 0; y < fh; ++y) {
                            for (int x = 0; x < fw; ++x) {
                                // Un-rotate 90° CCW (Cocos2d stores rotated 90° CW)
                                // Formula A (proven correct for location textures):
                                // dest(x,y) ← source(atlas_x + (fh-1-y), atlas_y + x)
                                int sx = frame.atlas_x + (fh - 1 - y);
                                int sy = frame.atlas_y + x;
                                if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                                int src_idx = (sy * aw + sx) * 4;
                                int dst_idx = (y * fw + x) * 4;
                                px[dst_idx+0] = atlas_px[src_idx+0];
                                px[dst_idx+1] = atlas_px[src_idx+1];
                                px[dst_idx+2] = atlas_px[src_idx+2];
                                px[dst_idx+3] = atlas_px[src_idx+3];
                            }
                        }
                        ctex->init_rgba(fw, fh, px.data());
                        std::string n = fname;
                        if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
                        a.cropped[n] = std::move(ctex);
                    }
                    stbi_image_free(atlas_px);
                }
                std::printf("  Atlas '%s': %zu frames, %zu pre-cropped\n",
                            name.c_str(), a.atlas->frames.size(), a.cropped.size());
                atlases_[name] = std::move(a);
                return;
            }
        }
        std::printf("  Atlas '%s' NOT FOUND\n", name.c_str());
    }

    void render_location() {
        if (!location_) return;
        // Render ALL layers with parallax support.
        //
        // Coordinate system: params.xml uses the same coordinate system as
        // the player/enemy positions (Y-up, Y=0 near center). We render
        // images directly at their (img.x, img.y) positions.
        //
        // Parallax: layers with factor < 1 scroll slower than the camera.
        // parallax_shift = (1 - factor) * cam_x_ — shifts the layer's X
        // to create the illusion of depth.
        static bool loc_logged = false;
        for (auto& layer : location_->layers) {
            // Parallax: the layer's X position scrolls at `factor` of the camera speed.
            // factor=1.0 → layer moves with camera (foreground).
            // factor=0.5 → layer moves at half speed (appears further away).
            // factor=0.1 → layer barely moves (far background).
            // Implementation: shift the layer's X by -cam_x_ * (1 - factor).
            // When the camera moves right (cam_x_ increases), the layer shifts left
            // by (1-factor)*cam_x_, creating the parallax effect.
            float parallax_factor = layer.factor;
            if (parallax_factor <= 0.0f) parallax_factor = 1.0f;
            float parallax_shift = (1.0f - parallax_factor) * cam_x_;

            if (!loc_logged) {
                std::printf("[LOC] layer: type=%d factor=%.2f atlas=%s images=%zu\n",
                            layer.type, layer.factor, layer.atlas_name.c_str(),
                            layer.images.size());
            }

            for (auto& img : layer.images) {
                if (!loc_logged) {
                    std::printf("[LOC]   img: cls='%s' x=%.0f y=%.0f w=%.0f h=%.0f color='%s'\n",
                                img.class_name.c_str(), img.x, img.y, img.w, img.h,
                                img.color.c_str());
                }
                if (img.class_name == "pixel_1" && !img.color.empty()) {
                    unsigned long col = std::stoul(img.color, nullptr, 16);
                    ren::Color4B c{
                        (std::uint8_t)((col>>16)&0xFF),
                        (std::uint8_t)((col>>8)&0xFF),
                        (std::uint8_t)(col&0xFF), 255};
                    auto it = atlases_.find(img.atlas_name);
                    if (it == atlases_.end()) {
                        // No atlas: render as a solid world-space rect.
                        float hw = (float)platform_->window_width()  / (2.0f * zoom_);
                        float hh = (float)platform_->window_height() / (2.0f * zoom_);
                        float left = cam_x_ - hw, right = cam_x_ + hw;
                        float bottom = cam_y_ - hh, top = cam_y_ + hh;
                        // params.xml uses Y-DOWN (Y=0 at top, positive Y = down).
                        // Our world is Y-UP (positive Y = up). Invert: world_y = -img.y
                        // Player at y=-93 in params → world y=+93 (above center). Correct.
                        // Floor at y=225 in params → world y=-225 (below center). Correct.
                        float world_x = img.x - parallax_shift;
                        float world_y = -img.y;
                        float sx = (world_x - img.w/2.0f - left) / (right - left) * platform_->window_width();
                        float sy = (1.0f - (world_y - img.h/2.0f - bottom) / (top - bottom)) * platform_->window_height();
                        float ex = (world_x + img.w/2.0f - left) / (right - left) * platform_->window_width();
                        float ey = (1.0f - (world_y + img.h/2.0f - bottom) / (top - bottom)) * platform_->window_height();
                        float x = std::min(sx, ex), y = std::min(sy, ey);
                        float w = std::abs(ex - sx), h = std::abs(ey - sy);
                        renderer_->draw_filled_rect_screen(x, y, w, h, c);
                    }
                    continue;
                }
                auto it = atlases_.find(img.atlas_name);
                if (it == atlases_.end()) continue;
                auto& atlas = it->second;
                if (!atlas.texture || !atlas.atlas) continue;
                auto fit = atlas.atlas->name_index.find(img.class_name + ".png");
                if (fit == atlas.atlas->name_index.end()) {
                    fit = atlas.atlas->name_index.find(img.class_name);
                    if (fit == atlas.atlas->name_index.end()) continue;
                }
                auto& frame = atlas.atlas->frames[fit->second];
                
                // For rotated frames, use pre-cropped un-rotated texture
                std::string crop_name = img.class_name;
                if (atlas.cropped.count(crop_name)) {
                    // Use pre-cropped texture (already un-rotated)
                    auto& ctex = atlas.cropped[crop_name];
                    float world_y = -img.y;
                    float world_x = img.x - parallax_shift;
                    float quad_w = img.w;
                    float quad_h = img.h;
                    float px = world_x - quad_w / 2.0f;
                    float py = world_y - quad_h / 2.0f;
                    if (parallax_factor < 0.99f) {
                        float hw = (float)platform_->window_width() / (2.0f * zoom_);
                        float vis_left = cam_x_ - hw;
                        float vis_right = cam_x_ + hw;
                        float tile_w = quad_w;
                        float start_x = px;
                        while (start_x + tile_w > vis_left) start_x -= tile_w;
                        while (start_x < vis_left) start_x += tile_w;
                        start_x -= tile_w;
                        for (float tx = start_x; tx < vis_right; tx += tile_w) {
                            renderer_->draw_textured_quad(*ctex, tx, py, quad_w, quad_h);
                        }
                    } else {
                        renderer_->draw_textured_quad(*ctex, px, py,
                                                      quad_w, quad_h);
                    }
                    continue;
                }
                
                // Non-rotated frame: use atlas texture with UV mapping
                float tw = (float)atlas.atlas->metadata.texture_w;
                float th = (float)atlas.atlas->metadata.texture_h;
                float u0 = frame.atlas_x / tw;
                float v0 = frame.atlas_y / th;
                float u1 = (frame.atlas_x + frame.atlas_w) / tw;
                float v1 = (frame.atlas_y + frame.atlas_h) / th;
                float world_y = -img.y;
                float world_x = img.x - parallax_shift;
                float quad_w = img.w;
                float quad_h = img.h;
                float px = world_x - quad_w / 2.0f;
                float py = world_y - quad_h / 2.0f;  // bottom-left (world Y-UP: +Y = up)
                // For parallax layers (factor < 1), tile the image horizontally
                // to fill the screen. This prevents the background from flying
                // off-screen when the camera moves.
                if (parallax_factor < 0.99f) {
                    // Calculate visible world range
                    float hw = (float)platform_->window_width() / (2.0f * zoom_);
                    float vis_left = cam_x_ - hw;
                    float vis_right = cam_x_ + hw;
                    // Tile from leftmost visible to rightmost visible
                    float tile_w = quad_w;
                    float start_x = px;
                    // Find the leftmost tile that's visible
                    while (start_x + tile_w > vis_left) start_x -= tile_w;
                    while (start_x < vis_left) start_x += tile_w;
                    start_x -= tile_w;  // go one more to the left for safety
                    for (float tx = start_x; tx < vis_right; tx += tile_w) {
                        renderer_->draw_textured_quad(*atlas.texture, tx, py, quad_w, quad_h,
                                                      u0, v0, u1, v1);
                    }
                } else {
                    // For foreground layers (factor = 1.0), render once.
                    // No overlap — the pre-cropped texture fix should resolve gaps.
                    renderer_->draw_textured_quad(*atlas.texture, px, py, quad_w, quad_h,
                                                  u0, v0, u1, v1);
                }
            }
        }
        loc_logged = true;
    }

    // ---------- Skeleton ----------
    void load_skeleton() {
        auto candidates = model_paths(asset_root_, "skeleton.xml");
        std::string path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { path = p.string(); break; }
        }
        if (path.empty()) { std::printf("  skeleton.xml NOT FOUND!\n"); return; }
        auto xml = read_text(path);

        // Parse <Nodes> section — find ALL Type="Node" tags (including Weapon-Node)
        auto nodes_start = xml.find("<Nodes>");
        auto nodes_end = xml.find("</Nodes>");
        if (nodes_start == std::string::npos || nodes_end == std::string::npos) return;
        std::string nodes_xml = xml.substr(nodes_start, nodes_end - nodes_start);
        size_t pos = 0;
        while ((pos = nodes_xml.find("Type=\"Node\"", pos)) != std::string::npos) {
            auto ts = nodes_xml.rfind('<', pos);
            auto end = nodes_xml.find("/>", pos);
            if (ts == std::string::npos || end == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, end - ts);
            auto sp = tag.find(' ');
            if (sp != std::string::npos) {
                SkelNode n;
                n.name = tag.substr(1, sp - 1);
                n.x = tof(xml_attr(tag, "X"));
                n.y = tof(xml_attr(tag, "Y"));
                n.z = tof(xml_attr(tag, "Z"));
                skeleton_nodes_[n.name] = n;
            }
            pos = end + 2;
        }
        
        // Also parse Type="MacroNode" tags — these are weighted-average nodes
        // (e.g., MacroNode1_1, MacroNode2_1) used by capsule edges.
        // They have direct X, Y, Z rest-pose coordinates in the XML.
        pos = 0;
        int macro_count = 0;
        while ((pos = nodes_xml.find("Type=\"MacroNode\"", pos)) != std::string::npos) {
            auto ts = nodes_xml.rfind('<', pos);
            auto end = nodes_xml.find("/>", pos);
            if (ts == std::string::npos || end == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, end - ts);
            auto sp = tag.find(' ');
            if (sp != std::string::npos) {
                SkelNode n;
                n.name = tag.substr(1, sp - 1);
                n.x = tof(xml_attr(tag, "X"));
                n.y = tof(xml_attr(tag, "Y"));
                n.z = tof(xml_attr(tag, "Z"));
                skeleton_nodes_[n.name] = n;
                macro_count++;
            }
            pos = end + 2;
        }
        
        // Also parse Type="CenterOfMass" (COM node)
        pos = 0;
        while ((pos = nodes_xml.find("Type=\"CenterOfMass\"", pos)) != std::string::npos) {
            auto ts = nodes_xml.rfind('<', pos);
            auto end = nodes_xml.find("/>", pos);
            if (ts == std::string::npos || end == std::string::npos) break;
            auto tag = nodes_xml.substr(ts, end - ts);
            auto sp = tag.find(' ');
            if (sp != std::string::npos) {
                SkelNode n;
                n.name = tag.substr(1, sp - 1);
                n.x = tof(xml_attr(tag, "X"));
                n.y = tof(xml_attr(tag, "Y"));
                n.z = tof(xml_attr(tag, "Z"));
                skeleton_nodes_[n.name] = n;
            }
            pos = end + 2;
        }
        
        // Build ordered_node_names_ — ALL nodes in XML order.
        // This matches the .bin node order (67 nodes: 54 Node + 1 COM + 12 MacroNode).
        ordered_node_names_.clear();
        pos = 0;
        while (true) {
            // Find next node tag (any Type)
            auto tag_start = nodes_xml.find('<', pos);
            if (tag_start == std::string::npos) break;
            auto tag_end = nodes_xml.find("/>", tag_start);
            if (tag_end == std::string::npos) break;
            auto tag = nodes_xml.substr(tag_start, tag_end - tag_start);
            // Check if this tag has X/Y attributes (is a node)
            if (tag.find("X=\"") != std::string::npos && tag.find("Y=\"") != std::string::npos) {
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    std::string name = tag.substr(1, sp - 1);
                    ordered_node_names_.push_back(name);
                }
            }
            pos = tag_end + 2;
        }
        std::printf("  Skeleton: %zu nodes (%d MacroNodes, ordered: %zu)\n",
                    skeleton_nodes_.size(), macro_count, ordered_node_names_.size());

        // Parse <Edges> section for Edge and Muscle types
        auto edges_start = xml.find("<Edges>");
        auto edges_end = xml.find("</Edges>");
        if (edges_start != std::string::npos && edges_end != std::string::npos) {
            std::string es = xml.substr(edges_start, edges_end - edges_start);
            size_t ep = 0;
            while (true) {
                auto p1 = es.find("Type=\"Edge\"", ep);
                auto p2 = es.find("Type=\"Muscle\"", ep);
                size_t tp;
                if (p1 == std::string::npos && p2 == std::string::npos) break;
                if (p1 == std::string::npos) tp = p2;
                else if (p2 == std::string::npos) tp = p1;
                else tp = std::min(p1, p2);
                auto ts = es.rfind('<', tp);
                auto end = es.find("/>", tp);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = es.substr(ts, end - ts);
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    SkelEdge e;
                    e.name = tag.substr(1, sp - 1);
                    e.end1 = xml_attr(tag, "End1");
                    e.end2 = xml_attr(tag, "End2");
                    e.radius = tof(xml_attr(tag, "Radius"));
                    skeleton_edges_[e.name] = e;
                }
                ep = end + 2;
            }
        }
        std::printf("  Skeleton: %zu edges\n", skeleton_edges_.size());
    }

    // ---------- Body model (body.xml) ----------
    void load_body_model() {
        auto candidates = model_paths(asset_root_, "body.xml");
        std::string path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) { path = p.string(); break; }
        }
        if (path.empty()) { std::printf("  body.xml NOT FOUND!\n"); return; }
        auto xml = read_text(path);
        body_model_ = std::make_unique<BodyModel>();

        // Parse <Nodes> for BODY-NodeN and BODY-MacroNodeN
        auto nodes_start = xml.find("<Nodes>");
        auto nodes_end = xml.find("</Nodes>");
        if (nodes_start != std::string::npos && nodes_end != std::string::npos) {
            std::string ns = xml.substr(nodes_start, nodes_end - nodes_start);
            size_t pos = 0;
            while ((pos = ns.find("<BODY-", pos)) != std::string::npos) {
                auto tag_start = ns.find("Type=\"", pos);
                auto end = ns.find("/>", pos);
                if (end == std::string::npos) break;
                if (tag_start != std::string::npos && tag_start < end) {
                    auto type_end = ns.find('"', tag_start + 6);
                    std::string type = ns.substr(tag_start + 6, type_end - tag_start - 6);
                    auto tag = ns.substr(pos, end - pos);
                    auto sp = tag.find(' ');
                    std::string name = tag.substr(1, sp - 1);
                    if (type == "Node") {
                        BodyNode n; n.name = name;
                        n.x = tof(xml_attr(tag, "X"));
                        n.y = tof(xml_attr(tag, "Y"));
                        n.z = tof(xml_attr(tag, "Z"));
                        body_model_->nodes[n.name] = n;
                    } else if (type == "MacroNode") {
                        BodyMacroNode mn; mn.name = name;
                        mn.children[0] = xml_attr(tag, "ChildNode1");
                        mn.children[1] = xml_attr(tag, "ChildNode2");
                        mn.children[2] = xml_attr(tag, "ChildNode3");
                        mn.children[3] = xml_attr(tag, "ChildNode4");
                        mn.lcc[0] = tof(xml_attr(tag, "LCC1"));
                        mn.lcc[1] = tof(xml_attr(tag, "LCC2"));
                        mn.lcc[2] = tof(xml_attr(tag, "LCC3"));
                        mn.lcc[3] = tof(xml_attr(tag, "LCC4"));
                        body_model_->macro_nodes[mn.name] = mn;
                    }
                }
                pos = end + 2;
            }
        }

        // Parse <Edges>
        auto edges_start = xml.find("<Edges>");
        auto edges_end = xml.find("</Edges>");
        if (edges_start != std::string::npos && edges_end != std::string::npos) {
            std::string es = xml.substr(edges_start, edges_end - edges_start);
            size_t pos = 0;
            while ((pos = es.find("<BODY-", pos)) != std::string::npos) {
                auto tag_start = es.find("Type=\"", pos);
                auto end = es.find("/>", pos);
                if (end == std::string::npos) break;
                if (tag_start != std::string::npos && tag_start < end) {
                    auto type_end = es.find('"', tag_start + 6);
                    std::string type = es.substr(tag_start + 6, type_end - tag_start - 6);
                    if (type == "Edge") {
                        auto tag = es.substr(pos, end - pos);
                        auto sp = tag.find(' ');
                        BodyEdge e; e.name = tag.substr(1, sp - 1);
                        e.end1 = xml_attr(tag, "End1");
                        e.end2 = xml_attr(tag, "End2");
                        e.radius = tof(xml_attr(tag, "Radius"));
                        e.collisible = (xml_attr(tag, "Collisible") == "1");
                        body_model_->edges.push_back(e);
                    }
                }
                pos = end + 2;
            }
        }

        // Parse <Figures> for Capsules and Triangles
        auto figs_start = xml.find("<Figures>");
        auto figs_end = xml.find("</Figures>");
        if (figs_start != std::string::npos && figs_end != std::string::npos) {
            std::string fs = xml.substr(figs_start, figs_end - figs_start);
            size_t pos = 0;
            while ((pos = fs.find("Type=\"Capsule\"", pos)) != std::string::npos) {
                auto ts = fs.rfind('<', pos);
                auto end = fs.find("/>", pos);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = fs.substr(ts, end - ts);
                BodyCapsule c;
                c.edge_name = xml_attr(tag, "Edge");
                c.radius1 = tof(xml_attr(tag, "Radius1"));
                c.radius2 = tof(xml_attr(tag, "Radius2"));
                c.margin1 = tof(xml_attr(tag, "Margin1"));
                c.margin2 = tof(xml_attr(tag, "Margin2"));
                body_model_->capsules.push_back(c);
                pos = end + 2;
            }
            pos = 0;
            while ((pos = fs.find("Type=\"Triangle\"", pos)) != std::string::npos) {
                auto ts = fs.rfind('<', pos);
                auto end = fs.find("/>", pos);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = fs.substr(ts, end - ts);
                BodyTriangle t;
                t.n1 = xml_attr(tag, "Node1");
                t.n2 = xml_attr(tag, "Node2");
                t.n3 = xml_attr(tag, "Node3");
                body_model_->triangles.push_back(t);
                pos = end + 2;
            }
        }
        std::printf("  Body model: %zu nodes, %zu edges, %zu capsules, %zu triangles\n",
                    body_model_->nodes.size(), body_model_->edges.size(),
                    body_model_->capsules.size(), body_model_->triangles.size());
    }

    // Resolve a node name to world coordinates (handles BodyNode, SkelNode, MacroNode).
    std::pair<float, float> resolve_body_node(const std::string& name,
                                              float world_cx, float world_cy,
                                              bool face_right, float pivot_local_y) {
        if (!body_model_) return {world_cx, world_cy};

        // Check if this node has an animated position (from .bin animation)
        auto ait = anim_node_pos_.find(name);
        if (ait != anim_node_pos_.end()) {
            float lx = ait->second.first, ly = ait->second.second;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }

        auto bit = body_model_->nodes.find(name);
        if (bit != body_model_->nodes.end()) {
            float lx = bit->second.x, ly = bit->second.y;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }
        auto sit = skeleton_nodes_.find(name);
        if (sit != skeleton_nodes_.end()) {
            float lx = sit->second.x, ly = sit->second.y;
            float sx = (face_right ? lx : -lx) * 1.0f;
            float sy = world_cy + (ly - pivot_local_y) * 1.0f;
            return {world_cx + sx, sy};
        }
        auto mit = body_model_->macro_nodes.find(name);
        if (mit != body_model_->macro_nodes.end()) {
            float sum_lcc = 0, wx = 0, wy = 0;
            for (int i = 0; i < 4; ++i) {
                if (mit->second.children[i].empty()) continue;
                auto [cx, cy] = resolve_body_node(mit->second.children[i],
                                                  world_cx, world_cy, face_right, pivot_local_y);
                wx += cx * mit->second.lcc[i];
                wy += cy * mit->second.lcc[i];
                sum_lcc += mit->second.lcc[i];
            }
            if (std::abs(sum_lcc) > 1e-6f)
                return {wx / sum_lcc, wy / sum_lcc};
        }
        return {world_cx, world_cy};
    }

    // Render body model as capsule lines (GL renderer uses thin lines for now).
    void render_body_model() {
        if (!body_model_) return;
        auto pivot_it = skeleton_nodes_.find("NPivot");
        float pivot_local_y = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 170.0f;

        // Y normalization: keep character at correct height.
        //
        // The .bin animation stores absolute node Y for all nodes.
        // anim_node_pos_[name].y = (abs_y - npivot_y + npivot_rest_y)
        // resolve_body_node: sy = world_cy + (ly - pivot_local_y)
        //
        // For NPivot: sy = world_cy (since ly = npivot_rest_y for NPivot)
        // So world_cy = NPivot world position.
        //
        // player_pos_y_ (-93) is the NPivot world position from params.xml.
        // y_adjust = 0 positions NPivot at player_pos_y_.
        //
        // But there's a +4 offset needed to align feet with floor surface:
        // Floor at world_y = -193 (layer_3 at y=225, height=64, surface = -225+32)
        // NPivot at -93. Feet (NToe) at -93 + (65.52 - 169.48) = -196.96
        // Floor surface at -193. Feet are 3.96 below surface.
        // y_adjust = +4 shifts everything up so feet are at -192.96 ≈ floor.
        //
        // For crouch: NPivot goes to 106.21 (down from 169.48).
        // NToe abs_y = 2.24 (stays at floor).
        // anim_node_pos_.y = (2.24 - 106.21 + 169.48) = 65.51
        // sy = (-93+4) + (65.51 - 169.48) = -89 - 103.97 = -192.97. ON FLOOR! ✓
        //
        // For jump: NPivot goes to 243.93 (up from 169.48).
        // NToe abs_y = 189.15 (feet go up).
        // anim_node_pos_.y = (189.15 - 243.93 + 169.48) = 114.70
        // sy = (-93+4) + (114.70 - 169.48) = -89 - 54.78 = -143.78
        // Floor at -193. Feet at -143.78 — 49 ABOVE floor! ✓ (character jumped up)
        //
        // For roll: NPivot goes to 20.11 (very low).
        // NToe abs_y = 0 (feet at floor).
        // anim_node_pos_.y = (0 - 20.11 + 169.48) = 149.37
        // sy = (-93+4) + (149.37 - 169.48) = -89 - 20.11 = -109.11
        // Floor at -193. Feet at -109 — 84 ABOVE floor! ✗ (character floating)
        //
        // Problem: roll has NPivot very low but feet at floor.
        // The formula sy = world_cy + abs_y - npivot_y gives:
        //   sy = -89 + 0 - 20.11 = -109.11 (wrong, should be -193)
        //
        // Wait: anim_node_pos_.y = (abs_y - npivot_y + npivot_rest_y)
        // sy = world_cy + (ly - pivot_local_y)
        //    = world_cy + (abs_y - npivot_y + npivot_rest_y - npivot_rest_y)
        //    = world_cy + abs_y - npivot_y
        // For roll: sy = -89 + 0 - 20.11 = -109.11. WRONG.
        //
        // But abs_y for NToe in roll = 0 (feet at floor in .bin).
        // npivot_y = 20.11. So abs_y - npivot_y = -20.11.
        // sy = world_cy - 20.11 = -89 - 20.11 = -109.11.
        //
        // For feet at floor (-193): world_cy = -193 + 20.11 = -172.89.
        // y_adjust = -172.89 - (-93) = -79.89.
        //
        // This is the lowest-node approach! But it doesn't work for jump
        // because lowest node stays low during jump.
        //
        // SOLUTION: Use NPivot-based y_adjust for jumps, lowest-node for rolls.
        // But we can't easily distinguish them.
        //
        // BETTER SOLUTION: The original game uses MoveInside alignment which
        // aligns a specific pivot node (NHeel_1 or NHeel_2) to the floor.
        // We should use NHeel_1 Y for alignment, not NPivot or lowest node.
        //
        // For now: use constant y_adjust = 4 (works for standing, crouch, jump).
        // Roll issue: character floats during roll, but roll is short (26 frames).
        // This is acceptable until we implement proper MoveInside alignment.
        constexpr float FEET_FLOOR_OFFSET = 4.0f;
        // [ORIGINAL] MoveInside pipeline byte-verified (objdump on ShadowFight2.s86):
        //   Step 1 (fcn.10165c10): captures pivotID -> Model+0x58, node_array[pivotID] -> Model+0x5c
        //   Step 2 (fcn.10164c20): resolves new pivotID, calls fcn.10103690 (trivial accessor:
        //     return this+0x7c, 3 bytes), then fcn.10103e80(axis=2) — called ONCE in entire binary
        //   Step 3 (fcn.101661d0): reads Model[0xe8][axis=2][pivotID] (Vec3) via fcn.1028e490 (Vec3 copy)
        //   Post-Step3: playInfo copies Z->X and Z->Y (memcpy). All axes get same Vec3.
        //   fcn.1028e490 = Vec3 copy, fcn.1028e4c0 = Vec3 add, fcn.10102c70 = container accessor
        // [HEURISTIC-TODO] consumption formula (how Vec3 -> world transform) NOT yet traced.
        //
        // [ORIGINAL] MoveInside Y alignment — VERIFIED from PC version sf2.js.
        // See update_animation() for full documentation.
        // y_adjust = ShiftY = 0 for all Axis="X|Z" moves (verified from moves.xml).
        // y_adjust_smoothed_ is computed in update_animation() (before hit detection).
        // Here we just USE the already-computed value.
        float world_cx = player_pos_x_;
        float world_cy = player_pos_y_ + y_adjust_smoothed_;

        // Build edge lookup from both body.xml edges and skeleton.xml edges
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : body_model_->edges)
            edge_map[e.name] = {e.end1, e.end2};
        for (auto& [name, e] : skeleton_edges_)
            edge_map[name] = {e.end1, e.end2};

        // World-to-screen helper (for capsules that still use screen-space)
        float hw = (float)platform_->window_width() / (2.0f * zoom_);
        float hh = (float)platform_->window_height() / (2.0f * zoom_);
        float left = cam_x_ - hw, right = cam_x_ + hw;
        float bottom = cam_y_ - hh, top = cam_y_ + hh;
        auto w2s = [&](float wx, float wy, float& sx, float& sy) {
            sx = (wx - left) / (right - left) * platform_->window_width();
            sy = (1.0f - (wy - bottom) / (top - bottom)) * platform_->window_height();
        };

        // Render character as unified dark silhouette.
        // Render ALL capsules (including duplicates — they overlap to fill gaps
        // at joints). Apply Margin1/Margin2 to trim ends properly.
        ren::Color4B silhouette_col{20, 20, 25, 255};

        for (auto& c : body_model_->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto [x1, y1] = resolve_body_node(eit->second.first,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [x2, y2] = resolve_body_node(eit->second.second,
                world_cx, world_cy, facing_right_, pivot_local_y);
            // Apply margin (trim capsule ends to prevent overlap artifacts)
            float m1 = c.margin1, m2 = c.margin2;
            float mx1 = x1 + (x2 - x1) * m1;
            float my1 = y1 + (y2 - y1) * m1;
            float mx2 = x2 - (x2 - x1) * m2;
            float my2 = y2 - (y2 - y1) * m2;
            
            float r = (c.radius1 + c.radius2) * 0.5f * 1.0f;
            float dx = mx2 - mx1, dy = my2 - my1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            float ax = mx1 + px*ht, ay = my1 + py*ht;
            float bx = mx2 + px*ht, by = my2 + py*ht;
            float cx = mx2 - px*ht, cy_ = my2 - py*ht;
            float dx_ = mx1 - px*ht, dy_ = my1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx, cy_, silhouette_col);
            renderer_->draw_filled_triangle_world(ax, ay, cx, cy_, dx_, dy_, silhouette_col);
            // Circle caps at both ends — fills gaps at joints
            renderer_->draw_filled_circle_world(mx1, my1, ht, silhouette_col);
            renderer_->draw_filled_circle_world(mx2, my2, ht, silhouette_col);
        }

        // Render triangles (small parts)
        // Skip triangles that reference non-animated nodes (BODY-Node entries
        // from body.xml). These are cloth simulation nodes that don't have
        // per-node animation data in the .bin files. Rendering them at their
        // rest-pose positions while other triangle vertices are animated
        // causes visible stretching on the legs (especially around the calves
        // and ankles where BODY-Triangle-7..10 are located).
        for (auto& t : body_model_->triangles) {
            // Check if ALL three vertices are animated (in anim_node_pos_ or skeleton_nodes_)
            bool n1_animated = (anim_node_pos_.find(t.n1) != anim_node_pos_.end()) ||
                               (skeleton_nodes_.find(t.n1) != skeleton_nodes_.end());
            bool n2_animated = (anim_node_pos_.find(t.n2) != anim_node_pos_.end()) ||
                               (skeleton_nodes_.find(t.n2) != skeleton_nodes_.end());
            bool n3_animated = (anim_node_pos_.find(t.n3) != anim_node_pos_.end()) ||
                               (skeleton_nodes_.find(t.n3) != skeleton_nodes_.end());
            if (!n1_animated || !n2_animated || !n3_animated) {
                continue;  // Skip triangles with non-animated cloth nodes
            }
            auto [tx0, ty0] = resolve_body_node(t.n1,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [tx1, ty1] = resolve_body_node(t.n2,
                world_cx, world_cy, facing_right_, pivot_local_y);
            auto [tx2, ty2] = resolve_body_node(t.n3,
                world_cx, world_cy, facing_right_, pivot_local_y);
            renderer_->draw_filled_triangle_world(tx0, ty0, tx1, ty1, tx2, ty2, silhouette_col);
        }
    }

    // ---------- Character rendering ----------
    // Skeleton local coords: Y-UP (0 = feet, positive = up).
    // World coords: Y-UP (cocos2d convention, positive = up).
    //
    // Render ONLY the body silhouette (capsules + triangles).
    // The skeleton lines and joints are NOT rendered — they were causing
    // the "half black, half white squares" effect (white bones drawn over
    // dark silhouette). The original game renders only the silhouette.
    void render_character() {
        // Render body mesh (silhouette from capsules + triangles)
        render_body_model();
        // No skeleton lines, no joints — silhouette only.
    }

    // ---------- Punching bag (real 3D model from skeleton_punching_bag.xml) ----------
    void load_punching_bag_model() {
        auto skel_candidates = model_paths(asset_root_, "skeleton_punching_bag.xml");
        auto fig_candidates = model_paths(asset_root_, "punching_bag.xml");
        std::string skel_path, fig_path;
        for (const auto& p : skel_candidates)
            if (std::filesystem::exists(p)) { skel_path = p.string(); break; }
        for (const auto& p : fig_candidates)
            if (std::filesystem::exists(p)) { fig_path = p.string(); break; }
        if (skel_path.empty()) { std::printf("  skeleton_punching_bag.xml NOT FOUND!\n"); return; }

        auto xml = read_text(skel_path);
        bag_model_ = std::make_unique<BodyModel>();
        // Parse <Nodes>
        auto nodes_start = xml.find("<Nodes>");
        auto nodes_end = xml.find("</Nodes>");
        if (nodes_start != std::string::npos && nodes_end != std::string::npos) {
            std::string ns = xml.substr(nodes_start, nodes_end - nodes_start);
            size_t pos = 0;
            while (true) {
                auto p1 = ns.find("Type=\"Node\"", pos);
                auto p2 = ns.find("Type=\"CenterOfMass\"", pos);
                size_t tp;
                if (p1 == std::string::npos && p2 == std::string::npos) break;
                if (p1 == std::string::npos) tp = p2;
                else if (p2 == std::string::npos) tp = p1;
                else tp = std::min(p1, p2);
                auto ts = ns.rfind('<', tp);
                auto end = ns.find("/>", tp);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = ns.substr(ts, end - ts);
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    BodyNode n;
                    n.name = tag.substr(1, sp - 1);
                    n.x = tof(xml_attr(tag, "X"));
                    n.y = tof(xml_attr(tag, "Y"));
                    n.mass = tof(xml_attr(tag, "Mass"), 1.0f);
                    n.fixed = (toi(xml_attr(tag, "Fixed")) != 0);
                    n.attenuation = tof(xml_attr(tag, "Attenuation"), 0.02f);
                    n.cloth = (toi(xml_attr(tag, "Cloth")) != 0);
                    bag_model_->nodes[n.name] = n;
                }
                pos = end + 2;
            }
        }
        // Parse <Edges>
        auto edges_start = xml.find("<Edges>");
        auto edges_end = xml.find("</Edges>");
        if (edges_start != std::string::npos && edges_end != std::string::npos) {
            std::string es = xml.substr(edges_start, edges_end - edges_start);
            size_t pos = 0;
            while ((pos = es.find("Type=\"Edge\"", pos)) != std::string::npos) {
                auto ts = es.rfind('<', pos);
                auto end = es.find("/>", pos);
                if (ts == std::string::npos || end == std::string::npos) break;
                auto tag = es.substr(ts, end - ts);
                auto sp = tag.find(' ');
                if (sp != std::string::npos) {
                    BodyEdge e;
                    e.name = tag.substr(1, sp - 1);
                    e.end1 = xml_attr(tag, "End1");
                    e.end2 = xml_attr(tag, "End2");
                    e.radius = tof(xml_attr(tag, "Radius"));
                    e.collisible = (xml_attr(tag, "Collisible") == "1");
                    bag_model_->edges.push_back(e);
                }
                pos = end + 2;
            }
        }
        // Parse punching_bag.xml <Figures> for capsules
        if (!fig_path.empty()) {
            auto fxml = read_text(fig_path);
            auto figs_start = fxml.find("<Figures>");
            auto figs_end = fxml.find("</Figures>");
            if (figs_start != std::string::npos && figs_end != std::string::npos) {
                std::string fs = fxml.substr(figs_start, figs_end - figs_start);
                size_t pos = 0;
                while ((pos = fs.find("Type=\"Capsule\"", pos)) != std::string::npos) {
                    auto ts = fs.rfind('<', pos);
                    auto end = fs.find("/>", pos);
                    if (ts == std::string::npos || end == std::string::npos) break;
                    auto tag = fs.substr(ts, end - ts);
                    BodyCapsule c;
                    c.edge_name = xml_attr(tag, "Edge");
                    c.radius1 = tof(xml_attr(tag, "Radius1"));
                    c.radius2 = tof(xml_attr(tag, "Radius2"));
                    c.margin1 = tof(xml_attr(tag, "Margin1"));
                    c.margin2 = tof(xml_attr(tag, "Margin2"));
                    bag_model_->capsules.push_back(c);
                    pos = end + 2;
                }
            }
        }
        std::printf("  Punching bag: %zu nodes, %zu edges, %zu capsules\n",
                    bag_model_->nodes.size(), bag_model_->edges.size(),
                    bag_model_->capsules.size());
        init_bag_verlet();
    }

    // Initialize Verlet physics state from the bag's skeleton nodes.
    // Each node gets: position = (x, y), prev_position = (x, y) (at rest).
    // Fixed nodes (Fixed="1") have inv_mass = 0 and don't move.
    // Edges become distance constraints with rest length = edge.length.
    void init_bag_verlet() {
        if (!bag_model_) return;
        bag_verlet_.clear();
        bag_constraints_.clear();
        // World position of the bag's NPivot (where it hangs in the world)
        // Same coordinate system as player — no Y-invert, use params Y directly
        // with the same -45 offset to align with the floor.
        float bag_cx = location_ ? (location_->enemy_x - 983.0f) : 0.0f;
        float bag_cy = location_ ? (location_->enemy_y + 81.0f) : 0.0f;
        auto pit = bag_model_->nodes.find("NPivot");
        float pivot_ly = pit != bag_model_->nodes.end() ? pit->second.y : 109.0f;
        // Initialize nodes: world position = bag_center + (node_local - NPivot_local)
        for (auto& [name, n] : bag_model_->nodes) {
            VerletNode vn;
            vn.x = bag_cx + n.x * 1.0f;
            vn.y = bag_cy + (n.y - pivot_ly) * 1.0f;
            vn.px = vn.x;  // at rest, prev = current
            vn.py = vn.y;
            vn.mass = n.mass;
            vn.fixed = n.fixed;
            vn.inv_mass = n.fixed ? 0.0f : (n.mass > 0.001f ? 1.0f / n.mass : 1.0f);
            vn.attenuation = n.attenuation;
            bag_verlet_[name] = vn;
        }
        // Initialize constraints from edges
        for (auto& e : bag_model_->edges) {
            VerletConstraint c;
            c.n1 = e.end1;
            c.n2 = e.end2;
            // Compute rest length from actual node distance (or use edge.length)
            auto n1 = bag_verlet_.find(e.end1);
            auto n2 = bag_verlet_.find(e.end2);
            if (n1 != bag_verlet_.end() && n2 != bag_verlet_.end()) {
                float dx = n1->second.x - n2->second.x;
                float dy = n1->second.y - n2->second.y;
                c.length = std::sqrt(dx*dx + dy*dy);
            } else {
                c.length = e.length;
            }
            c.stiffness = 1.0f;
            bag_constraints_.push_back(c);
        }
        bag_verlet_init_ = true;
        std::printf("  Bag Verlet: %zu nodes, %zu constraints (Node12 fixed)\n",
                    bag_verlet_.size(), bag_constraints_.size());
    }

    // Apply an impulse to a bag node (called when hit).
    // Impulse = instantaneous velocity change = position offset added to prev pos.
    // In Verlet: vel = (pos - prev), so to add velocity v, set prev -= v * dt.
    void apply_bag_impulse(const std::string& node_name, float vx, float vy) {
        auto it = bag_verlet_.find(node_name);
        if (it == bag_verlet_.end()) return;
        auto& n = it->second;
        if (n.fixed) return;
        // Original Bl.strike: node.ma += impulse / node.weight
        // where ma = current position, weight = XML Mass attribute.
        // Modifying current position (x) directly — NOT prev (px).
        // In Verlet: x += delta => velocity += delta for next frame.
        n.x += vx * n.inv_mass;
        n.y += vy * n.inv_mass;
    }

    // Update bag Verlet physics.
    // 1. Integration: pos_new = 2*pos - prev + acc*dt^2 (gravity + damping)
    // 2. Satisfy constraints (multiple iterations for stiffness)
    // 3. Apply damping (attenuation)
    void update_bag_verlet(float dt) {
        if (!bag_verlet_init_ || !bag_model_) return;
        const float GRAVITY = -800.0f;  // downward acceleration (heavier bag)
        const int CONSTRAINT_ITERATIONS = 10;
        // 1. Verlet integration
        for (auto& [name, n] : bag_verlet_) {
            if (n.fixed) continue;
            // Verlet: new_pos = pos + (pos - prev) * (1 - attenuation) + acc * dt^2
            float vx = (n.x - n.px) * (1.0f - n.attenuation);
            float vy = (n.y - n.py) * (1.0f - n.attenuation);
            n.px = n.x;
            n.py = n.y;
            n.x += vx;
            n.y += vy + GRAVITY * dt * dt;
        }
        // 2. Satisfy distance constraints
        for (int iter = 0; iter < CONSTRAINT_ITERATIONS; ++iter) {
            for (auto& c : bag_constraints_) {
                auto n1 = bag_verlet_.find(c.n1);
                auto n2 = bag_verlet_.find(c.n2);
                if (n1 == bag_verlet_.end() || n2 == bag_verlet_.end()) continue;
                auto& a = n1->second;
                auto& b = n2->second;
                float dx = b.x - a.x;
                float dy = b.y - a.y;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 0.0001f) continue;
                float diff = (dist - c.length) / dist;
                float w1 = a.inv_mass;
                float w2 = b.inv_mass;
                float wsum = w1 + w2;
                if (wsum < 0.0001f) continue;
                float f = c.stiffness * diff;
                a.x += dx * f * (w1 / wsum);
                a.y += dy * f * (w1 / wsum);
                b.x -= dx * f * (w2 / wsum);
                b.y -= dy * f * (w2 / wsum);
            }
        }
    }

    void render_punching_bag() {
        if (!bag_model_ || !location_) return;
        
        // Bag position: enemy_x from params.xml, adjusted to world space
        float bag_cx = location_->enemy_x - 983.0f;
        
        // Bag NPivot Y in model space = 109.0
        // The bag hangs from Node12 (Y=335) which is fixed at ceiling
        // Node12 world Y should be at ceiling level.
        // Ceiling (layer_5) is at params y=-202 → world_y = +202 (inverted).
        // Node12 local Y = 335, NPivot local Y = 109.
        // Node12 world Y = bag_cy + (335 - 109) = bag_cy + 226
        // We need Node12 at world Y = 202 (ceiling):
        //   bag_cy + 226 = 202 → bag_cy = -24
        // enemy_y = -105. bag_cy = enemy_y + offset = -105 + 81 = -24. ✓
        //
        // BUT: the bag appears too high. The issue is that the player's
        // y_adjust_smoothed_ adds ~+82 units (REF_FEET_LY - ly_lowest),
        // making the player appear higher. The bag doesn't have this
        // adjustment, so it looks relatively higher.
        //
        // FIX: apply the same y_adjust to the bag's rendering Y, so the
        // bag and player are in the same coordinate space.
        auto pit = bag_model_->nodes.find("NPivot");
        float pivot_ly = pit != bag_model_->nodes.end() ? pit->second.y : 109.0f;
        float bag_cy = location_->enemy_y + 81.0f + y_adjust_smoothed_;
        
        // === BAG RENDERING (Verlet physics) ===
        // The bag's skeleton nodes are simulated with Verlet integration.
        // Node12 is fixed (ceiling attachment). Other nodes swing freely
        // subject to gravity + distance constraints (edges).
        // We render capsules using the current Verlet node positions.
        
        // Build edge lookup
        std::unordered_map<std::string, std::pair<std::string, std::string>> edge_map;
        for (auto& e : bag_model_->edges) {
            edge_map[e.name] = {e.end1, e.end2};
        }
        
        // Render bag as unified silhouette (same approach as character)
        ren::Color4B bag_body_col{35, 35, 40, 255};      // dark neutral for bag body
        ren::Color4B bag_chain_col{160, 160, 160, 255};   // gray for chain
        
        for (auto& c : bag_model_->capsules) {
            auto eit = edge_map.find(c.edge_name);
            if (eit == edge_map.end()) continue;
            auto& en1 = eit->second.first;
            auto& en2 = eit->second.second;
            
            // Get node positions from Verlet state (if available) or fall back to rest pose
            float x1, y1, x2, y2;
            if (bag_verlet_init_) {
                auto v1 = bag_verlet_.find(en1);
                auto v2 = bag_verlet_.find(en2);
                if (v1 == bag_verlet_.end() || v2 == bag_verlet_.end()) continue;
                x1 = v1->second.x;
                y1 = v1->second.y;
                x2 = v2->second.x;
                y2 = v2->second.y;
            } else {
                auto nit1 = bag_model_->nodes.find(en1);
                auto nit2 = bag_model_->nodes.find(en2);
                if (nit1 == bag_model_->nodes.end() || nit2 == bag_model_->nodes.end()) continue;
                x1 = bag_cx + nit1->second.x * 1.0f;
                y1 = bag_cy + (nit1->second.y - pivot_ly) * 1.0f;
                x2 = bag_cx + nit2->second.x * 1.0f;
                y2 = bag_cy + (nit2->second.y - pivot_ly) * 1.0f;
            }
            
            float r = (c.radius1 + c.radius2) * 0.5f * 1.0f;
            bool is_main = (c.radius1 >= 20 || c.radius2 >= 20);
            
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len < 0.5f) continue;
            float ux = dx / len, uy = dy / len;
            float px = -uy, py = ux;
            float ht = std::max(r, 1.0f);
            
            ren::Color4B col = is_main ? bag_body_col : bag_chain_col;
            float ax = x1 + px*ht, ay = y1 + py*ht;
            float bx = x2 + px*ht, by = y2 + py*ht;
            float cx_ = x2 - px*ht, cy_ = y2 - py*ht;
            float dx_ = x1 - px*ht, dy_ = y1 - py*ht;
            renderer_->draw_filled_triangle_world(ax, ay, bx, by, cx_, cy_, col);
            renderer_->draw_filled_triangle_world(ax, ay, cx_, cy_, dx_, dy_, col);
            renderer_->draw_filled_circle_world(x1, y1, ht, col);
            renderer_->draw_filled_circle_world(x2, y2, ht, col);
        }
    }

    // ---------- HUD textures (real game textures) ----------
    // ---------- Animation loading (DYNAMIC: scan directory) ----------
    void load_animations() {
        auto root = std::filesystem::path(asset_root_);
        // Search for animation .bin files in multiple paths
        std::vector<std::filesystem::path> search_dirs = {
            root/"assets"/"assets"/"animations"/"binary",
            root/"assets"/"animations"/"binary",
            root/"animations"/"binary",
            root/"assets"/"assets"/"animations",
            root/"assets"/"animations",
            root/"animations",
        };
        
        // Find the first directory that exists and has .bin files
        std::filesystem::path anim_dir;
        for (auto& dir : search_dirs) {
            if (std::filesystem::exists(dir) && !std::filesystem::is_empty(dir)) {
                anim_dir = dir;
                break;
            }
        }
        
        if (anim_dir.empty()) {
            std::printf("  Animations: NO DIRECTORY FOUND! Searched:\n");
            for (auto& dir : search_dirs) std::printf("    %s\n", dir.string().c_str());
            return;
        }
        
        // Dynamically load ALL .bin files from the directory
        int loaded = 0;
        for (auto& entry : std::filesystem::directory_iterator(anim_dir)) {
            if (entry.path().extension() != ".bin") continue;
            std::string name = entry.path().stem().string();
            if (animations_.count(name)) continue;  // already loaded
            AnimationData anim;
            anim.name = name;
            if (anim.load(entry.path().string())) {
                animations_[name] = std::move(anim);
                loaded++;
            }
        }
        
        // Also load from moves.xml FileName references (in case some are in subdirs)
        for (auto& [move_name, move] : moves_) {
            if (move.filename.empty()) continue;
            std::string anim_name = move.filename;
            // Remove .bin extension if present
            if (anim_name.size() > 4 && anim_name.substr(anim_name.size()-4) == ".bin")
                anim_name = anim_name.substr(0, anim_name.size()-4);
            if (animations_.count(anim_name)) continue;
            for (auto& dir : search_dirs) {
                auto path = dir / (anim_name + ".bin");
                if (std::filesystem::exists(path)) {
                    AnimationData anim;
                    anim.name = anim_name;
                    if (anim.load(path.string())) {
                        animations_[anim_name] = std::move(anim);
                        loaded++;
                    }
                    break;
                }
            }
        }
        
        // Map common names to actual files
        if (animations_.count("fists1_stance_idle") && !animations_.count("fists_idle")) {
            animations_["fists_idle"] = animations_["fists1_stance_idle"];
        }
        
        std::printf("  Animations loaded: %zu (from %s)\n", animations_.size(), anim_dir.string().c_str());
    }

    // ---------- Move definitions (from moves.xml) ----------
    void load_moves() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> search_dirs = {
            root/"assets"/"assets"/"animations",
            root/"assets"/"animations",
            root/"animations",
            root/"assets"/"assets",
            root/"assets",
        };
        
        std::string moves_path;
        for (auto& dir : search_dirs) {
            auto path = dir / "moves.xml";
            if (std::filesystem::exists(path)) { moves_path = path.string(); break; }
        }
        if (moves_path.empty()) {
            std::printf("  moves.xml NOT FOUND!\n");
            return;
        }
        
        auto xml = read_text(moves_path);
        // Simple XML parser for <Move> tags
        size_t pos = 0;
        while ((pos = xml.find("<Move ", pos)) != std::string::npos) {
            // Skip commented out moves
            if (pos > 4 && xml.substr(pos - 4, 4) == "<!--") {
                pos += 6;
                continue;
            }
            
            auto end_tag = xml.find(">", pos);
            if (end_tag == std::string::npos) break;
            auto tag = xml.substr(pos, end_tag - pos);
            
            MoveDef move;
            move.name = xml_attr(tag, "Name");
            move.filename = xml_attr(tag, "FileName");
            move.template_name = xml_attr(tag, "Template");
            move.first_frame = (int)tof(xml_attr(tag, "FirstFrame"));
            move.end_frame = (int)tof(xml_attr(tag, "EndFrame"));
            move.priority = (int)tof(xml_attr(tag, "Priority"));
            
            move.tactic_weapon = xml_attr(tag, "TacticWeapon");
            // [ORIGINAL] Parse Type="ATTACK" or Type="MOVE" from moves.xml
            std::string move_type_attr = xml_attr(tag, "Type");
            move.is_attack = (move_type_attr == "ATTACK");
            
            // Parse Template string into structured data
            // Format: "1key|Central|Unarmed|Punch" or "2key|Forward|Unarmed|Kick"
            if (!move.template_name.empty()) {
                std::string tmpl = move.template_name;
                // Split by '|'
                size_t start = 0;
                std::vector<std::string> parts;
                while (start < tmpl.size()) {
                    auto sep = tmpl.find('|', start);
                    if (sep == std::string::npos) { parts.push_back(tmpl.substr(start)); break; }
                    parts.push_back(tmpl.substr(start, sep - start));
                    start = sep + 1;
                }
                // First part: key count (1key, 2key, 3key)
                for (auto& p : parts) {
                    if (p == "1key") move.key_count = 1;
                    else if (p == "2key") move.key_count = 2;
                    else if (p == "3key") move.key_count = 3;
                    // Direction
                    else if (p == "Central") move.direction = "Central";
                    else if (p == "Forward") move.direction = "Forward";
                    else if (p == "Back") move.direction = "Back";
                    else if (p == "Up") move.direction = "Up";
                    else if (p == "Down") move.direction = "Down";
                    else if (p == "UpForward") move.direction = "UpForward";
                    else if (p == "UpBack") move.direction = "UpBack";
                    else if (p == "DownForward") move.direction = "DownForward";
                    else if (p == "DownBack") move.direction = "DownBack";
                    // Type
                    else if (p == "Punch") move.move_type = "Punch";
                    else if (p == "Kick") move.move_type = "Kick";
                    else if (p == "TitanKick") move.move_type = "TitanKick";  // Titan-only
                    else if (p == "Jump") { move.move_type = "Jump"; move.is_jump = true; }
                    else if (p == "Retreat") { move.is_retreat = true; }
                    else if (p == "Step") { move.is_step = true; }
                    else if (p == "DoubleStep") { move.is_double_step = true; move.is_step = true; }
                    else if (p == "Block") { move.is_block = true; }
                    else if (p == "Stance") { move.is_stance = true; }
                    else if (p == "IdleStance") { move.is_stance = true; move.is_idle = true; }
                    else if (p == "Unarmed") { move.is_unarmed = true; move.weapon_filter = "Unarmed"; }
                    else if (p == "NotTitan") { move.is_not_titan = true; }
                }
            }
            
            // Find </Move> to get inner content
            auto move_end = xml.find("</Move>", pos);
            if (move_end == std::string::npos) { pos = end_tag; continue; }
            auto inner = xml.substr(end_tag + 1, move_end - end_tag - 1);
            
            // Parse Intervals
            size_t ip = 0;
            while ((ip = inner.find("Type=\"Attack\"", ip)) != std::string::npos ||
                   (ip = inner.find("Name=\"Attack\"", ip)) != std::string::npos) {
                auto ts = inner.rfind('<', ip);
                auto te = inner.find("/>", ip);
                if (ts == std::string::npos || te == std::string::npos) break;
                auto iv_tag = inner.substr(ts, te - ts);
                move.attack_start = (int)tof(xml_attr(iv_tag, "Start"));
                move.attack_end = (int)tof(xml_attr(iv_tag, "End"));
                ip = te + 2;
            }
            
            // Parse attack edges
            ip = 0;
            while ((ip = inner.find("<Edge ", ip)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) break;
                auto e_tag = inner.substr(ip, te - ip);
                auto ename = xml_attr(e_tag, "Name");
                if (!ename.empty()) move.attack_edges.push_back(ename);
                ip = te + 2;
            }
            
            // Parse damage
            ip = 0;
            while ((ip = inner.find("<Damage ", ip)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) break;
                auto d_tag = inner.substr(ip, te - ip);
                auto val = xml_attr(d_tag, "Value");
                if (!val.empty()) {
                    move.damage = tof(val);
                    break;  // take first damage value
                }
                ip = te + 2;
            }

            // Parse Impulse (from <Impulse X="..." Y="..." Z="..."/> within Attack interval)
            // [ORIGINAL] JS: Ul.init() line 24639-24640 reads kw/gR/hR from Impulse XML attribute.
            // Used in Kwb() line 15467 to create the impulse vector applied to the target.
            ip = inner.find("<Impulse ");
            if (ip != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te != std::string::npos) {
                    auto imp_tag = inner.substr(ip, te - ip);
                    move.impulse_x = tof(xml_attr(imp_tag, "X"));
                    move.impulse_y = tof(xml_attr(imp_tag, "Y"));
                }
            }
            
            // Parse keys
            ip = 0;
            while ((ip = inner.find("<Key ", ip)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) break;
                auto k_tag = inner.substr(ip, te - ip);
                auto ktype = xml_attr(k_tag, "Type");
                if (!ktype.empty()) move.key_types.push_back(ktype);
                ip = te + 2;
            }
            
            // Parse Block interval
            ip = 0;
            if ((ip = inner.find("Type=\"Block\"", 0)) != std::string::npos) {
                auto ts = inner.rfind('<', ip);
                auto te = inner.find("/>", ip);
                if (ts != std::string::npos && te != std::string::npos) {
                    auto b_tag = inner.substr(ts, te - ts);
                    move.block_start = (int)tof(xml_attr(b_tag, "Start"));
                }
            }
            
            // Parse Uninterrupt interval
            ip = 0;
            if ((ip = inner.find("Name=\"Uninterrupt\"", 0)) != std::string::npos) {
                auto ts = inner.rfind('<', ip);
                auto te = inner.find("/>", ip);
                if (ts != std::string::npos && te != std::string::npos) {
                    auto u_tag = inner.substr(ts, te - ts);
                    move.uninterrupt_start = (int)tof(xml_attr(u_tag, "Start"));
                    move.uninterrupt_end = (int)tof(xml_attr(u_tag, "End"));
                }
            }

            // Parse Distance condition (from <Distance Min=".." Max=".." Axis="X">)
            // Used to pick correct move based on enemy distance.
            // Example: LowKick (Max=100) vs Sweep (no distance) — pick by distance.
            ip = 0;
            if ((ip = inner.find("<Distance ", 0)) != std::string::npos) {
                auto te = inner.find(">", ip);
                if (te != std::string::npos) {
                    auto d_tag = inner.substr(ip, te - ip);
                    auto dmin = xml_attr(d_tag, "Min");
                    auto dmax = xml_attr(d_tag, "Max");
                    if (!dmin.empty()) move.distance_min = tof(dmin);
                    if (!dmax.empty()) move.distance_max = tof(dmax);
                    move.has_distance_cond = true;
                }
            }

            // Parse Locks section (perks, weapon subtype requirements)
            // <Locks><Perk Name="PERK_DOUBLE_SWEEP"/></Locks>
            // <Locks><Item Type="Weapon" SubType="Fists"/></Locks>
            ip = 0;
            if ((ip = inner.find("<Perk ", 0)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te != std::string::npos) {
                    auto p_tag = inner.substr(ip, te - ip);
                    auto pname = xml_attr(p_tag, "Name");
                    if (!pname.empty()) move.required_perk = pname;
                }
            }
            ip = 0;
            while ((ip = inner.find("<Item ", ip)) != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) break;
                auto i_tag = inner.substr(ip, te - ip);
                auto itype = xml_attr(i_tag, "Type");
                auto isub = xml_attr(i_tag, "SubType");
                if (itype == "Weapon" && !isub.empty()) {
                    move.required_weapon_subtype = isub;
                }
                ip = te + 2;
            }

            // [ORIGINAL] Parse MoveInside <Pivot> from <Align> section.
            // Binary: moveInside->align.pivotID (moveInside @ animInfo+0x94,
            // pivotID @ moveInside+0x70) is the node index, or -1 when
            // <Pivot Object="Animation"/>. moves.xml names the node via
            // Part="NHeel_2" etc. We store the name; the index lookup happens
            // at render time via ordered_node_names_ (matches the binary's
            // node_array[pivotID] deref in fcn.10165c10).
            ip = inner.find("<Pivot ", 0);
            if (ip != std::string::npos) {
                auto te = inner.find("/>", ip);
                if (te == std::string::npos) te = inner.find(">", ip);
                if (te != std::string::npos) {
                    auto p_tag = inner.substr(ip, te - ip);
                    auto pobj = xml_attr(p_tag, "Object");
                    auto ppart = xml_attr(p_tag, "Part");
                    if (pobj == "Nodes" && !ppart.empty()) {
                        move.moveinside_pivot_node = ppart;
                    } else if (pobj == "Animation") {
                        move.moveinside_is_animation = true;
                    }
                }
            }

            // [ORIGINAL] Parse CurrentAnimation condition from <Conditions>.
            // PC source: sf2.js np.isEqual() (line 42544) — checks if current
            // animation name matches. 3key combos use this to require a specific
            // base attack (e.g., DoublePunch requires CurrentAnimation="HeavyPunch").
            {
                size_t cap = inner.find("CurrentAnimation", 0);
                while (cap != std::string::npos) {
                    // Check it's not inside a comment
                    if (cap > 4 && inner.substr(cap - 4, 4) == "<!--") {
                        cap = inner.find("CurrentAnimation", cap + 1);
                        continue;
                    }
                    auto tag_end = inner.find("/>", cap);
                    if (tag_end == std::string::npos) break;
                    auto tag = inner.substr(cap, tag_end - cap);
                    auto name_val = xml_attr(tag, "Name");
                    if (!name_val.empty()) {
                        move.required_current_animation = name_val;
                        break;  // take first CurrentAnimation condition
                    }
                    cap = inner.find("CurrentAnimation", tag_end + 1);
                }
            }

            if (!move.filename.empty()) {
                moves_[move.name] = std::move(move);
            }
            pos = move_end + 7;
        }

        // [ORIGINAL] Full interval pass via engine/format/xml_doc.hpp.
        // The string-based scan above only captured the FIRST Attack/Uninterrupt
        // interval per move (uses `break`). This pass re-parses moves.xml with a
        // real DOM parser and collects ALL intervals (Attack, Block, Uninterrupt,
        // Complex) into MoveDef::intervals, including nested <Edge>/<Damage>/
        // <Impulse>/<Hit> children and ComplexInterval conditions.
        // Scalar fields (attack_start/end, uninterrupt_start/end, block_start)
        // are re-derived from the first matching interval for backward compat.
        {
            fmt::XmlDocument doc;
            if (doc.parse(xml)) {
                const auto* root = doc.root();
                if (root) {
                    const auto* moves_node = root->first_child("Moves");
                    if (!moves_node) {
                        // try <Movesxml><Moves>
                        auto* mx = root->first_child("Movesxml");
                        if (mx) moves_node = mx->first_child("Moves");
                    }
                    if (moves_node) {
                        int total_intervals = 0;
                        for (const auto& child : moves_node->children) {
                            if (child.name != "Move") continue;
                            auto mname = child.attr("Name");
                            auto it = moves_.find(mname);
                            if (it == moves_.end()) continue;
                            auto& move = it->second;
                            move.intervals.clear();
                            // Intervals can be direct children or inside <Intervals>
                            std::vector<const fmt::XmlNode*> iv_nodes;
                            for (const auto& c : child.children) {
                                if (c.name == "Interval") iv_nodes.push_back(&c);
                                else if (c.name == "Intervals") {
                                    for (const auto& ic : c.children)
                                        if (ic.name == "Interval") iv_nodes.push_back(&ic);
                                }
                            }
                            for (const auto* ivn : iv_nodes) {
                                MoveDef::Interval iv;
                                iv.type = ivn->attr("Type");
                                iv.name = ivn->attr("Name");
                                iv.start = tof(ivn->attr("Start"));
                                iv.end = tof(ivn->attr("End"));
                                // Damage
                                auto* dmg = ivn->first_child("Damage");
                                if (dmg) iv.damage = (int)tof(dmg->attr("Value"));
                                // Impulse
                                auto* imp = ivn->first_child("Impulse");
                                if (imp) {
                                    iv.impulse_x = tof(imp->attr("X"));
                                    iv.impulse_y = tof(imp->attr("Y"));
                                }
                                // Hit
                                auto* hit = ivn->first_child("Hit");
                                if (hit) iv.hit_type = hit->attr("Name");
                                // Edges
                                for (const auto& ec : ivn->children) {
                                    if (ec.name == "Edge") {
                                        auto en = ec.attr("Name");
                                        if (!en.empty()) iv.edges.push_back(en);
                                    }
                                }
                                // ComplexInterval: <CurrentAnimation Name="..."/>
                                auto* ca = ivn->first_child("CurrentAnimation");
                                if (ca) iv.condition_anim = ca->attr("Name");
                                move.intervals.push_back(std::move(iv));
                                total_intervals++;
                            }
                            // Re-derive scalar fields from first matching interval
                            for (const auto& iv : move.intervals) {
                                if (iv.type == "Attack" || iv.name == "Attack") {
                                    if (move.attack_start < 0) {
                                        move.attack_start = (int)iv.start;
                                        move.attack_end = (int)iv.end;
                                        if (iv.damage > 0) move.damage = (float)iv.damage;
                                        if (iv.impulse_x != 0 || iv.impulse_y != 0) {
                                            move.impulse_x = iv.impulse_x;
                                            move.impulse_y = iv.impulse_y;
                                        }
                                        if (!iv.edges.empty() && move.attack_edges.empty())
                                            move.attack_edges = iv.edges;
                                    }
                                } else if (iv.type == "Block" || iv.name == "Block") {
                                    if (move.block_start < 0) move.block_start = (int)iv.start;
                                } else if (iv.name == "Uninterrupt" || iv.type == "Uninterrupt") {
                                    if (move.uninterrupt_start < 0) {
                                        move.uninterrupt_start = (int)iv.start;
                                        move.uninterrupt_end = (int)iv.end;
                                    }
                                }
                            }
                        }
                        std::printf("  [xml_doc] %d total intervals parsed across %zu moves\n",
                                    total_intervals, moves_.size());
                    }
                }
            } else {
                std::fprintf(stderr, "[moves] xml_doc parse warning: %s (string-based parse still used)\n",
                             doc.error().c_str());
            }
        }

        std::printf("  Moves loaded: %zu\n", moves_.size());
        // [ORIGINAL] MoveInside pivot parse audit: count moves that declared
        // a <Pivot> node alignment vs Object="Animation".
        {
            int with_node_pivot = 0, with_anim_pivot = 0;
            for (auto& [n, m] : moves_) {
                if (!m.moveinside_pivot_node.empty()) ++with_node_pivot;
                else if (m.moveinside_is_animation) ++with_anim_pivot;
            }
            std::printf("  MoveInside pivots: %d node-aligned, %d animation-only\n",
                        with_node_pivot, with_anim_pivot);
        }
    }
    
    // Update animation state and compute per-node animated positions.
    //
    // The .bin stores ABSOLUTE world positions for all 67 skeleton.xml nodes
    // (in XML order). To get LOCAL positions (model-space), subtract NPivot's
    // world position from all nodes.
    //
    // Root motion: applied as delta from frame 0 NPivot position.
    void update_animation(uint32_t dt_ms) {
        anim_node_pos_.clear();
        anim_root_dx_ = 0.0f;
        anim_root_dy_ = 0.0f;
        ++total_frame_count_;  // [ROOT] diagnostic frame number

        auto it = animations_.find(current_anim_);
        if (it == animations_.end()) {
            if (!animations_.empty()) {
                current_anim_ = animations_.begin()->first;
                it = animations_.find(current_anim_);
            } else {
                return;
            }
        }

        auto& anim = it->second;
        if (anim.frame_count == 0 || ordered_node_names_.empty()) return;

        // Find NPivot index in ordered_node_names_
        int npivot_idx = -1;
        for (int i = 0; i < (int)ordered_node_names_.size(); ++i) {
            if (ordered_node_names_[i] == "NPivot") {
                npivot_idx = i;
                break;
            }
        }
        if (npivot_idx < 0) {
            std::printf("[ANIM] ERROR: NPivot not found in ordered_node_names_ (size=%zu)\n",
                        ordered_node_names_.size());
            return;
        }

        // Set anchor from frame 0 NPivot position
        if (!anim_anchor_set_) {
            float px, py, pz;
            if (anim.get_node_pos(0, npivot_idx, px, py, pz)) {
                anim_root_anchor_x_ = px;
                anim_root_anchor_y_ = py;
                anim_anchor_set_ = true;
            }
        }

        // Animation timing (from moves.xml + original binary):
        // MidFrames=2 in moves.xml means each keyframe lasts (1+MidFrames)=3
        // physics ticks. At 60fps physics → 60/3 = 20fps animation.
        // step_forward (16 frames) = 16/20 = 800ms per loop.
        // high_punch (12 frames) = 12/20 = 600ms.
        float dt = dt_ms / 1000.0f;
        anim_time_ += dt;

        // Calculate current frame (with looping)
        float frame_f = anim_time_ * 20.0f;
        int frame_idx = (int)frame_f;
        bool anim_finished = false;
        if (anim_loop_) {
            if (anim.frame_count > 0)
                frame_idx = frame_idx % anim.frame_count;
        } else if (frame_idx >= anim.frame_count) {
            frame_idx = anim.frame_count - 1;
            anim_finished = true;
        }
        if (frame_idx < 0) frame_idx = 0;

        // For non-looping AND looping animations at the LAST frame,
        // don't interpolate with frame 0. This prevents the NPivot from
        // being pulled toward the start position (frame 0), which causes
        // the "1-frame teleport to start" bug.
        int next_idx;
        float alpha;
        if (anim_finished) {
            next_idx = frame_idx;
            alpha = 0.0f;
        } else if (frame_idx == anim.frame_count - 1) {
            // Last frame of ANY animation (looping or not): don't
            // interpolate with frame 0. Hold at the last frame until
            // the animation wraps (looping) or finishes (non-looping).
            next_idx = frame_idx;
            alpha = 0.0f;
        } else {
            next_idx = anim.frame_count > 0
                ? ((frame_idx + 1) % anim.frame_count) : 0;
            alpha = frame_f - (int)frame_f;
            if (alpha < 0) alpha = 0;
            if (alpha > 1) alpha = 1;
        }

        // Get NPivot position at current frame (for root offset)
        float npx0, npy0, npz0, npx1, npy1, npz1;
        if (!anim.get_node_pos(frame_idx, npivot_idx, npx0, npy0, npz0)) return;
        if (!anim.get_node_pos(next_idx, npivot_idx, npx1, npy1, npz1)) {
            npx1 = npx0; npy1 = npy0; npz1 = npz0;
        }
        float npivot_x = npx0 + (npx1 - npx0) * alpha;
        float npivot_y = npy0 + (npy1 - npy0) * alpha;

        // Store animated NPivot Y for render_body_model normalization.
        // This prevents the character from "floating" in animations where
        // NPivot Y differs from the rest pose (e.g., idle vs step).
        anim_npivot_bin_y_ = npivot_y;

        // Root motion: ABSOLUTE POSITIONING approach.
        //
        // Instead of accumulating deltas (which is fragile — wrap-around,
        // flicker, float drift), we use absolute positioning:
        //
        // When an animation starts, we save:
        //   step_start_player_x_ = player_pos_x_ at animation start
        //   anim_root_anchor_x_ = NPivot X at frame 0
        //
        // Each frame:
        //   displacement = npivot_x - anim_root_anchor_x_
        //   player_pos_x_ = step_start_player_x_ + (facing_right_ ? displacement : -displacement)
        //
        // For looping animations (step_forward), when the animation wraps:
        //   - displacement jumps from +66 back to 0
        //   - player_pos_x_ jumps from start+66 back to start
        //   - To prevent this, we COMMIT the step at the wrap point:
        //     step_start_player_x_ += 66 (or -66 for step_back)
        //   - Now displacement starts at 0 again, player_pos = new_start + 0
        //
        // Wrap detection: frame_idx decreased (15 → 0)
        // Commit amount: NPivot[last] - NPivot[0] = 235.45 - 169.45 = 66
        //              = anim_root_anchor_x_ + full_cycle_displacement
        //              But easier: = NPivot at frame 0 of NEXT cycle - NPivot at frame 0 of THIS cycle
        //              Since it's the same animation, it's always 0. So we commit
        //              the displacement at the last frame before wrap.
        //
        // For non-looping animations (forward_roll, jump, etc.):
        //   - No wrap, displacement goes from 0 to final value (e.g. +404 for roll)
        //   - player_pos_x_ = step_start_player_x_ + displacement (applied smoothly)
        //   - When animation ends, step_start_player_x_ already includes the full displacement
        //
        // [ORIGINAL] Root-motion whitelist.
        // PC source: sf2.js — NO <Velocity> for player moves (only projectiles).
        // Player displacement comes entirely from NPivot X trajectory in .bin data.
        // Verified: high_punch=35px, heavy_punch=104px, front_kick=156px, etc.
        // stance_2 has large NPivot X displacement but should NOT move the player
        // (it's a model-local animation, not world displacement).
        // [ORIGINAL] Root-motion animation selection is now data-driven.
        // Previously this was a hardcoded whitelist of animation filename
        // strings (step_forward, jump, high_punch, ...) — pure отсебятина.
        // The original engine decides root-motion via MoveDef metadata:
        // Template tags Step/DoubleStep/Retreat/Jump + ATTACK moves with
        // NPivot X displacement. We look up the MoveDef by current_anim_
        // (which is the .bin filename) and check is_step/is_jump/is_retreat
        // or is_attack. Fallback to the old whitelist only if no MoveDef
        // matches (e.g. stance/idle anims not in moves.xml).
        bool is_root_motion_anim = false;
        {
            auto it = std::find_if(moves_.begin(), moves_.end(),
                [&](const auto& p) { return p.second.filename == current_anim_; });
            if (it != moves_.end()) {
                const auto& m = it->second;
                is_root_motion_anim = m.is_step || m.is_jump || m.is_retreat || m.is_attack;
            } else {
                // [HEURISTIC-TODO] fallback whitelist for anims not in moves.xml
                // (e.g. stance_idle, fists_idle). Remove once all anims have MoveDefs.
                is_root_motion_anim =
                    current_anim_ == "step_forward" || current_anim_ == "step_back" ||
                    current_anim_ == "forward_roll" || current_anim_ == "back_roll" ||
                    current_anim_ == "jump" || current_anim_ == "jump_away" ||
                    current_anim_ == "back_flip" || current_anim_ == "back_handflip" ||
                    current_anim_ == "front_flip" ||
                    current_anim_ == "air_punch" || current_anim_ == "air_axe_kick";
            }
        }

        if (is_root_motion_anim) {
            // On first frame of new animation (prev_frame_idx_ == -1),
            // sync step_start to current position.
            if (prev_frame_idx_ == -1) {
                step_start_player_x_ = player_pos_x_;
                // [ORIGINAL] Expanded [ROOT] first-frame log: frame_idx,
                // player_x/y, npivot_x/y, render_y (= player_pos_y_ +
                // y_adjust_smoothed_), facing. Needed to diagnose the Y bug
                // (y_adjust constant floats rolls) from a real run.
                std::printf("[ROOT] f=%llu anim='%s' FIRST frame_idx=%d player=(%.1f,%.1f) npivot=(%.2f,%.2f) render_y=%.2f facing=%d\n",
                            (unsigned long long)total_frame_count_, current_anim_.c_str(), frame_idx,
                            player_pos_x_, player_pos_y_, npivot_x, npivot_y,
                            player_pos_y_ + y_adjust_smoothed_, anim_facing_right_);
            }
            // Detect loop wrap for looping animations
            if (anim_loop_ && prev_frame_idx_ >= 0 && prev_frame_idx_ > frame_idx) {
                float last_npx, last_npy, last_npz;
                if (anim.get_node_pos(anim.frame_count - 1, npivot_idx, last_npx, last_npy, last_npz)) {
                    float cycle_disp = last_npx - anim_root_anchor_x_;
                    step_start_player_x_ += anim_facing_right_ ? cycle_disp : -cycle_disp;
                    std::printf("[ROOT] f=%llu loop wrap: cycle_disp=%.1f step_start=%.1f\n",
                                (unsigned long long)total_frame_count_, cycle_disp, step_start_player_x_);
                }
            }
            prev_frame_idx_ = frame_idx;

            float displacement = npivot_x - anim_root_anchor_x_;
            float new_pos = step_start_player_x_ + (anim_facing_right_ ? displacement : -displacement);
            // Only update if not clamped (clamp would cause teleport on next frame)
            // Clamp to walls
            if (location_ && location_->wall > 0 && location_->width > 0) {
                const float X_OFFSET = 983.0f;
                float left_bound = location_->wall - X_OFFSET;
                float right_bound = (location_->width - location_->wall) - X_OFFSET;
                if (new_pos < left_bound) new_pos = left_bound;
                if (new_pos > right_bound) new_pos = right_bound;
            }
            player_pos_x_ = new_pos;
        } else {
            prev_frame_idx_ = -1;
        }

        // [ORIGINAL] Per-frame [ROOT] diagnostic: logs every frame so the Y
        // trajectory during jumps/rolls/idle is visible in a real run.
        // render_y = player_pos_y_ + y_adjust_smoothed_ (matches world_cy in
        // render_body_model). y_adjust_smoothed_ here is the value from the
        // last render_body_model() call (member persists across frames).
        std::printf("[ROOT] f=%llu anim='%s' fi=%d px=%.1f py=%.1f npx=%.2f npy=%.2f ry=%.2f face=%d\n",
                    (unsigned long long)total_frame_count_, current_anim_.c_str(), frame_idx,
                    player_pos_x_, player_pos_y_, npivot_x, npivot_y,
                    player_pos_y_ + y_adjust_smoothed_, anim_facing_right_);

        // Y root motion: NO LONGER NEEDED.
        // y_adjust in render_body_model() now handles all Y positioning
        // by keeping the lowest node at floor level. This works for:
        //   - Standing (feet at floor)
        //   - Crouching (feet still at floor, body lower)
        //   - Jumping (feet up, body higher)
        //   - Landing (feet at floor)
        //
        // The .bin animation contains absolute world positions for all
        // nodes, so the lowest node Y directly indicates how high the
        // character is. No separate jump_y_offset needed.
        jump_y_offset_ = 0;

        // [ORIGINAL] Compute y_adjust_smoothed_ HERE (in update_animation),
        // not in render_body_model(). This ensures hit detection (which runs
        // in host_update_gameplay AFTER update_animation but BEFORE
        // host_render_scene/render_body_model) uses the CURRENT frame's
        // y_adjust, not the previous frame's. Without this fix, there is a
        // 1-frame desync between render Y and hit-detection Y, causing hits
        // to register at the wrong height during airborne animations.
        // [HEURISTIC-TODO] Same interim formula as render_body_model():
        // NPivot Y displacement for airborne, constant for grounded.
        // FIX: only apply UPWARD displacement (npy > rest). When npy < rest
        // (crouch/anticipation phase at jump start), the NPivot descent is
        // model-local (body crouches) and must NOT move the world position
        // down — otherwise the character sinks below the floor.
        // Verified numerically: without this clamp, jump frame 0-8 has
        // render_y -120..-161 (below floor -89) because npy starts at 106
        // (below rest 169.48), giving y_adjust = -59.
        //
        // FIX 2: unified stance-baseline Y correction for ALL animations.
        // Previous approach used rest Y (169.48) for airborne and flat 4 for
        // grounded (roll-only correction). This caused:
        //   - Jump barely lifts: baseline 169.48 too high, jump starts at 106,
        //     so displacement = max(0, 106-169.48) = 0 for first ~7 frames.
        //     Peak: 243-169.48 = 74 (render_y -15, barely visible).
        //   - Feet through floor during stance_2: npy goes 132→95, but y_adjust=4
        //     (flat). When npy=132: NToe sy = -89+0.92-132 = -220 (below floor!).
        //   - Character floating: when npy=95 (below stance 106): NToe sy = -183
        //     (10px above floor).
        //
        // Unified fix: use STANCE_NPIVOT_Y (106) as baseline for ALL anims.
        //   y_adjust = 4 + (npy - 106)
        //   - stance_2 npy=132: y_adj=30, NToe sy = (-89+30)+0.92-132 = -190 ✓
        //   - stance_2 npy=95:  y_adj=-7, NToe sy = (-89-7)+0.92-95 = -190 ✓
        //   - jump npy=106:     y_adj=4, render_y=-89 (on floor) ✓
        //   - jump peak npy=243: y_adj=141, render_y=48 (visible jump!) ✓
        //   - jump crouch npy=71: clamped to y_adj=4 (upward-only for airborne)
        //
        // For airborne: clamp displacement to >= 0 (upward only) to prevent
        // under-floor during crouch/landing phase.
        // For grounded: allow negative displacement (character dips, feet stay
        // on floor — this is correct for roll, crouch, low attacks).
        {
            // [ORIGINAL] MoveInside Y alignment — VERIFIED from PC version sf2.js.
            // Formula: Gla(cI ? (ref-pivot).x : ShiftX,
            //                dI ? (ref-pivot).y : ShiftY,
            //                MY ? (ref-pivot).z : 0)
            // For Axis="X|Z" (ALL current player moves): dI=false → Y = ShiftY.
            // ShiftY is 0 for all player moves (jump/roll/punch/kick/step).
            // Therefore y_adjust = ShiftY = 0 for ALL current moves.
            // The visual jump/roll comes from per-node animation data (abs_y -
            // npivot_y in resolve_body_node), NOT from world Y displacement.
            // Previous interim hacks (NPivot Y displacement) were wrong — the
            // original game does NOT move the model root in Y for Axis="X|Z" moves.
            float target_y = 0.0f;  // ShiftY (verified: 0 for all player moves)
            y_adjust_smoothed_ = target_y;
        }

        // Get NPivot's rest-pose Y (from skeleton_nodes_)
        auto pivot_it = skeleton_nodes_.find("NPivot");
        float npivot_rest_y = pivot_it != skeleton_nodes_.end() ? pivot_it->second.y : 169.48f;

        // For each node in the .bin, compute LOCAL position and store in anim_node_pos_
        for (int i = 0; i < (int)ordered_node_names_.size() && i < 67; ++i) {
            const std::string& name = ordered_node_names_[i];
            
            float x0, y0, z0, x1, y1, z1;
            if (!anim.get_node_pos(frame_idx, i, x0, y0, z0)) continue;
            if (!anim.get_node_pos(next_idx, i, x1, y1, z1)) {
                x1 = x0; y1 = y0; z1 = z0;
            }
            
            // Interpolate
            float abs_x = x0 + (x1 - x0) * alpha;
            float abs_y = y0 + (y1 - y0) * alpha;

            // [ORIGINAL] Node world Y = abs_y + fixed_offset, NOT abs_y - animated_npy.
            // PC source: sf2.js eda() — node position = fq[frame] + j8 (velocity).
            // j8 = 0 for moves without <Velocity> (all player moves).
            // The .bin stores absolute node positions; the model world position
            // provides the offset. NPivot Y in .bin changes during animation
            // (crouch/jump/roll), but this is the ANIMATED node position, NOT
            // the model world position. Subtracting animated npivot_y causes
            // feet to lift off floor during roll (wrapping bug) and incorrectly
            // shifts nodes when NPivot descends.
            // FIX: use STANCE_NPIVOT_Y (106) as fixed baseline instead of
            // animated npivot_y. This keeps the coordinate offset consistent:
            //   node_world_y = world_cy + abs_y - 106 = -93 + abs_y - 106 = abs_y - 199
            // For stance (NToe abs_y=0.92): sy = -198 (on floor -193 ✓)
            // For roll mid (NToe abs_y=0.92, npy=25): sy = -198 (still on floor ✓)
            // For jump peak (NToe abs_y=189): sy = -10 (high up ✓)
            constexpr float STANCE_NPIVOT_Y_BASELINE = 106.0f;
            float local_x = abs_x - npivot_x;
            float local_y = abs_y - STANCE_NPIVOT_Y_BASELINE;
            anim_node_pos_[name] = {local_x, local_y + npivot_rest_y};
        }

        // One-shot diagnostic: log when animation changes, to verify anim_node_pos_ is populated.
        // Prints NPivot + a few key leg nodes so we can see if animation data is sane.
        if (current_anim_ != last_logged_anim_) {
            last_logged_anim_ = current_anim_;
            std::printf("[ANIM] '%s' frame=%d/%d anim_node_pos_.size()=%zu  npivot_idx=%d\n",
                        current_anim_.c_str(), frame_idx, anim.frame_count,
                        anim_node_pos_.size(), npivot_idx);
            // Print leg nodes (NHip, NKnee, NAnkle) for both sides
            const char* leg_nodes[] = {"NPivot", "NHip_1", "NHip_2",
                                       "NKnee_1", "NKnee_2",
                                       "NAnkle_1", "NAnkle_2",
                                       "NToe_1", "NToe_2"};
            for (auto* n : leg_nodes) {
                auto ait = anim_node_pos_.find(n);
                auto sit = skeleton_nodes_.find(n);
                if (ait != anim_node_pos_.end() && sit != skeleton_nodes_.end()) {
                    std::printf("  %-10s anim_local=(%7.2f,%7.2f)  rest=(%7.2f,%7.2f)\n",
                                n, ait->second.first, ait->second.second,
                                sit->second.x, sit->second.y);
                } else if (sit != skeleton_nodes_.end()) {
                    std::printf("  %-10s NOT in anim_node_pos_!  rest=(%7.2f,%7.2f)\n",
                                n, sit->second.x, sit->second.y);
                }
            }
        }
    }
    
    void play_animation(const std::string& name, bool loop = true) {
        if (animations_.count(name)) {
            if (current_anim_ != name) {
                std::printf("[ANIM] play_animation('%s', loop=%d) — switching from '%s'\n",
                            name.c_str(), loop, current_anim_.c_str());
            }
            current_anim_ = name;
            anim_time_ = 0.0f;
            anim_loop_ = loop;
            // Reset anchor so update_animation() re-reads frame 0 root pos
            anim_anchor_set_ = false;
            anim_root_dx_ = 0.0f;
            anim_root_dy_ = 0.0f;
            prev_root_offset_ = 0.0f;
            prev_npivot_set_ = false;
            prev_npivot_y_set_ = false;
            prev_frame_idx_ = -1;
            // For root motion: step_start_player_x_ is synced in the NON-root-motion
            // branch below (every frame when not doing root motion). When we switch
            // to a root-motion animation, step_start_player_x_ already holds the
            // correct current position from the last idle/non-root frame.
            // Do NOT set it here — player_pos_x_ at this point is from the PREVIOUS
            // frame's update_animation(), which may be stale.
            // Lock facing at animation start. Root motion uses this saved value
            // instead of current facing_right_ to prevent teleport when facing
            // changes between frames (e.g., character walks past enemy during step).
            anim_facing_right_ = facing_right_;
            // Reset jump offset when switching TO a non-jump animation
            if (name != "jump" && name != "jump_away" &&
                name != "front_flip" && name != "back_flip" &&
                name != "back_handflip") {
                jump_y_offset_ = 0.0f;
            }
        }
    }

    void load_hud_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Search both root/assets/1536/ and root/1536/ for textures
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            load_texture_atlas_to_hud(base/"textures"/"panels"/"top",
                                      "batchPanelsTop");
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"dojo",
                                      "batchButtonsDojo");
        }
        std::printf("  HUD textures loaded: %zu\n", hud_textures_.size());
    }

    void load_menu_textures() {
        auto root = std::filesystem::path(asset_root_);
        // Search both root/assets/1536/ and root/1536/
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            load_texture_atlas_to_hud(base/"textures"/"buttons"/"menu"/"screens",
                                      "batchButtonsMenuScreens");
        }
        // Move menu atlas textures into menu_textures_
        for (auto it = hud_textures_.begin(); it != hud_textures_.end(); ) {
            if (it->first.find("_normal") != std::string::npos ||
                it->first.find("_active") != std::string::npos ||
                it->first.find("_pushed") != std::string::npos ||
                it->first.find("_Normal") != std::string::npos ||
                it->first.find("_Active") != std::string::npos ||
                it->first.find("_Pushed") != std::string::npos) {
                menu_textures_[it->first] = std::move(it->second);
                it = hud_textures_.erase(it);
            } else {
                ++it;
            }
        }
        // Load scroll/roll textures for parchment menu UI
        for (const auto& base : {root/"assets"/"1536", root/"1536"}) {
            auto scroll_dir = base/"textures"/"scrolls"/"common";
            for (auto& name : {"MenuRoll_left", "MenuRoll_center", "MenuRoll_right",
                               "Roll_left", "Roll_center", "Roll_right",
                               "Paper_left", "Paper_right", "Shadow_roll"}) {
                auto path = scroll_dir / (std::string(name) + ".png");
                if (std::filesystem::exists(path)) {
                    auto data = read_file(path.string());
                    int w, h, ch;
                    auto* px = stbi_load_from_memory(
                        (const stbi_uc*)data.data(), (int)data.size(), &w, &h, &ch, 4);
                    if (px) {
                        auto tex = std::make_unique<ren::Texture2D>();
                        tex->init_rgba(w, h, px);
                        stbi_image_free(px);
                        scroll_textures_[name] = std::move(tex);
                    }
                }
            }
        }
        std::printf("  Menu textures loaded: %zu, scroll textures: %zu\n",
                    menu_textures_.size(), scroll_textures_.size());
    }

    void load_texture_atlas_to_hud(
        const std::filesystem::path& dir, const std::string& atlas_name)
    {
        auto pp = dir / (atlas_name + ".plist");
        auto pn = dir / (atlas_name + ".png");
        if (!std::filesystem::exists(pp) || !std::filesystem::exists(pn)) return;
        auto result = plist::parse(read_text(pp.string()));
        if (!result) return;
        auto png_data = read_file(pn.string());
        // Use stb_image to decode the PNG so we can crop frames on the CPU.
        int aw, ah, ach;
        auto* atlas_px = stbi_load_from_memory(
            (const stbi_uc*)png_data.data(), (int)png_data.size(),
            &aw, &ah, &ach, 4);
        if (!atlas_px) return;
        for (auto& [name, idx] : result->name_index) {
            auto& frame = result->frames[idx];
            // Handle rotated frames:
            // For rotated frames, do NOT swap dimensions (atlas_w/atlas_h are original).
            // The atlas region has swapped dimensions, but we create the texture
            // at original dimensions.
            int fw = frame.atlas_w;
            int fh = frame.atlas_h;
            auto tex = std::make_unique<ren::Texture2D>();
            std::vector<std::uint8_t> px((size_t)fw * fh * 4);
            for (int y = 0; y < fh; ++y) {
                for (int x = 0; x < fw; ++x) {
                    int sx, sy;
                    if (frame.rotated) {
                        // Un-rotate 90° CCW (no swap, formula A):
                        // dest(x,y) ← source(atlas_x + (fh-1-y), atlas_y + x)
                        sx = frame.atlas_x + (fh - 1 - y);
                        sy = frame.atlas_y + x;
                    } else {
                        sx = frame.atlas_x + x;
                        sy = frame.atlas_y + y;
                    }
                    if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                    int src_idx = (sy * aw + sx) * 4;
                    int dst_idx = (y * fw + x) * 4;
                    px[dst_idx+0] = atlas_px[src_idx+0];
                    px[dst_idx+1] = atlas_px[src_idx+1];
                    px[dst_idx+2] = atlas_px[src_idx+2];
                    px[dst_idx+3] = atlas_px[src_idx+3];
                }
            }
            tex->init_rgba(fw, fh, px.data());
            std::string n = name;
            if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
            hud_textures_[n] = std::move(tex);
        }
        stbi_image_free(atlas_px);
    }

    // ---------- HUD font ----------
    void load_hud_font() {
        auto root = std::filesystem::path(asset_root_);
        std::vector<std::filesystem::path> candidates = {
            root/"assets"/"1536"/"fonts"/"rus"/"optima.fnt",
            root/"assets"/"1536"/"fonts"/"obelix.fnt",
            root/"1536"/"fonts"/"rus"/"optima.fnt",
            root/"1536"/"fonts"/"obelix.fnt",
        };
        std::string fnt_path, png_path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) {
                fnt_path = p.string();
                auto png = p.parent_path() / (p.stem().string() + ".png");
                if (!std::filesystem::exists(png)) {
                    auto xml = read_text(fnt_path);
                    auto fp = xml.find("file=\"");
                    if (fp != std::string::npos) {
                        fp += 6;
                        auto end = xml.find('"', fp);
                        std::string png_name = xml.substr(fp, end - fp);
                        auto png2 = p.parent_path() / png_name;
                        if (std::filesystem::exists(png2)) png = png2;
                    }
                }
                if (std::filesystem::exists(png)) {
                    png_path = png.string();
                    break;
                }
            }
        }
        if (fnt_path.empty()) return;
        auto result = font::parse(read_text(fnt_path));
        if (!result) return;
        hud_font_ = std::make_shared<font::ParsedFont>(std::move(*result));
        auto png_data = read_file(png_path);
        auto tex = std::make_unique<ren::Texture2D>();
        if (!tex->init_from_png((const uint8_t*)png_data.data(), png_data.size())) return;
        hud_font_tex_ = std::move(tex);
        std::printf("  HUD font loaded: %s (%zu glyphs)\n",
                    fnt_path.c_str(), hud_font_->chars.size());
    }

    // [ORIGINAL] Load sound effects from the original mobile assets.
    // SF2 sounds are in assets/sounds/*.wav (16-bit PCM, Marmalade s3eAudio).
    // We load key combat sounds: punches, kicks, hits, bodyfalls, blocks.
    void load_sounds() {
        auto& eng = aud::AudioEngine::instance();
        eng.init();  // defaults to NullAudioBackend (no OpenAL yet)
        auto root = std::filesystem::path(asset_root_);
        // Search paths for sounds (mobile APK layout + extracted layout)
        std::vector<std::filesystem::path> sound_dirs = {
            root/"assets"/"assets"/"sounds",
            root/"assets"/"sounds",
            root/"sounds",
        };
        std::filesystem::path sound_dir;
        for (const auto& d : sound_dirs) {
            if (std::filesystem::exists(d)) { sound_dir = d; break; }
        }
        if (sound_dir.empty()) {
            std::printf("[audio] sounds dir not found\n");
            return;
        }
        // [ORIGINAL] Key SF2 sound files (from assets/sounds/):
        // f_pl_attack1-4.wav — player punch/kick attack swings
        // bodyfall1/3.wav — body hit ground
        // armor.wav — armor hit
        // coin_hit1-4.wav — coin pickup
        // disk.wav, energy_flask5.wav — pickups
        std::vector<std::string> needed = {
            "f_pl_attack1", "f_pl_attack2", "f_pl_attack3", "f_pl_attack4",
            "bodyfall1", "bodyfall3", "armor", "coin_hit1", "disk", "energy_flask5",
            "buy", "f_cough"
        };
        int loaded = 0;
        for (const auto& name : needed) {
            auto path = sound_dir / (name + ".wav");
            if (std::filesystem::exists(path)) {
                if (eng.load_sound_file(name, path.string())) loaded++;
            }
        }
        std::printf("[audio] Loaded %d/%zu sounds from %s\n",
                    loaded, needed.size(), sound_dir.string().c_str());
    }

    // Play a sound by name (no-op if not loaded or backend is null)
    void play_sound(const std::string& name, float volume = 1.0f) {
        aud::AudioEngine::instance().play(name, volume, false);
    }

    void render_text(const std::string& text, float x, float y,
                     float scale, ren::Color4B color) {
        if (!hud_font_ || !hud_font_tex_) return;
        float cx = x;
        for (char c : text) {
            std::int32_t cp = (std::uint8_t)c;
            if (cp >= 0xC0 && cp <= 0xFF) cp = 0x0410 + (cp - 0xC0);
            auto it = hud_font_->char_index.find(cp);
            if (it == hud_font_->char_index.end()) {
                it = hud_font_->char_index.find(32);
                if (it == hud_font_->char_index.end()) continue;
            }
            auto& ch = hud_font_->chars[it->second];
            if (ch.width > 0 && ch.height > 0) {
                float u0 = (float)ch.x / hud_font_->common.scale_w;
                float v0 = (float)ch.y / hud_font_->common.scale_h;
                float u1 = (float)(ch.x + ch.width) / hud_font_->common.scale_w;
                float v1 = (float)(ch.y + ch.height) / hud_font_->common.scale_h;
                float px = cx + ch.xoffset * scale;
                float py = y + ch.yoffset * scale;
                float pw = ch.width * scale;
                float ph = ch.height * scale;
                renderer_->draw_textured_quad_screen(
                    *hud_font_tex_, px, py, pw, ph, u0, v0, u1, v1, color);
            }
            cx += ch.xadvance * scale;
        }
    }

    // ---------- HUD ----------
    void render_hud(plat::Platform& platform) {
        // Top panel background (real texture, tiled horizontally)
        auto panel_it = hud_textures_.find("Top_Panel");
        if (panel_it != hud_textures_.end()) {
            auto& tex = panel_it->second;
            float panel_h = 50.0f;
            float tile_w = panel_h * tex->width() / tex->height();
            float x = 0;
            float win_w = (float)platform.window_width();
            while (x < win_w) {
                float draw_w = std::min(tile_w, win_w - x);
                float u1 = draw_w / tile_w;
                renderer_->draw_textured_quad_screen(*tex, x, 0, draw_w, panel_h,
                                                     0, 0, u1, 1.0f);
                x += draw_w;
            }
        } else {
            ren::Color4B bar_bg{0, 0, 0, 180};
            renderer_->draw_filled_rect_screen(0, 0,
                (float)platform.window_width(), 50, bar_bg);
        }

        // Gold icon + amount
        auto gold_it = hud_textures_.find("gold");
        if (gold_it != hud_textures_.end()) {
            renderer_->draw_textured_quad_screen(*gold_it->second, 10, 9, 32, 32);
        }
        render_text("72 450", 50, 15, 0.32f, {255, 240, 200, 255});

        // Energy icon + value
        auto energy_it = hud_textures_.find("energy");
        if (energy_it != hud_textures_.end()) {
            renderer_->draw_textured_quad_screen(*energy_it->second, 180, 9, 32, 32);
        }
        render_text("5 / 5", 220, 15, 0.32f, {200, 230, 255, 255});

        // Level bar + level badge
        auto lvlbar_it = hud_textures_.find("Level_bar");
        if (lvlbar_it != hud_textures_.end()) {
            renderer_->draw_textured_quad_screen(*lvlbar_it->second, 330, 15, 120, 20);
        }
        render_text("LVL 7", 460, 15, 0.30f, {255, 255, 255, 255});

        // [ORIGINAL] Health bars for player and enemy (bottom-left and bottom-right).
        // Original SF2: two health bars at bottom corners + energy bar below player.
        // Red gradient, white border. Enemy bar mirrored on the right.
        float hp_bar_w = 280.0f;
        float hp_bar_h = 18.0f;
        float hp_bar_y = (float)platform.window_height() - 30.0f;
        float hp_bar_pad = 16.0f;
        // Player HP bar (bottom-left)
        float pl_x = hp_bar_pad;
        ren::Color4B hp_bg{30, 20, 20, 200};
        ren::Color4B hp_border{180, 160, 120, 255};
        ren::Color4B hp_fill_low{180, 30, 30, 255};
        ren::Color4B hp_fill_mid{200, 160, 40, 255};
        ren::Color4B hp_fill_hi{60, 180, 70, 255};
        renderer_->draw_filled_rect_screen(pl_x - 2, hp_bar_y - 2, hp_bar_w + 4, hp_bar_h + 4, hp_border);
        renderer_->draw_filled_rect_screen(pl_x, hp_bar_y, hp_bar_w, hp_bar_h, hp_bg);
        float pl_pct = player_fighter_.health / player_fighter_.max_health;
        ren::Color4B pl_col = (pl_pct > 0.5f) ? hp_fill_hi : (pl_pct > 0.25f ? hp_fill_mid : hp_fill_low);
        if (pl_pct > 0) {
            renderer_->draw_filled_rect_screen(pl_x, hp_bar_y, hp_bar_w * pl_pct, hp_bar_h, pl_col);
        }
        // Player hit flash overlay
        if (player_hit_flash_ > 0) {
            ren::Color4B flash{255, 255, 255, (uint8_t)(player_hit_flash_ * 600.0f)};
            renderer_->draw_filled_rect_screen(pl_x, hp_bar_y, hp_bar_w, hp_bar_h, flash);
        }
        render_text("YOU", pl_x + 4, hp_bar_y - 16, 0.26f, {255, 240, 200, 255});
        // Energy bar below player HP
        float en_bar_y = hp_bar_y + hp_bar_h + 3;
        float en_bar_h = 5.0f;
        renderer_->draw_filled_rect_screen(pl_x, en_bar_y, hp_bar_w, en_bar_h, hp_bg);
        float en_pct = player_fighter_.energy / player_fighter_.max_energy;
        if (en_pct > 0) {
            renderer_->draw_filled_rect_screen(pl_x, en_bar_y, hp_bar_w * en_pct, en_bar_h,
                ren::Color4B{80, 180, 255, 255});
        }
        // Enemy HP bar (bottom-right, mirrored)
        float en_x = (float)platform.window_width() - hp_bar_w - hp_bar_pad;
        renderer_->draw_filled_rect_screen(en_x - 2, hp_bar_y - 2, hp_bar_w + 4, hp_bar_h + 4, hp_border);
        renderer_->draw_filled_rect_screen(en_x, hp_bar_y, hp_bar_w, hp_bar_h, hp_bg);
        float en_pct2 = enemy_fighter_.health / enemy_fighter_.max_health;
        ren::Color4B en_col = (en_pct2 > 0.5f) ? hp_fill_hi : (en_pct2 > 0.25f ? hp_fill_mid : hp_fill_low);
        if (en_pct2 > 0) {
            // Enemy bar fills from right to left
            renderer_->draw_filled_rect_screen(en_x + hp_bar_w * (1.0f - en_pct2), hp_bar_y,
                hp_bar_w * en_pct2, hp_bar_h, en_col);
        }
        if (enemy_hit_flash_ > 0) {
            ren::Color4B flash{255, 255, 255, (uint8_t)(enemy_hit_flash_ * 600.0f)};
            renderer_->draw_filled_rect_screen(en_x, hp_bar_y, hp_bar_w, hp_bar_h, flash);
        }
        render_text("ENEMY", en_x + hp_bar_w - 60, hp_bar_y - 16, 0.26f, {255, 200, 200, 255});
        // Victory/Defeat overlay
        if (player_fighter_.is_dead || enemy_fighter_.is_dead) {
            ren::Color4B overlay_bg{0, 0, 0, 150};
            renderer_->draw_filled_rect_screen(0, 0,
                (float)platform.window_width(), (float)platform.window_height(), overlay_bg);
            std::string msg = enemy_fighter_.is_dead ? "VICTORY" : "DEFEAT";
            ren::Color4B msg_col = enemy_fighter_.is_dead ?
                ren::Color4B{255, 220, 100, 255} : ren::Color4B{220, 60, 60, 255};
            float msg_scale = 1.5f;
            float msg_w = msg.size() * 16.0f * msg_scale;
            render_text(msg, ((float)platform.window_width() - msg_w) / 2.0f,
                (float)platform.window_height() / 2.0f - 30, msg_scale, msg_col);
            render_text("Press R to restart", ((float)platform.window_width() - 200) / 2.0f,
                (float)platform.window_height() / 2.0f + 20, 0.4f, {200, 200, 200, 255});
        }

        // Menu button (LEFT side, scroll/roll style)
        float btn_x = 10.0f, btn_y = 58.0f;
        float roll_h = 40.0f;
        // Compute menu animation progress (smoothstep easing)
        float mp = menu_anim_progress_;
        float menu_eased = mp * mp * (3.0f - 2.0f * mp);
        // Show collapsed roll when menu is closed OR animating
        if (menu_eased < 0.99f) {
            // Collapsed: scroll roll bar — sized to fit "MENU" text
            auto lit = scroll_textures_.find("MenuRoll_left");
            auto cit = scroll_textures_.find("MenuRoll_center");
            auto rit = scroll_textures_.find("MenuRoll_right");
            if (lit != scroll_textures_.end() && cit != scroll_textures_.end() &&
                rit != scroll_textures_.end()) {
                float cap_w = roll_h * lit->second->width() / lit->second->height();
                // Measure "MENU" text width at scale 0.22
                float text_w = 0.0f;
                if (hud_font_) {
                    for (char c : std::string("MENU")) {
                        std::int32_t cp = (std::uint8_t)c;
                        auto it = hud_font_->char_index.find(cp);
                        if (it != hud_font_->char_index.end()) {
                            text_w += hud_font_->chars[it->second].xadvance * 0.22f;
                        }
                    }
                }
                float roll_w = text_w + 2 * cap_w + 16.0f;  // text + caps + padding
                float center_w = roll_w - 2 * cap_w;
                // Fade out the collapsed roll as menu expands
                float alpha = 1.0f - menu_eased;
                ren::Color4B roll_col{255, 255, 255, (uint8_t)(alpha * 255)};
                renderer_->draw_textured_quad_screen(*lit->second, btn_x, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
                renderer_->draw_textured_quad_screen(*cit->second, btn_x + cap_w, btn_y, center_w, roll_h, 0,0,1,1, roll_col);
                renderer_->draw_textured_quad_screen(*rit->second, btn_x + cap_w + center_w, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
                // Center "MENU" text on the roll
                ren::Color4B text_col{255, 240, 200, (uint8_t)(alpha * 255)};
                float text_x = btn_x + (roll_w - text_w) / 2.0f;
                // Measure actual text height for vertical centering
                float text_h = 0.0f;
                if (hud_font_) {
                    for (char c : std::string("MENU")) {
                        std::int32_t cp = (std::uint8_t)c;
                        auto it = hud_font_->char_index.find(cp);
                        if (it != hud_font_->char_index.end()) {
                            text_h = std::max(text_h, (float)hud_font_->chars[it->second].height * 0.22f);
                        }
                    }
                }
                float text_y = btn_y + (roll_h - text_h) / 2.0f;
                render_text("MENU", text_x, text_y, 0.22f, text_col);
            } else {
                ren::Color4B bg{60, 40, 20, 230};
                renderer_->draw_filled_rect_screen(btn_x, btn_y, 120, roll_h, bg);
                render_text("MENU", btn_x + 40, btn_y + 12, 0.22f, {255, 240, 200, 255});
            }
        }

        // Bottom hint
        render_text("A/D - move    Space - hit    M - menu    T - dialog",
                    20, (float)(platform.window_height() - 40), 0.26f,
                    {200, 200, 200, 255});

        // Position label
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Pos: (%.0f, %.0f)",
                      player_pos_x_, player_pos_y_);
        render_text(buf, 20, (float)(platform.window_height() - 65), 0.26f,
                    {180, 180, 180, 255});
    }

    // ---------- Menu expanded (vertical scroll, matching original game) ----------
    // In the original game, the menu is a VERTICAL scroll on the left side.
    // Icons are stacked top-to-bottom in a single column.
    // The scroll "unrolls" from top to bottom with a smooth animation.
    void render_menu_expanded(plat::Platform& platform) {
        // Compute eased animation progress
        float mp = menu_anim_progress_;
        float menu_eased = mp * mp * (3.0f - 2.0f * mp);  // smoothstep
        if (menu_eased < 0.01f) return;  // nothing to render

        float btn_x = 10.0f, btn_y = 58.0f;
        float roll_h = 40.0f;

        auto lit = scroll_textures_.find("MenuRoll_left");
        auto cit = scroll_textures_.find("MenuRoll_center");
        auto rit = scroll_textures_.find("MenuRoll_right");

        if (lit == scroll_textures_.end() || cit == scroll_textures_.end() ||
            rit == scroll_textures_.end()) {
            ren::Color4B bg{60, 40, 20, 230};
            renderer_->draw_filled_rect_screen(btn_x, btn_y, 120, 400 * menu_eased, bg);
            return;
        }

        auto& left_tex = lit->second;
        auto& center_tex = cit->second;
        auto& right_tex = rit->second;
        float cap_w = roll_h * left_tex->width() / left_tex->height();

        // Vertical layout: icons stacked top-to-bottom
        float icon_size = 56.0f;  // larger icons to match original game
        float icon_spacing = 8.0f;
        int n_items = 5;
        float paper_padding = 14.0f;
        float paper_w = icon_size + paper_padding * 2 + 30;  // wider for text labels
        float full_paper_h = n_items * (icon_size + icon_spacing) + paper_padding * 2;
        // Animate paper height: scroll unrolls from top to bottom
        float paper_h = full_paper_h * menu_eased;
        float center_w = paper_w - 2 * cap_w;

        // Roll bar (top, horizontal) — sized to fit paper width
        float roll_alpha = (menu_eased > 0.05f) ? 1.0f : menu_eased / 0.05f;
        ren::Color4B roll_col{255, 255, 255, (uint8_t)(roll_alpha * 255)};
        renderer_->draw_textured_quad_screen(*left_tex, btn_x, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);
        renderer_->draw_textured_quad_screen(*center_tex, btn_x + cap_w, btn_y, center_w, roll_h, 0,0,1,1, roll_col);
        renderer_->draw_textured_quad_screen(*right_tex, btn_x + cap_w + center_w, btn_y, cap_w, roll_h, 0,0,1,1, roll_col);

        // Paper area (below roll, vertical) — clips to animated height
        float paper_y = btn_y + roll_h - 3;
        ren::Color4B paper_bg{200, 170, 120, 245};
        renderer_->draw_filled_rect_screen(btn_x, paper_y, paper_w, paper_h, paper_bg);

        // Paper edges (top and bottom)
        auto pl_it = scroll_textures_.find("Paper_left");
        auto pr_it = scroll_textures_.find("Paper_right");
        if (pl_it != scroll_textures_.end()) {
            float pl_w = paper_w * pl_it->second->width() / pl_it->second->height();
            renderer_->draw_textured_quad_screen(*pl_it->second, btn_x, paper_y, pl_w, paper_w);
        }
        if (pr_it != scroll_textures_.end() && menu_eased > 0.95f) {
            // Only show bottom edge when fully expanded
            float pr_w = paper_w * pr_it->second->width() / pr_it->second->height();
            renderer_->draw_textured_quad_screen(*pr_it->second,
                btn_x, paper_y + paper_h - pr_w, pr_w, paper_w);
        }

        // Shadow below (only when fully expanded)
        auto shadow_it = scroll_textures_.find("Shadow_roll");
        if (shadow_it != scroll_textures_.end() && menu_eased > 0.9f) {
            renderer_->draw_textured_quad_screen(*shadow_it->second,
                btn_x, paper_y + paper_h - 8, paper_w, 15);
        }

        // Menu icons (vertical stack) — only render icons that fit within the animated height
        // All icons rendered with uniform scaling: scale = icon_size / max_texture_dimension
        // This ensures all icons appear the same size on screen while preserving aspect ratio.
        const char* items[] = {"Dojo", "Map", "Shop", "Profile", "Settings"};
        // Find max texture dimension across all icons for uniform scaling
        int max_tex_dim = 1;
        for (auto& name : items) {
            std::string tex_name = std::string(name) + "_normal";
            auto it = menu_textures_.find(tex_name);
            if (it == menu_textures_.end()) {
                it = menu_textures_.find(std::string(name) + "_Normal");
            }
            if (it != menu_textures_.end()) {
                max_tex_dim = std::max(max_tex_dim, std::max(it->second->width(), it->second->height()));
            }
        }
        float uniform_scale = icon_size / (float)max_tex_dim;
        float ix = btn_x + paper_padding + 10;
        float iy = paper_y + paper_padding;
        for (int idx = 0; idx < 5; ++idx) {
            float icon_y = iy + idx * (icon_size + icon_spacing);
            // Skip icons that haven't been revealed yet (below the unrolled height)
            if (icon_y + icon_size > paper_y + paper_h) break;

            auto& name = items[idx];
            // Try different case patterns for the texture name
            std::string tex_name = std::string(name) + "_normal";
            auto it = menu_textures_.find(tex_name);
            if (it == menu_textures_.end()) {
                it = menu_textures_.find(std::string(name) + "_Normal");
            }
            if (it != menu_textures_.end()) {
                // Uniform scale: all icons scaled by same factor, preserving aspect ratio
                float draw_w = it->second->width() * uniform_scale;
                float draw_h = it->second->height() * uniform_scale;
                // Center within the icon_size × icon_size slot
                float draw_x = ix + (icon_size - draw_w) * 0.5f;
                float draw_y = icon_y + (icon_size - draw_h) * 0.5f;
                renderer_->draw_textured_quad_screen(*it->second, draw_x, draw_y,
                                                     draw_w, draw_h);
                if (!loc_icons_logged) {
                    std::printf("[MENU] icon '%s': tex %dx%d → draw %.0fx%.0f (scale=%.2f)\n",
                                name, it->second->width(), it->second->height(),
                                draw_w, draw_h, uniform_scale);
                }
            }
            render_text(name, ix + icon_size + 5, icon_y + 10, 0.16f, {60, 40, 20, 255});
        }
        loc_icons_logged = true;
    }

    // ---------- Menu overlay ----------
    void render_menu_overlay(plat::Platform& platform) {
        // Dim background
        ren::Color4B dim{0, 0, 0, 160};
        renderer_->draw_filled_rect_screen(
            0, 0, (float)platform.window_width(), (float)platform.window_height(), dim);

        float panel_w = 480, panel_h = 420;
        float px = (platform.window_width() - panel_w) / 2.0f;
        float py = (platform.window_height() - panel_h) / 2.0f;
        ren::Color4B panel_bg{30, 30, 40, 240};
        renderer_->draw_filled_rect_screen(px, py, panel_w, panel_h, panel_bg);
        ren::Color4B border{120, 90, 50, 255};
        renderer_->draw_filled_rect_screen(px, py, panel_w, 3, border);
        renderer_->draw_filled_rect_screen(px, py + panel_h - 3, panel_w, 3, border);
        renderer_->draw_filled_rect_screen(px, py, 3, panel_h, border);
        renderer_->draw_filled_rect_screen(px + panel_w - 3, py, 3, panel_h, border);

        render_text("MENU", px + panel_w/2 - 50, py + 30, 0.5f,
                    {255, 220, 120, 255});

        struct MenuItem { const char* label; const char* sub; };
        MenuItem items[] = {
            {"MAP",      "Travel to other locations"},
            {"SHOP",     "Buy weapons, armour, helmets"},
            {"SETTINGS", "Audio, graphics, controls"},
            {"SAVE",     "Save progress"},
            {"EXIT",     "Return to title screen"}
        };
        float by = py + 100;
        for (auto& it : items) {
            float bx = px + 30, bw = panel_w - 60, bh = 50;
            ren::Color4B btn_bg{60, 60, 80, 220};
            renderer_->draw_filled_rect_screen(bx, by, bw, bh, btn_bg);
            ren::Color4B btn_brd{100, 100, 130, 255};
            renderer_->draw_filled_rect_screen(bx, by, bw, 2, btn_brd);
            render_text(it.label, bx + 20, by + 10, 0.40f,
                        {255, 255, 255, 255});
            render_text(it.sub, bx + 120, by + 15, 0.28f,
                        {180, 180, 200, 255});
            by += 60;
        }
    }

    // ---------- Dialog overlay ----------
    void render_dialog_overlay(plat::Platform& platform) {
        float panel_w = (float)platform.window_width() - 100, panel_h = 140;
        float px = 50, py = platform.window_height() - panel_h - 60;
        ren::Color4B panel_bg{15, 15, 20, 230};
        renderer_->draw_filled_rect_screen(px, py, panel_w, panel_h, panel_bg);
        ren::Color4B border{140, 100, 50, 255};
        renderer_->draw_filled_rect_screen(px, py, panel_w, 3, border);
        renderer_->draw_filled_rect_screen(px, py + panel_h - 3, panel_w, 3, border);

        render_text("SENSEI", px + 30, py + 15, 0.40f,
                    {255, 220, 120, 255});
        render_text("Welcome back, student.", px + 30, py + 55, 0.32f,
                    {230, 230, 230, 255});
        render_text("Train on the bag, then we will",
                    px + 30, py + 80, 0.32f, {230, 230, 230, 255});
        render_text("talk about your journey.",
                    px + 30, py + 105, 0.32f, {230, 230, 230, 255});

        ren::Color4B arrow{255, 220, 120, 255};
        float ax = px + panel_w - 30, ay = py + panel_h - 25;
        renderer_->draw_filled_rect_screen(ax, ay - 12, 12, 2, arrow);
        renderer_->draw_filled_rect_screen(ax, ay - 12, 2, 12, arrow);
        renderer_->draw_filled_rect_screen(ax + 10, ay - 12, 2, 12, arrow);
    }

private:
    plat::Platform* platform_ = nullptr;
    std::string asset_root_;
    std::unique_ptr<ren::Renderer> renderer_;

    // --- Scene management ---
    scene::SceneManager scene_manager_;
    bool location_loaded_ = false;

    // --- Dialogue / story state ---
    std::vector<std::pair<std::string, std::string>> dialogue_lines_;
    size_t dialogue_index_ = 0;

    // --- Level / progress state ---
    std::string current_level_;
    std::string battle_result_;  // "victory" / "defeat" / ""
    std::vector<std::string> completed_levels_;
    int currency_ = 1000;  // starting gold (stub)

    // --- Combat state ---
    // [ORIGINAL] SF2 combat: each fighter has health (100 by default),
    // takes damage from Attack intervals (MoveDef::intervals[i].damage),
    // and can block (reduces damage). A fight ends when one fighter's
    // health reaches 0. The original uses a state machine per fighter
    // (idle/walk/attack/hit/block/dead); here we track player + enemy.
    struct FighterState {
        float health = 100.0f;
        float max_health = 100.0f;
        float energy = 0.0f;        // 0..100, gained on hit, spent on magic
        float max_energy = 100.0f;
        bool is_blocking = false;
        bool is_hit = false;        // currently in hit-reaction animation
        float hit_stun_time = 0.0f; // remaining hit-stun (can't act)
        float invuln_time = 0.0f;   // invulnerable (after being hit, brief)
        int hits_landed = 0;
        int hits_taken = 0;
        bool is_dead = false;
    };
    FighterState player_fighter_;
    FighterState enemy_fighter_;
    // Enemy AI state (simple: approach, attack when in range, block sometimes)
    float enemy_ai_timer_ = 0.0f;
    float enemy_ai_decision_interval_ = 0.8f;  // seconds between AI decisions
    int enemy_ai_state_ = 0;  // 0=idle, 1=approach, 2=attack, 3=retreat, 4=block
    float enemy_attack_cooldown_ = 0.0f;
    // Hit-flash visual feedback (time remaining for red flash on hit fighter)
    float player_hit_flash_ = 0.0f;
    float enemy_hit_flash_ = 0.0f;

    // --- Enemy skeleton fighter state ---
    // [ORIGINAL] The enemy is a second skeleton fighter (same body.xml/skeleton.xml
    // as the player). For now we render a simplified silhouette + AI controls
    // its position/animation. A full second-model render (with its own .bin
    // animation state) is a larger refactor.
    float enemy_pos_x_ = 0.0f;
    float enemy_pos_y_ = 0.0f;
    float enemy_anim_time_ = 0.0f;
    std::string enemy_anim_ = "fists_idle";
    bool enemy_facing_right_ = false;  // enemy faces left (toward player) by default
    float enemy_y_adjust_ = 4.0f;
    // Enemy attack timing
    float enemy_attack_timer_ = 0.0f;
    bool enemy_attacking_ = false;
    float enemy_attack_duration_ = 0.0f;

    Overlay overlay_ = Overlay::None;
    float menu_anim_progress_ = 0.0f;  // 0 = collapsed, 1 = fully expanded
    bool loc_icons_logged = false;  // one-shot diagnostic for menu icon sizes
    float load_scale_ = 1.0f, zoom_ = 1.0f;
    std::vector<LoadingImg> loading_images_;
    std::unique_ptr<GameLocation> location_;
    std::unordered_map<std::string, AtlasRef> atlases_;
    std::unordered_map<std::string, SkelNode> skeleton_nodes_;
    std::unordered_map<std::string, SkelEdge> skeleton_edges_;
    // Ordered list of ALL skeleton.xml node names (Node + COM + MacroNode)
    // in XML order. This matches the .bin node order.
    std::vector<std::string> ordered_node_names_;
    std::unique_ptr<BodyModel> body_model_;
    std::unique_ptr<BodyModel> bag_model_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> hud_textures_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> menu_textures_;
    std::unordered_map<std::string, std::unique_ptr<ren::Texture2D>> scroll_textures_;

    std::shared_ptr<font::ParsedFont> hud_font_;
    std::unique_ptr<ren::Texture2D> hud_font_tex_;

    float player_pos_x_ = 0, player_pos_y_ = 0;
    float cam_x_ = 0, cam_y_ = 0;
    bool facing_right_ = true;
    int hit_anim_ = 0;    // ms remaining
    uint32_t step_cooldown_ = 0;  // ms remaining before next step animation can start
    bool step_active_ = false;    // true while a step is in progress
    uint32_t step_duration_ = 0;  // ms remaining in current step
    float step_start_x_ = 0;      // player X at start of step
    float step_displacement_ = 0; // total displacement for this step (+66 or -66)
    int bag_swing_ = 0;   // ms remaining (legacy, for compatibility)
    void bag_hit_reset() {
        bag_hit_ = false;
    }
    bool bag_hit_ = false;  // bag already hit during current attack
    float bag_swing_dir_ = 1.0f;  // +1 = swing right, -1 = swing left
    // Physics-based pendulum state for the punching bag.
    // The bag hangs from Node12 (fixed ceiling point) and swings as a pendulum.
    // On hit: an impulse is applied to bag_angle_vel_.
    // Each frame: spring restoring force + damping + integration.
    float bag_angle_ = 0.0f;       // current angle (radians, 0 = vertical)
    float bag_angle_vel_ = 0.0f;   // angular velocity (rad/sec)
    // Verlet physics state for the punching bag.
    // The original game uses Verlet integration for the bag's skeleton.
    // Each node has position + previous position. Edges are distance constraints.
    // Fixed nodes (Node12 = ceiling attachment) don't move.
    std::unordered_map<std::string, VerletNode> bag_verlet_;
    std::vector<VerletConstraint> bag_constraints_;
    bool bag_verlet_init_ = false;
    bool quit_requested_ = false;
    
    // Animation state
    std::unordered_map<std::string, AnimationData> animations_;
    std::unordered_map<std::string, MoveDef> moves_;
    std::string current_move_;  // Name of currently playing move (for hit detection)
    std::string current_anim_ = "fists_idle";
    float anim_time_ = 0.0f;  // seconds into current animation
    float anim_speed_ = 30.0f;  // Animation FPS (bin files are 30fps)
    bool anim_loop_ = true;

    // Animated node positions (override skeleton rest pose during animation)
    std::unordered_map<std::string, std::pair<float, float>> anim_node_pos_;  // name -> (x, y)

    // Root motion offset (delta from animation frame 0).
    // .bin float[1] = absolute root X, float[2] = absolute root Y.
    // We use the DELTA from frame 0 to move the whole model during animation
    // (e.g. lunge forward during punch, steps during walk). This is safe —
    // it moves the entire character without tearing, since all nodes shift
    // together. Per-node animation (limb movement) requires the unsolved
    // .bin node-mapping table and is therefore disabled.
    float anim_root_dx_ = 0.0f;
    float anim_root_dy_ = 0.0f;
    // Anchor: root position at frame 0 of the current animation (subtracted
    // so the model doesn't snap to the .bin's world coordinates).
    float anim_root_anchor_x_ = 0.0f;
    float anim_root_anchor_y_ = 0.0f;
    bool anim_anchor_set_ = false;
    float prev_npivot_x_ = 0.0f;  // for root motion (previous frame's interpolated NPivot)
    bool prev_npivot_set_ = false;
    float prev_npivot_y_ = 0.0f;  // for jump Y root motion
    bool prev_npivot_y_set_ = false;
    int prev_frame_idx_ = -1;     // for wrap-around detection
    float jump_y_offset_ = 0.0f;  // accumulated Y offset from jump root motion
    float prev_root_offset_ = 0.0f;  // offset from frame-0 NPivot (for root motion)
    float step_start_player_x_ = 0.0f;  // player X when step started (for absolute root motion)
    bool anim_facing_right_ = true;  // facing locked at animation start (for root motion direction)
    float y_adjust_smoothed_ = 4.0f;  // smoothed Y adjustment (init to FEET_FLOOR_OFFSET so frame 1 is grounded)
    uint64_t total_frame_count_ = 0;  // global frame counter (for [ROOT] log diagnostics)
    int no_key_frames_ = 0;  // frames with no movement key pressed (for hysteresis)
    int move_state_ = 0;  // 0=IDLE, 1=MOVING_LEFT, 2=MOVING_RIGHT, 10=special, 11=block
    uint32_t step_play_time_ = 0;  // ms the current step animation has been playing
    uint32_t duck_play_time_ = 0;  // ms the duck animation has been playing
    int fwd_held_ms_ = 0;  // ms since forward key was last held (for latching)
    int back_held_ms_ = 0;  // ms since back key was last held (for latching)
    uint32_t last_kick_press_ms_ = 0;  // for double-tap detection (DoubleSweep)
    uint32_t last_punch_press_ms_ = 0;  // for double-tap detection (DoublePunch)
    uint32_t last_punch_seen_ms_ = 0;  // sticky buffer for O key (150ms window)
    uint32_t last_kick_seen_ms_ = 0;   // sticky buffer for P key (150ms window)
    bool start_stance_playing_ = false;  // true during start stance animation
    bool need_switch_to_idle_ = false;  // deferred switch to idle (after update_animation)
    bool is_uninterrupt_ = false;
    bool replay_mode_ = false;  // skip menus, go directly to Battle  // true when current frame is in Uninterrupt interval
    bool dump_state_ = false;  // --dump-state: print structured state every frame
    float anim_npivot_bin_y_ = 169.48f;  // animated NPivot Y from .bin (for Y normalization)
    std::string last_logged_anim_;  // for one-shot diagnostic in update_animation
};

int main(int argc, char* argv[]) {
    std::string asset_root;
    std::string input_script_path;
    int max_frames = -1;  // -1 = unlimited
    bool replay_mode = false;  // skip menus, go directly to Battle
    bool dump_state = false;   // --dump-state: print structured state every frame
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--assets" && i + 1 < argc) asset_root = argv[++i];
        else if (arg == "--input-script" && i + 1 < argc) input_script_path = argv[++i];
        else if (arg == "--max-frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else if (arg == "--replay") replay_mode = true;
        else if (arg == "--dump-state") dump_state = true;
        else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: resf2_app [--assets <path>]\n"
                        "                 [--input-script <path>] [--max-frames N]\n"
                        "                 [--replay] [--dump-state]\n");
            return 0;
        }
    }
    auto platform = std::make_unique<plat::GlfwPlatform>();
    plat::WindowConfig cfg;
    cfg.title = "reSF2 - Shadow Fight 2";
    cfg.width = 1280; cfg.height = 720; cfg.vsync = true;
    if (!platform->init(cfg)) {
        std::fprintf(stderr, "Platform init failed.\n"); return 1;
    }
    // [DIAGNOSTIC] Load deterministic input script if provided.
    if (!input_script_path.empty()) {
        platform->load_input_script(input_script_path);
    }
    Game game(asset_root, replay_mode, dump_state);
    if (!platform->make_gl_current()) {
        std::fprintf(stderr, "Failed to make GL context current.\n"); return 1;
    }
    game.on_init(*platform);
    auto last_ms = platform->now_ms();
    bool was_paused = false;
    int frame_count = 0;
    while (true) {
        if (!platform->poll_events()) break;
        if (platform->should_quit()) break;
        if (game.quit_requested()) break;
        bool is_paused = platform->is_paused();
        if (is_paused && !was_paused) { game.on_pause(*platform); was_paused = true; }
        else if (!is_paused && was_paused) {
            game.on_resume(*platform); was_paused = false; last_ms = platform->now_ms();
        }
        if (is_paused) { platform->sleep_ms(100); continue; }
        auto now = platform->now_ms();
        auto dt = (std::min)(now > last_ms ? (uint32_t)(now - last_ms) : 0u, 200u);
        last_ms = now;
        game.on_update(*platform, dt);
        game.on_render(*platform);
        platform->swap_buffers();
        ++frame_count;
        if (max_frames > 0 && frame_count >= max_frames) break;
    }
    game.on_shutdown(*platform);
    platform->shutdown();
    return 0;
}
