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
    explicit Catalog(std::vector<AppEntry> entries);

    // Reads the sources with libappstream. Blocks for a while on the first run, when there is no
    // cache yet: call it off the main thread.
    static std::expected<Catalog, std::string> load(const std::vector<CatalogSource>& sources);

    const std::vector<AppEntry>& entries() const noexcept { return m_entries; }
    bool empty() const noexcept { return m_entries.empty(); }
    // The entries of this app id, user installation first.
    std::vector<const AppEntry*> find(std::string_view id) const;

  private:
    std::vector<AppEntry> m_entries;
    std::unordered_multimap<std::string, std::size_t> m_byId;
  };
} // namespace etal
