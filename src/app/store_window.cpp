#include "app/store_window.h"

#include "app/worker.h"
#include "etal_build_info.h"
#include "flatpak/update_plan.h"
#include "platform/notifier.h"
#include "catalog/categories.h"
#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "net/flathub_client.h"
#include "queue/transaction_queue.h"
#include "render/render_context.h"
#include "ui/app_tile.h"
#include "ui/details_page.h"
#include "ui/library_page.h"
#include "ui/settings_page.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/image.h"
#include "ui/controls/segmented.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/spinner.h"
#include "ui/controls/virtual_grid_view.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "wayland/wayland_connection.h"

#include <format>
#include <tuple>
#include <gio/gio.h>
#include <linux/input-event-codes.h>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {
  const Logger kLog("window");
  constexpr float kSidebarWidth = 232.0F;
  constexpr float kHeaderHeight = 56.0F;
  constexpr float kTileMinWidth = 320.0F;
  constexpr float kTileHeight = 104.0F;
  constexpr float kBannerIcon = 72.0F;
  constexpr std::array<const char*, 4> kCollections{"trending", "popular", "recently-added", "recently-updated"};
} // namespace

namespace etal {
  class AppGridAdapter final : public VirtualGridAdapter {
  public:
    AppGridAdapter(const Catalog& catalog, const std::vector<std::size_t>& results, const std::unordered_set<std::string>& installed)
        : m_catalog(catalog), m_results(results), m_installed(installed) {}

    void setRenderer(Renderer* renderer) { m_renderer = renderer; }
    void setOnActivate(std::function<void(const AppEntry&)> callback) { m_onActivate = std::move(callback); }

    std::size_t itemCount() const override { return m_results.size(); }
    std::unique_ptr<Node> createTile() override { return std::make_unique<AppTile>(); }
    void bindTile(Node& tile, std::size_t index, bool selected, bool hovered) override {
      if (index >= m_results.size() || !m_renderer) {
        return;
      }
      const AppEntry& entry = m_catalog.entries()[m_results[index]];
      static_cast<AppTile&>(tile).bind(
          entry, m_installed.contains(installedRefKey(entry.installation, entry.ref)), selected, hovered, *m_renderer
      );
    }
    void onActivate(std::size_t index) override {
      if (index < m_results.size() && m_onActivate) {
        m_onActivate(m_catalog.entries()[m_results[index]]);
      }
    }

  private:
    const Catalog& m_catalog;
    const std::vector<std::size_t>& m_results;
    const std::unordered_set<std::string>& m_installed;
    Renderer* m_renderer = nullptr;
    std::function<void(const AppEntry&)> m_onActivate;
  };
} // namespace etal

struct etal::StoreWindow::Loaded {
  std::expected<Catalog, std::string> catalog;
  std::vector<InstalledApp> installed;
  std::unordered_set<std::string> refs;
  std::vector<std::string> problems;
};

etal::StoreWindow::StoreWindow(Services services)
    : m_services(services), m_surface(services.connection), m_settings(services.settings.settings()) {
  m_adapter = std::make_unique<AppGridAdapter>(m_catalog, m_results, m_installed);
  m_adapter->setOnActivate([this](const AppEntry& entry) { showDetails(entry); });
  m_surface.setRenderContext(&m_services.renderer);
  m_surface.setAnimationManager(&m_services.animations);
  m_surface.setPrepareFrameCallback([this](bool, bool) { prepare(); });
  m_surface.setUpdateCallback([] {});
  m_surface.setConfigureCallback([this](std::uint32_t, std::uint32_t) { m_surface.requestLayout(); });
  m_dispatcher.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) {
    m_services.connection.extension<DesktopIntegration>().setCursorShape(serial, shape);
  });
  if (!m_surface.initialize(
          nullptr, {.width = 1180U, .height = 760U, .minWidth = 640, .minHeight = 420, .title = "Etal", .appId = "de.flk.Etal"}
      )) {
    throw std::runtime_error("cannot create the window");
  }
  m_dispatcher.setTextInputContext(m_surface.wlSurface(), &m_services.textInput);
  m_services.queue.setOnChanged([this](const Job& job, bool finished) { jobChanged(job, finished); });
  load();
  loadDiscover();
}

etal::StoreWindow::~StoreWindow() { m_services.queue.setOnChanged({}); }

void etal::StoreWindow::requestUpdate() { m_surface.requestLayout(); }

// ── Data ─────────────────────────────────────────────────────────────────────

void etal::StoreWindow::load() {
  m_loading = true;
  m_problem.clear();
  requestUpdate();
  std::weak_ptr<int> alive = m_alive;
  runInThread<Loaded>(
      [] {
        Loaded result{.catalog = std::unexpected(std::string()), .installed = {}, .refs = {}, .problems = {}};
        RemoteScan scan = scanRemotes();
        result.problems = std::move(scan.problems);
        // A remote added without its catalog: fetch it once, as `flatpak remote-add` leaves it out. Only
        // the user's own; the system's would ask for a password before the window is even up.
        for (auto& source : scan.missing) {
          if (source.installation != InstallationKind::User) {
            continue;
          }
          if (auto refreshed = refreshAppstream(source)) {
            scan.sources.push_back(std::move(source));
          } else {
            result.problems.push_back(refreshed.error());
          }
        }
        result.catalog = Catalog::load(scan.sources);
        result.installed = installedApps();
        result.refs = installedRefKeys();
        return result;
      },
      [this, alive](Loaded result) {
        if (!alive.expired()) {
          loaded(std::move(result));
        }
      }
  );
}

