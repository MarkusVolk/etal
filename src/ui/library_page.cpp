#include "ui/library_page.h"

#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/glyph.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <format>
#include <glib.h>

namespace {
  constexpr float kIconSize = 40.0F;
  constexpr float kMaxContentWidth = 980.0F;

  std::string formatSize(std::uint64_t bytes) {
    char* text = g_format_size(bytes);
    std::string out(text);
    g_free(text);
    return out;
  }

  std::unique_ptr<Label> plain(std::string value, float size, ColorRole role, int maxLines = 1) {
    return ui::label({
        .text = std::move(value),
        .fontSize = size,
        .color = colorSpecFromRole(role),
        .maxLines = maxLines,
        .ellipsize = TextEllipsize::End,
        .configure = [](Label& label) { label.setUseMarkup(false); },
    });
  }

  std::unique_ptr<Flex> sectionHeader(std::string title, std::unique_ptr<Node> trailing = nullptr) {
    auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm});
    row->addChild(ui::label({
        .text = std::move(title),
        .fontSize = Style::fontSizeTitle,
        .fontWeight = FontWeight::Bold,
        .color = colorSpecFromRole(ColorRole::OnSurface),
        .flexGrow = 1.0F,
        .configure = [](Label& label) { label.setUseMarkup(false); },
    }));
    if (trailing) {
      row->addChild(std::move(trailing));
    }
    return row;
  }

  void clear(Flex* box) {
    while (!box->children().empty()) {
      box->removeChild(box->children().back().get());
    }
  }

  const char* scope(etal::InstallationKind kind) { return kind == etal::InstallationKind::User ? "user" : "system"; }
} // namespace

etal::LibraryPage::LibraryPage(Callbacks callbacks) : m_callbacks(std::move(callbacks)) {
  setDirection(FlexDirection::Vertical);
  setAlign(FlexAlign::Stretch);
  addChild(ui::row(
      {.align = FlexAlign::Center, .paddingH = Style::spaceLg, .height = 56.0F},
      ui::label({
          .text = "Installed",
          .fontSize = Style::fontSizeHeader,
          .fontWeight = FontWeight::Bold,
          .color = colorSpecFromRole(ColorRole::OnSurface),
          .configure = [](Label& label) { label.setUseMarkup(false); },
      })
  ));
  addChild(ui::scrollView({
      .out = &m_scroll,
      .state = &m_scrollState,
      .scrollbarVisible = true,
      .flexGrow = 1.0F,
      .configure = [](ScrollView& view) {
        view.clearFill();
        view.clearBorder();
      },
  }));
  Flex* content = m_scroll->content();
  content->setDirection(FlexDirection::Vertical);
  content->setAlign(FlexAlign::Center);
  auto body = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceLg,
      .paddingV = Style::spaceSm,
      .paddingH = Style::spaceLg,
      .maxWidth = kMaxContentWidth,
      .fillWidth = true,
  });
  body->addChild(ui::column({.out = &m_jobsBox, .align = FlexAlign::Stretch, .gap = Style::spaceXs}));
  body->addChild(ui::column({.out = &m_updatesBox, .align = FlexAlign::Stretch, .gap = Style::spaceXs}));
  body->addChild(ui::column({.out = &m_installedBox, .align = FlexAlign::Stretch, .gap = Style::spaceXs}));
  body->addChild(ui::column({.out = &m_leftoverBox, .align = FlexAlign::Stretch, .gap = Style::spaceXs}));
  content->addChild(std::move(body));
}

void etal::LibraryPage::setCatalog(const Catalog* catalog) {
  m_catalog = catalog;
  m_updatesStale = m_installedStale = m_leftoversStale = true;
}

void etal::LibraryPage::setInstalled(std::vector<InstalledApp> apps) {
  m_installed = std::move(apps);
  m_installedStale = true;
}

void etal::LibraryPage::setUpdates(std::vector<UpdateItem> updates, bool checking) {
  m_updates = std::move(updates);
  m_checking = checking;
  m_updatesStale = true;
}

void etal::LibraryPage::setJobs(const std::deque<Job>& jobs) {
  m_jobs = jobs;
  m_jobsStale = true;
}

const etal::AppEntry* etal::LibraryPage::entryOf(const std::string& id, InstallationKind installation) const {
  if (!m_catalog) {
    return nullptr;
  }
  const auto found = m_catalog->find(id);
  for (const AppEntry* entry : found) {
    if (entry->installation == installation) {
      return entry;
    }
  }
  return found.empty() ? nullptr : found.front();
}

