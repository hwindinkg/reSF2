#include "../engine/game/asset_manager.hpp"
#include <cstdio>
int main(){
    resf2::game::AssetManager a; a.load_moves("assets");
    for (const char* n : {"WallJump_50","HighPunch"}) {
        auto it=a.moves().find(n);
        if(it==a.moves().end()){ std::printf("%s: MISSING\n",n); continue; }
        std::printf("%s: keys=%zu press=%zu hold=%d\n",n,
            it->second.key_types.size(),it->second.key_press_types.size(),
            (int)it->second.needs_hold);
        for(size_t i=0;i<it->second.key_types.size();++i)
            std::printf("   %s / %s\n",it->second.key_types[i].c_str(),
                it->second.key_press_types[i].c_str());
    }
}
