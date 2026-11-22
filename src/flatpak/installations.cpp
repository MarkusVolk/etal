#include "flatpak/installations.h"

#include <filesystem>
#include <flatpak.h>
#include <format>

namespace {
  struct Installation {
    FlatpakInstallation* handle = nullptr;
    etal::InstallationKind kind = etal::InstallationKind::User;
  };

  // The user installation and the default system one, whichever can be opened.
  std::vector<Installation> openInstallations(std::vector<std::string>* problems) {
    std::vector<Installation> out;
    for (const auto kind : {etal::InstallationKind::User, etal::InstallationKind::System}) {
      GError* error = nullptr;
      FlatpakInstallation* handle = kind == etal::InstallationKind::User ? flatpak_installation_new_user(nullptr, &error)
                                                                          : flatpak_installation_new_system(nullptr, &error);
      if (handle) {
        flatpak_installation_set_no_interaction(handle, TRUE);
        out.push_back({handle, kind});
      } else if (problems) {
        problems->push_back(std::format(
            "{} installation: {}", kind == etal::InstallationKind::User ? "user" : "system",
            error ? error->message : "cannot be opened"
        ));
      }
      g_clear_error(&error);
    }
    return out;
  }

  void closeInstallations(std::vector<Installation>& installations) {
    for (auto& installation : installations) {
      g_object_unref(installation.handle);
    }
    installations.clear();
  }

  std::string str(const char* text) { return text ? std::string(text) : std::string(); }
} // namespace

etal::RemoteScan etal::scanRemotes() {
  RemoteScan scan;
  auto installations = openInstallations(&scan.problems);
  for (const auto& installation : installations) {
    GError* error = nullptr;
    GPtrArray* remotes = flatpak_installation_list_remotes(installation.handle, nullptr, &error);
    if (!remotes) {
      scan.problems.push_back(error ? error->message : "cannot list the remotes");
      g_clear_error(&error);
      continue;
    }
    for (guint i = 0; i < remotes->len; ++i) {
      auto* remote = static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, i));
      if (flatpak_remote_get_disabled(remote) || flatpak_remote_get_noenumerate(remote)) {
        continue;
      }
      GFile* dir = flatpak_remote_get_appstream_dir(remote, nullptr);
      char* path = dir ? g_file_get_path(dir) : nullptr;
      CatalogSource source{
          .remote = str(flatpak_remote_get_name(remote)),
          .installation = installation.kind,
          .directory = str(path),
      };
      g_free(path);
      if (dir) {
        g_object_unref(dir);
      }
      std::error_code missing;
      const bool present = std::filesystem::exists(source.directory / "appstream.xml.gz", missing)
                        || std::filesystem::exists(source.directory / "appstream.xml", missing);
      (present ? scan.sources : scan.missing).push_back(std::move(source));
    }
    g_ptr_array_unref(remotes);
  }
  closeInstallations(installations);
  return scan;
}

std::expected<void, std::string> etal::refreshAppstream(const CatalogSource& source) {
  GError* error = nullptr;
  FlatpakInstallation* handle = source.installation == InstallationKind::User
                                  ? flatpak_installation_new_user(nullptr, &error)
                                  : flatpak_installation_new_system(nullptr, &error);
  if (!handle) {
    std::string message = error ? error->message : "cannot open the installation";
    g_clear_error(&error);
    return std::unexpected(std::move(message));
  }
  const bool done = flatpak_installation_update_appstream_full_sync(
      handle, source.remote.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, &error
  );
  g_object_unref(handle);
  if (!done) {
    std::string message = std::format("{}: {}", source.remote, error ? error->message : "cannot be downloaded");
    g_clear_error(&error);
    return std::unexpected(std::move(message));
  }
  return {};
}

std::vector<etal::InstalledApp> etal::installedApps() {
  std::vector<InstalledApp> out;
  auto installations = openInstallations(nullptr);
  for (const auto& installation : installations) {
    GPtrArray* refs = flatpak_installation_list_installed_refs_by_kind(installation.handle, FLATPAK_REF_KIND_APP, nullptr, nullptr);
    for (guint i = 0; refs && i < refs->len; ++i) {
      auto* ref = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));
      char* formatted = flatpak_ref_format_ref(FLATPAK_REF(ref));
      out.push_back({
          .id = str(flatpak_ref_get_name(FLATPAK_REF(ref))),
          .ref = str(formatted),
          .remote = str(flatpak_installed_ref_get_origin(ref)),
          .installation = installation.kind,
          .version = str(flatpak_installed_ref_get_appdata_version(ref)),
          .installedSize = flatpak_installed_ref_get_installed_size(ref),
      });
      g_free(formatted);
    }
    if (refs) {
      g_ptr_array_unref(refs);
    }
  }
  closeInstallations(installations);
  return out;
}