void etal::StoreWindow::loaded(Loaded result) {
  m_loading = false;
  for (const auto& problem : result.problems) {
    kLog.warn("{}", problem);
  }
  if (result.catalog) {
    m_catalog = std::move(*result.catalog);
    if (m_catalog.empty()) {
      m_problem = "No remote offers any apps. Add Flathub with\nflatpak remote-add --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo";
    }
  } else {
    m_catalog = Catalog();
    m_problem = result.catalog.error();
  }
  if (m_library) {
    m_library->setCatalog(&m_catalog);
  }
  setInstalled(std::move(result.installed), std::move(result.refs));
  m_bannerStale = true;
  kLog.info("{} apps, {} installed", m_catalog.entries().size(), m_installed.size());
  m_resultsStale = true;
  checkUpdates();
  if (auto [name, parameter] = std::exchange(m_pendingAction, {}); !name.empty() && !action(name, parameter)) {
    kLog.warn("no action named {}", name);
  }
  requestUpdate();
}

void etal::StoreWindow::actionWhenLoaded(std::string name, std::string parameter) {
  if (m_loading) {
    m_pendingAction = {std::move(name), std::move(parameter)};
  } else if (!action(name, parameter)) {
    kLog.warn("no action named {}", name);
  }
}

void etal::StoreWindow::setInstalled(std::vector<InstalledApp> apps, std::unordered_set<std::string> refs) {
  m_installedApps = std::move(apps);
  m_installed = std::move(refs);
  if (m_library) {
    m_library->setInstalled(m_installedApps);
  }
  scanLeftovers();
}

void etal::StoreWindow::scanLeftovers() {
  std::weak_ptr<int> alive = m_alive;
  runInThread<std::vector<LeftoverData>>([apps = m_installedApps] { return leftoverData(apps); }, [this, alive](std::vector<LeftoverData> found) {
    if (alive.expired() || !m_library) {
      return;
    }
    m_library->setLeftovers(std::move(found));
    requestUpdate();
  });
}

void etal::StoreWindow::loadDiscover() {
  std::weak_ptr<int> alive = m_alive;
  for (std::size_t i = 0; i < kCollections.size(); ++i) {
    m_services.flathub.collection(kCollections[i], 48, [this, alive, i](std::vector<std::string> ids) {
      if (alive.expired()) {
        return;
      }
      m_collections[i] = std::move(ids);
      if (m_discover && i == m_collection) {
        m_resultsStale = true;
        requestUpdate();
      }
    });
  }
  m_services.flathub.appOfTheDay([this, alive](std::string id) {
    if (alive.expired()) {
      return;
    }
    m_appOfTheDay = std::move(id);
    m_bannerStale = true;
    requestUpdate();
  });
}

void etal::StoreWindow::checkUpdates() {
  if (m_checkingUpdates) {
    return;
  }
  m_checkingUpdates = true;
  if (m_library) {
    m_library->setUpdates(m_updates, true);
  }
  std::weak_ptr<int> alive = m_alive;
  runInThread<UpdateScan>([] { return listUpdates(); }, [this, alive](UpdateScan scan) {
    if (alive.expired()) {
      return;
    }
    for (const auto& problem : scan.problems) {
      kLog.warn("updates: {}", problem);
    }
    m_checkingUpdates = false;
    m_updates = std::move(scan.items);
    kLog.info("{} updates", m_updates.size());
    if (m_library) {
      m_library->setUpdates(m_updates, false);
    }
    if (std::exchange(m_backgroundCheck, false)) {
      backgroundFollowUp();
    }
    m_sidebarStale = true;
    requestUpdate();
  });
}

void etal::StoreWindow::backgroundCheck() {
  // Fresh catalogs first, as `etal --check-updates` gets them; reading them leads to checkUpdates().
  m_backgroundCheck = true;
  std::weak_ptr<int> alive = m_alive;
  runInThread<bool>(
      [] {
        RemoteScan scan = scanRemotes();
        for (const auto& source : scan.sources) {
          // Without a network the catalogs on disk stay; the check goes on with them.
          std::ignore = refreshAppstream(source);
        }
        return true;
      },
      [this, alive](bool) {
        if (!alive.expired()) {
          load();
        }
      }
  );
}

void etal::StoreWindow::backgroundFollowUp() {
  UpdatePlan plan = planUpdates(m_updates, m_settings.automaticUpdates);
  for (const auto kind : {InstallationKind::User, InstallationKind::System}) {
    JobRequest request;
    request.kind = JobKind::Update;
    request.installation = kind;
    // Nobody may be there to answer a password prompt.
    request.noInteraction = true;
    for (const auto& item : plan.automatic) {
      if (item.installation == kind) {
        request.refs.push_back(item.ref);
      }
    }
    if (request.refs.empty()) {
      continue;
    }
    request.title = std::format("{} automatically ({})", request.refs.size() == 1 ? "1 update" : std::format("{} updates", request.refs.size()),
                                kind == InstallationKind::User ? "user" : "system");
    m_services.queue.enqueue(std::move(request));
  }
  if (!m_settings.updateNotifications) {
    return;
  }
  CheckOutcome outcome;
  outcome.pending = std::move(plan.manual);
  const auto notice = noticeFor(outcome, [this](const std::string& id) {
    const auto found = m_catalog.find(id);
    return found.empty() ? id : found.front()->name;
  });
  if (notice) {
    std::weak_ptr<int> alive = m_alive;
    m_services.notifier.notify(notice->summary, notice->body, [this, alive] {
      if (!alive.expired()) {
        showLibrary();
      }
    });
  }
}

