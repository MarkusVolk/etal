#include "net/flathub_parse.h"

#include <nlohmann/json.hpp>

namespace {
  nlohmann::json parse(std::string_view text) { return nlohmann::json::parse(text, nullptr, false); }
} // namespace

std::vector<std::string> etal::parseCollection(std::string_view json) {
  std::vector<std::string> ids;
  const auto document = parse(json);
  if (!document.is_object()) {
    return ids;
  }
  const auto hits = document.find("hits");
  if (hits == document.end() || !hits->is_array()) {
    return ids;
  }
  for (const auto& hit : *hits) {
    if (const auto id = hit.find("app_id"); hit.is_object() && id != hit.end() && id->is_string()) {
      ids.push_back(id->get<std::string>());
    }
  }
  return ids;
}

std::string etal::parseAppOfTheDay(std::string_view json) {
  const auto document = parse(json);
  if (!document.is_object()) {
    return {};
  }
  const auto id = document.find("app_id");
  return id != document.end() && id->is_string() ? id->get<std::string>() : std::string();
}

std::optional<etal::FlathubStats> etal::parseStats(std::string_view json) {
  const auto document = parse(json);
  if (!document.is_object()) {
    return std::nullopt;
  }
  const auto number = [&document](const char* key) -> std::optional<std::uint64_t> {
    const auto value = document.find(key);
    if (value == document.end() || !value->is_number_unsigned()) {
      return std::nullopt;
    }
    return value->get<std::uint64_t>();
  };
  const auto total = number("installs_total");
  const auto month = number("installs_last_month");
  if (!total && !month) {
    return std::nullopt;
  }
  return FlathubStats{.installsTotal = total.value_or(0), .installsLastMonth = month.value_or(0)};
}
