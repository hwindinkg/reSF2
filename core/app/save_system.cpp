// Save system implementation — users.xml save/load (JS `Aa`/`SF2User`).
//
// The save document mirrors users_default.xml (see save_system.hpp for the
// JS line refs). On first run (no save file) the template is parsed and
// returned; `save()` rewrites the current Warrior's progression into the
// document and writes it to `save_path`.

#include "app/save_system.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "codec.hpp"
#include "sf2_envelope.hpp"
#include "xml_doc.hpp"
#include "zstd_stream.hpp"

namespace sf2::app {

namespace {

std::string read_file_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("SaveSystem: cannot open " + path);
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<std::size_t>(size));
    in.read(data.data(), size);
    if (!in) {
        throw std::runtime_error("SaveSystem: cannot read " + path);
    }
    return std::string(data.begin(), data.end());
}

void write_file_text(const std::string& path, const std::string& text) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("SaveSystem: cannot write " + path);
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        throw std::runtime_error("SaveSystem: write failed " + path);
    }
}

// Locates the first <Warrior> under <Root>/<Warriors>.
pugi::xml_node find_warrior(pugi::xml_node root) {
    pugi::xml_node warriors = root.child("Warriors");
    if (!warriors) {
        return {};
    }
    return warriors.child("Warrior");
}

} // namespace

SaveSystem::SaveSystem(std::string save_path, std::string default_path)
    : save_path_(std::move(save_path)), default_path_(std::move(default_path)) {}

bool SaveSystem::has_save() const {
    std::error_code ec;
    return std::filesystem::exists(save_path_, ec);
}

