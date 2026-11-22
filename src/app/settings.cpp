#include "app/settings.h"

#include "fs/xdg_dirs.h"

#include <format>
#include <glib.h>
#include <sstream>
#include <toml++/toml.hpp>

etal::SettingsStore::SettingsStore() {
  try {
    m_directory = configHome() / "etal";
  } catch (const std::exception& error) {
    m_issues.emplace_back(error.what());
    return;
  }
  read(m_directory / "config.toml");
  read(m_directory / "settings.toml");
}

void etal::SettingsStore::read(const std::filesystem::path& path) {
  std::error_code missing;
  if (!std::filesystem::exists(path, missing)) {
    return;
  }
  toml::table table;
  try {
    table = toml::parse_file(path.string());
  } catch (const toml::parse_error& error) {
    m_issues.push_back(std::format("{}:{}: {}", path.string(), error.source().begin.line, std::string_view(error.description())));
    return;
  }
  const auto flag = [](const toml::node_view<toml::node>& section, const char* key, bool& target) {
    if (const auto value = section[key].value<bool>()) {
      target = *value;
    }
  };
  const auto store = table["store"];
  if (const auto value = store["installation"].value<std::string>()) {
    if (*value == "user") {
      m_settings.installation = InstallationKind::User;
    } else if (*value == "system") {
      m_settings.installation = InstallationKind::System;
    } else {
      m_issues.push_back(std::format("{}: installation is \"user\" or \"system\", not \"{}\"", path.string(), *value));
    }
  }
  flag(store, "only_verified", m_settings.onlyVerified);
  flag(store, "only_free", m_settings.onlyFree);
  const auto updates = table["updates"];
  flag(updates, "automatic", m_settings.automaticUpdates);
  flag(updates, "notify", m_settings.updateNotifications);
}

void etal::SettingsStore::save(const Settings& settings) {
  m_settings = settings;
  if (m_directory.empty()) {
    return;
  }
  toml::table table{
      {"store",
       toml::table{
           {"installation", settings.installation == InstallationKind::System ? "system" : "user"},
           {"only_verified", settings.onlyVerified},
           {"only_free", settings.onlyFree},
       }},
      {"updates",
       toml::table{
           {"automatic", settings.automaticUpdates},
           {"notify", settings.updateNotifications},
       }},
  };
  std::ostringstream text;
  text << "# Written by Etal. Your own defaults belong in config.toml next to this file.\n" << table << "\n";
  std::error_code error;
  std::filesystem::create_directories(m_directory, error);
  g_file_set_contents((m_directory / "settings.toml").c_str(), text.str().c_str(), -1, nullptr);
}