void etal::StoreWindow::rebuildResults() {
  if (m_discover && m_query.empty()) {
    // Flathub's order, for the apps the catalog knows and the filters let through.
    m_results.clear();
    for (const auto& id : m_collections[m_collection]) {
      const auto found = m_catalog.find(id);
      if (found.empty() || (m_settings.onlyVerified && !found.front()->verified) || (m_settings.onlyFree && !found.front()->freeLicense)) {
        continue;
      }
      m_results.push_back(static_cast<std::size_t>(found.front() - m_catalog.entries().data()));
    }
  } else {
    m_results = search(
        m_catalog, m_query,
        {.onlyVerified = m_settings.onlyVerified, .onlyFree = m_settings.onlyFree, .category = m_query.empty() ? m_category : std::string()}
    );
  }
  if (m_grid) {
    m_grid->notifyDataChanged();
    m_grid->scrollToIndex(0, true);
  }
  m_resultsStale = false;
}

// ── Actions ──────────────────────────────────────────────────────────────────

void etal::StoreWindow::showDiscover() {
  m_discover = true;
  m_page = Page::Browse;
  if (!m_query.empty()) {
    m_query.clear();
    if (m_search) {
      m_search->setValue("");
    }
  }
  m_resultsStale = true;
  m_sidebarStale = true;
  requestUpdate();
}

void etal::StoreWindow::selectCategory(std::string category) {
  m_category = std::move(category);
  m_discover = false;
  m_page = Page::Browse;
  if (!m_query.empty()) {
    m_query.clear();
    if (m_search) {
      m_search->setValue("");
    }
  }
  m_resultsStale = true;
  m_sidebarStale = true;
  requestUpdate();
}

void etal::StoreWindow::setQuery(std::string query) {
  if (query == m_query) {
    return;
  }
  m_query = std::move(query);
  m_page = Page::Browse;
  m_resultsStale = true;
  m_sidebarStale = true;
  requestUpdate();
}

void etal::StoreWindow::toggleFilter(bool verified) {
  bool& flag = verified ? m_settings.onlyVerified : m_settings.onlyFree;
  flag = !flag;
  m_services.settings.save(m_settings);
  m_resultsStale = true;
  m_sidebarStale = true;
  requestUpdate();
}

bool etal::StoreWindow::action(const std::string& name, const std::string& parameter) {
  if (name == "search") {
    if (m_search) {
      m_search->setValue(parameter);
    }
    setQuery(parameter);
  } else if (name == "category") {
    selectCategory(parameter);
  } else if (name == "discover") {
    const auto wanted = std::ranges::find(kCollections, parameter, [](const char* value) { return std::string_view(value); });
    if (wanted != kCollections.end()) {
      m_collection = static_cast<std::size_t>(wanted - kCollections.begin());
    }
    showDiscover();
  } else if (name == "only-verified") {
    if (m_settings.onlyVerified != (parameter != "false")) {
      toggleFilter(true);
    }
  } else if (name == "only-free") {
    if (m_settings.onlyFree != (parameter != "false")) {
      toggleFilter(false);
    }
  } else if (name == "reload") {
    load();
  } else if (name == "details") {
    const auto found = m_catalog.find(parameter);
    if (found.empty()) {
      return false;
    }
    showDetails(*found.front());
  } else if (name == "back") {
    showBrowse();
  } else if (name == "installed") {
    showLibrary();
  } else if (name == "settings") {
    showSettings();
  } else if (name == "background-check") {
    backgroundCheck();
  } else if (name == "remove-remote" && !parameter.empty()) {
    // Of the user installation; the system's goes through the Settings page.
    JobRequest request;
    request.kind = JobKind::RemoveRemote;
    request.remote = parameter;
    request.title = std::format("remote {}", parameter);
    m_services.queue.enqueue(std::move(request));
  } else if (name == "check-updates") {
    checkUpdates();
  } else if (name == "update-all") {
    updateAll();
  } else if (name == "install" && m_page == Page::Details && m_detailsEntry && !isInstalled(*m_detailsEntry)) {
    install(*m_detailsEntry);
  } else if (name == "remove" && m_page == Page::Details && m_detailsEntry && isInstalled(*m_detailsEntry)) {
    remove(*m_detailsEntry, parameter == "delete-data");
  } else if (name == "cancel" && m_detailsJob) {
    m_services.queue.cancel(*m_detailsJob);
  } else if (name == "scroll" && m_page == Page::Details) {
    m_details->scrollPage(parameter == "up" ? -1 : parameter == "top" ? 0 : 1);
  } else if (name == "screenshot" && m_page == Page::Details) {
    m_details->stepScreenshot(parameter == "previous" ? -1 : 1);
  } else {
    return false;
  }
  return true;
}

bool etal::StoreWindow::isInstalled(const AppEntry& entry) const {
  return m_installed.contains(installedRefKey(entry.installation, entry.ref));
}

void etal::StoreWindow::showDetails(const AppEntry& entry) {
  if (!m_details) {
    return;
  }
  m_detailsEntry = entry;
  m_detailsJob.reset();
  if (const Job* job = m_services.queue.pendingFor(entry.installation, entry.ref)) {
    m_detailsJob = job->id;
  }
  m_details->show(entry, {.installed = isInstalled(entry), .job = m_detailsJob ? std::optional<Job>(*m_services.queue.pendingFor(entry.installation, entry.ref)) : std::nullopt});
  m_detailsName = entry.name;
  if (m_page != Page::Details) {
    m_previousPage = m_page;
  }
  m_page = Page::Details;
  std::weak_ptr<int> alive = m_alive;
  const std::string ref = entry.ref;
  runInThread<std::expected<RefInfo, std::string>>(
      [kind = entry.installation, remote = entry.remote, ref] { return fetchRefInfo(kind, remote, ref); },
      [this, alive, ref](std::expected<RefInfo, std::string> info) {
        if (alive.expired()) {
          return;
        }
        if (!info) {
          kLog.warn("{}", info.error());
        }
        m_details->setRefInfo(ref, info);
      }
  );
  m_details->setRelated([&] {
    std::vector<AppEntry> related;
    for (const AppEntry* other : m_catalog.byDeveloper(entry, 8)) {
      related.push_back(*other);
    }
    return related;
  }());
  syncAddons();
  if (entry.remote == "flathub") {
    m_services.flathub.stats(entry.id, [this, alive, id = entry.id](std::optional<FlathubStats> stats) {
      if (!alive.expired()) {
        m_details->setStats(id, stats);
      }
    });
  }
  requestUpdate();
}