WarriorSave SaveSystem::load() {
    const std::string path = has_save() ? save_path_ : default_path_;
    std::string text = read_file_text(path);
    // Dual-format boot read: plain XML starts with `<` (after whitespace);
    // otherwise it is an SF2User envelope (base64+zstd).
    std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && text[first] != '<') {
        text = envelope_decode(text);
    }
    sf2::data::xml_doc doc;
    doc.parse(text);

    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "Root") {
        throw std::runtime_error("SaveSystem: save root element missing in " + path);
    }
    const pugi::xml_node warrior = find_warrior(root);
    if (!warrior) {
        throw std::runtime_error("SaveSystem: no <Warrior> in " + path);
    }

    WarriorSave out;
    out.id = sf2::data::xml_attr_int(warrior, "ID", 1);
    if (warrior.attribute("FirstName")) out.first_name = warrior.attribute("FirstName").value();
    out.money = warrior.attribute("Money") ? warrior.attribute("Money").as_llong() : 0;
    out.bonus = sf2::data::xml_attr_int(warrior, "Bonus", 50);
    out.strength = sf2::data::xml_attr_int(warrior, "Strength", 3);
    out.stamina = sf2::data::xml_attr_int(warrior, "Stamina", 3);
    out.level = sf2::data::xml_attr_int(warrior, "Level", 1);
    out.experience = sf2::data::xml_attr_int(warrior, "Experience", 0);
    out.power = sf2::data::xml_attr_int(warrior, "Power", 5);
    if (warrior.attribute("Skeleton")) out.skeleton = warrior.attribute("Skeleton").value();
    if (warrior.attribute("Armor")) out.armor = warrior.attribute("Armor").value();
    if (warrior.attribute("Helm")) out.helm = warrior.attribute("Helm").value();
    if (warrior.attribute("Weapon")) out.weapon = warrior.attribute("Weapon").value();
    if (warrior.attribute("Ranged")) out.ranged = warrior.attribute("Ranged").value();
    if (warrior.attribute("Magic")) out.magic = warrior.attribute("Magic").value();
    if (warrior.attribute("Tutorial")) out.tutorial = warrior.attribute("Tutorial").value();
    if (warrior.attribute("Tactic")) out.tactic = warrior.attribute("Tactic").value();
    if (warrior.attribute("CurrentZone")) out.current_zone = warrior.attribute("CurrentZone").value();
    out.show_upgrades = sf2::data::xml_attr_bool(warrior, "ShowUpgrades", false);
    // `p.Dc` snapshot (see `live_clock`); absent in the shipped seed -> 0.
    if (warrior.attribute("GameClock")) {
        try {
            out.game_clock = std::stoll(warrior.attribute("GameClock").value());
        } catch (const std::exception&) {
        }
    }

    // Bus mutes (JS `sc.ckb` L113759): `<CurrentUser><Sounds>/<Sound|Music>@Mute`.
    // where `a` is the CurrentUser node (`sc.Ju`, `Aa.save(sc.Ju.parent)`).
    if (pugi::xml_node cu = root.child("CurrentUser")) {
        if (pugi::xml_node snd_root = cu.child("Sounds")) {
            out.sound_muted =
                sf2::data::xml_attr_bool(snd_root.child("Sound"), "Mute", false);
            out.music_muted =
                sf2::data::xml_attr_bool(snd_root.child("Music"), "Mute", false);
        }
    }

    // The owned items (JS `$g.parse` reads the Warrior <Items> children).
    out.items.clear();
    for (pugi::xml_node item : warrior.child("Items").children("Item")) {
        WarriorSave::OwnedItem oi;
        if (item.attribute("Name")) oi.name = item.attribute("Name").value();
        oi.count = sf2::data::xml_attr_int(item, "Count", 1);
        oi.equipped = sf2::data::xml_attr_bool(item, "Equipped", false);
        oi.upgrade_level = sf2::data::xml_attr_int(item, "UpgradeLevel", 0);
        if (item.attribute("AcquireType")) oi.acquire_type = item.attribute("AcquireType").value();
        oi.delivery_upgrade_level = item.attribute("DeliveryUpgradeLevel")
                                        ? item.attribute("DeliveryUpgradeLevel").as_int(-1)
                                        : -1;
        // `<Enchantments>` rows (JS item `aJa` via `Kia`, `xe.Qd` L692882):
        // each `<Perk Name>` + its `<Set>` override attrs. Mirrors the
        // `<Perks>` read just above. `<Set>` attr values are raw expressions
        // (`?RandomAspect[-30,30]`), so they are kept verbatim, in order.
        for (pugi::xml_node enc : item.child("Enchantments").children("Perk")) {
            WarriorSave::ItemEnchantment ie;
            if (enc.attribute("Name")) ie.name = enc.attribute("Name").value();
            if (enc.attribute("ItemType")) {
                const std::string it = enc.attribute("ItemType").value();
                std::size_t start = 0;
                for (;;) {  // `m.addRange(b.g2, c.split("|"))`
                    const std::size_t bar = it.find('|', start);
                    if (bar == std::string::npos) {
                        ie.item_types.push_back(it.substr(start));
                        break;
                    }
                    ie.item_types.push_back(it.substr(start, bar - start));
                    start = bar + 1;
                }
            }
            for (const pugi::xml_attribute a : enc.child("Set").attributes()) {
                ie.sets.push_back({a.name(), a.value()});
            }
            if (!ie.name.empty()) oi.enchantments.push_back(std::move(ie));
        }
        out.items.push_back(std::move(oi));
    }

    // Shop locks (JS `p.o.R$`, ctor L124103; the `sc` parse L126188 walks
    // every `<Shop>` child's `Name` -> `vq(name)` -> `R$.add`). The writer
    // emits `<Shop><Lock Name="...">`.
    out.shop_locks.clear();
    for (pugi::xml_node lock : warrior.child("Shop").children("Lock")) {
        if (lock.attribute("Name")) {
            out.shop_locks.push_back(lock.attribute("Name").value());
        }
    }

    // Battle records (JS `iF`): `<Battles><Battle Name="...">` presence.
    out.battles.clear();
    for (pugi::xml_node b : warrior.child("Battles").children("Battle")) {
        if (b.attribute("Name")) out.battles.push_back(b.attribute("Name").value());
    }

    // Battle progress records (JS `hl`): split Name "ZONE|BATTLE|" and read
    // the Locked/Hidden flags that drive `Qr.lla` (L2094) visibility.
    out.battle_records.clear();
    for (pugi::xml_node b : warrior.child("Battles").children("Battle")) {
        WarriorSave::BattleRecord r;
        const std::string raw = b.attribute("Name").value();
        const std::size_t p1 = raw.find('|');
        if (p1 == std::string::npos) {
            r.name = raw;
        } else {
            r.zone = raw.substr(0, p1);
            const std::size_t p2 = raw.find('|', p1 + 1);
            r.name = raw.substr(p1 + 1, p2 == std::string::npos ? std::string::npos
                                                                : p2 - (p1 + 1));
        }
        if (r.name.empty()) continue;
        r.locked = sf2::data::xml_attr_bool(b, "Locked", false);
        r.hidden = sf2::data::xml_attr_bool(b, "Hidden", false);
        r.replay_count = sf2::data::xml_attr_int(b, "ReplayCount", 0);
        out.battle_records.push_back(std::move(r));
    }

    // Fight records (JS `yc` = `il`, L141476): `<Fights><Fight .../></Fights>`.
    // Identity = `IDS` (`il.Atb` L143548), and the ctor (L141476) guarantees
    // `CompletedCount`/`LossCount`/`EclipseCompletedCount`/`EclipseLossCount`/
    // `StoryCount`/`CompletedTime`/`TimeLeft`/`RandomizeTimeLeft`/`Level`.
    // The win/loss/level writers are `Fab`/`Lab`/`xL` (L143548); `Pz` (L143548)
    // is the `TimeLeft` attr the `?Fight.Timestamp` query reads (L498369).
    out.fights.clear();
    for (pugi::xml_node f : warrior.child("Fights").children("Fight")) {
        WarriorSave::FightWins fw;
        if (f.attribute("IDS")) fw.name = f.attribute("IDS").value();
        fw.wins = sf2::data::xml_attr_int(f, "CompletedCount", 0);
        fw.losses = sf2::data::xml_attr_int(f, "LossCount", 0);
        fw.eclipse_completed =
            sf2::data::xml_attr_int(f, "EclipseCompletedCount", 0);
        fw.eclipse_loss = sf2::data::xml_attr_int(f, "EclipseLossCount", 0);
        fw.story_count = sf2::data::xml_attr_int(f, "StoryCount", 0);
        fw.completed_time = sf2::data::xml_attr_int(f, "CompletedTime", 0);
        fw.time_left = sf2::data::xml_attr_int(f, "TimeLeft", 0);
        fw.randomize_time_left =
            sf2::data::xml_attr_int(f, "RandomizeTimeLeft", 0);
        fw.level = sf2::data::xml_attr_int(f, "Level", 0);
        out.fights.push_back(std::move(fw));
    }

    // Quests + variables (JS `kF`/`rv`). Absent in the seed -> empty.
    // The quest list is the NESTED `<Quests><Quests>` (sc parse L126400:
    // `a.A("Quests").A("Quests")`), and each `<Quest>` carries `Name` +
    // `FileName` (`Et` ctor L144813); there is no `State` attribute.
    out.quests.clear();
    out.variables.clear();
    if (pugi::xml_node quests = warrior.child("Quests")) {
        for (pugi::xml_node q : quests.child("Quests").children("Quest")) {
            WarriorSave::QuestState qs;
            if (q.attribute("Name")) qs.name = q.attribute("Name").value();
            if (q.attribute("FileName")) qs.file_name = q.attribute("FileName").value();
            // `Et.parameters` (`fl`, L144813): `ScreenIndex`/`ChekPointIndex`.
            if (pugi::xml_node qp = q.child("QuestParameters")) {
                qs.has_parameters = true;  // `Et` ctor: `A("QuestParameters")!=null`
                try {
                    if (qp.attribute("ScreenIndex"))
                        qs.screen_index = std::stoi(qp.attribute("ScreenIndex").value());
                    if (qp.attribute("ChekPointIndex"))
                        qs.checkpoint_index =
                            std::stoi(qp.attribute("ChekPointIndex").value());
                } catch (const std::exception&) {
                }
            }
            out.quests.push_back(std::move(qs));
        }
        if (pugi::xml_node vars = quests.child("Variables")) {
            for (pugi::xml_node v : vars.children("Variable")) {
                if (v.attribute("Name")) {
                    const std::string name = v.attribute("Name").value();
                    const std::string val =
                        v.attribute("Value") ? v.attribute("Value").value() : "";
                    // `wkb` (L132880) stores `"_" + Name`; the port also keeps
                    // the raw name so its unprefixed readers still resolve.
                    out.variables[WarriorSave::variable_key_for(name)] = val;
                    out.variables[name] = val;
                }
            }
        }
    }

    // MapFocus (`ys` attr; absent in seed -> "").
    out.map_focus.clear();
    if (warrior.attribute("MapFocus")) out.map_focus = warrior.attribute("MapFocus").value();

    // Currencies (`pG`): ATTRIBUTES on `<Currencies>` keyed by currency name
    // (`xf.Jia` L139448 reads `a.attributes.get(d.name)`); no child elements.
    out.currencies.clear();
    if (pugi::xml_node cur = warrior.child("Currencies")) {
        for (pugi::xml_attribute a : cur.attributes()) {
            try {
                out.currencies[a.name()] = std::stoi(a.value());
            } catch (const std::exception&) {
            }
        }
    }

    // Resistances (`Pw`): ATTRS on <Resistances> (`Resistance_2="0"`).
    out.resistances.clear();
    if (pugi::xml_node res = warrior.child("Resistances")) {
        for (pugi::xml_attribute a : res.attributes()) {
            try {
                out.resistances[a.name()] = std::stoi(a.value());
            } catch (const std::exception&) {
            }
        }
    }

    // Perk progression (`Bt.KS`/`Ht.parse` L1327): `<PerkHistory><Level
    // Perk="NAME" Value="N"/>`.
    out.perk_history.clear();
    for (pugi::xml_node lvl : warrior.child("PerkHistory").children("Level")) {
        WarriorSave::PerkLevel pl;
        if (lvl.attribute("Perk")) pl.name = lvl.attribute("Perk").value();
        pl.level = sf2::data::xml_attr_int(lvl, "Value", 0);
        if (!pl.name.empty()) out.perk_history.push_back(std::move(pl));
    }

    // Perk unlock/upgrade records (JS `Bt.jF`/`Ji` L284-285):
    // `<Perks><Perk Name=".." Level=".." UpgradeLevel="..">`.
    out.perks.clear();
    for (pugi::xml_node p : warrior.child("Perks").children("Perk")) {
        WarriorSave::PerkState ps;
        if (p.attribute("Name")) ps.name = p.attribute("Name").value();
        ps.level = sf2::data::xml_attr_int(p, "Level", 0);
        ps.upgrade_level = sf2::data::xml_attr_int(p, "UpgradeLevel", 0);
        if (!ps.name.empty()) out.perks.push_back(std::move(ps));
    }

    // Achievement counters (`kl`/`yi.mC`, L1249/L294): `<Counters><Counter
    // Name=".." CurrentValue=".."/>`.
    out.counters.clear();
    for (pugi::xml_node c : warrior.child("Counters").children("Counter")) {
        WarriorSave::AchievementCounter ac;
        if (c.attribute("Name")) ac.name = c.attribute("Name").value();
        ac.value = sf2::data::xml_attr_int(c, "CurrentValue", 0);
        if (!ac.name.empty()) out.counters.push_back(std::move(ac));
    }

    // Achievement unlocks (`ll`/`yi.jO`, L1247/L294): `<Achievements>
    // <Achievement Name=".." ObtainedReward=".."/>`.
    out.achievement_unlocks.clear();
    for (pugi::xml_node a : warrior.child("Achievements").children("Achievement")) {
        WarriorSave::AchievementUnlock au;
        if (a.attribute("Name")) au.name = a.attribute("Name").value();
        au.obtained_reward = sf2::data::xml_attr_bool(a, "ObtainedReward", false);
        if (!au.name.empty()) out.achievement_unlocks.push_back(std::move(au));
    }

    // "New move" trick list (JS `Bt.lsa`, L250: `this.lsa = a.A("OpenTricks");
    // this.lsa != null && for each child -> this.Nua(name, false)`). `Nua`
    // (L268) pushes `Bt.NN` and flips `aE` on the matching `Ru` catalog
    // entry; `Bt.sCa` (L256) - the profile tab 1 badge - counts the current
    // weapon's `v.uQ()` (L1218) entries with `aE`. Absent in the seed -> 0.
    out.open_tricks.clear();
    for (pugi::xml_node t : warrior.child("OpenTricks").children("Trick")) {
        if (t.attribute("Name")) {
            const std::string n = t.attribute("Name").value();
            if (!n.empty()) out.open_tricks.push_back(n);
        }
    }

    // "New item" list (JS `Bt.Gjb` L269: `a = this.ga.A("CounterItems")` ->
    // `a.A("Items")` -> each `<Item Name>` -> `p.items.$b(name)` ->
    // `b.gU == 0 && b.Ir(true)`). `Bt.vCa` (L256) - the profile tab 3 badge -
    // counts the owned `I.Vr` (Seal) rows with `pd() > 0` and `ib.yj`.
    // Absent in the seed -> 0.
    out.counter_items.clear();
    for (pugi::xml_node i : warrior.child("CounterItems").child("Items").children("Item")) {
        if (i.attribute("Name")) {
            const std::string n = i.attribute("Name").value();
            if (!n.empty()) out.counter_items.push_back(n);
        }
    }

    // Session settings (JS `jfa` L256 / `Aka` L264): `<SessionSettings>
    // <Disciple Value="0|1"/>` (`Y0` L271) + `<ShowDojoDisciple Value="0|1"/>`
    // (`g$a` L271). Absent in the seed -> both default 0.
    out.disciple = false;
    out.show_dojo_disciple = false;
    if (pugi::xml_node ss = warrior.child("SessionSettings")) {
        if (pugi::xml_node d = ss.child("Disciple")) {
            out.disciple = sf2::data::xml_attr_int(d, "Value", 0) > 0;
        }
        if (pugi::xml_node s = ss.child("ShowDojoDisciple")) {
            out.show_dojo_disciple = sf2::data::xml_attr_int(s, "Value", 0) > 0;
        }
    }

    // Delivery timers (`yl`/`Ct` under save `<Timers>`, L250: `Uaa/BXa`
    // set, `gJ` get, `H4` clear). Child schema `<Timer Name Due>` is the
    // shell's choice (no Timers element ships in the seed).
    out.timers.clear();
    if (pugi::xml_node timers = warrior.child("Timers")) {
        for (pugi::xml_node t : timers.children("Timer")) {
            if (!t.attribute("Name")) continue;
            try {
                out.timers[t.attribute("Name").value()] =
                    std::stoll(t.attribute("Due").value());
            } catch (const std::exception&) {
            }
        }
    }

    // Seed the live `p.Dc` on the FIRST load only (a later `load` must not
    // rewind the tick-advanced clock). `game_clock` resumes the persisted
    // absolute domain; a fresh/template save with none starts at real epoch
    // (`Hb.khb` L... re-syncs `N$=ed.rfa()` on boot).
    if (WarriorSave::live_clock() <= 0.0) {
        WarriorSave::live_clock() = out.game_clock > 0
            ? static_cast<double>(out.game_clock)
            : static_cast<double>(WarriorSave::wall_now());
    }
    return out;
}

