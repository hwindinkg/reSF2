#pragma once

#include <string>

namespace resf2::game {

struct UserSettingsLanguage {
    std::string value;
    int number = -1;
};

UserSettingsLanguage load_user_settings_language(const std::string& asset_root);
std::string normalize_localization_path(const std::string& value);

} // namespace resf2::game
