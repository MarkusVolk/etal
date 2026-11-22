#include "ui/details_page.h"

#include "net/image_cache.h"
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
  ImageSourceOptions sourceOptions(int targetSize) {
    ImageSourceOptions options;
    options.targetSize = targetSize;
    options.mipmap = true;
    return options;
  }

  constexpr float kIconSize = 96.0F;
  constexpr float kShotHeight = 380.0F;
  constexpr float kTileWidth = 200.0F;
  constexpr float kMaxContentWidth = 980.0F;

  std::string formatSize(std::uint64_t bytes) {
    char* text = g_format_size(bytes);
    std::string out(text);
    g_free(text);
    return out;
  }

  std::string formatDate(std::int64_t timestamp) {
    GDateTime* time = g_date_time_new_from_unix_local(timestamp);
    if (!time) {
      return {};
    }
    char* text = g_date_time_format(time, "%e %B %Y");
    std::string out(text ? text : "");
    g_free(text);
    g_date_time_unref(time);
    if (!out.empty() && out.front() == ' ') {
      out.erase(0, 1);
    }
    return out;
  }

  std::unique_ptr<Label> text(std::string value, float size, ColorRole role, bool markup = false, int maxLines = 0) {
    return ui::label({
        .text = std::move(value),
        .fontSize = size,
        .color = colorSpecFromRole(role),
        .maxLines = maxLines,
        .configure = [markup](Label& label) { label.setUseMarkup(markup); },
    });
  }

  std::unique_ptr<Label> heading(std::string value) {
    return ui::label({
        .text = std::move(value),
        .fontSize = Style::fontSizeTitle,
        .fontWeight = FontWeight::Bold,
        .color = colorSpecFromRole(ColorRole::OnSurface),
        .configure = [](Label& label) { label.setUseMarkup(false); },
    });
  }

  ColorRole toneOf(etal::SafetyLevel level) {
    switch (level) {
    case etal::SafetyLevel::Safe:
      return ColorRole::Primary;
    case etal::SafetyLevel::Unsafe:
      return ColorRole::Tertiary;
    case etal::SafetyLevel::Privileged:
      return ColorRole::Error;
    }
    return ColorRole::Primary;
  }
} // namespace

etal::DetailsPage::DetailsPage(ImageCache& images, Callbacks callbacks) : m_images(images), m_callbacks(std::move(callbacks)) {
  setDirection(FlexDirection::Vertical);
  setAlign(FlexAlign::Stretch);
  build();
}

etal::DetailsPage::~DetailsPage() = default;