std::int64_t WarriorSave::wall_now() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

// The live `p.Dc` backing store (see `WarriorSave::live_clock`). A process
// singleton: seeded at the first `load`, advanced by the app tick.
double& WarriorSave::live_clock() {
    static double c = 0.0;
    return c;
}

// JS `Oqb` (L181): `p.o.EB.A("Root").A("Versions").A("DataVersion")
// .set("Value", a); p.o.save(!0)`. Reads the current document (save or
// template), sets the ROOT `<Versions><DataVersion Value>`, and writes the
// whole document back (plain XML, like every other port save).
void SaveSystem::set_data_version(const std::string& value) {
    const std::string src_path = has_save() ? save_path_ : default_path_;
    sf2::data::xml_doc doc;
    doc.parse(read_file_text(src_path));
    pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "Root") {
        throw std::runtime_error(
            "SaveSystem: cannot set DataVersion - root element missing");
    }
    pugi::xml_node versions = root.child("Versions");
    if (!versions) versions = root.append_child("Versions");
    pugi::xml_node dv = versions.child("DataVersion");
    if (!dv) dv = versions.append_child("DataVersion");
    dv.attribute("Value").set_value(value.c_str());
    std::ostringstream oss;
    doc.save(oss, "\t", pugi::format_default, pugi::encoding_auto);
    write_file_text(save_path_, oss.str());
}

