#include "flatpak/transactions.h"

#include "catalog/safety.h"
#include "flatpak/installations.h"

#include <algorithm>
#include <chrono>
#include <flatpak.h>
#include <format>
#include <set>

namespace {
  FlatpakInstallation* openInstallation(etal::InstallationKind kind, GError** error) {
    return kind == etal::InstallationKind::User ? flatpak_installation_new_user(nullptr, error)
                                                : flatpak_installation_new_system(nullptr, error);
  }

  std::string str(const char* text) { return text ? std::string(text) : std::string(); }

  std::string refName(const char* ref) {
    const std::string full = str(ref);
    const auto first = full.find('/');
    const auto second = full.find('/', first + 1);
    return first == std::string::npos ? full : full.substr(first + 1, second - first - 1);
  }

  const char* verb(FlatpakTransactionOperationType type) {
    switch (type) {
    case FLATPAK_TRANSACTION_OPERATION_INSTALL:
    case FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE:
      return "Installing";
    case FLATPAK_TRANSACTION_OPERATION_UPDATE:
      return "Updating";
    case FLATPAK_TRANSACTION_OPERATION_UNINSTALL:
      return "Removing";
    default:
      return "Working on";
    }
  }

  // The state shared by the transaction's signal handlers.
  struct Run {
    const etal::ProgressFn* progress = nullptr;
    FlatpakTransaction* transaction = nullptr;
    guint operations = 1;
    guint started = 0;
    std::string current;
    std::chrono::steady_clock::time_point lastReport{};
    std::string firstError;
  };

