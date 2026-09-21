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

#include <algorithm>
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
        // JS `$g` item `Ce` (upgrade level): `this.Ce =
        // u.I(this.ga.attributes.get("UpgradeLevel"))` — the owned item node's
        // `UpgradeLevel` attr, default 0. Read by `?Purchase[x].UpgradeLevel`.
        int upgrade_level = 0;
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

    // A battle is "recorded" when either the raw `<Battle Name>` list carries
    // it (legacy native rows, incl. the zone-qualified raw form) or a parsed
    // `hl` record does (`battle_records` below). JS `WDa` (L256) keys the
    // zone-qualified `hb` triple; the native also accepts the bare name.
    bool has_battle(const std::string& name) const {
        for (const std::string& b : battles) {
            if (b == name) return true;
        }
        for (const BattleRecord& r : battle_records) {
            if (r.name == name) return true;
        }
        return false;
    }

    // Legacy native writer (kept for the old bare-name rows). The JS-exact
    // path is `battle_unlock` below (`J1a` L259, zone-qualified `hb` key).
    void record_battle_win(const std::string& name) {
        if (!has_battle(name)) battles.push_back(name);
    }

    // Battle progress records (JS `hl`, `<Battles><Battle Name="ZONE_1|BOSS_LYNX|"
    // Locked=".." Hidden=".." EndTime="..">`): `hl` ctor L276-277, `tt()` L278
    // (locked art), `li()` L278 (Hidden/expired). The map node button's
    // visibility is `Qr.lla` L2094 `X(hs.isActive && !a)` where
    // `hs.isActive = WDa(zone|battle)` (L205/L256) and `a = li()`.
    // `Name` splits on "|" -> zone / battle; rows written before this existed
    // carry a bare battle name (no "|") -> `zone` empty.
