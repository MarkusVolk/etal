#include "app/store_window.h"

#include "app/worker.h"
#include "catalog/categories.h"
#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "queue/transaction_queue.h"
#include "render/render_context.h"
#include "ui/app_tile.h"
#include "ui/details_page.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/spinner.h"
#include "ui/controls/virtual_grid_view.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "wayland/wayland_connection.h"

#include <format>
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

  std::string installedKey(etal::InstallationKind kind, std::string_view id) {
    return std::format("{}:{}", kind == etal::InstallationKind::User ? 'u' : 's', id);
  }
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
          entry, m_installed.contains(installedKey(entry.installation, entry.id)), selected, hovered, *m_renderer
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
        Loaded result{.catalog = std::unexpected(std::string()), .installed = {}, .problems = {}};
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
  m_installed.clear();
  for (const auto& app : result.installed) {
    m_installed.insert(installedKey(app.installation, app.id));
  }
  kLog.info("{} apps, {} installed", m_catalog.entries().size(), m_installed.size());
  m_resultsStale = true;
  requestUpdate();
}

void etal::StoreWindow::rebuildResults() {
  m_results = search(
      m_catalog, m_query,
      {.onlyVerified = m_settings.onlyVerified, .onlyFree = m_settings.onlyFree, .category = m_query.empty() ? m_category : std::string()}
  );
  if (m_grid) {
    m_grid->notifyDataChanged();
    m_grid->scrollToIndex(0, true);
  }
  m_resultsStale = false;
}

// ── Actions ──────────────────────────────────────────────────────────────────

void etal::StoreWindow::selectCategory(std::string category) {
  m_category = std::move(category);
  m_detailsShown = false;
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
  m_detailsShown = false;
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
  } else if (name == "install" && m_detailsShown && m_detailsEntry && !isInstalled(*m_detailsEntry)) {
    install(*m_detailsEntry);
  } else if (name == "remove" && m_detailsShown && m_detailsEntry && isInstalled(*m_detailsEntry)) {
    remove(*m_detailsEntry, parameter == "delete-data");
  } else if (name == "cancel" && m_detailsJob) {
    m_services.queue.cancel(*m_detailsJob);
  } else if (name == "scroll" && m_detailsShown) {
    m_details->scrollPage(parameter == "up" ? -1 : parameter == "top" ? 0 : 1);
  } else if (name == "screenshot" && m_detailsShown) {
    m_details->stepScreenshot(parameter == "previous" ? -1 : 1);
  } else {
    return false;
  }
  return true;
}

bool etal::StoreWindow::isInstalled(const AppEntry& entry) const {
  return m_installed.contains(installedKey(entry.installation, entry.id));
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
  m_detailsShown = true;
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
  requestUpdate();
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
}

void etal::StoreWindow::jobChanged(const Job& job, bool finished) {
  if (finished) {
    if (job.state == JobState::Failed) {
      kLog.warn("{}: {}", job.request.title, job.error);
    }
    refreshInstalled();
    if (job.request.kind == JobKind::AddRemote && job.state == JobState::Done) {
      load();
    }
  }
  syncDetailsState();
  requestUpdate();
}

void etal::StoreWindow::refreshInstalled() {
  std::weak_ptr<int> alive = m_alive;
  runInThread<std::vector<InstalledApp>>([] { return installedApps(); }, [this, alive](std::vector<InstalledApp> apps) {
    if (alive.expired()) {
      return;
    }
    m_installed.clear();
    for (const auto& app : apps) {
      m_installed.insert(installedKey(app.installation, app.id));
    }
    if (m_grid) {
      m_grid->notifyDataChanged();
    }
    syncDetailsState();
    requestUpdate();
  });
}

void etal::StoreWindow::showBrowse() {
  m_detailsShown = false;
  requestUpdate();
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
  sidebar->addChild(categoryButton("", "All Apps", "apps"));
  for (const auto& category : mainCategories()) {
    sidebar->addChild(categoryButton(std::string(category.id), category.title, category.glyph));
  }
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

  browse->addChild(ui::virtualGridView({
      .out = &m_grid,
      .state = &m_scrollState,
      .minCellWidth = kTileMinWidth,
      .cellHeight = kTileHeight,
      .squareCells = false,
      .columnGap = Style::spaceSm,
      .rowGap = Style::spaceSm,
      .adapter = m_adapter.get(),
      .flexGrow = 1.0F,
      .configure = [](VirtualGridView& view) {
        view.setFillWidth(true);
        view.setPadding(0.0F, Style::spaceLg, Style::spaceLg, Style::spaceLg);
      },
  }));

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
      }
  );
  details->setFlexGrow(1.0F);
  details->setVisible(false);
  details->setParticipatesInLayout(false);
  m_details = details.get();
  content->addChild(std::move(details));
  m_root->addChild(std::move(content));
}

void etal::StoreWindow::syncSidebar() {
  for (auto& [id, button] : m_categoryButtons) {
    const bool selected = m_query.empty() && id == m_category;
    button->setVariant(selected ? ButtonVariant::Secondary : ButtonVariant::Ghost);
    button->setSelected(selected);
  }
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

  std::string title = "All Apps";
  if (!m_query.empty()) {
    title = "Search";
  } else if (!m_category.empty()) {
    for (const auto& category : mainCategories()) {
      if (category.id == m_category) {
        title = category.title;
      }
    }
  }
  m_title->setText(title);
  m_count->setText(m_loading ? std::string() : std::format("{} apps", m_results.size()));
  m_surface.setTitle(std::format("{} — Etal", title));

  const bool showGrid = !m_loading && m_problem.empty() && !m_results.empty();
  m_grid->setVisible(showGrid);
  m_grid->setParticipatesInLayout(showGrid);
  m_status->setVisible(!showGrid);
  m_status->setParticipatesInLayout(!showGrid);
  m_spinner->setVisible(m_loading);
  if (m_loading) {
    m_spinner->start();
    m_statusText->setText("Reading the catalogs…");
  } else {
    m_spinner->stop();
    m_statusText->setText(!m_problem.empty() ? m_problem : !m_query.empty() ? "No app matches the search." : "No apps here.");
  }

  m_browse->setVisible(!m_detailsShown);
  m_browse->setParticipatesInLayout(!m_detailsShown);
  m_details->setVisible(m_detailsShown);
  m_details->setParticipatesInLayout(m_detailsShown);
  if (m_detailsShown) {
    m_details->sync(renderer);
    m_surface.setTitle(std::format("{} — Etal", m_detailsName));
  }

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
    if (m_detailsShown && event.modifiers == 0 && sym == XKB_KEY_Escape) {
      showBrowse();
      return;
    }
    if (m_detailsShown && event.modifiers == KeyMod::Alt && sym == XKB_KEY_Left) {
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