std::string SaveSystem::data_version() {
    const std::string src_path = has_save() ? save_path_ : default_path_;
    sf2::data::xml_doc doc;
    doc.parse(read_file_text(src_path));
    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "Root") return std::string();
    const pugi::xml_node versions = root.child("Versions");
    if (!versions) return std::string();
    const pugi::xml_node dv = versions.child("DataVersion");
    if (!dv) return std::string();
    const pugi::xml_attribute v = dv.attribute("Value");
    return v ? std::string(v.value()) : std::string();
}

void SaveSystem::save(const WarriorSave& w) {
    // Load the current document (the save, or the template when none yet),
    // patch the Warrior attributes, and write back. This preserves the
    // full users.xml structure (Items/Battles/Versions/...) exactly like
    // the game's `Aa.save` (which re-serializes the whole Rb document).
    const std::string src_path = has_save() ? save_path_ : default_path_;
    sf2::data::xml_doc doc;
    doc.parse(read_file_text(src_path));

    const pugi::xml_node root = doc.root().first_child();
    if (root == nullptr || std::string(root.name()) != "Root") {
        throw std::runtime_error("SaveSystem: cannot save — root element missing");
    }
    pugi::xml_node warrior = find_warrior(root);
    if (!warrior) {
        throw std::runtime_error("SaveSystem: cannot save — no <Warrior>");
    }

    warrior.attribute("Money").set_value(w.money);
    warrior.attribute("Bonus").set_value(w.bonus);
    warrior.attribute("Strength").set_value(w.strength);
    warrior.attribute("Stamina").set_value(w.stamina);
    warrior.attribute("Level").set_value(w.level);
    warrior.attribute("Experience").set_value(w.experience);
    warrior.attribute("Power").set_value(w.power);
    warrior.attribute("Armor").set_value(w.armor.c_str());
    warrior.attribute("Helm").set_value(w.helm.c_str());
    warrior.attribute("Weapon").set_value(w.weapon.c_str());
    warrior.attribute("Ranged").set_value(w.ranged.c_str());
    warrior.attribute("Magic").set_value(w.magic.c_str());
    warrior.attribute("Tutorial").set_value(w.tutorial.c_str());
    warrior.attribute("Tactic").set_value(w.tactic.c_str());
    warrior.attribute("CurrentZone").set_value(w.current_zone.c_str());
    warrior.attribute("ShowUpgrades").set_value(w.show_upgrades ? "1" : "0");

    // Bus mutes (JS `sc.Gpb` L114249): `<Sounds>/<Sound|Music>@Mute` from `ta.$D`
    // (SFX bus = `sound_muted`, `lb.Mz()`) / `ta.ZD` (music bus, `lb.Lz()`).
    if (pugi::xml_node cu = root.child("CurrentUser")) {
        pugi::xml_node snd_root = cu.child("Sounds");
        if (!snd_root) snd_root = cu.append_child("Sounds");
        pugi::xml_node snd = snd_root.child("Sound");
        if (!snd) snd = snd_root.append_child("Sound");
        snd.attribute("Mute").set_value(w.sound_muted ? "1" : "0");
        pugi::xml_node mus = snd_root.child("Music");
        if (!mus) mus = snd_root.append_child("Music");
        mus.attribute("Mute").set_value(w.music_muted ? "1" : "0");
    }

    // The owned items (JS `$g` + `Aa.save`): replace the <Items> children.
    // The template always has an <Items> element (the Warrior's equipped
    // Body/Head/Fists/NoRanged/NoMagic); append missing items, patch
    // Count/Equipped on the ones already present. Collect the children into
    // a stable vector first — pugixml's range iterator skips every other
    // node when remove_child is called mid-iteration, so a live loop leaks
    // items across saves (accumulating duplicates).
    pugi::xml_node items = warrior.child("Items");
    if (!items) {
        items = warrior.append_child("Items");
    }
    std::vector<pugi::xml_node> old_items;
    for (pugi::xml_node existing : items.children("Item")) {
        old_items.push_back(existing);
    }
    for (const pugi::xml_node& existing : old_items) {
        items.remove_child(existing);
    }
    for (const WarriorSave::OwnedItem& oi : w.items) {
        pugi::xml_node item = items.append_child("Item");
        item.append_attribute("Name").set_value(oi.name.c_str());
        item.append_attribute("Equipped").set_value(oi.equipped ? "1" : "0");
        item.append_attribute("Count").set_value(oi.count);
        // `Ce` round-trip: only materialize a non-zero level so an unupgraded
        // item row keeps the shipped shape (no spurious UpgradeLevel="0").
        if (oi.upgrade_level > 0) {
            item.append_attribute("UpgradeLevel").set_value(oi.upgrade_level);
        }
        // `j7` round-trip: only materialize a non-default AcquireType so a
        // plain owned row keeps the shipped shape (no spurious "Item").
        if (oi.acquire_type != "Item") {
            item.append_attribute("AcquireType").set_value(oi.acquire_type.c_str());
        }
        // `by` round-trip: only materialize a set (>= 0) level.
        if (oi.delivery_upgrade_level >= 0) {
            item.append_attribute("DeliveryUpgradeLevel").set_value(oi.delivery_upgrade_level);
        }
        // `<Enchantments>` (JS `xe` L692882; writer L646621 emits
        // `<Enchantments><Perk Name=".."><Set k="v"/>...</Perk>`). Materialize
        // the node only when the item carries enchantments (mirrors the lazy
        // `<Perks>`/`<PerkHistory>` rule above).
        if (!oi.enchantments.empty()) {
            pugi::xml_node enc = item.child("Enchantments");
            if (!enc) enc = item.append_child("Enchantments");
            for (const WarriorSave::ItemEnchantment& ie : oi.enchantments) {
                pugi::xml_node p = enc.append_child("Perk");
                p.append_attribute("Name").set_value(ie.name.c_str());
                // JS writer emits `<Set>` only when `ll` is non-empty.
                if (!ie.sets.empty()) {
                    pugi::xml_node s = p.append_child("Set");
                    for (const WarriorSave::ItemEnchantment::SetAttr& kv : ie.sets) {
                        s.append_attribute(kv.key.c_str()).set_value(kv.value.c_str());
                    }
                }
            }
        }
    }

    // Shop locks (`p.o.R$` -> `<Shop><Lock Name>`; `vq` L267 appends, `tnb`
    // L267 removes). Replace the `<Lock>` children; the `<Shop>` node is
    // created lazily (`vq` L267 `ga.A("Shop") ?? ga.appendChild("Shop")`).
    {
        pugi::xml_node shop = warrior.child("Shop");
        if (shop) {
            std::vector<pugi::xml_node> old;
            for (pugi::xml_node l : shop.children("Lock")) old.push_back(l);
            for (const pugi::xml_node& l : old) shop.remove_child(l);
        }
        if (!w.shop_locks.empty()) {
            if (!shop) shop = warrior.append_child("Shop");
            for (const std::string& n : w.shop_locks) {
                shop.append_child("Lock").append_attribute("Name").set_value(n.c_str());
            }
        }
    }

    // MapFocus (`ys`): get-or-append (absent in the seed).
    {
        pugi::xml_attribute mf = warrior.attribute("MapFocus");
        if (!mf) mf = warrior.append_attribute("MapFocus");
        mf.set_value(w.map_focus.c_str());
    }

    // `p.Dc` snapshot (`live_clock`): always stamp the LIVE clock so the saved
    // absolute domain tracks the tick (`game_clock` is the `p.Dc` value the
    // next boot resumes from).
    {
        pugi::xml_attribute gc = warrior.attribute("GameClock");
        if (!gc) gc = warrior.append_attribute("GameClock");
        gc.set_value(static_cast<long long>(
            std::llround(WarriorSave::live_clock())));
    }

    // Battles (`iF`): replace the <Battle Name> children.
    {
        pugi::xml_node battles = warrior.child("Battles");
        if (!battles) battles = warrior.append_child("Battles");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node b : battles.children("Battle")) old.push_back(b);
        for (const pugi::xml_node& b : old) battles.remove_child(b);
        for (const std::string& name : w.battles) {
            battles.append_child("Battle").append_attribute("Name").set_value(name.c_str());
        }
        // `hl` Locked/Hidden flags (L277-278): patch the matching row (the
        // Name list above already carries the zone-qualified "ZONE|BATTLE|").
        for (const WarriorSave::BattleRecord& r : w.battle_records) {
            const std::string raw =
                r.zone.empty() ? r.name : (r.zone + "|" + r.name + "|");
            pugi::xml_node node;
            for (pugi::xml_node b : battles.children("Battle")) {
                if (raw == b.attribute("Name").value()) {
                    node = b;
                    break;
                }
            }
            if (!node) {
                node = battles.append_child("Battle");
                node.append_attribute("Name").set_value(raw.c_str());
            }
            // `hl` flags (`CMa` L278, `gx` L278, `yla` L279): set the flag
            // when on; clear the attribute when off so a `HideBattle`
            // (`battle_remove`) / show transition is reflected exactly.
            pugi::xml_attribute la = node.attribute("Locked");
            if (r.locked) {
                if (!la) la = node.append_attribute("Locked");
                la.set_value("1");
            } else if (la) {
                node.remove_attribute("Locked");
            }
            pugi::xml_attribute ha = node.attribute("Hidden");
            if (r.hidden) {
                if (!ha) ha = node.append_attribute("Hidden");
                ha.set_value("1");
            } else if (ha) {
                node.remove_attribute("Hidden");
            }
            pugi::xml_attribute ra = node.attribute("ReplayCount");
            if (r.replay_count > 0) {
                if (!ra) ra = node.append_attribute("ReplayCount");
                ra.set_value(r.replay_count);
            } else if (ra) {
                node.remove_attribute("ReplayCount");
            }
        }
    }

    // Fights (`yc` = `il`): replace the <Fight> children. Identity = `IDS`
    // (`il.Atb` L143548); the full ctor attr set (`il` L141476) is written so
    // the record round-trips the JS shape.
    {
        pugi::xml_node fights = warrior.child("Fights");
        if (!fights) fights = warrior.append_child("Fights");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node f : fights.children("Fight")) old.push_back(f);
        for (const pugi::xml_node& f : old) fights.remove_child(f);
        for (const WarriorSave::FightWins& fw : w.fights) {
            pugi::xml_node f = fights.append_child("Fight");
            f.append_attribute("IDS").set_value(fw.name.c_str());
            f.append_attribute("CompletedCount").set_value(fw.wins);
            f.append_attribute("LossCount").set_value(fw.losses);
            f.append_attribute("EclipseCompletedCount")
                .set_value(fw.eclipse_completed);
            f.append_attribute("EclipseLossCount").set_value(fw.eclipse_loss);
            f.append_attribute("StoryCount").set_value(fw.story_count);
            f.append_attribute("CompletedTime").set_value(fw.completed_time);
            f.append_attribute("TimeLeft").set_value(fw.time_left);
            f.append_attribute("RandomizeTimeLeft")
                .set_value(fw.randomize_time_left);
            f.append_attribute("Level").set_value(fw.level);
        }
    }

    // Quests + variables (`kF`/`rv`). The quest list is the NESTED
    // `<Quests><Quests><Quest Name FileName/></Quests>` (`WO` L132600:
    // `c.A("Quests") ?? c.appendChild("Quests")` then `appendChild("Quest")`,
    // setting `Name` + `FileName`); the variables stay direct children of the
    // outer `<Quests><Variables>` (`WA` L133404).
    {
        pugi::xml_node quests = warrior.child("Quests");
        if (!quests) quests = warrior.append_child("Quests");
        pugi::xml_node quest_list = quests.child("Quests");
        if (!quest_list) quest_list = quests.append_child("Quests");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node q : quest_list.children("Quest")) old.push_back(q);
        for (const pugi::xml_node& q : old) quest_list.remove_child(q);
        for (const WarriorSave::QuestState& qs : w.quests) {
            pugi::xml_node q = quest_list.append_child("Quest");
            q.append_attribute("Name").set_value(qs.name.c_str());
            q.append_attribute("FileName").set_value(qs.file_name.c_str());
            // `fl` (L114518): the `QuestParameters` row written by `Ln`
            // `Checkpoint` (`setParameters`). Present whenever `setParameters`
            // ran (`has_parameters`), even at 0/0 — the JS ctor force-defaults
            // both indices rather than omitting the node.
            if (qs.has_parameters || qs.screen_index != 0 ||
                qs.checkpoint_index != 0) {
                pugi::xml_node qp = q.append_child("QuestParameters");
                qp.append_attribute("ScreenIndex").set_value(qs.screen_index);
                qp.append_attribute("ChekPointIndex").set_value(qs.checkpoint_index);
            }
        }
        pugi::xml_node vars = quests.child("Variables");
        if (!vars) vars = quests.append_child("Variables");
        std::vector<pugi::xml_node> old_vars;
        for (pugi::xml_node v : vars.children("Variable")) old_vars.push_back(v);
        for (const pugi::xml_node& v : old_vars) vars.remove_child(v);
        // `WA` (L133404) writes the PUBLIC name (`c.set("Name", a)`), i.e. the
        // `rv` key without its leading `_`. The parse keeps both forms, so the
        // `_`-prefixed twin is skipped when the raw key is also present.
        for (const auto& kv : w.variables) {
            const std::string& key = kv.first;
            const bool prefixed = !key.empty() && key[0] == '_';
            // `WA` (L133404) writes the PUBLIC name (the `rv` key minus its
            // leading `_`). The parse keeps both forms, so a `_`-prefixed key
            // whose raw twin is present is already emitted by the twin.
            if (prefixed && w.variables.find(key.substr(1)) != w.variables.end()) {
                continue;
            }
            pugi::xml_node v = vars.append_child("Variable");
            v.append_attribute("Name").set_value(key.c_str());
            v.append_attribute("Value").set_value(kv.second.c_str());
        }
    }

    // Currencies (`pG`): counts are ATTRIBUTES on `<Currencies>` keyed by the
    // currency name (`GLa` L137813: `this.pG.set(a, "" + b)`).
    {
        pugi::xml_node cur = warrior.child("Currencies");
        if (!cur) cur = warrior.append_child("Currencies");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node c : cur.children("Currency")) old.push_back(c);
        for (const pugi::xml_node& c : old) cur.remove_child(c);
        for (const auto& kv : w.currencies) {
            pugi::xml_attribute a = cur.attribute(kv.first.c_str());
            if (!a) a = cur.append_attribute(kv.first.c_str());
            a.set_value(kv.second);
        }
    }

    // Resistances (`Pw`): attributes on <Resistances>.
    {
        pugi::xml_node res = warrior.child("Resistances");
        if (!res) res = warrior.append_child("Resistances");
        for (const auto& kv : w.resistances) {
            pugi::xml_attribute a = res.attribute(kv.first.c_str());
            if (!a) a = res.append_attribute(kv.first.c_str());
            a.set_value(kv.second);
        }
    }

    // Delivery timers (`yl` under `<Timers>`): rewrite rows.
    {
        pugi::xml_node timers = warrior.child("Timers");
        if (!timers) timers = warrior.append_child("Timers");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node t : timers.children("Timer")) old.push_back(t);
        for (const pugi::xml_node& t : old) timers.remove_child(t);
        for (const auto& kv : w.timers) {
            pugi::xml_node t = timers.append_child("Timer");
            t.append_attribute("Name").set_value(kv.first.c_str());
            t.append_attribute("Due").set_value(kv.second);
        }
    }

    // Perk progression (`Bt.Epb` L307 appends a <Level> per perk level):
    // rewrite <PerkHistory> (create only when there is something to hold —
    // `Bt.parse` appends it lazily via `Epb`).
    {
        pugi::xml_node ph = warrior.child("PerkHistory");
        if (!ph && !w.perk_history.empty()) ph = warrior.append_child("PerkHistory");
        if (ph) {
            std::vector<pugi::xml_node> old;
            for (pugi::xml_node l : ph.children("Level")) old.push_back(l);
            for (const pugi::xml_node& l : old) ph.remove_child(l);
            for (const WarriorSave::PerkLevel& pl : w.perk_history) {
                pugi::xml_node l = ph.append_child("Level");
                l.append_attribute("Perk").set_value(pl.name.c_str());
                l.append_attribute("Value").set_value(pl.level);
            }
        }
    }

    // Perk unlock/upgrade records (JS `Bt.parse` L305 / `Ji` L284): rewrite
    // `<Perks><Perk Name Level UpgradeLevel>`. Materialize only when there
    // is something to hold (same lazy rule as `<PerkHistory>`).
    {
        pugi::xml_node pk = warrior.child("Perks");
        if (!pk && !w.perks.empty()) pk = warrior.append_child("Perks");
        if (pk) {
            std::vector<pugi::xml_node> old;
            for (pugi::xml_node p : pk.children("Perk")) old.push_back(p);
            for (const pugi::xml_node& p : old) pk.remove_child(p);
            for (const WarriorSave::PerkState& ps : w.perks) {
                pugi::xml_node p = pk.append_child("Perk");
                p.append_attribute("Name").set_value(ps.name.c_str());
                p.append_attribute("Level").set_value(ps.level);
                p.append_attribute("UpgradeLevel").set_value(ps.upgrade_level);
            }
        }
    }

    // Session settings (JS `Aka`/`xLa` L264): `Y0`/`g$a` (L271) materialize
    // their defaults on read, so the native writes both rows every save.
    {
        pugi::xml_node ss = warrior.child("SessionSettings");
        if (!ss) ss = warrior.append_child("SessionSettings");
        const auto set_val = [&ss](const char* tag, bool on) {
            pugi::xml_node n = ss.child(tag);
            if (!n) n = ss.append_child(tag);
            pugi::xml_attribute v = n.attribute("Value");
            if (!v) v = n.append_attribute("Value");
            v.set_value(on ? 1 : 0);
        };
        set_val("Disciple", w.disciple);
        set_val("ShowDojoDisciple", w.show_dojo_disciple);
    }

    // Achievement counters (`yt.parse` L294 always materializes <Counters>).
    {
        pugi::xml_node ctr = warrior.child("Counters");
        if (!ctr) ctr = warrior.append_child("Counters");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node c : ctr.children("Counter")) old.push_back(c);
        for (const pugi::xml_node& c : old) ctr.remove_child(c);
        for (const WarriorSave::AchievementCounter& ac : w.counters) {
            pugi::xml_node c = ctr.append_child("Counter");
            c.append_attribute("Name").set_value(ac.name.c_str());
            c.append_attribute("CurrentValue").set_value(ac.value);
        }
    }

    // Achievement unlocks (`yt.parse` L294 always materializes <Achievements>).
    {
        pugi::xml_node ach = warrior.child("Achievements");
        if (!ach) ach = warrior.append_child("Achievements");
        std::vector<pugi::xml_node> old;
        for (pugi::xml_node a : ach.children("Achievement")) old.push_back(a);
        for (const pugi::xml_node& a : old) ach.remove_child(a);
        for (const WarriorSave::AchievementUnlock& au : w.achievement_unlocks) {
            pugi::xml_node a = ach.append_child("Achievement");
            a.append_attribute("Name").set_value(au.name.c_str());
            a.append_attribute("ObtainedReward").set_value(au.obtained_reward ? "true" : "false");
        }
    }

    // "New move" tricks (JS `Bt.Nua` L268 appends `<OpenTricks><Trick
    // Name>`; `Bt.inb` L269 removes one row per cleared name). Materialize
    // only when something is flagged, mirroring `es.zha` (L2239) which
    // leaves an empty `<OpenTricks>` behind only if it already existed.
    {
        pugi::xml_node ot = warrior.child("OpenTricks");
        if (!ot && !w.open_tricks.empty()) ot = warrior.append_child("OpenTricks");
        if (ot) {
            std::vector<pugi::xml_node> old;
            for (pugi::xml_node t : ot.children("Trick")) old.push_back(t);
            for (const pugi::xml_node& t : old) ot.remove_child(t);
            for (const std::string& name : w.open_tricks) {
                ot.append_child("Trick").append_attribute("Name").set_value(name.c_str());
            }
        }
    }

    // "New item" counters (JS `Bt.bM` L269: drop the existing
    // `<CounterItems>`, then re-create `<CounterItems><Items><Item Name>`
    // only when at least one def carries `yj`).
    {
        pugi::xml_node ci = warrior.child("CounterItems");
        if (ci) warrior.remove_child(ci);
        if (!w.counter_items.empty()) {
            pugi::xml_node items_node = warrior.append_child("CounterItems").append_child("Items");
            for (const std::string& name : w.counter_items) {
                items_node.append_child("Item").append_attribute("Name").set_value(name.c_str());
            }
        }
    }

    std::ostringstream oss;
    doc.save(oss, "\t", pugi::format_default, pugi::encoding_auto);
    write_file_text(save_path_, oss.str());
}

std::string SaveSystem::envelope_decode(const std::string& envelope_text) {
    // `Aa.load` (L70-71) shape, extended: strip whitespace; a leading
    // `SF2` selects the framed form (`Ddb`); otherwise the legacy
    // whole-blob form (base64 -> un-zstd -> XML).
    std::string b64;
    for (char c : envelope_text) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') b64.push_back(c);
    }
    if (b64.size() >= 3 && b64[0] == 'S' && b64[1] == 'F' && b64[2] == '2') {
        return sf2::data::envelope_decode_users(
            sf2::data::base64_decode(b64.substr(3)));
    }
    const std::vector<std::uint8_t> compressed = sf2::data::base64_decode(b64);
    const std::vector<std::uint8_t> xml =
        sf2::data::zstd_decompress(compressed.data(), compressed.size());
    return std::string(xml.begin(), xml.end());
}

std::string SaveSystem::export_sf2(const std::string& users_xml,
                                   const std::string& packs_xml, bool h1, bool vf) {
    return sf2::data::envelope_export(users_xml, packs_xml, h1, vf);
}

} // namespace sf2::app
