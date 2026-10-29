// [dev panel] Global tester flags (key `5`). One instance per process.
//
// These are NOT gameplay state: they are forced outcomes the tester toggles
// from the cheat panel (`dev_panel_*` in screens.cpp). The fight reads them at
// the shock decision so "100% shock" is testable without editing data.
#pragma once

namespace sf2::app {

struct DevFlags {
    bool shock_player = false;  // every enemy hit shocks the PLAYER
    bool shock_enemy = false;   // every player hit shocks the ENEMY
};

inline DevFlags& dev_flags() {
    static DevFlags f;
    return f;
}

}  // namespace sf2::app