struct BattleRecord {
    std::string zone;      // `hb.Me`
    std::string name;      // `hb.Re`
    bool locked = false;   // `hl.zo` / `tt()` (Locked attr)
    bool hidden = false;   // `hl.d9` / `li()` (Hidden attr)
    int replay_count = 0;  // `hl.zH` / `yla` (ReplayCount attr, L279)
};
    std::vector<BattleRecord> battle_records;

    // The `<Battles>` record for (zone, name) (`At.get` L276 via
    // `WDa(a) = iF.get(a) != null`, L256). A zone-qualified row wins; a
    // bare-name row (empty zone) matches any zone.
    const BattleRecord* find_battle(const std::string& zone,
                                    const std::string& name) const {
        const BattleRecord* loose = nullptr;
        for (const BattleRecord& r : battle_records) {
            if (r.name != name) continue;
            if (r.zone == zone) return &r;
            if (r.zone.empty() && loose == nullptr) loose = &r;
        }
        return loose;
    }

    // Mutable lookup (the `hl` write path). Same match rule as `find_battle`.
    BattleRecord* battle_record(const std::string& zone, const std::string& name) {
        BattleRecord* loose = nullptr;
        for (BattleRecord& r : battle_records) {
            if (r.name != name) continue;
            if (r.zone == zone) return &r;
            if (r.zone.empty() && loose == nullptr) loose = &r;
        }
        return loose;
    }

    // JS `J1a` L259 / `u4a` L260: find-or-append the `<Battles><Battle
    // Name="zone|name|">` row (`hb.toString` L1416: `Me+"|"+Re+"|"`). Record
    // presence IS the `WDa` unlock (L256: `iF.get(a)!=null`). JS `J1a` wraps
    // the node in `hl` and adds it to `iF`; the native stores the parsed
    // `BattleRecord` (the raw `battles` string list is written alongside by
    // `SaveSystem::save`, which re-serializes every `<Battle>` row).
    void battle_unlock(const std::string& zone, const std::string& name) {
        if (name.empty()) return;
        if (find_battle(zone, name) != nullptr) return;
        BattleRecord r;
        r.zone = zone;
        r.name = name;
        battle_records.push_back(std::move(r));
    }

    // JS `Iaa(a,b,c,d,e,f)` L260-261: the `hl` flag write (`uMa/CMa/gx/yla`).
    // Ensures the row first (the `c=true` -> `u4a` path) then sets
    // Locked/Hidden/ReplayCount (`d`/`e`/`f`).
    void battle_set_visibility(const std::string& zone, const std::string& name,
                               bool locked, bool hidden, int replay_count) {
        if (name.empty()) return;
        BattleRecord* rec = battle_record(zone, name);
        if (rec == nullptr) {
            battle_unlock(zone, name);
            rec = battle_record(zone, name);
        }
        if (rec == nullptr) return;
        rec->locked = locked;
        rec->hidden = hidden;
        rec->replay_count = replay_count;
    }

    // JS `Eja` L261 / `Rmb` L261 (+ `inb`-style row removal): drop the battle
    // record entirely (`HideBattle` -> `Aj(false)` -> `Iaa` `c=false` ->
    // `Eja`). The node's `WDa` bit then reads false.
    void battle_remove(const std::string& zone, const std::string& name) {
        if (name.empty()) return;
        const std::string raw =
            zone.empty() ? name : (zone + "|" + name + "|");
        battles.erase(std::remove(battles.begin(), battles.end(), name), battles.end());
        battles.erase(std::remove(battles.begin(), battles.end(), raw), battles.end());
        for (std::size_t i = 0; i < battle_records.size();) {
            const BattleRecord& r = battle_records[i];
            if (r.name == name && (r.zone == zone || r.zone.empty())) {
                battle_records.erase(battle_records.begin() +
                                     static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    // Fight records (JS `yc` = `il`, L141476): `<Fights><Fight .../></Fights>`.
    // The record's identity is the `IDS` attribute (`il.Atb` L143548:
    // `this.yG = a; this.node.set("IDS", a)`; the ctor defaults a missing
    // `IDS` to `"-1|-1|-1"`) and the win count is `CompletedCount`
    // (`il.Fab` L143548: `this.no++; this.node.set("CompletedCount", K.T(no))`).
    // `il` also reads/writes LossCount, EclipseCompletedCount,
    // EclipseLossCount, StoryCount, CompletedTime, TimeLeft,
    // RandomizeTimeLeft and Level; the port tracks only the win count.
    struct FightWins {
        std::string name;  // `IDS` (the JS `il.yG`)
        int wins = 0;      // `CompletedCount` (the JS `il.no`)
    };
    std::vector<FightWins> fights;

    // Quest records + story variables (JS `kF`/`rv`). The save shape is
    // `<Quests><Quests><Quest Name FileName/></Quests><Variables>
    // <Variable Name Value/></Variables></Quests>` (`sc` parse L126400:
    // `a.A("Quests").A("Quests")` -> `SIa` -> `new Et`; the quest writer
    // `WO` L132600 appends into the nested `Quests`). `Et` (L144813) reads
    // `FileName`, `Name` and `Type` — there is no `State` attribute.
    struct QuestState {
        std::string name;       // `Et.name` (`Name` attr)
        std::string file_name;  // `Et.fileName` (`FileName` attr)
    };
    std::vector<QuestState> quests;
    // `rv`: the quest variables. JS keys carry a LEADING `_` (`wkb` L132880:
    // `c = "_" + Name`; `WA` L133404 writes the public name back), so the
    // port's map uses the same `_`-prefixed key. See `variable_key_for`.
    std::map<std::string, std::string> variables;  // quest vars (`rv`)

    // The JS `rv` key for a save `<Variable Name>` (L132880: `"_" + Name`).
    // Reading a real authored save therefore stores BOTH the raw `Name` and
    // the `_`-prefixed key, so `story_step()` (which looks up
    // `_$StoryTutorialStep`) and the port's raw-name readers both resolve.
    static std::string variable_key_for(const std::string& name) {
        return name.empty() || name[0] == '_' ? name : ("_" + name);
    }

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

    // Session settings (JS `Aka` -> `xLa` L264: `<SessionSettings><Name
    // Value="0|1"/>`). `Disciple` = `Y0()` (L271) — the dojo disciple toggle
    // (`oub(a)` L271 writes it; `za.Nfb` L1981 flips it); `ShowDojoDisciple`
    // = `g$a()` (L271) — gates the `za.zq` toggle's visibility (`v.FU`
    // L1207: shown only when the active screen is Dojo AND this is > 0).
    bool disciple = false;
    bool show_dojo_disciple = false;

    // Persisted bus mutes (JS `sc.Gpb` L114249 / `sc.ckb` L113759):
    // `<CurrentUser><Sounds><Sound Mute>` = `ta.$D` (SFX, `lb.Mz()`),
    // `<Music Mute>` = `ta.ZD` (music, `lb.Lz()`). "1" = muted.
    bool sound_muted = false;  // `ta.$D` / `lb.Mz()` (L1265/L1276)
    bool music_muted = false;  // `ta.ZD` / `lb.Lz()` (L1265/L1276)

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

    // Currencies (JS `pG`, L126965/L139448). `xf.Jia` (L139448) reads the
    // counts as ATTRIBUTES of `<Currencies>` keyed by the currency's name
    // (`a.attributes.get(d.name)`), and `GLa` (L137813) writes
    // `this.pG.set(a, "" + b)` — there is no `<Currency Name Count>` child
    // and no `Count` attribute. Resistances (JS `Pw`: `<Resistances>` ATTRS,
    // e.g. `Resistance_2="0"` — certain, in the seed).
    std::map<std::string, int> currencies;
    std::map<std::string, int> resistances;

    // Perk progression (JS `Bt.KS` = `Ht`, `<PerkHistory><Level Perk="NAME"
    // Value="N"/>`; `Ht.parse` L1327, exposed as `p.o.co.KS.Oa`). The `ds`
    // perk tree merges it via `id.cPa` (L1353) and `Mw.K1` (L1358).
    struct PerkLevel {
        std::string name;   // `Mj.name` (Perk attr)
        int level = 0;      // `Mj.level` (Value attr)
    };
    std::vector<PerkLevel> perk_history;

    // Perk unlock/upgrade records (JS `Bt.jF` of `Ji`, `<Perks><Perk
    // Name=".." Level=".." UpgradeLevel="..">`; `Ji` ctor L284-285:
    // `Ba`=Name, `ZB`=Level, `Ce`=UpgradeLevel). Written by `Bt.L1a`
    // (L306) -> `Qua` (L307). `learned_level` in the Profile tree is the
    // `<PerkHistory>` max for the name (`id.cPa` L1353).
    struct PerkState {
        std::string name;         // `Ji.Ba`
        int level = 0;            // `Ji.ZB` (the learned tier)
        int upgrade_level = 0;    // `Ji.Ce` (`Ih.PQ()` = the def `Tc`)
    };
    std::vector<PerkState> perks;

    // JS `Ht.xI`/`Epb` L1328/L307: append a `<PerkHistory><Level Perk Value>`
    // row unless that level already exists (`Ht.fcb` L1328), keeping the
    // list sorted (`Ht.parse` L1327 sorts `Oa` by `Wy`).
    void record_perk_level(const std::string& name, int level) {
        if (name.empty() || level <= 0) return;
        for (const PerkLevel& pl : perk_history) {
            if (pl.level == level) return;  // `Ht.fcb(level)` guard
        }
        perk_history.push_back({name, level});
        std::sort(perk_history.begin(), perk_history.end(),
                  [](const PerkLevel& a, const PerkLevel& b) { return a.level < b.level; });
    }

    // JS `Bt.L1a` L306 (create branch) + `Ji` L284-285 + `Ht.xI` L1328: write
    // the `<Perks><Perk>` record (one per name) and its `<PerkHistory>` row.
    void learn_perk(const std::string& name, int level, int upgrade_level) {
        if (name.empty() || level <= 0) return;
        bool found = false;
        for (PerkState& p : perks) {
            if (p.name != name) continue;
            p.level = level;                                   // `Ji.xL`
            if (upgrade_level > 0) p.upgrade_level = upgrade_level;  // `Ji.Np`
            found = true;
            break;
        }
        if (!found) perks.push_back({name, level, upgrade_level});
        record_perk_level(name, level);
    }

    // JS `Bt.L1a` L306 match branch (`e&&f`, existing name + type 2): only
    // `Np(a.PQ())` (UpgradeLevel) is written — `Ji.xL` (Level) is untouched.
    // Falls back to `learn_perk` when no `<Perks>` row exists yet.
    void learn_perk_upgrade(const std::string& name, int tier, int upgrade_level) {
        if (name.empty()) return;
        for (PerkState& p : perks) {
            if (p.name != name) continue;
            if (upgrade_level > 0) p.upgrade_level = upgrade_level;  // `Ji.Np`
            record_perk_level(name, tier);
            return;
        }
        learn_perk(name, tier, upgrade_level);
    }

    // Achievement counter values (JS `kl`, `yi.mC`: `<Counters><Counter
    // Name="PerfectRound" CurrentValue="3"/>`; `kl` ctor L1249, `yt.parse`
    // L294). The `fs` list join keys this Name against achievements.xml's
    // `<Counter Name>` (`fs.uZ` L2214).
    struct AchievementCounter {
        std::string name;   // `kl.Ba` (Name)
        int value = 0;      // `kl.AB` (CurrentValue)
    };
    std::vector<AchievementCounter> counters;

    // Achievement unlock records (JS `ll`, `yi.jO`: `<Achievements>
    // <Achievement Name=".." ObtainedReward="true"/>`; `ll` ctor L1247,
    // `yt.parse` L294). `yt.Yua` L297 sets the def's `completed` + reward
    // flag; `yt.sca` L296 writes it back.
    struct AchievementUnlock {
        std::string name;              // `ll.Ba` (Name)
        bool obtained_reward = false;  // `ll.gO` (ObtainedReward)
    };
    std::vector<AchievementUnlock> achievement_unlocks;

    // JS `vb.exb` L2199 + `yt.sca` L296: claim the achievement reward. Writes
    // `ObtainedReward="true"` (`ll.LMa`), then pays the prizes
    // (`p.o.Fr(p.o.Tb+AE)` money += MoneyPrize, `p.o.vl(p.o.fd+dP,2)` bonus
    // += BonusPrize). Self-guarded by the caller on `reward_available`
    // (JS `exb` `if(!a.bS)`; `bS` = already claimed).
    void claim_achievement(const std::string& name, int money_prize, int bonus_prize) {
        if (name.empty()) return;
        bool found = false;
        for (AchievementUnlock& u : achievement_unlocks) {
            if (u.name == name) {
                u.obtained_reward = true;  // `ll.LMa(true)` L296
                found = true;
                break;
            }
        }
        if (!found) achievement_unlocks.push_back({name, true});
        if (money_prize > 0) money += money_prize;   // `exb` L2199
        if (bonus_prize > 0) bonus += bonus_prize;   // `exb` L2199
    }

    // "New move" trick list (JS `Bt.NN`, `<OpenTricks><Trick Name=".."/>`).
    // On save parse (`ht` L250) every `<Trick Name>` calls `Nua(name, false)`
    // (L268), which pushes `NN` and sets `aE = true` on the matching `Ru`
    // catalog entry (`ra.zfa()`). `Bt.sCa` (L256) - profile tab 1's badge -
    // counts the CURRENT WEAPON's `v.uQ()` entries with `aE`; the Moves tab
    // clears them on entry (`es.zha` L2239 -> `Bt.inb` L269 + save).
    std::vector<std::string> open_tricks;

    bool has_open_trick(const std::string& name) const {
        for (const std::string& t : open_tricks) {
            if (t == name) return true;
        }
        return false;
    }

    void clear_open_tricks() { open_tricks.clear(); }  // `es.zha` L2239

    // "New item" list (JS `Bt.bM`/`Gjb`, `<CounterItems><Items><Item
    // Name=".."/>`). `bM` (L269) persists every `p.items.Xm` def whose `yj`
    // flag is set; `Gjb` (L269) restores `Ir(true)` on the named defs, gated
    // on `gU == 0` (`SilentRecieve`). `Bt.vCa` (L256) - profile tab 3's
    // badge - counts the owned `I.Vr` (Seal) rows with `pd() > 0` and
    // `ib.yj`. All seven shipped `Type="Seal"` rows carry
    // `SilentRecieve="0"`, so the `gU` gate is vacuous for them.
    std::vector<std::string> counter_items;

    bool has_counter_item(const std::string& name) const {
        for (const std::string& i : counter_items) {
            if (i == name) return true;
        }
        return false;
    }
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