std::unique_ptr<Node> etal::LibraryPage::appRow(
    Renderer& renderer, const std::string& id, InstallationKind installation, std::string detail, std::unique_ptr<Node> trailing
) {
  const AppEntry* entry = entryOf(id, installation);
  auto row = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceMd,
      .paddingV = Style::spaceSm,
      .paddingH = Style::spaceMd,
      .fill = colorSpecFromRole(ColorRole::SurfaceContainer),
      .styleRadius = Style::radiusLg,
  });
  if (entry && !entry->iconFile.empty()) {
    Image* icon = nullptr;
    row->addChild(ui::image({.out = &icon, .fit = ImageFit::Contain, .width = kIconSize, .height = kIconSize}));
    ImageSourceOptions options;
    options.targetSize = static_cast<int>(kIconSize);
    options.mipmap = true;
    icon->setAsyncReadyCallback([this] { m_callbacks.changed(); });
    icon->setSourceFile(renderer, entry->iconFile, options);
  } else {
    row->addChild(ui::row(
        {.align = FlexAlign::Center, .justify = FlexJustify::Center, .width = kIconSize, .height = kIconSize},
        ui::glyph({.glyph = "package", .glyphSize = kIconSize * 0.6F, .color = colorSpecFromRole(ColorRole::OnSurfaceVariant)})
    ));
  }
  auto names = ui::column({.align = FlexAlign::Stretch, .gap = 2.0F, .flexGrow = 1.0F});
  names->addChild(plain(entry ? entry->name : id, Style::fontSizeBody, ColorRole::OnSurface));
  names->addChild(plain(std::move(detail), Style::fontSizeCaption, ColorRole::OnSurfaceVariant, 2));
  row->addChild(std::move(names));
  if (trailing) {
    row->addChild(std::move(trailing));
  }
  return row;
}

void etal::LibraryPage::rebuildJobs() {
  clear(m_jobsBox);
  m_jobsBox->setVisible(!m_jobs.empty());
  m_jobsBox->setParticipatesInLayout(!m_jobs.empty());
  if (m_jobs.empty()) {
    return;
  }
  const bool anyFinished = std::ranges::any_of(m_jobs, &Job::finished);
  m_jobsBox->addChild(sectionHeader(
      "Activity", anyFinished ? ui::button({
                                    .text = "Clear",
                                    .variant = ButtonVariant::Ghost,
                                    .onClick = [this] { m_callbacks.clearFinished(); },
                                })
                              : nullptr
  ));
  for (const Job& job : m_jobs) {
    const char* verb = job.request.kind == JobKind::Uninstall ? "Remove"
                     : job.request.kind == JobKind::Update    ? "Update"
                     : job.request.kind == JobKind::AddRemote ? "Add"
                                                              : "Install";
    auto row = ui::row({
        .align = FlexAlign::Center,
        .gap = Style::spaceMd,
        .paddingV = Style::spaceSm,
        .paddingH = Style::spaceMd,
        .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow),
        .styleRadius = Style::radiusLg,
    });
    const char* glyph = job.state == JobState::Done        ? "circle-check"
                      : job.state == JobState::Failed      ? "alert-triangle"
                      : job.state == JobState::Cancelled   ? "circle-x"
                      : job.state == JobState::Running     ? "loader-2"
                                                           : "clock";
    row->addChild(ui::glyph({
        .glyph = glyph,
        .glyphSize = Style::fontSizeTitle,
        .color = colorSpecFromRole(job.state == JobState::Failed ? ColorRole::Error : ColorRole::OnSurfaceVariant),
    }));
    auto texts = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceXs, .flexGrow = 1.0F});
    texts->addChild(plain(std::format("{} {}", verb, job.request.title), Style::fontSizeBody, ColorRole::OnSurface));
    if (job.state == JobState::Running) {
      texts->addChild(ui::progressBar({.progress = static_cast<float>(job.fraction), .height = 6.0F}));
    }
    texts->addChild(plain(
        job.state == JobState::Failed ? job.error : job.status, Style::fontSizeCaption,
        job.state == JobState::Failed ? ColorRole::Error : ColorRole::OnSurfaceVariant, 3
    ));
    row->addChild(std::move(texts));
    if (!job.finished()) {
      const std::uint64_t id = job.id;
      row->addChild(ui::button({
          .text = "Cancel",
          .variant = ButtonVariant::Ghost,
          .onClick = [this, id] { m_callbacks.cancel(id); },
      }));
    }
    m_jobsBox->addChild(std::move(row));
  }
}