void etal::StoreWindow::syncAddons() {
  if (!m_details || !m_detailsEntry) {
    return;
  }
  std::vector<DetailsPage::AddonState> addons;
  for (const AppEntry* addon : m_catalog.addonsOf(*m_detailsEntry)) {
    addons.push_back({
        .addon = *addon,
        .installed = isInstalled(*addon),
        .busy = m_services.queue.pendingFor(addon->installation, addon->ref) != nullptr,
    });
  }
  m_details->setAddons(std::move(addons));
}

// ── Transactions ─────────────────────────────────────────────────────────────

void etal::StoreWindow::install(const AppEntry& entry) {
  JobRequest request;
  request.kind = JobKind::Install;
  request.installation = entry.installation;
  request.remote = entry.remote;
  request.refs = {entry.ref};
  request.appId = entry.id;
  request.title = entry.name;
  m_detailsJob = m_services.queue.enqueue(std::move(request));
  syncDetailsState();
}

void etal::StoreWindow::remove(const AppEntry& entry, bool deleteData) {
  JobRequest request;
  request.kind = JobKind::Uninstall;
  request.installation = entry.installation;
  request.refs = {entry.ref};
  request.appId = entry.id;
  request.title = entry.name;
  request.deleteData = deleteData;
  m_detailsJob = m_services.queue.enqueue(std::move(request));
  syncDetailsState();
}

void etal::StoreWindow::openFile(const std::filesystem::path& file) {
  const std::string extension = file.extension().string();
  JobRequest request;
  request.installation = m_settings.installation;
  request.file = file;
  request.title = file.filename().string();
  if (extension == ".flatpakrepo") {
    request.kind = JobKind::AddRemote;
    request.remote = file.stem().string();
  } else if (extension == ".flatpakref" || extension == ".flatpak") {
    request.kind = JobKind::InstallFile;
  } else {
    kLog.warn("{}: not a .flatpakref, .flatpakrepo or .flatpak file", file.string());
    return;
  }
  m_services.queue.enqueue(std::move(request));
  showLibrary();
}

void etal::StoreWindow::updateAll() {
  // One transaction per installation: the system one asks for a password once.
  for (const auto kind : {InstallationKind::User, InstallationKind::System}) {
    JobRequest request;
    request.kind = JobKind::Update;
    request.installation = kind;
    for (const auto& item : m_updates) {
      if (item.installation == kind) {
        request.refs.push_back(item.ref);
      }
    }
    if (request.refs.empty()) {
      continue;
    }
    request.title = std::format("{} ({})", request.refs.size() == 1 ? "1 update" : std::format("{} updates", request.refs.size()),
                                kind == InstallationKind::User ? "user" : "system");
    m_services.queue.enqueue(std::move(request));
  }
}

void etal::StoreWindow::syncDetailsState() {
  if (!m_details || !m_detailsEntry) {
    return;
  }
  DetailsPage::ActionState state{.installed = isInstalled(*m_detailsEntry), .job = std::nullopt};
  if (m_detailsJob) {
    for (const Job& job : m_services.queue.jobs()) {
      if (job.id == *m_detailsJob) {
        state.job = job;
      }
    }
  }
  m_details->setActionState(std::move(state));
  syncAddons();
}

void etal::StoreWindow::jobChanged(const Job& job, bool finished) {
  if (finished) {
    if (job.state == JobState::Failed) {
      kLog.warn("{}: {}", job.request.title, job.error);
    }
    refreshInstalled();
    const bool remote = job.request.kind == JobKind::AddRemote || job.request.kind == JobKind::RemoveRemote;
    if (remote && job.state == JobState::Done) {
      load();
    } else {
      checkUpdates();
    }
    scanSettingsData();
  }
  if (m_library) {
    m_library->setJobs(m_services.queue.jobs());
  }
  m_sidebarStale = true;
  syncDetailsState();
  requestUpdate();
}

void etal::StoreWindow::refreshInstalled() {
  std::weak_ptr<int> alive = m_alive;
  using Scan = std::pair<std::vector<InstalledApp>, std::unordered_set<std::string>>;
  runInThread<Scan>([] { return Scan{installedApps(), installedRefKeys()}; }, [this, alive](Scan scan) {
    if (alive.expired()) {
      return;
    }
    setInstalled(std::move(scan.first), std::move(scan.second));
    if (m_grid) {
      m_grid->notifyDataChanged();
    }
    syncDetailsState();
    requestUpdate();
  });
}

void etal::StoreWindow::showBrowse() {
  m_page = m_page == Page::Details ? m_previousPage : Page::Browse;
  m_sidebarStale = true;
  requestUpdate();
}

void etal::StoreWindow::showLibrary() {
  m_page = Page::Library;
  m_sidebarStale = true;
  requestUpdate();
}

void etal::StoreWindow::showSettings() {
  m_page = Page::Settings;
  m_sidebarStale = true;
  scanSettingsData();
  requestUpdate();
}

void etal::StoreWindow::scanSettingsData() {
  using Data = std::pair<std::vector<RemoteInfo>, std::vector<UnusedRef>>;
  std::weak_ptr<int> alive = m_alive;
  runInThread<Data>([] { return Data{listRemotes(), listUnusedRefs()}; }, [this, alive](Data data) {
    if (alive.expired() || !m_settingsPage) {
      return;
    }
    m_settingsPage->setRemotes(std::move(data.first));
    m_settingsPage->setUnused(std::move(data.second));
    requestUpdate();
  });
}

