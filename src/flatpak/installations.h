#pragma once

#include "app/settings.h"
#include "catalog/catalog.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
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

  struct RemoteInfo {
    std::string name;
    std::string title;
    std::string url;
    InstallationKind installation = InstallationKind::User;
    bool disabled = false;
  };
  // Every remote of both installations, disabled ones too.
  std::vector<RemoteInfo> listRemotes();

  // A runtime or extension that nothing installed needs any more.
  struct UnusedRef {
    std::string ref;
    InstallationKind installation = InstallationKind::User;
    std::uint64_t installedSize = 0;
  };
  std::vector<UnusedRef> listUnusedRefs();

  // Downloads the appstream data of one remote. The system installation asks polkit.
  std::expected<void, std::string> refreshAppstream(const CatalogSource& source);

  // Tells when something was installed, updated or removed by another program, such as the
  // flatpak command. Create it on the main thread; `changed` runs there, once per burst.
  class InstallationWatch {
  public:
    explicit InstallationWatch(std::function<void()> changed);
    ~InstallationWatch();
    InstallationWatch(const InstallationWatch&) = delete;
    InstallationWatch& operator=(const InstallationWatch&) = delete;

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };

  std::vector<InstalledApp> installedApps();
  // Every installed ref, apps, runtimes and extensions, as installedRefKey().
  std::unordered_set<std::string> installedRefKeys();
  std::string installedRefKey(InstallationKind installation, std::string_view ref);

  // A folder in ~/.var/app that no installed app owns any more.
  struct LeftoverData {
    std::string id;
    std::uint64_t size = 0;
  };
  // Blocks while it adds up the sizes.
  std::vector<LeftoverData> leftoverData(const std::vector<InstalledApp>& installed);
  void deleteLeftoverData(const std::string& id);

  // What a remote says about one ref, for the details page: sizes, the metadata file with the
  // permissions, and an end-of-life notice. Uses the remote's cached summary, so it is quick
  // after the first call; blocks.
  struct RefInfo {
    std::uint64_t downloadSize = 0;
    std::uint64_t installedSize = 0;
    std::string metadata;
    std::string endOfLife;
    std::string endOfLifeRebase;
  };
  std::expected<RefInfo, std::string> fetchRefInfo(InstallationKind installation, const std::string& remote, const std::string& ref);
} // namespace etal