void etal::DetailsPage::build() {
  addChild(ui::row(
      {.align = FlexAlign::Center, .paddingH = Style::spaceMd, .height = 56.0F},
      ui::button({
          .text = "Back",
          .glyph = "arrow-left",
          .variant = ButtonVariant::Ghost,
          .onClick = [this] { m_callbacks.back(); },
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
  content->addChild(ui::column({
      .out = &m_body,
      .align = FlexAlign::Stretch,
      .gap = Style::spaceLg,
      .paddingV = Style::spaceSm,
      .paddingH = Style::spaceLg,
      .maxWidth = kMaxContentWidth,
      .fillWidth = true,
  }));
}

void etal::DetailsPage::show(const AppEntry& entry, ActionState state) {
  m_entry = entry;
  m_installed = state.installed;
  m_state = std::move(state);
  m_confirmRemove = false;
  m_shotIndex = 0;
  m_safety.reset();
  m_pendingIcon.reset();
  m_pendingShot.reset();
  m_wantedShotUrl.clear();
  m_scrollState.offset = 0.0F;
  // Callbacks of the previous app's downloads find their page gone.
  m_alive = std::make_shared<int>(0);
  rebuildBody();
  if (!m_entry.iconFile.empty()) {
    m_pendingIcon = {m_entry.iconFile, m_entry.iconFile};
  } else {
    requestImage(m_entry.iconUrl, false);
  }
  showScreenshot();
}

void etal::DetailsPage::rebuildBody() {
  while (!m_body->children().empty()) {
    m_body->removeChild(m_body->children().back().get());
  }
  const AppEntry& entry = m_entry;

  // Header: icon, name, developer, summary.
  auto names = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceXs, .flexGrow = 1.0F});
  names->addChild(ui::label({
      .out = &m_name,
      .text = entry.name,
      .fontSize = Style::fontSizeHeader * 1.3F,
      .fontWeight = FontWeight::Bold,
      .color = colorSpecFromRole(ColorRole::OnSurface),
      .configure = [](Label& label) { label.setUseMarkup(false); },
  }));
  names->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceXs},
      ui::label({
          .out = &m_developer,
          .text = entry.developer.empty() ? entry.id : entry.developer,
          .fontSize = Style::fontSizeBody,
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .configure = [](Label& label) { label.setUseMarkup(false); },
      }),
      ui::glyph({
          .out = &m_verified,
          .glyph = "rosette-discount-check",
          .glyphSize = Style::fontSizeBody,
          .color = colorSpecFromRole(ColorRole::Primary),
      })
  ));
  m_verified->setVisible(entry.verified);
  names->addChild(text(entry.summary, Style::fontSizeBody, ColorRole::OnSurface));
  auto chipRow = ui::row({.align = FlexAlign::Center});
  chipRow->addChild(ui::row(
      {.out = &m_installedChip,
       .align = FlexAlign::Center,
       .gap = Style::spaceXs,
       .paddingV = 2.0F,
       .paddingH = Style::spaceSm,
       .fill = colorSpecFromRole(ColorRole::TertiaryContainer),
       .styleRadius = Style::radiusMd,
       .visible = m_installed},
      ui::glyph({.glyph = "circle-check", .glyphSize = Style::fontSizeCaption, .color = colorSpecFromRole(ColorRole::OnTertiaryContainer)}),
      text(
          std::format("Installed ({})", entry.installation == InstallationKind::User ? "user" : "system"), Style::fontSizeCaption,
          ColorRole::OnTertiaryContainer
      )
  ));
  chipRow->addChild(ui::spacer());
  names->addChild(std::move(chipRow));
  m_body->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceLg},
      ui::image({
          .out = &m_icon,
          .fit = ImageFit::Contain,
          .width = kIconSize,
          .height = kIconSize,
          .configure = [this](Image& image) { image.setAsyncReadyCallback([this] { m_callbacks.changed(); }); },
      }),
      std::move(names), ui::column({.out = &m_actions, .align = FlexAlign::End, .gap = Style::spaceXs})
  ));
  rebuildActions();

  m_body->addChild(ui::row(
      {.out = &m_eolBanner,
       .align = FlexAlign::Center,
       .gap = Style::spaceSm,
       .padding = Style::spaceMd,
       .fill = colorSpecFromRole(ColorRole::ErrorContainer),
       .styleRadius = Style::radiusLg,
       .visible = false},
      ui::glyph({.glyph = "alert-triangle", .glyphSize = Style::fontSizeTitle, .color = colorSpecFromRole(ColorRole::OnErrorContainer)}),
      ui::label({
          .out = &m_eolText,
          .fontSize = Style::fontSizeBody,
          .color = colorSpecFromRole(ColorRole::OnErrorContainer),
          .flexGrow = 1.0F,
          .configure = [](Label& label) { label.setUseMarkup(false); },
      })
  ));

  // Screenshots, one at a time.
  m_shots = nullptr;
  if (!entry.screenshots.empty()) {
    auto shots = ui::column({.out = &m_shots, .align = FlexAlign::Stretch, .gap = Style::spaceSm});
    shots->addChild(ui::row(
        {.align = FlexAlign::Center,
         .gap = Style::spaceSm,
         .padding = Style::spaceSm,
         .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow),
         .styleRadius = Style::radiusXl,
         .height = kShotHeight},
        ui::button({
            .out = &m_shotPrevious,
            .glyph = "chevron-left",
            .variant = ButtonVariant::Ghost,
            .onClick = [this] { stepScreenshot(-1); },
        }),
        ui::image({
            .out = &m_shot,
            .fit = ImageFit::Contain,
            .styleRadius = Style::radiusMd,
            .height = kShotHeight - 2.0F * Style::spaceSm,
            .flexGrow = 1.0F,
            .configure = [this](Image& image) { image.setAsyncReadyCallback([this] { m_callbacks.changed(); }); },
        }),
        ui::button({
            .out = &m_shotNext,
            .glyph = "chevron-right",
            .variant = ButtonVariant::Ghost,
            .onClick = [this] { stepScreenshot(1); },
        })
    ));
    shots->addChild(ui::label({
        .out = &m_shotCaption,
        .fontSize = Style::fontSizeCaption,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .textAlign = TextAlign::Center,
        .configure = [](Label& label) { label.setUseMarkup(false); },
    }));
    m_body->addChild(std::move(shots));
  }

  if (!entry.description.empty()) {
    m_body->addChild(text(entry.description, Style::fontSizeBody, ColorRole::OnSurface, true));
  }

  // Context tiles.
  auto tiles = ui::row({.align = FlexAlign::Stretch, .wrap = true, .gap = Style::spaceSm});
  for (Tile* tile : {&m_safetyTile, &m_sizeTile, &m_ageTile, &m_licenseTile}) {
    tiles->addChild(ui::column(
        {.out = &tile->box,
         .align = FlexAlign::Center,
         .gap = Style::spaceXs,
         .padding = Style::spaceMd,
         .fill = colorSpecFromRole(ColorRole::SurfaceContainer),
         .styleRadius = Style::radiusLg,
         .width = kTileWidth},
        ui::glyph({.out = &tile->glyph, .glyphSize = Style::fontSizeHeader * 1.4F}),
        ui::label({
            .out = &tile->title,
            .fontSize = Style::fontSizeBody,
            .fontWeight = FontWeight::Bold,
            .color = colorSpecFromRole(ColorRole::OnSurface),
            .textAlign = TextAlign::Center,
            .configure = [](Label& label) { label.setUseMarkup(false); },
        }),
        ui::label({
            .out = &tile->detail,
            .fontSize = Style::fontSizeCaption,
            .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
            .maxLines = 3,
            .textAlign = TextAlign::Center,
            .ellipsize = TextEllipsize::End,
            .configure = [](Label& label) { label.setUseMarkup(false); },
        })
    ));
  }
  m_body->addChild(std::move(tiles));
  setTile(m_safetyTile, "shield", "Safety", "Checking permissions…", ColorRole::OnSurfaceVariant);
  setTile(m_sizeTile, "database", "Size", "…", ColorRole::OnSurfaceVariant);
  if (!entry.hasContentRating) {
    setTile(m_ageTile, "users", "No age rating", "The developer has not rated the content", ColorRole::OnSurfaceVariant);
  } else if (entry.minimumAge == 0) {
    setTile(m_ageTile, "mood-smile", "All ages", "Nothing in it needs an age limit", ColorRole::Primary);
  } else {
    setTile(m_ageTile, "users", std::format("Ages {}+", entry.minimumAge), "From the content rating", ColorRole::Tertiary);
  }
  if (entry.freeLicense) {
    setTile(m_licenseTile, "heart-handshake", "Free software", entry.license, ColorRole::Primary);
  } else if (entry.license.empty() || entry.license.starts_with("LicenseRef-proprietary")) {
    setTile(m_licenseTile, "lock", "Proprietary", "The source code is not public", ColorRole::Tertiary);
  } else {
    setTile(m_licenseTile, "certificate", "Other license", entry.license, ColorRole::OnSurfaceVariant);
  }

  m_body->addChild(ui::column({.out = &m_permissions, .align = FlexAlign::Stretch, .gap = Style::spaceXs, .visible = false}));

  // Version history.
  if (!entry.releases.empty()) {
    auto history = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceSm});
    history->addChild(heading("Version History"));
    for (std::size_t i = 0; i < entry.releases.size() && i < 3; ++i) {
      const Release& release = entry.releases[i];
      const std::string date = release.timestamp > 0 ? formatDate(release.timestamp) : std::string();
      auto item = ui::column({
          .align = FlexAlign::Stretch,
          .gap = Style::spaceXs,
          .padding = Style::spaceMd,
          .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow),
          .styleRadius = Style::radiusLg,
      });
      item->addChild(ui::label({
          .text = date.empty() ? "Version " + release.version : std::format("Version {} — {}", release.version, date),
          .fontSize = Style::fontSizeBody,
          .fontWeight = FontWeight::Medium,
          .color = colorSpecFromRole(ColorRole::OnSurface),
          .configure = [](Label& label) { label.setUseMarkup(false); },
      }));
      item->addChild(text(
          release.notes.empty() ? std::string("No details for this version.") : release.notes, Style::fontSizeCaption,
          ColorRole::OnSurfaceVariant, !release.notes.empty()
      ));
      history->addChild(std::move(item));
    }
    m_body->addChild(std::move(history));
  }

  // Links.
  auto links = ui::row({.align = FlexAlign::Center, .wrap = true, .gap = Style::spaceSm});
  const auto link = [this, &links](const std::string& url, const char* label, const char* glyph) {
    if (url.empty()) {
      return;
    }
    links->addChild(ui::button({
        .text = std::string(label),
        .glyph = std::string(glyph),
        .variant = ButtonVariant::Outline,
        .tooltip = url,
        .onClick = [this, url] { m_callbacks.openUrl(url); },
    }));
  };
  link(entry.links.homepage, "Website", "world");
  link(entry.links.donation, "Donate", "heart");
  link(entry.links.bugtracker, "Report an Issue", "bug");
  link(entry.links.help, "Help", "help");
  link(entry.links.translate, "Translate", "language");
  link(entry.links.vcs, "Source Code", "brand-git");
  if (!links->children().empty()) {
    m_body->addChild(std::move(links));
  }
  m_body->addChild(text(
      std::format("{} · {} · {}", entry.id, entry.remote, entry.installation == InstallationKind::User ? "user" : "system"),
      Style::fontSizeMini, ColorRole::OnSurfaceVariant
  ));
}