// ── Scene ────────────────────────────────────────────────────────────────────

void etal::StoreWindow::build() {
  m_root = ui::row({.align = FlexAlign::Stretch, .fill = colorSpecFromRole(ColorRole::Surface)});
  m_root->setAnimationManager(&m_services.animations);
  m_surface.setSceneRoot(m_root.get());
  m_dispatcher.setSceneRoot(m_root.get());

  auto sidebar = ui::column({
      .align = FlexAlign::Stretch,
      .gap = 2.0F,
      .paddingV = Style::spaceMd,
      .paddingH = Style::spaceSm,
      .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow),
      .width = kSidebarWidth,
  });
  sidebar->addChild(ui::input({
      .out = &m_search,
      .placeholder = "Search apps",
      .clearButtonEnabled = true,
      .onChange = [this](const std::string& text) { setQuery(text); },
  }));
  sidebar->addChild(ui::separator({.spacing = Style::spaceSm}));
  const auto categoryButton = [this](std::string id, std::string_view title, std::string_view glyph) {
    Button* button = nullptr;
    auto node = ui::button({
        .out = &button,
        .text = std::string(title),
        .glyph = std::string(glyph),
        .contentAlign = ButtonContentAlign::Start,
        .variant = ButtonVariant::Ghost,
        .onClick = [this, id] { selectCategory(id); },
    });
    m_categoryButtons.emplace_back(std::move(id), button);
    return node;
  };
  sidebar->addChild(ui::button({
      .out = &m_libraryButton,
      .text = "Installed",
      .glyph = "device-desktop",
      .contentAlign = ButtonContentAlign::Start,
      .variant = ButtonVariant::Ghost,
      .onClick = [this] { showLibrary(); },
  }));
  sidebar->addChild(ui::separator({.spacing = Style::spaceSm}));
  sidebar->addChild(ui::button({
      .out = &m_discoverButton,
      .text = "Discover",
      .glyph = "compass",
      .contentAlign = ButtonContentAlign::Start,
      .variant = ButtonVariant::Ghost,
      .onClick = [this] { showDiscover(); },
  }));
  sidebar->addChild(categoryButton("", "All Apps", "apps"));
  for (const auto& category : mainCategories()) {
    sidebar->addChild(categoryButton(std::string(category.id), category.title, category.glyph));
  }
  sidebar->addChild(ui::spacer());
  sidebar->addChild(ui::button({
      .out = &m_settingsButton,
      .text = "Settings",
      .glyph = "adjustments-horizontal",
      .contentAlign = ButtonContentAlign::Start,
      .variant = ButtonVariant::Ghost,
      .onClick = [this] { showSettings(); },
  }));
  m_root->addChild(std::move(sidebar));

  auto content = ui::column({.align = FlexAlign::Stretch, .flexGrow = 1.0F});
  auto browse = ui::column({.out = &m_browse, .align = FlexAlign::Stretch, .flexGrow = 1.0F});
  browse->addChild(ui::row(
      {.out = &m_header, .align = FlexAlign::Center, .gap = Style::spaceSm, .paddingH = Style::spaceLg, .height = kHeaderHeight},
      ui::label({
          .out = &m_title,
          .fontSize = Style::fontSizeHeader,
          .fontWeight = FontWeight::Bold,
          .color = colorSpecFromRole(ColorRole::OnSurface),
          .maxLines = 1,
          .configure = [](Label& label) { label.setUseMarkup(false); },
      }),
      ui::label({
          .out = &m_count,
          .fontSize = Style::fontSizeCaption,
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .maxLines = 1,
          .flexGrow = 1.0F,
          .configure = [](Label& label) { label.setUseMarkup(false); },
      }),
      ui::button({
          .out = &m_verifiedButton,
          .text = "Verified",
          .glyph = "rosette-discount-check",
          .variant = ButtonVariant::Outline,
          .tooltip = "Only apps whose developer Flathub has verified",
          .onClick = [this] { toggleFilter(true); },
      }),
      ui::button({
          .out = &m_freeButton,
          .text = "Free software",
          .glyph = "heart-handshake",
          .variant = ButtonVariant::Outline,
          .tooltip = "Only apps under a free license",
          .onClick = [this] { toggleFilter(false); },
      })
  ));

  auto bannerText = ui::column({.align = FlexAlign::Stretch, .gap = 2.0F, .flexGrow = 1.0F});
  bannerText->addChild(ui::label({
      .text = "APP OF THE DAY",
      .fontSize = Style::fontSizeMini,
      .fontWeight = FontWeight::Bold,
      .color = colorSpecFromRole(ColorRole::Primary),
      .configure = [](Label& label) { label.setUseMarkup(false); },
  }));
  bannerText->addChild(ui::label({
      .out = &m_bannerName,
      .fontSize = Style::fontSizeHeader,
      .fontWeight = FontWeight::Bold,
      .color = colorSpecFromRole(ColorRole::OnSurface),
      .maxLines = 1,
      .ellipsize = TextEllipsize::End,
      .configure = [](Label& label) { label.setUseMarkup(false); },
  }));
  bannerText->addChild(ui::label({
      .out = &m_bannerSummary,
      .fontSize = Style::fontSizeBody,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = 1,
      .ellipsize = TextEllipsize::End,
      .configure = [](Label& label) { label.setUseMarkup(false); },
  }));
  auto discover = ui::column({
      .out = &m_discoverBox,
      .align = FlexAlign::Stretch,
      .gap = Style::spaceMd,
      .configure = [](Flex& box) { box.setPadding(0.0F, Style::spaceLg, Style::spaceMd, Style::spaceLg); },
  });
  discover->addChild(ui::row(
      {.out = &m_banner,
       .align = FlexAlign::Center,
       .gap = Style::spaceLg,
       .padding = Style::spaceLg,
       .fill = colorSpecFromRole(ColorRole::SurfaceContainerHigh),
       .styleRadius = Style::radiusXl},
      ui::image({
          .out = &m_bannerIcon,
          .fit = ImageFit::Contain,
          .width = kBannerIcon,
          .height = kBannerIcon,
          .configure = [this](Image& image) { image.setAsyncReadyCallback([this] { requestUpdate(); }); },
      }),
      std::move(bannerText),
      ui::button({
          .text = "View",
          .variant = ButtonVariant::Primary,
          .onClick = [this] {
            if (const auto found = m_catalog.find(m_appOfTheDay); !found.empty()) {
              showDetails(*found.front());
            }
          },
      })
  ));
  discover->addChild(ui::row(
      {.align = FlexAlign::Center},
      ui::segmented({
          .options = std::vector<ui::SegmentedOption>{
              {.label = "Trending", .glyph = "trending-up", .tooltip = "Installed more often than usual these two weeks"},
              {.label = "Popular", .glyph = "flame", .tooltip = "Installed most in the last month"},
              {.label = "New", .glyph = "sparkles", .tooltip = "Added to Flathub lately"},
              {.label = "Updated", .glyph = "refresh", .tooltip = "With a new version lately"},
          },
          .selectedIndex = m_collection,
          .onChange = [this](std::size_t index) {
            m_collection = std::min(index, m_collections.size() - 1);
            m_resultsStale = true;
            requestUpdate();
          },
      }),
      ui::spacer()
  ));
  browse->addChild(std::move(discover));

  // The margin is the box's: padding on the grid itself is not taken off its cells.
  browse->addChild(ui::column(
      {.out = &m_gridBox,
       .align = FlexAlign::Stretch,
       .flexGrow = 1.0F,
       .configure = [](Flex& box) { box.setPadding(0.0F, Style::spaceLg, Style::spaceLg, Style::spaceLg); }},
      ui::virtualGridView({
          .out = &m_grid,
          .state = &m_scrollState,
          .minCellWidth = kTileMinWidth,
          .cellHeight = kTileHeight,
          .squareCells = false,
          .columnGap = Style::spaceSm,
          .rowGap = Style::spaceSm,
          .adapter = m_adapter.get(),
          .flexGrow = 1.0F,
          .configure = [](VirtualGridView& view) { view.setFillWidth(true); },
      })
  ));

  browse->addChild(ui::column(
      {.out = &m_status, .align = FlexAlign::Center, .justify = FlexJustify::Center, .gap = Style::spaceMd, .flexGrow = 1.0F},
      ui::spinner({.out = &m_spinner, .spinnerSize = 32.0F}),
      ui::label({
          .out = &m_statusText,
          .fontSize = Style::fontSizeBody,
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .textAlign = TextAlign::Center,
          .configure = [](Label& label) { label.setUseMarkup(false); },
      })
  ));
  content->addChild(std::move(browse));
  auto details = std::make_unique<DetailsPage>(
      m_services.images,
      DetailsPage::Callbacks{
          .back = [this] { showBrowse(); },
          .changed = [this] { requestUpdate(); },
          .openUrl =
              [](const std::string& url) {
                GError* error = nullptr;
                if (!g_app_info_launch_default_for_uri(url.c_str(), nullptr, &error)) {
                  kLog.warn("cannot open {}: {}", url, error ? error->message : "no handler");
                  g_clear_error(&error);
                }
              },
          .install =
              [this] {
                if (m_detailsEntry) {
                  install(*m_detailsEntry);
                }
              },
          .remove =
              [this](bool deleteData) {
                if (m_detailsEntry) {
                  remove(*m_detailsEntry, deleteData);
                }
              },
          .launch =
              [this] {
                if (!m_detailsEntry) {
                  return;
                }
                runInThread<std::expected<void, std::string>>(
                    [kind = m_detailsEntry->installation, ref = m_detailsEntry->ref] { return launchApp(kind, ref); },
                    [](std::expected<void, std::string> result) {
                      if (!result) {
                        kLog.warn("cannot start the app: {}", result.error());
                      }
                    }
                );
              },
          .cancel = [this](std::uint64_t job) { m_services.queue.cancel(job); },
          .addon =
              [this](const AppEntry& addon, bool install) {
                JobRequest request;
                request.kind = install ? JobKind::Install : JobKind::Uninstall;
                request.installation = addon.installation;
                request.remote = addon.remote;
                request.refs = {addon.ref};
                request.appId = addon.id;
                request.title = addon.name;
                m_services.queue.enqueue(std::move(request));
              },
          .openApp =
              [this](const std::string& id) {
                // Not from inside the click: showing another app rebuilds the page the button is on.
                std::weak_ptr<int> alive = m_alive;
                runLater([this, alive, id] {
                  if (alive.expired()) {
                    return;
                  }
                  if (const auto found = m_catalog.find(id); !found.empty()) {
                    showDetails(*found.front());
                  }
                });
              },
      }
  );
  details->setFlexGrow(1.0F);
  details->setVisible(false);
  details->setParticipatesInLayout(false);
  m_details = details.get();
  content->addChild(std::move(details));
  auto library = std::make_unique<LibraryPage>(LibraryPage::Callbacks{
      .open =
          [this](const InstalledApp& app) {
            for (const AppEntry* entry : m_catalog.find(app.id)) {
              if (entry->installation == app.installation) {
                showDetails(*entry);
                return;
              }
            }
          },
      .updateAll = [this] { updateAll(); },
      .update =
          [this](const UpdateItem& item) {
            JobRequest request;
            request.kind = JobKind::Update;
            request.installation = item.installation;
            request.refs = {item.ref};
            request.appId = item.id;
            const auto found = m_catalog.find(item.id);
            request.title = found.empty() ? item.id : found.front()->name;
            m_services.queue.enqueue(std::move(request));
          },
      .checkUpdates = [this] { checkUpdates(); },
      .cancel = [this](std::uint64_t job) { m_services.queue.cancel(job); },
      .clearFinished =
          [this] {
            m_services.queue.clearFinished();
            m_library->setJobs(m_services.queue.jobs());
            requestUpdate();
          },
      .deleteData =
          [this](const std::string& id) {
            std::weak_ptr<int> alive = m_alive;
            runInThread<bool>(
                [id] {
                  deleteLeftoverData(id);
                  return true;
                },
                [this, alive](bool) {
                  if (!alive.expired()) {
                    scanLeftovers();
                  }
                }
            );
          },
      .changed = [this] { requestUpdate(); },
  });
  library->setFlexGrow(1.0F);
  library->setVisible(false);
  library->setParticipatesInLayout(false);
  m_library = library.get();
  m_library->setCatalog(&m_catalog);
  m_library->setInstalled(m_installedApps);
  m_library->setUpdates(m_updates, m_checkingUpdates);
  m_library->setJobs(m_services.queue.jobs());
  content->addChild(std::move(library));
  auto settings = std::make_unique<SettingsPage>(
      m_settings, ETAL_VERSION,
      SettingsPage::Callbacks{
          .save =
              [this](const Settings& changed) {
                // The filters live in the header; this page does not know them.
                const bool verified = m_settings.onlyVerified, free = m_settings.onlyFree;
                m_settings = changed;
                m_settings.onlyVerified = verified;
                m_settings.onlyFree = free;
                m_services.settings.save(m_settings);
              },
          .removeUnused =
              [this] {
                std::weak_ptr<int> alive = m_alive;
                runInThread<std::vector<UnusedRef>>([] { return listUnusedRefs(); }, [this, alive](std::vector<UnusedRef> unused) {
                  if (alive.expired()) {
                    return;
                  }
                  for (const auto kind : {InstallationKind::User, InstallationKind::System}) {
                    JobRequest request;
                    request.kind = JobKind::Uninstall;
                    request.installation = kind;
                    for (const auto& ref : unused) {
                      if (ref.installation == kind) {
                        request.refs.push_back(ref.ref);
                      }
                    }
                    if (!request.refs.empty()) {
                      request.title = std::format("{} unused {}", request.refs.size(), request.refs.size() == 1 ? "runtime" : "runtimes");
                      m_services.queue.enqueue(std::move(request));
                    }
                  }
                });
              },
          .removeRemote =
              [this](const RemoteInfo& remote) {
                JobRequest request;
                request.kind = JobKind::RemoveRemote;
                request.installation = remote.installation;
                request.remote = remote.name;
                request.title = std::format("remote {}", remote.name);
                m_services.queue.enqueue(std::move(request));
              },
          .changed = [this] { requestUpdate(); },
      }
  );
  settings->setFlexGrow(1.0F);
  settings->setVisible(false);
  settings->setParticipatesInLayout(false);
  m_settingsPage = settings.get();
  content->addChild(std::move(settings));
  m_root->addChild(std::move(content));
}

