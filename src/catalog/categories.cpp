#include "catalog/categories.h"

#include <array>

std::span<const etal::Category> etal::mainCategories() {
  static constexpr std::array<Category, 10> kCategories{{
      {"AudioVideo", "Audio & Video", "movie"},
      {"Development", "Development", "code"},
      {"Education", "Education", "school"},
      {"Game", "Games", "device-gamepad-2"},
      {"Graphics", "Graphics & Photography", "photo"},
      {"Network", "Networking", "world"},
      {"Office", "Productivity", "briefcase"},
      {"Science", "Science", "flask"},
      {"System", "System", "settings"},
      {"Utility", "Utilities", "tool"},
  }};
  return kCategories;
}