  void report(Run& run, double withinOperation, bool force) {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - run.lastReport < std::chrono::milliseconds(100)) {
      return;
    }
    run.lastReport = now;
    const double done = run.started > 0 ? run.started - 1 : 0;
    const double fraction = std::clamp((done + withinOperation) / std::max(1U, run.operations), 0.0, 1.0);
    (*run.progress)({fraction, run.current});
  }

  gboolean onReady(FlatpakTransaction* transaction, gpointer data) {
    auto& run = *static_cast<Run*>(data);
    GList* operations = flatpak_transaction_get_operations(transaction);
    run.operations = std::max(1U, g_list_length(operations));
    g_list_free_full(operations, g_object_unref);
    return TRUE;
  }

  void onProgressChanged(FlatpakTransactionProgress* progress, gpointer data) {
    auto& run = *static_cast<Run*>(data);
    report(run, flatpak_transaction_progress_get_progress(progress) / 100.0, false);
  }

  void onNewOperation(FlatpakTransaction*, FlatpakTransactionOperation* operation, FlatpakTransactionProgress* progress, gpointer data) {
    auto& run = *static_cast<Run*>(data);
    ++run.started;
    run.current = std::format(
        "{} {}", verb(flatpak_transaction_operation_get_operation_type(operation)),
        refName(flatpak_transaction_operation_get_ref(operation))
    );
    flatpak_transaction_progress_set_update_frequency(progress, 150);
    g_signal_connect(progress, "changed", G_CALLBACK(onProgressChanged), data);
    report(run, 0.0, true);
  }

  gboolean onOperationError(
      FlatpakTransaction*, FlatpakTransactionOperation* operation, const GError* error, FlatpakTransactionErrorDetails detail, gpointer data
  ) {
    auto& run = *static_cast<Run*>(data);
    // A ref that is already installed or has nothing to update is no failure.
    if (g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_SKIPPED) || g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED)) {
      return TRUE;
    }
    if (run.firstError.empty()) {
      run.firstError = std::format("{}: {}", refName(flatpak_transaction_operation_get_ref(operation)), error->message);
    }
    // A related ref such as a locale or debug extension may fail without failing the app.
    return (detail & FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL) != 0;
  }

  gboolean onAddNewRemote(FlatpakTransaction*, FlatpakTransactionRemoteReason, const char*, const char*, const char*, gpointer) {
    // Asked for while installing a .flatpakref the user opened: its runtime comes from that remote.
    return TRUE;
  }

  gboolean onEndOfLifedWithRebase(
      FlatpakTransaction* transaction, const char* remote, const char* ref, const char*, const char* rebasedTo, const char** previousIds,
      gpointer
  ) {
    if (!rebasedTo || !remote) {
      return FALSE;
    }
    GError* error = nullptr;
    const gboolean added = flatpak_transaction_add_rebase_and_uninstall(transaction, remote, rebasedTo, ref, nullptr, previousIds, &error);
    g_clear_error(&error);
    return added;
  }

  // The refs of `kind` (should-delete or should-download) that flatpak relates to an installed ref.
  std::set<std::string> relatedRefs(FlatpakInstallation* installation, FlatpakInstalledRef* ref, bool toDelete) {
    std::set<std::string> out;
    char* formatted = flatpak_ref_format_ref(FLATPAK_REF(ref));
    GPtrArray* related = flatpak_installation_list_installed_related_refs_sync(
        installation, flatpak_installed_ref_get_origin(ref), formatted, nullptr, nullptr
    );
    for (guint i = 0; related && i < related->len; ++i) {
      auto* part = static_cast<FlatpakRelatedRef*>(g_ptr_array_index(related, i));
      if (toDelete ? flatpak_related_ref_should_delete(part) : flatpak_related_ref_should_download(part)) {
        char* name = flatpak_ref_format_ref(FLATPAK_REF(part));
        out.insert(name);
        g_free(name);
      }
    }
    if (related) {
      g_ptr_array_unref(related);
    }
    g_free(formatted);
    return out;
  }

  // What goes with `refs` when they are uninstalled: their locales, debug files and extensions,
  // but none that something staying installed would fetch again. Flatpak itself deletes an
  // extension marked autodelete with the first runtime that goes, though another shares it: the
  // extra codecs of the freedesktop runtime went with every GNOME or KDE runtime, and came back
  // with the next update.
  std::vector<std::string> partsToRemove(FlatpakInstallation* installation, const std::vector<std::string>& refs) {
    const std::set<std::string> removing(refs.begin(), refs.end());
    std::set<std::string> candidates, wanted;
    GPtrArray* installed = flatpak_installation_list_installed_refs(installation, nullptr, nullptr);
    for (guint i = 0; installed && i < installed->len; ++i) {
      auto* ref = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(installed, i));
      char* formatted = flatpak_ref_format_ref(FLATPAK_REF(ref));
      const bool goes = removing.contains(formatted);
      g_free(formatted);
      auto parts = relatedRefs(installation, ref, goes);
      (goes ? candidates : wanted).merge(parts);
    }
    if (installed) {
      g_ptr_array_unref(installed);
    }
    std::vector<std::string> out;
    for (const auto& part : candidates) {
      if (!wanted.contains(part) && !removing.contains(part)) {
        out.push_back(part);
      }
    }
    return out;
  }

  std::expected<void, std::string> addOperations(FlatpakInstallation* installation, FlatpakTransaction* transaction, const etal::JobRequest& request) {
    GError* error = nullptr;
    bool ok = true;
    switch (request.kind) {
    case etal::JobKind::Install:
      for (const auto& ref : request.refs) {
        ok = ok && flatpak_transaction_add_install(transaction, request.remote.c_str(), ref.c_str(), nullptr, &error);
      }
      break;
    case etal::JobKind::Uninstall:
      flatpak_transaction_set_disable_related(transaction, TRUE);
      for (const auto& ref : request.refs) {
        ok = ok && flatpak_transaction_add_uninstall(transaction, ref.c_str(), &error);
      }
      for (const auto& part : partsToRemove(installation, request.refs)) {
        ok = ok && flatpak_transaction_add_uninstall(transaction, part.c_str(), &error);
      }
      break;
    case etal::JobKind::Update: {
      std::vector<std::string> refs = request.refs;
      if (refs.empty()) {
        GPtrArray* updates = flatpak_installation_list_installed_refs_for_update(installation, nullptr, &error);
        if (!updates) {
          ok = false;
          break;
        }
        for (guint i = 0; i < updates->len; ++i) {
          char* formatted = flatpak_ref_format_ref(FLATPAK_REF(g_ptr_array_index(updates, i)));
          refs.emplace_back(formatted);
          g_free(formatted);
        }
        g_ptr_array_unref(updates);
      }
      for (const auto& ref : refs) {
        ok = ok && flatpak_transaction_add_update(transaction, ref.c_str(), nullptr, nullptr, &error);
      }
      break;
    }
    case etal::JobKind::InstallFile: {
      const auto name = request.file.filename().string();
      if (name.ends_with(".flatpakref")) {
        gchar* contents = nullptr;
        gsize length = 0;
        ok = g_file_get_contents(request.file.c_str(), &contents, &length, &error);
        if (ok) {
          GBytes* bytes = g_bytes_new_take(contents, length);
          ok = flatpak_transaction_add_install_flatpakref(transaction, bytes, &error);
          g_bytes_unref(bytes);
        }
      } else {
        GFile* file = g_file_new_for_path(request.file.c_str());
        ok = flatpak_transaction_add_install_bundle(transaction, file, nullptr, &error);
        g_object_unref(file);
      }
      break;
    }
    case etal::JobKind::AddRemote:
    case etal::JobKind::RemoveRemote:
      break;
    }
    if (!ok) {
      std::string message = error ? error->message : "cannot prepare the transaction";
      g_clear_error(&error);
      return std::unexpected(std::move(message));
    }
    return {};
  }

  std::expected<void, std::string> addRemote(FlatpakInstallation* installation, const etal::JobRequest& request) {
    GError* error = nullptr;
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(request.file.c_str(), &contents, &length, &error)) {
      std::string message = error->message;
      g_clear_error(&error);
      return std::unexpected(std::move(message));
    }
    GBytes* bytes = g_bytes_new_take(contents, length);
    FlatpakRemote* remote = flatpak_remote_new_from_file(request.remote.c_str(), bytes, &error);
    g_bytes_unref(bytes);
    if (!remote) {
      std::string message = std::format("Not a .flatpakrepo file: {}", error ? error->message : "it cannot be read");
      g_clear_error(&error);
      return std::unexpected(std::move(message));
    }
    const bool ok = remote && flatpak_installation_add_remote(installation, remote, TRUE, nullptr, &error);
    if (remote) {
      g_object_unref(remote);
    }
    if (!ok) {
      std::string message = error ? error->message : "cannot add the remote";
      g_clear_error(&error);
      return std::unexpected(std::move(message));
    }
    // The new remote's catalog, so its apps show up at once.
    flatpak_installation_update_appstream_full_sync(installation, request.remote.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    return {};
  }

  std::set<std::string> permissionTitles(std::string_view metadata) {
    std::set<std::string> titles;
    for (const auto& permission : etal::assessPermissions(metadata).permissions) {
      if (permission.title != "No permissions") {
        titles.insert(permission.title);
      }
    }
    return titles;
  }

  std::string bytesToString(GBytes* bytes) {
    if (!bytes) {
      return {};
    }
    gsize size = 0;
    const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
    std::string out(data ? data : "", size);
    g_bytes_unref(bytes);
    return out;
  }
} // namespace