void etal::DetailsPage::setActionState(ActionState state) {
  if (state == m_state) {
    return;
  }
  const bool jobEnded = m_state.job && !m_state.job->finished() && state.job && state.job->finished();
  m_state = std::move(state);
  m_installed = m_state.installed;
  if (jobEnded) {
    m_confirmRemove = false;
  }
  m_installedChip->setVisible(m_installed);
  rebuildActions();
  m_callbacks.changed();
}

void etal::DetailsPage::rebuildActions() {
  while (!m_actions->children().empty()) {
    m_actions->removeChild(m_actions->children().back().get());
  }
  const std::optional<Job>& job = m_state.job;
  auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm});
  if (job && !job->finished()) {
    const std::uint64_t id = job->id;
    auto progress = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceXs, .width = 220.0F});
    progress->addChild(ui::progressBar({.progress = static_cast<float>(job->fraction), .height = 6.0F}));
    progress->addChild(ui::label({
        .text = job->status,
        .fontSize = Style::fontSizeCaption,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = 1,
        .ellipsize = TextEllipsize::End,
        .configure = [](Label& label) { label.setUseMarkup(false); },
    }));
    row->addChild(std::move(progress));
    row->addChild(ui::button({
        .text = "Cancel",
        .variant = ButtonVariant::Ghost,
        .onClick = [this, id] { m_callbacks.cancel(id); },
    }));
  } else if (m_confirmRemove) {
    m_actions->addChild(text(std::format("Remove {}?", m_entry.name), Style::fontSizeBody, ColorRole::OnSurface));
    row->addChild(ui::button({
        .text = "Remove",
        .variant = ButtonVariant::Destructive,
        .onClick = [this] { m_callbacks.remove(false); },
    }));
    row->addChild(ui::button({
        .text = "Remove and Delete Data",
        .variant = ButtonVariant::Outline,
        .tooltip = std::format("Also deletes ~/.var/app/{}", m_entry.id),
        .onClick = [this] { m_callbacks.remove(true); },
    }));
    row->addChild(ui::button({
        .text = "Keep",
        .variant = ButtonVariant::Ghost,
        .onClick = [this] {
          m_confirmRemove = false;
          rebuildActions();
          m_callbacks.changed();
        },
    }));
  } else if (m_installed) {
    row->addChild(ui::button({
        .text = "Open",
        .glyph = "player-play",
        .variant = ButtonVariant::Primary,
        .onClick = [this] { m_callbacks.launch(); },
    }));
    row->addChild(ui::button({
        .glyph = "trash",
        .variant = ButtonVariant::Outline,
        .tooltip = "Remove",
        .onClick = [this] {
          m_confirmRemove = true;
          rebuildActions();
          m_callbacks.changed();
        },
    }));
  } else {
    row->addChild(ui::button({
        .text = "Install",
        .glyph = "download",
        .variant = ButtonVariant::Primary,
        .tooltip = m_entry.installation == InstallationKind::User ? "For you only" : "For all users of this computer",
        .onClick = [this] { m_callbacks.install(); },
    }));
  }
  m_actions->addChild(std::move(row));
  if (job && job->state == JobState::Failed) {
    m_actions->addChild(ui::label({
        .text = job->error,
        .fontSize = Style::fontSizeCaption,
        .color = colorSpecFromRole(ColorRole::Error),
        .maxWidth = 320.0F,
        .maxLines = 3,
        .ellipsize = TextEllipsize::End,
        .configure = [](Label& label) { label.setUseMarkup(false); },
    }));
  }
}