void etal::LibraryPage::rebuildUpdates(Renderer& renderer) {
  clear(m_updatesBox);
  auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm});
  actions->addChild(ui::button({
      .text = m_checking ? "Checking…" : "Check",
      .glyph = "refresh",
      .enabled = !m_checking,
      .variant = ButtonVariant::Ghost,
      .onClick = [this] { m_callbacks.checkUpdates(); },
  }));
  if (!m_updates.empty()) {
    actions->addChild(ui::button({
        .text = "Update All",
        .glyph = "download",
        .variant = ButtonVariant::Primary,
        .onClick = [this] { m_callbacks.updateAll(); },
    }));
  }
  m_updatesBox->addChild(sectionHeader(
      m_updates.empty() ? std::string("Updates") : std::format("Updates ({})", m_updates.size()), std::move(actions)
  ));
  if (m_updates.empty()) {
    m_updatesBox->addChild(plain(
        m_checking ? "Asking the remotes…" : "Everything is up to date.", Style::fontSizeBody, ColorRole::OnSurfaceVariant
    ));
    return;
  }
  std::uint64_t runtimeBytes = 0;
  std::size_t runtimes = 0;
  for (const UpdateItem& item : m_updates) {
    if (item.runtime) {
      ++runtimes;
      runtimeBytes += item.downloadSize;
      continue;
    }
    std::string detail = item.partsOnly ? std::format("Additional parts · {}", scope(item.installation))
                                        : std::format("{} to download · {}", formatSize(item.downloadSize), scope(item.installation));
    if (!item.newPermissions.empty()) {
      detail += "\nNew permissions: ";
      for (std::size_t i = 0; i < item.newPermissions.size(); ++i) {
        detail += (i ? ", " : "") + item.newPermissions[i];
      }
    }
    m_updatesBox->addChild(appRow(
        renderer, item.id, item.installation, std::move(detail),
        ui::button({
            .text = "Update",
            .variant = item.newPermissions.empty() ? ButtonVariant::Outline : ButtonVariant::Destructive,
            .onClick = [this, item] { m_callbacks.update(item); },
        })
    ));
  }
  if (runtimes > 0) {
    // Runtimes are plumbing: one row for all of them.
    auto row = ui::row({
        .align = FlexAlign::Center,
        .gap = Style::spaceMd,
        .paddingV = Style::spaceSm,
        .paddingH = Style::spaceMd,
        .fill = colorSpecFromRole(ColorRole::SurfaceContainer),
        .styleRadius = Style::radiusLg,
    });
    row->addChild(ui::row(
        {.align = FlexAlign::Center, .justify = FlexJustify::Center, .width = kIconSize, .height = kIconSize},
        ui::glyph({.glyph = "stack-2", .glyphSize = kIconSize * 0.6F, .color = colorSpecFromRole(ColorRole::OnSurfaceVariant)})
    ));
    auto names = ui::column({.align = FlexAlign::Stretch, .gap = 2.0F, .flexGrow = 1.0F});
    names->addChild(plain(std::format("System components ({})", runtimes), Style::fontSizeBody, ColorRole::OnSurface));
    names->addChild(plain(
        runtimeBytes > 0 ? std::format("{} to download · runtimes and drivers the apps share", formatSize(runtimeBytes))
                         : std::string("Runtimes, drivers and codecs the apps share"),
        Style::fontSizeCaption, ColorRole::OnSurfaceVariant
    ));
    row->addChild(std::move(names));
    m_updatesBox->addChild(std::move(row));
  }
}

