#pragma once

#include "app/settings.h"
#include "catalog/catalog.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace etal {
  struct InstalledApp {
    std::string id;
    std::string ref;
    std::string remote;
    InstallationKind installation = InstallationKind::User;
    std::string version;
    std::uint64_t installedSize = 0;
  };

  // The enabled, listed remotes of the user and the default system installation. Each call opens
  // the installations anew, so these may run on any thread; they all block.
  struct RemoteScan {
    std::vector<CatalogSource> sources;
    // Remotes whose appstream data has never been downloaded.
    std::vector<CatalogSource> missing;
    std::vector<std::string> problems;
  };
  RemoteScan scanRemotes();

  // Downloads the appstream data of one remote. The system installation asks polkit.
  std::expected<void, std::string> refreshAppstream(const CatalogSource& source);

  std::vector<InstalledApp> installedApps();
} // namespace etal
