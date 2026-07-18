#include "engine/format/json_atlas.hpp"
#include <iostream>
#include <fstream>

int main() {
    std::ifstream f("E:/reSF2/sf2_pc/www/res/locations/dojo/dojo.d31b1e71.json");
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    
    std::cout << "JSON length: " << json.size() << std::endl;
    std::cout << "First 500 chars:" << std::endl;
    std::cout << json.substr(0, 500) << std::endl;
    
    auto result = resf2::format::parse_json_atlas(json);
    if (result) {
        std::cout << "SUCCESS: " << result->frames.size() << " frames" << std::endl;
        std::cout << "Meta: " << result->meta.image << " " << result->meta.w << "x" << result->meta.h << std::endl;
        for (size_t i = 0; i < std::min<size_t>(5, result->frames.size()); i++) {
            std::cout << "  Frame " << i << ": " << result->frames[i].filename 
                      << " (" << result->frames[i].x << "," << result->frames[i].y 
                      << " " << result->frames[i].w << "x" << result->frames[i].h << ")" << std::endl;
        }
    } else {
        std::cout << "FAILED: " << resf2::format::to_string(result.error()) << std::endl;
    }
    return 0;
}