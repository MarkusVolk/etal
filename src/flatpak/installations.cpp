#include "flatpak/installations.h"

#include <algorithm>
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

std::expected<etal::RefInfo, std::string>
etal::fetchRefInfo(InstallationKind installation, const std::string& remote, const std::string& ref) {
  GError* error = nullptr;
  FlatpakRef* parsed = flatpak_ref_parse(ref.c_str(), &error);
  FlatpakInstallation* handle = nullptr;
  if (parsed) {
    handle = installation == InstallationKind::User ? flatpak_installation_new_user(nullptr, &error)
                                                    : flatpak_installation_new_system(nullptr, &error);
  }
  FlatpakRemoteRef* remoteRef = nullptr;
  if (handle) {
    flatpak_installation_set_no_interaction(handle, TRUE);
    remoteRef = flatpak_installation_fetch_remote_ref_sync(
        handle, remote.c_str(), flatpak_ref_get_kind(parsed), flatpak_ref_get_name(parsed), flatpak_ref_get_arch(parsed),
        flatpak_ref_get_branch(parsed), nullptr, &error
    );
  }
  std::expected<RefInfo, std::string> result = std::unexpected(std::format("{}: {}", ref, error ? error->message : "not found"));
  if (remoteRef) {
    RefInfo info;
    info.downloadSize = flatpak_remote_ref_get_download_size(remoteRef);
    info.installedSize = flatpak_remote_ref_get_installed_size(remoteRef);
    if (GBytes* bytes = flatpak_remote_ref_get_metadata(remoteRef)) {
      gsize size = 0;
      const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
      info.metadata.assign(data ? data : "", size);
    }
    info.endOfLife = str(flatpak_remote_ref_get_eol(remoteRef));
    info.endOfLifeRebase = str(flatpak_remote_ref_get_eol_rebase(remoteRef));
    result = std::move(info);
    g_object_unref(remoteRef);
  }
  g_clear_error(&error);
  if (handle) {
    g_object_unref(handle);
  }
  if (parsed) {
    g_object_unref(parsed);
  }
  return result;
}

std::string etal::installedRefKey(InstallationKind installation, std::string_view ref) {
  return std::format("{}:{}", installation == InstallationKind::User ? 'u' : 's', ref);
}

std::unordered_set<std::string> etal::installedRefKeys() {
  std::unordered_set<std::string> out;
  auto installations = openInstallations(nullptr);
  for (const auto& installation : installations) {
    GPtrArray* refs = flatpak_installation_list_installed_refs(installation.handle, nullptr, nullptr);
    for (guint i = 0; refs && i < refs->len; ++i) {
      char* formatted = flatpak_ref_format_ref(FLATPAK_REF(g_ptr_array_index(refs, i)));
      out.insert(installedRefKey(installation.kind, formatted));
      g_free(formatted);
    }
    if (refs) {
      g_ptr_array_unref(refs);
    }
  }
  closeInstallations(installations);
  return out;
}

namespace {
  std::filesystem::path appDataRoot() { return std::filesystem::path(g_get_home_dir()) / ".var" / "app"; }

  std::uint64_t directorySize(const std::filesystem::path& directory) {
    std::uint64_t total = 0;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied, error
         );
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
      std::error_code ignored;
      if (it->is_regular_file(ignored) && !it->is_symlink(ignored)) {
        total += it->file_size(ignored);
      }
    }
    return total;
  }
} // namespace

std::vector<etal::LeftoverData> etal::leftoverData(const std::vector<InstalledApp>& installed) {
  std::unordered_set<std::string> owned;
  for (const auto& app : installed) {
    owned.insert(app.id);
  }
  std::vector<LeftoverData> out;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(appDataRoot(), error)) {
    const std::string id = entry.path().filename().string();
    if (entry.is_directory(error) && !owned.contains(id)) {
      out.push_back({id, directorySize(entry.path())});
    }
  }
  std::ranges::sort(out, {}, &LeftoverData::id);
  return out;
}

void etal::deleteLeftoverData(const std::string& id) {
  if (id.empty() || id.find('/') != std::string::npos || id.starts_with('.')) {
    return;
  }
  std::error_code error;
  std::filesystem::remove_all(appDataRoot() / id, error);
}

std::vector<etal::RemoteInfo> etal::listRemotes() {
  std::vector<RemoteInfo> out;
  auto installations = openInstallations(nullptr);
  for (const auto& installation : installations) {
    GPtrArray* remotes = flatpak_installation_list_remotes(installation.handle, nullptr, nullptr);
    for (guint i = 0; remotes && i < remotes->len; ++i) {
      auto* remote = static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, i));
      char* title = flatpak_remote_get_title(remote);
      char* url = flatpak_remote_get_url(remote);
      out.push_back({
          .name = str(flatpak_remote_get_name(remote)),
          .title = str(title),
          .url = str(url),
          .installation = installation.kind,
          .disabled = flatpak_remote_get_disabled(remote) != FALSE,
      });
      g_free(title);
      g_free(url);
    }
    if (remotes) {
      g_ptr_array_unref(remotes);
    }
  }
  closeInstallations(installations);
  return out;
}

std::vector<etal::UnusedRef> etal::listUnusedRefs() {
  std::vector<UnusedRef> out;
  auto installations = openInstallations(nullptr);
  for (const auto& installation : installations) {
    GPtrArray* refs = flatpak_installation_list_unused_refs(installation.handle, nullptr, nullptr, nullptr);
    for (guint i = 0; refs && i < refs->len; ++i) {
      auto* ref = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));
      char* formatted = flatpak_ref_format_ref(FLATPAK_REF(ref));
      out.push_back({str(formatted), installation.kind, flatpak_installed_ref_get_installed_size(ref)});
      g_free(formatted);
    }
    if (refs) {
      g_ptr_array_unref(refs);
    }
  }
  closeInstallations(installations);
  return out;
}
