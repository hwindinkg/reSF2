#pragma once

// Save system — the users.xml save (JS `Aa`/`SF2User`, JS_MAP §6).
//
// The web game stores the player save under the storage key `SF2User`
// (JS L2462: `Aa.WU="SF2User"`): a serialized XML document, zstd+base64
// encoded, read/written through the `Ck` storage handle (`Aa.load()` /
// `Aa.save()`, JS L70-71). There is no literal `users.xml` string in the
// JS — "users.xml" of the recovery docs IS the SF2User save. The native
// port keeps the same document (users.xml text) but stores it as a plain
// file on disk instead of localStorage.
//
// The template comes from `reference/www/res/users_default.b7da2019.xml`
// (asset id 9, `G.rq[9]="users_default.xml"`). The Warrior carries the
// progression: Money, Strength, Stamina, Level, Experience, Power,
// Skeleton/Armor/Helm/Weapon/Ranged/Magic, Tutorial, CurrentZone, and the
// equipped Items list.
//
// On first run the native port copies the template (the game's
// `L.aia`: `this.BJ=!Aa.Ue() && Aa.init()`, JS L65) — when no save exists
// the game re-initializes from the default. `SaveSystem` mirrors that:
// `load()` returns the template when no save file exists, `save()` writes
// the current document back.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sf2::app {

// The player's progression fields the shell needs (a trimmed projection of
// the full `<Warrior>` element — the JS `p.o` user state).
struct WarriorSave {
    int id = 1;
    std::string first_name = "NAME_SHADOW";
    int money = 0;
    int bonus = 50;
    int strength = 3;
    int stamina = 3;
    int level = 1;
    int experience = 0;
    int power = 5;
    std::string skeleton = "Skeleton";
    std::string armor = "Body";
    std::string helm = "Head";
    std::string weapon = "Fists";
    std::string ranged = "NoRanged";
    std::string magic = "NoMagic";
    std::string tutorial = "MOVE";
    std::string tactic = "Player";
    std::string current_zone = "ZONE_1";

    // The owned items (JS `$g.items`, the users.xml `<Items><Item Name=..>`).
    // `equipped` mirrors the JS `Ru` flag (the item's `Equipped="1"` attr).
    struct OwnedItem {
        std::string name;      // the list.xml Item Name ("WEAPON_KNIVES", "Body", ...)
        int count = 1;
        bool equipped = false; // Equipped="1"
    };
    std::vector<OwnedItem> items;

    bool has_item(const std::string& name) const {
        for (const OwnedItem& it : items) {
            if (it.name == name && it.count > 0) return true;
        }
        return false;
    }

    // Battle records (JS `iF`, `<Battles><Battle Name="ZONE_1|BOSS_LYNX|">`).
    // Presence = node progress record for the `WDa` unlock rule.
    std::vector<std::string> battles;

    bool has_battle(const std::string& name) const {
        for (const std::string& b : battles) {
            if (b == name) return true;
        }
        return false;
    }

    void record_battle_win(const std::string& name) {
        if (!has_battle(name)) battles.push_back(name);
    }

    // Fight win counts (JS `yc`, `<Fights>/<Fight>`; the `no` win count).
    // The count attr name is OPEN (no <Fights> in the seed) — "Wins" used.
    struct FightWins {
        std::string name;
        int wins = 0;
    };
    std::vector<FightWins> fights;

    // Quest states + story variables (JS `kF`/`rv`:
    // `<Quests>/<Quest Name State>` + `<Quests>/<Variables>/<Variable>`).
    // Nesting follows FLOW_STATIC section 3.2; attr names flagged OPEN
    // (no <Quests> in the seed).
    struct QuestState {
        std::string name;
        std::string state;
    };
    std::vector<QuestState> quests;
    std::map<std::string, std::string> variables;  // quest vars (`rv`)

    // Story tutorial step (JS `_$StoryTutorialStep`, `p.L3`/`ha.WO`).
    // Stored as a quest variable; empty = not started.
    std::string story_step() const {
        const auto it = variables.find("_$StoryTutorialStep");
        return it != variables.end() ? it->second : std::string();
    }

    void set_story_step(const std::string& step) {
        variables["_$StoryTutorialStep"] = step;
    }

    std::string map_focus;  // `ys` (MapFocus attr; absent in seed)

    // Delivery countdowns (JS `yl`/`Ct` timers: `Uaa/BXa/bva` set, `gJ`
    // get, `H4` clear, persisted under save `<Timers>`, L250/291; `Gb`
    // setTime/Tma stamps Cla(now) + save): item name -> wall-clock due
    // epoch (seconds). due <= now means matured (`Bma/Oda` ->
    // QUEST_EVENT_DELIVERY). Wall-clock-delta (no ticking needed).
    std::map<std::string, std::int64_t> timers;

    // Seconds until maturity (<0 = no timer; <=0 with entry = matured).
    std::int64_t timer_remaining(const std::string& name, std::int64_t now) const {
        const auto it = timers.find(name);
        if (it == timers.end()) return -1;
        return it->second - now;
    }

    // Current wall-clock epoch seconds (Cla(now) analog).
    static std::int64_t wall_now();

    // Currencies (JS `pG`: `<Currencies>/<Currency Name Count>`; the Count
    // attr name is OPEN) and Resistances (JS `Pw`: `<Resistances>` ATTRS,
    // e.g. `Resistance_2="0"` — certain, in the seed).
    std::map<std::string, int> currencies;
    std::map<std::string, int> resistances;
};

// Loads/saves the users.xml document. Portable C++17 — the path is passed
// in (the app layer resolves the repo-relative location).
class SaveSystem {
public:
    // `save_path` = where the users.xml save file lives (created on first
    // save). `default_path` = the users_default template (copied verbatim
    // when no save exists).
    SaveSystem(std::string save_path, std::string default_path);

    // Returns true if a save file already exists on disk.
    bool has_save() const;

    // Loads the Warrior (incl. the owned items). When no save exists, the
    // template is parsed and returned (nothing is written yet — the game
    // only writes on first real save). Throws std::runtime_error on
    // malformed XML.
    WarriorSave load();

    // Writes `warrior` back into the users.xml document and saves it. The
    // Warrior attributes + the <Items> list are patched (mirroring the JS
    // `Aa.save` re-serializing the whole document).
    void save(const WarriorSave& warrior);

    // The SF2User envelope (FLOW_STATIC section 3.1 + R7, JS L70-73/L2333):
    // decode: base64 -> `Ug` frames (`ke(len)+yna(bytes)` length-prefixed
    // zstd frames, no separator) -> XML text (`Aa.load`); the `.sf2` export
    // is `"SF2" + base64(ke+yna(users) + ke+yna(packs) + $p(H1) + $p(VF))`
    // (`Aa.Ddb/Dpb`). `ke` = u32 (cP unset in the bundle -> falsy -> LE);
    // `ke` covers the COMPRESSED length (`Aa.save` round-trippable form;
    // `Dpb` writes the string length — latent game inconsistency, noted).
    // `load()` below accepts plain XML (first char `<`), framed envelopes
    // (`SF2` prefix), and legacy whole-blob envelopes (transparent read);
    // `save()` keeps writing plain XML (existing saves keep working).
    static std::string envelope_decode(const std::string& envelope_text);
    static std::string export_sf2(const std::string& users_xml,
                                  const std::string& packs_xml, bool h1, bool vf);

    // Path of the save file (for logging/tests).
    const std::string& save_path() const { return save_path_; }

private:
    std::string save_path_;
    std::string default_path_;
};

} // namespace sf2::app
