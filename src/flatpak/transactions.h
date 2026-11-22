#pragma once

#include "app/settings.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

typedef struct _GCancellable GCancellable;

namespace etal {
  enum class JobKind : std::uint8_t {
    Install,
    Uninstall,
    // Updates `refs`, or everything that has an update when `refs` is empty.
    Update,
    // A .flatpakref or .flatpak bundle in `file`.
    InstallFile,
    // A .flatpakrepo in `file`, added as `remote`.
    AddRemote,
  };

  struct JobRequest {
    JobKind kind = JobKind::Install;
    InstallationKind installation = InstallationKind::User;
    std::string remote;
    std::vector<std::string> refs;
    // For the list of jobs and for deleting the data of an uninstalled app.
    std::string appId;
    std::string title;
    bool deleteData = false;
    std::filesystem::path file;
  };

  struct JobProgress {
    // 0 to 1 over the whole transaction, related refs and runtimes included.
    double fraction = 0.0;
    std::string status;
  };
  using ProgressFn = std::function<void(const JobProgress&)>;

  // Runs one transaction with libflatpak. Blocks; `progress` is called on the calling thread. The
  // system installation asks polkit for permission.
  std::expected<void, std::string> runJob(const JobRequest& request, const ProgressFn& progress, GCancellable* cancellable);

  // Starts an installed app.
  std::expected<void, std::string> launchApp(InstallationKind installation, const std::string& ref);

  // Something installed that has an update.
  struct UpdateItem {
    std::string id;
    std::string ref;
    std::string remote;
    InstallationKind installation = InstallationKind::User;
    bool runtime = false;
    // The ref itself is current; only parts that belong to it, such as codecs or locales, are missing.
    bool partsOnly = false;
    // Of the ref itself; 0 when only parts are missing, whose size the remote does not tell here.
    std::uint64_t downloadSize = 0;
    // Permissions the new version asks for that the installed one does not have.
    std::vector<std::string> newPermissions;
  };
  struct UpdateScan {
    std::vector<UpdateItem> items;
    std::vector<std::string> problems;
  };
  // Asks the remotes of both installations; blocks.
  UpdateScan listUpdates();
} // namespace etal