std::expected<void, std::string>
etal::runJob(const JobRequest& request, const ProgressFn& progress, GCancellable* cancellable) {
  GError* error = nullptr;
  FlatpakInstallation* installation = openInstallation(request.installation, &error);
  if (!installation) {
    std::string message = error ? error->message : "cannot open the installation";
    g_clear_error(&error);
    return std::unexpected(std::move(message));
  }
  struct Closer {
    FlatpakInstallation* installation;
    ~Closer() { g_object_unref(installation); }
  } closer{installation};

  if (request.noInteraction) {
    flatpak_installation_set_no_interaction(installation, TRUE);
  }
  if (request.kind == JobKind::AddRemote) {
    progress({0.0, std::format("Adding {}", request.remote)});
    return addRemote(installation, request);
  }
  if (request.kind == JobKind::RemoveRemote) {
    progress({0.0, std::format("Removing {}", request.remote)});
    if (!flatpak_installation_remove_remote(installation, request.remote.c_str(), nullptr, &error)) {
      std::string message = error ? error->message : "cannot remove the remote";
      g_clear_error(&error);
      return std::unexpected(std::move(message));
    }
    return {};
  }

  FlatpakTransaction* transaction = flatpak_transaction_new_for_installation(installation, cancellable, &error);
  if (!transaction) {
    std::string message = error ? error->message : "cannot start a transaction";
    g_clear_error(&error);
    return std::unexpected(std::move(message));
  }
  flatpak_transaction_set_no_interaction(transaction, request.noInteraction);
  Run run;
  run.progress = &progress;
  run.transaction = transaction;
  g_signal_connect(transaction, "ready", G_CALLBACK(onReady), &run);
  g_signal_connect(transaction, "new-operation", G_CALLBACK(onNewOperation), &run);
  g_signal_connect(transaction, "operation-error", G_CALLBACK(onOperationError), &run);
  g_signal_connect(transaction, "add-new-remote", G_CALLBACK(onAddNewRemote), &run);
  g_signal_connect(transaction, "end-of-lifed-with-rebase", G_CALLBACK(onEndOfLifedWithRebase), &run);

  auto prepared = addOperations(installation, transaction, request);
  if (!prepared) {
    g_object_unref(transaction);
    return prepared;
  }
  progress({0.0, "Preparing…"});
  const bool ok = flatpak_transaction_run(transaction, cancellable, &error);
  g_object_unref(transaction);
  if (!ok) {
    std::string message;
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) || g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_ABORTED)) {
      message = "Cancelled";
    } else {
      message = !run.firstError.empty() ? run.firstError : error ? std::string(error->message) : std::string("failed");
    }
    g_clear_error(&error);
    return std::unexpected(std::move(message));
  }
  if (request.kind == JobKind::Uninstall && request.deleteData) {
    deleteLeftoverData(request.appId);
  }
  progress({1.0, "Done"});
  return {};
}