void etal::StoreWindow::syncSidebar() {
  // An app's page keeps the mark of the list it was opened from.
  const Page shown = m_page == Page::Details ? m_previousPage : m_page;
  const bool browsing = shown == Page::Browse || shown == Page::Details;
  for (auto& [id, button] : m_categoryButtons) {
    const bool selected = browsing && !m_discover && m_query.empty() && id == m_category;
    button->setVariant(selected ? ButtonVariant::Secondary : ButtonVariant::Ghost);
    button->setSelected(selected);
  }
  const bool discover = browsing && m_discover && m_query.empty();
  m_discoverButton->setVariant(discover ? ButtonVariant::Secondary : ButtonVariant::Ghost);
  m_discoverButton->setSelected(discover);
  m_libraryButton->setVariant(shown == Page::Library ? ButtonVariant::Secondary : ButtonVariant::Ghost);
  m_libraryButton->setSelected(shown == Page::Library);
  m_settingsButton->setVariant(shown == Page::Settings ? ButtonVariant::Secondary : ButtonVariant::Ghost);
  m_settingsButton->setSelected(shown == Page::Settings);
  const std::size_t waiting = m_updates.size() + m_services.queue.pendingCount();
  m_libraryButton->setBadge(waiting > 0 ? std::to_string(waiting) : std::string());
  m_verifiedButton->setVariant(m_settings.onlyVerified ? ButtonVariant::Secondary : ButtonVariant::Outline);
  m_freeButton->setVariant(m_settings.onlyFree ? ButtonVariant::Secondary : ButtonVariant::Outline);
  m_sidebarStale = false;
}

