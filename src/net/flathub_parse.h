#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace etal {
  struct FlathubStats {
    std::uint64_t installsTotal = 0;
    std::uint64_t installsLastMonth = 0;
  };

  // The app ids of a /collection/... answer, in its order. Empty for anything else.
  std::vector<std::string> parseCollection(std::string_view json);
  // The app id of /app-picks/app-of-the-day/<date>.
  std::string parseAppOfTheDay(std::string_view json);
  // /stats/<id>.
  std::optional<FlathubStats> parseStats(std::string_view json);
} // namespace etal
