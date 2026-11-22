#pragma once

#include <span>
#include <string_view>

namespace etal {
  struct Category {
    // The freedesktop main category the catalog uses.
    std::string_view id;
    std::string_view title;
    // A Tabler glyph name.
    std::string_view glyph;
  };

  // The main categories of the freedesktop menu specification, in the order the sidebar shows them.
  std::span<const Category> mainCategories();
} // namespace etal