void etal::DetailsPage::setTile(Tile& tile, std::string glyph, std::string title, std::string detail, ColorRole tone) {
  tile.glyph->setGlyph(glyph);
  tile.glyph->setColor(colorSpecFromRole(tone));
  tile.title->setText(title);
  tile.detail->setText(detail);
}

void etal::DetailsPage::setRefInfo(const std::string& ref, const std::expected<RefInfo, std::string>& info) {
  if (ref != m_entry.ref) {
    return;
  }
  if (!info) {
    setTile(m_safetyTile, "shield-question", "Safety unknown", "The remote did not answer", ColorRole::OnSurfaceVariant);
    setTile(m_sizeTile, "database", "Size unknown", info.error(), ColorRole::OnSurfaceVariant);
    return;
  }
  m_safety = assessPermissions(info->metadata);
  std::string detail;
  for (std::size_t i = 0; i < m_safety->permissions.size() && i < 2; ++i) {
    detail += (i ? ", " : "") + m_safety->permissions[i].title;
  }
  setTile(
      m_safetyTile,
      m_safety->level == SafetyLevel::Safe     ? "shield-check"
      : m_safety->level == SafetyLevel::Unsafe ? "shield-exclamation"
                                               : "shield-x",
      std::string(safetyTitle(m_safety->level)), detail, toneOf(m_safety->level)
  );
  setTile(
      m_sizeTile, "database", formatSize(info->installedSize),
      m_installed ? std::string("Installed size") : std::format("{} to download", formatSize(info->downloadSize)),
      ColorRole::Primary
  );
  if (!info->endOfLife.empty() || !info->endOfLifeRebase.empty()) {
    m_eolText->setText(
        !info->endOfLife.empty() ? std::format("This app is no longer supported: {}", info->endOfLife)
                                 : std::format("This app has been replaced by {}.", info->endOfLifeRebase)
    );
    m_eolBanner->setVisible(true);
  }
  m_permissionsStale = true;
  m_callbacks.changed();
}