std::expected<void, std::string> etal::launchApp(InstallationKind kind, const std::string& ref) {
  GError* error = nullptr;
  FlatpakRef* parsed = flatpak_ref_parse(ref.c_str(), &error);
  FlatpakInstallation* installation = parsed ? openInstallation(kind, &error) : nullptr;
  bool ok = false;
  if (installation) {
    ok = flatpak_installation_launch(
        installation, flatpak_ref_get_name(parsed), flatpak_ref_get_arch(parsed), flatpak_ref_get_branch(parsed), nullptr, nullptr, &error
    );
  }
  std::string message = ok ? std::string() : (error ? error->message : "cannot start");
  g_clear_error(&error);
  if (installation) {
    g_object_unref(installation);
  }
  if (parsed) {
    g_object_unref(parsed);
  }
  if (!ok) {
    return std::unexpected(std::move(message));
  }
  return {};
}

etal::UpdateScan etal::listUpdates() {
  UpdateScan scan;
  for (const auto kind : {InstallationKind::User, InstallationKind::System}) {
    GError* error = nullptr;
    FlatpakInstallation* installation = openInstallation(kind, &error);
    if (!installation) {
      g_clear_error(&error);
      continue;
    }
    flatpak_installation_set_no_interaction(installation, TRUE);
    GPtrArray* refs = flatpak_installation_list_installed_refs_for_update(installation, nullptr, &error);
    if (!refs) {
      scan.problems.push_back(std::format("{}: {}", kind == InstallationKind::User ? "user" : "system", error ? error->message : "no answer"));
      g_clear_error(&error);
      g_object_unref(installation);
      continue;
    }
    for (guint i = 0; i < refs->len; ++i) {
      auto* installed = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));
      auto* ref = FLATPAK_REF(installed);
      char* formatted = flatpak_ref_format_ref(ref);
      UpdateItem item{
          .id = str(flatpak_ref_get_name(ref)),
          .ref = str(formatted),
          .remote = str(flatpak_installed_ref_get_origin(installed)),
          .installation = kind,
          .runtime = flatpak_ref_get_kind(ref) == FLATPAK_REF_KIND_RUNTIME,
          .partsOnly = false,
          .downloadSize = 0,
          .newPermissions = {},
      };
      g_free(formatted);
      FlatpakRemoteRef* remote = flatpak_installation_fetch_remote_ref_sync(
          installation, item.remote.c_str(), flatpak_ref_get_kind(ref), flatpak_ref_get_name(ref), flatpak_ref_get_arch(ref),
          flatpak_ref_get_branch(ref), nullptr, nullptr
      );
      if (remote) {
        // The remote offers the commit that is installed: the update is for parts of the ref only.
        item.partsOnly = str(flatpak_ref_get_commit(FLATPAK_REF(remote))) == str(flatpak_ref_get_commit(ref));
        item.downloadSize = item.partsOnly ? 0 : flatpak_remote_ref_get_download_size(remote);
        if (!item.runtime && !item.partsOnly) {
          GBytes* newBytes = flatpak_remote_ref_get_metadata(remote);
          const std::string updated = newBytes ? std::string(static_cast<const char*>(g_bytes_get_data(newBytes, nullptr)), g_bytes_get_size(newBytes)) : std::string();
          const std::string current = bytesToString(flatpak_installed_ref_load_metadata(installed, nullptr, nullptr));
          const auto before = permissionTitles(current);
          for (const auto& title : permissionTitles(updated)) {
            if (!before.contains(title)) {
              item.newPermissions.push_back(title);
            }
          }
        }
        g_object_unref(remote);
      }
      scan.items.push_back(std::move(item));
    }
    g_ptr_array_unref(refs);
    g_object_unref(installation);
  }
  return scan;
}