void etal::StoreWindow::prepare() {
  if (!m_surface.isRunning() || !m_services.renderer.makeCurrent(m_surface.renderTarget())) {
    return;
  }
  UiPhaseScope phase(UiPhase::Layout);
  if (!m_root) {
    build();
  }
  auto& renderer = m_surface.renderTarget().renderer();
  m_adapter->setRenderer(&renderer);
  if (m_sidebarStale) {
    syncSidebar();
  }
  if (m_resultsStale && !m_loading) {
    rebuildResults();
  }

  const bool discover = m_discover && m_query.empty();
  std::string title = "All Apps";
  if (!m_query.empty()) {
    title = "Search";
  } else if (discover) {
    title = "Discover";
  } else if (!m_category.empty()) {
    for (const auto& category : mainCategories()) {
      if (category.id == m_category) {
        title = category.title;
      }
    }
  }
  m_title->setText(title);
  m_count->setText(m_loading || discover ? std::string() : std::format("{} apps", m_results.size()));

  // The app of the day, once Flathub named it and the catalog knows it.
  const AppEntry* featured = nullptr;
  if (const auto found = m_catalog.find(m_appOfTheDay); discover && !found.empty()) {
    featured = found.front();
  }
  m_discoverBox->setVisible(discover && !m_loading);
  m_discoverBox->setParticipatesInLayout(discover && !m_loading);
  m_banner->setVisible(featured != nullptr);
  m_banner->setParticipatesInLayout(featured != nullptr);
  if (featured && m_bannerStale) {
    m_bannerName->setText(featured->name);
    m_bannerSummary->setText(featured->summary);
    if (!featured->iconFile.empty()) {
      ImageSourceOptions options;
      options.targetSize = static_cast<int>(kBannerIcon);
      options.mipmap = true;
      m_bannerIcon->setSourceFile(renderer, featured->iconFile, options);
    } else {
      m_bannerIcon->clear(renderer);
    }
    m_bannerStale = false;
  }
  m_surface.setTitle(std::format("{} — Etal", title));

  const bool showGrid = !m_loading && m_problem.empty() && !m_results.empty();
  m_gridBox->setVisible(showGrid);
  m_gridBox->setParticipatesInLayout(showGrid);
  m_status->setVisible(!showGrid);
  m_status->setParticipatesInLayout(!showGrid);
  m_spinner->setVisible(m_loading);
  if (m_loading) {
    m_spinner->start();
    m_statusText->setText("Reading the catalogs…");
  } else {
    m_spinner->stop();
    m_statusText->setText(
        !m_problem.empty()  ? m_problem
        : !m_query.empty()  ? "No app matches the search."
        : discover          ? "Flathub's lists are not available right now.\nAll Apps and the categories work without them."
                            : "No apps here."
    );
  }

  m_browse->setVisible(m_page == Page::Browse);
  m_browse->setParticipatesInLayout(m_page == Page::Browse);
  m_details->setVisible(m_page == Page::Details);
  m_details->setParticipatesInLayout(m_page == Page::Details);
  m_library->setVisible(m_page == Page::Library);
  m_library->setParticipatesInLayout(m_page == Page::Library);
  if (m_page == Page::Details) {
    m_details->sync(renderer);
    m_surface.setTitle(std::format("{} — Etal", m_detailsName));
  } else if (m_page == Page::Library) {
    m_library->sync(renderer);
    m_surface.setTitle("Installed — Etal");
  } else if (m_page == Page::Settings) {
    m_settingsPage->sync();
    m_surface.setTitle("Settings — Etal");
  }
  m_settingsPage->setVisible(m_page == Page::Settings);
  m_settingsPage->setParticipatesInLayout(m_page == Page::Settings);

  const auto width = static_cast<float>(m_surface.width()), height = static_cast<float>(m_surface.height());
  m_root->arrange(renderer, {0, 0, width, height});
}