void etal::DetailsPage::requestImage(const std::string& url, bool screenshot) {
  if (url.empty()) {
    return;
  }
  std::weak_ptr<int> alive = m_alive;
  m_images.fetch(url, [this, alive, url, screenshot](const std::filesystem::path& file) {
    if (alive.expired() || file.empty()) {
      return;
    }
    if (screenshot) {
      m_pendingShot = {url, file};
    } else {
      m_pendingIcon = {url, file};
    }
    m_callbacks.changed();
  });
}

void etal::DetailsPage::showScreenshot() {
  if (!m_shots || m_entry.screenshots.empty()) {
    return;
  }
  const std::size_t count = m_entry.screenshots.size();
  m_shotIndex %= count;
  const Screenshot& shot = m_entry.screenshots[m_shotIndex];
  m_shotPrevious->setEnabled(count > 1);
  m_shotNext->setEnabled(count > 1);
  m_shotCaption->setText(
      count > 1 ? std::format("{}{}{} / {}", shot.caption, shot.caption.empty() ? "" : "   ", m_shotIndex + 1, count) : shot.caption
  );
  m_wantedShotUrl = shot.thumbnailUrl;
  requestImage(shot.thumbnailUrl, true);
}

void etal::DetailsPage::stepScreenshot(int delta) {
  const auto count = static_cast<int>(m_entry.screenshots.size());
  if (count < 2) {
    return;
  }
  m_shotIndex = static_cast<std::size_t>((static_cast<int>(m_shotIndex) + delta + count) % count);
  showScreenshot();
  m_callbacks.changed();
}

void etal::DetailsPage::scrollPage(int pages) {
  if (pages == 0) {
    m_scroll->setScrollOffset(0.0F);
  } else {
    m_scroll->scrollBy(static_cast<float>(pages) * std::max(100.0F, m_scroll->height() * 0.8F));
  }
  m_callbacks.changed();
}

void etal::DetailsPage::sync(Renderer& renderer) {
  if (m_pendingIcon) {
    m_icon->setSourceFile(renderer, m_pendingIcon->second.string(), sourceOptions(static_cast<int>(kIconSize)));
    m_pendingIcon.reset();
  }
  if (m_pendingShot && m_shot) {
    if (m_pendingShot->first == m_wantedShotUrl) {
      m_shot->setSourceFile(renderer, m_pendingShot->second.string(), sourceOptions(0));
    }
    m_pendingShot.reset();
  }
  if (m_permissionsStale && m_safety) {
    while (!m_permissions->children().empty()) {
      m_permissions->removeChild(m_permissions->children().back().get());
    }
    m_permissions->addChild(heading("Permissions"));
    for (const Permission& permission : m_safety->permissions) {
      m_permissions->addChild(ui::row(
          {.align = FlexAlign::Center, .gap = Style::spaceSm},
          ui::glyph({.glyph = permission.glyph, .glyphSize = Style::fontSizeTitle, .color = colorSpecFromRole(toneOf(permission.level))}),
          text(permission.title, Style::fontSizeBody, ColorRole::OnSurface)
      ));
    }
    m_permissions->setVisible(true);
    m_permissionsStale = false;
  }
}