void etal::LibraryPage::rebuildInstalled(Renderer& renderer) {
  clear(m_installedBox);
  std::uint64_t total = 0;
  for (const auto& app : m_installed) {
    total += app.installedSize;
  }
  m_installedBox->addChild(sectionHeader(
      std::format("Apps ({}, {})", m_installed.size(), formatSize(total)),
      ui::button({
          .text = m_sortBySize ? "By size" : "By name",
          .glyph = "arrows-sort",
          .variant = ButtonVariant::Ghost,
          .tooltip = "Change the order",
          .onClick = [this] {
            m_sortBySize = !m_sortBySize;
            m_installedStale = true;
            m_callbacks.changed();
          },
      })
  ));
  std::vector<const InstalledApp*> sorted;
  for (const auto& app : m_installed) {
    sorted.push_back(&app);
  }
  const auto nameOf = [this](const InstalledApp* app) {
    const AppEntry* entry = entryOf(app->id, app->installation);
    return entry ? entry->name : app->id;
  };
  std::ranges::sort(sorted, [&](const InstalledApp* a, const InstalledApp* b) {
    if (m_sortBySize && a->installedSize != b->installedSize) {
      return a->installedSize > b->installedSize;
    }
    return g_utf8_collate(nameOf(a).c_str(), nameOf(b).c_str()) < 0;
  });
  for (const InstalledApp* app : sorted) {
    std::string detail = app->version.empty() ? std::string() : app->version + " · ";
    detail += std::format("{} · {} · {}", formatSize(app->installedSize), app->remote, scope(app->installation));
    m_installedBox->addChild(appRow(
        renderer, app->id, app->installation, std::move(detail),
        ui::button({
            .glyph = "chevron-right",
            .variant = ButtonVariant::Ghost,
            .tooltip = "Details",
            .onClick = [this, copy = *app] { m_callbacks.open(copy); },
        })
    ));
  }
  if (m_installed.empty()) {
    m_installedBox->addChild(plain("No apps are installed yet.", Style::fontSizeBody, ColorRole::OnSurfaceVariant));
  }
}

void etal::LibraryPage::setLeftovers(std::vector<LeftoverData> leftovers) {
  m_leftovers = std::move(leftovers);
  m_confirmDelete.clear();
  m_leftoversStale = true;
}

void etal::LibraryPage::rebuildLeftovers() {
  clear(m_leftoverBox);
  m_leftoverBox->setVisible(!m_leftovers.empty());
  m_leftoverBox->setParticipatesInLayout(!m_leftovers.empty());
  if (m_leftovers.empty()) {
    return;
  }
  std::uint64_t total = 0;
  for (const auto& leftover : m_leftovers) {
    total += leftover.size;
  }
  m_leftoverBox->addChild(sectionHeader(std::format("Leftover Data ({})", formatSize(total))));
  m_leftoverBox->addChild(plain(
      "Settings and files of apps that are no longer installed, in ~/.var/app.", Style::fontSizeCaption,
      ColorRole::OnSurfaceVariant, 2
  ));
  for (const LeftoverData& leftover : m_leftovers) {
    const AppEntry* entry = entryOf(leftover.id, InstallationKind::User);
    const bool confirm = m_confirmDelete == leftover.id;
    auto names = ui::column({.align = FlexAlign::Stretch, .gap = 2.0F, .flexGrow = 1.0F});
    names->addChild(plain(entry ? entry->name : leftover.id, Style::fontSizeBody, ColorRole::OnSurface));
    names->addChild(plain(
        entry ? std::format("{} · {}", formatSize(leftover.size), leftover.id) : formatSize(leftover.size), Style::fontSizeCaption,
        ColorRole::OnSurfaceVariant
    ));
    m_leftoverBox->addChild(ui::row(
        {.align = FlexAlign::Center,
         .gap = Style::spaceMd,
         .paddingV = Style::spaceSm,
         .paddingH = Style::spaceMd,
         .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow),
         .styleRadius = Style::radiusLg},
        ui::glyph({.glyph = "folder", .glyphSize = Style::fontSizeTitle, .color = colorSpecFromRole(ColorRole::OnSurfaceVariant)}),
        std::move(names),
        ui::button({
            .text = confirm ? "Delete for good" : "Delete",
            .variant = confirm ? ButtonVariant::Destructive : ButtonVariant::Outline,
            .onClick = [this, id = leftover.id, confirm] {
              if (confirm) {
                m_callbacks.deleteData(id);
              } else {
                m_confirmDelete = id;
                m_leftoversStale = true;
                m_callbacks.changed();
              }
            },
        })
    ));
  }
}

void etal::LibraryPage::sync(Renderer& renderer) {
  if (m_leftoversStale) {
    rebuildLeftovers();
    m_leftoversStale = false;
  }
  if (m_jobsStale) {
    rebuildJobs();
    m_jobsStale = false;
  }
  if (m_updatesStale) {
    rebuildUpdates(renderer);
    m_updatesStale = false;
  }
  if (m_installedStale) {
    rebuildInstalled(renderer);
    m_installedStale = false;
  }
}