// ── Input ────────────────────────────────────────────────────────────────────

void etal::StoreWindow::keyboard(const KeyboardEvent& event) {
  if (event.pressed && !event.preedit) {
    const std::uint32_t sym = event.sym;
    if (event.modifiers == KeyMod::Ctrl && (sym == XKB_KEY_f || sym == XKB_KEY_F)) {
      m_dispatcher.setFocus(m_search->inputArea());
      requestUpdate();
      return;
    }
    if (m_page == Page::Details && event.modifiers == 0 && sym == XKB_KEY_Escape) {
      showBrowse();
      return;
    }
    if (m_page == Page::Details && event.modifiers == KeyMod::Alt && sym == XKB_KEY_Left) {
      showBrowse();
      return;
    }
    if (event.modifiers == KeyMod::Ctrl && (sym == XKB_KEY_q || sym == XKB_KEY_Q || sym == XKB_KEY_w || sym == XKB_KEY_W)) {
      close();
      return;
    }
  }
  if (InputArea* focused = m_dispatcher.focusedArea(); focused && focused->capturesAllKeys()) {
    focused->dispatchKey(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
  } else {
    m_dispatcher.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
  }
  requestUpdate();
}

void etal::StoreWindow::pointer(const PointerEvent& event) {
  if (event.surface != m_surface.wlSurface()) {
    return;
  }
  const auto x = static_cast<float>(event.sx), y = static_cast<float>(event.sy);
  switch (event.type) {
  case PointerEvent::Type::Enter:
    m_dispatcher.pointerEnter(x, y, event.serial);
    break;
  case PointerEvent::Type::Leave:
    m_dispatcher.pointerLeave();
    break;
  case PointerEvent::Type::Motion:
    m_dispatcher.pointerMotion(x, y, event.serial);
    break;
  case PointerEvent::Type::Button: {
    const bool taken = m_dispatcher.pointerButton(x, y, event.button, event.pressed, event.serial, event.time, event.touch);
    // Without server decorations the empty header moves the window.
    if (!taken && event.pressed && event.button == BTN_LEFT && !m_surface.serverDecorated() && y < kHeaderHeight) {
      m_surface.beginMove(event.serial);
    }
    break;
  }
  case PointerEvent::Type::Axis:
    m_dispatcher.pointerAxis(
        x, y, event.axis, event.axisSource, event.axisValue, event.axisDiscrete, event.axisValue120, event.axisLines,
        event.axisGestureSerial
    );
    break;
  }
  requestUpdate();
}

void etal::StoreWindow::focus(wl_surface* surface, bool entered) {
  if (surface != m_surface.wlSurface()) {
    return;
  }
  if (entered) {
    m_dispatcher.setTextInputContext(m_surface.wlSurface(), &m_services.textInput);
  }
  requestUpdate();
}
