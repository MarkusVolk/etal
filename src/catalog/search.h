#pragma once

#include "catalog/catalog.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace etal {
  struct SearchFilter {
    bool onlyVerified = false;
    bool onlyFree = false;
    // A freedesktop main category ("Game", "Graphics", ...); empty for all.
    std::string category;
  };

  // Indices into catalog.entries(), one per app id (the user installation's entry first), best
  // match first. Every word of the query must match the name, id, keywords, summary or developer;
  // a match in the name counts most. An empty query yields every app, sorted by name.
  std::vector<std::size_t> search(const Catalog& catalog, std::string_view query, const SearchFilter& filter);

  // Lower case for matching.
  std::string fold(std::string_view text);
} // namespace etal
