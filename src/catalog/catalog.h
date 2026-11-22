#pragma once

#include "catalog/app_entry.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace etal {
  // The appstream data one remote of one installation keeps on disk: the folder with
  // appstream.xml(.gz) and icons/.
  struct CatalogSource {
    std::string remote;
    InstallationKind installation = InstallationKind::User;
    std::filesystem::path directory;
    bool operator==(const CatalogSource&) const = default;
  };

  // Every application the sources offer, read once; entries keep their order of reading.
  class Catalog {
  public:
    Catalog() = default;
    explicit Catalog(std::vector<AppEntry> entries, std::vector<AppEntry> addons = {});

    // Reads the sources with libappstream. Blocks for a while on the first run, when there is no
    // cache yet: call it off the main thread.
    static std::expected<Catalog, std::string> load(const std::vector<CatalogSource>& sources);

    const std::vector<AppEntry>& entries() const noexcept { return m_entries; }
    bool empty() const noexcept { return m_entries.empty(); }
    // The entries of this app id, user installation first.
    std::vector<const AppEntry*> find(std::string_view id) const;
    // The add-ons the same remote of the same installation offers for this app, by name.
    std::vector<const AppEntry*> addonsOf(const AppEntry& app) const;
    // Other apps of the app's developer, one entry per app, by name; at most `limit`.
    std::vector<const AppEntry*> byDeveloper(const AppEntry& app, std::size_t limit) const;

  private:
    std::vector<AppEntry> m_entries;
    std::vector<AppEntry> m_addons;
    std::unordered_multimap<std::string, std::size_t> m_addonsByApp;
    std::unordered_multimap<std::string, std::size_t> m_byId;
  };
} // namespace etal
