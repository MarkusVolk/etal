#include "app/background.h"

#include "app/instance_service.h"
#include "app/worker.h"
#include "catalog/catalog.h"
#include "core/log.h"
#include "flatpak/installations.h"
#include "platform/notifier.h"

#include <algorithm>
#include <format>
#include <glib.h>

namespace {
  const Logger kLog("background");
  constexpr guint kWaitForClickSeconds = 30 * 60;

  struct Everything {
    etal::CheckOutcome outcome;
    etal::Catalog catalog;
  };

  // Blocks: network, disk and transactions.
  Everything checkAndUpdate(const etal::Settings& settings) {
    using namespace etal;
    Everything result;
    CheckOutcome& outcome = result.outcome;
    RemoteScan scan = scanRemotes();
    std::vector<CatalogSource> sources = scan.sources;
    sources.insert(sources.end(), scan.missing.begin(), scan.missing.end());
    for (const auto& source : sources) {
      if (auto refreshed = refreshAppstream(source); !refreshed) {
        outcome.problems.push_back(refreshed.error());
      }
    }
    // Also fills libappstream's cache, so the next window opens fast.
    if (auto catalog = Catalog::load(scanRemotes().sources)) {
      result.catalog = std::move(*catalog);
    }

    UpdateScan updates = listUpdates();
    outcome.problems.insert(outcome.problems.end(), updates.problems.begin(), updates.problems.end());
    UpdatePlan plan = planUpdates(updates.items, settings.automaticUpdates);
    outcome.pending = std::move(plan.manual);
    const ProgressFn quiet = [](const JobProgress&) {};
    for (const auto kind : {InstallationKind::User, InstallationKind::System}) {
      JobRequest request;
      request.kind = JobKind::Update;
      request.installation = kind;
      request.noInteraction = true;
      std::vector<UpdateItem> items;
      for (const auto& item : plan.automatic) {
        if (item.installation == kind) {
          request.refs.push_back(item.ref);
          items.push_back(item);
        }
      }
      if (request.refs.empty()) {
        continue;
      }
      auto done = runJob(request, quiet, nullptr);
      auto& target = done ? outcome.updated : outcome.pending;
      target.insert(target.end(), items.begin(), items.end());
      if (!done) {
        outcome.problems.push_back(done.error());
      }
    }
    if (settings.automaticUpdates) {
      const auto unused = listUnusedRefs();
      for (const auto kind : {InstallationKind::User, InstallationKind::System}) {
        JobRequest request;
        request.kind = JobKind::Uninstall;
        request.installation = kind;
        request.noInteraction = true;
        for (const auto& ref : unused) {
          if (ref.installation == kind) {
            request.refs.push_back(ref.ref);
          }
        }
        if (request.refs.empty()) {
          continue;
        }
        if (auto done = runJob(request, quiet, nullptr)) {
          outcome.removedRuntimes += request.refs.size();
        } else {
          outcome.problems.push_back(done.error());
        }
      }
    }
    return result;
  }

  void openInstalledPage() {
    GError* error = nullptr;
    gchar* self = g_file_read_link("/proc/self/exe", &error);
    if (!self) {
      g_clear_error(&error);
      return;
    }
    const gchar* argv[] = {self, "--action", "installed", nullptr};
    if (!g_spawn_async(nullptr, const_cast<gchar**>(argv), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &error)) {
      kLog.warn("cannot start Etal: {}", error->message);
      g_clear_error(&error);
    }
    g_free(self);
  }
} // namespace

int etal::runBackgroundCheck() {
  // A running Etal has the queue the transactions belong in.
  if (InstanceService::sendAction("background-check", "")) {
    return EXIT_SUCCESS;
  }
  SettingsStore store;
  const Settings settings = store.settings();
  GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
  Notifier notifier;
  int result = EXIT_SUCCESS;
  runInThread<Everything>([settings] { return checkAndUpdate(settings); }, [&](Everything everything) {
    const CheckOutcome& outcome = everything.outcome;
    for (const auto& problem : outcome.problems) {
      kLog.warn("{}", problem);
    }
    kLog.info(
        "{} updated, {} waiting, {} unused runtimes removed", outcome.updated.size(), outcome.pending.size(), outcome.removedRuntimes
    );
    const auto nameOf = [&everything](const std::string& id) {
      const auto found = everything.catalog.find(id);
      return found.empty() ? id : found.front()->name;
    };
    const auto notice = noticeFor(outcome, nameOf);
    // Stay for the click: the notification server calls back, it does not start the app.
    const bool waiting = settings.updateNotifications && notice
                      && notifier.notify(
                             notice->summary, notice->body,
                             [loop] {
                               openInstalledPage();
                               g_main_loop_quit(loop);
                             },
                             [loop] { g_main_loop_quit(loop); }
                         );
    if (!waiting) {
      g_main_loop_quit(loop);
      return;
    }
    g_timeout_add_seconds(
        kWaitForClickSeconds,
        [](gpointer data) -> gboolean {
          g_main_loop_quit(static_cast<GMainLoop*>(data));
          return G_SOURCE_REMOVE;
        },
        loop
    );
  });
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
  return result;
}
