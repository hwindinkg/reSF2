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

#include <string>

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

    // Loads the Warrior. When no save exists, the template is parsed and
    // returned (nothing is written yet — the game only writes on first
    // real save). Throws std::runtime_error on malformed XML.
    WarriorSave load();

    // Writes `warrior` back into the users.xml document and saves it.
    void save(const WarriorSave& warrior);

    // Path of the save file (for logging/tests).
    const std::string& save_path() const { return save_path_; }

private:
    std::string save_path_;
    std::string default_path_;
};

} // namespace sf2::app
