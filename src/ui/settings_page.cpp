#include "ui/settings_page.h"

#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/segmented.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <format>
#include <glib.h>

namespace {
  constexpr float kMaxContentWidth = 760.0F;

  std::unique_ptr<Label> plain(std::string value, float size, ColorRole role, int maxLines = 0) {
    return ui::label({
        .text = std::move(value),
        .fontSize = size,
        .color = colorSpecFromRole(role),
        .maxLines = maxLines,
        .configure = [](Label& label) { label.setUseMarkup(false); },
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

  // A card row: title and explanation on the left, a control on the right.
  std::unique_ptr<Flex> row(std::string title, std::string detail, std::unique_ptr<Node> control) {
    auto texts = ui::column({.align = FlexAlign::Stretch, .gap = 2.0F, .flexGrow = 1.0F});
    texts->addChild(plain(std::move(title), Style::fontSizeBody, ColorRole::OnSurface));
    if (!detail.empty()) {
      texts->addChild(plain(std::move(detail), Style::fontSizeCaption, ColorRole::OnSurfaceVariant));
    }
    auto out = ui::row({
        .align = FlexAlign::Center,
        .gap = Style::spaceMd,
        .paddingV = Style::spaceSm,
        .paddingH = Style::spaceMd,
        .fill = colorSpecFromRole(ColorRole::SurfaceContainer),
        .styleRadius = Style::radiusLg,
    });
    out->addChild(std::move(texts));
    if (control) {
      out->addChild(std::move(control));
    }
    return out;
  }

  std::string formatSize(std::uint64_t bytes) {
    char* text = g_format_size(bytes);
    std::string out(text);
    g_free(text);
    return out;
  }
} // namespace

etal::SettingsPage::SettingsPage(Settings settings, std::string version, Callbacks callbacks)
    : m_settings(settings), m_version(std::move(version)), m_callbacks(std::move(callbacks)) {
  setDirection(FlexDirection::Vertical);
  setAlign(FlexAlign::Stretch);
  addChild(ui::row(
      {.align = FlexAlign::Center, .paddingH = Style::spaceLg, .height = 56.0F},
      ui::label({
          .text = "Settings",
          .fontSize = Style::fontSizeHeader,
          .fontWeight = FontWeight::Bold,
          .color = colorSpecFromRole(ColorRole::OnSurface),
          .configure = [](Label& label) { label.setUseMarkup(false); },
      })
  ));
  ScrollView* scroll = nullptr;
  addChild(ui::scrollView({
      .out = &scroll,
      .state = &m_scrollState,
      .scrollbarVisible = true,
      .flexGrow = 1.0F,
      .configure = [](ScrollView& view) {
        view.clearFill();
        view.clearBorder();
      },
  }));
  Flex* content = scroll->content();
  content->setDirection(FlexDirection::Vertical);
  content->setAlign(FlexAlign::Center);
  content->addChild(ui::column({
      .out = &m_body,
      .align = FlexAlign::Stretch,
      .gap = Style::spaceXs,
      .paddingV = Style::spaceSm,
      .paddingH = Style::spaceLg,
      .maxWidth = kMaxContentWidth,
      .fillWidth = true,
  }));
}

void etal::SettingsPage::setRemotes(std::vector<RemoteInfo> remotes) {
  m_remotes = std::move(remotes);
  m_confirmRemote.clear();
  m_stale = true;
}

void etal::SettingsPage::setUnused(std::vector<UnusedRef> unused) {
  m_unused = std::move(unused);
  m_stale = true;
}

void etal::SettingsPage::setAddStatus(std::string status, bool failed) {
  m_addStatus = std::move(status);
  m_addFailed = failed;
  if (!failed && !m_addStatus.empty()) {
    // On its way: the field is free for the next one.
    m_addUrl.clear();
  }
  m_stale = true;
}

void etal::SettingsPage::sync() {
  if (m_stale) {
    rebuild();
    m_stale = false;
  }
}

void etal::SettingsPage::rebuild() {
  while (!m_body->children().empty()) {
    m_body->removeChild(m_body->children().back().get());
  }
  const auto save = [this] { m_callbacks.save(m_settings); };
  const auto gap = [this] { m_body->addChild(ui::row({.height = Style::spaceMd})); };

  m_body->addChild(heading("Updates"));
  m_body->addChild(row(
      "Update automatically",
      "Once a day, in the background. An update that asks for new permissions waits for you; runtimes nothing needs any more are removed.",
      ui::toggle({
          .checked = m_settings.automaticUpdates,
          .onChange = [this, save](bool on) {
            m_settings.automaticUpdates = on;
            save();
          },
      })
  ));
  m_body->addChild(row(
      "Notify about updates", "When apps were updated, or updates wait.",
      ui::toggle({
          .checked = m_settings.updateNotifications,
          .onChange = [this, save](bool on) {
            m_settings.updateNotifications = on;
            save();
          },
      })
  ));
  std::uint64_t unusedBytes = 0;
  for (const auto& ref : m_unused) {
    unusedBytes += ref.installedSize;
  }
  m_body->addChild(row(
      "Unused runtimes",
      m_unused.empty() ? std::string("None. Runtimes and extensions no installed app needs would show here.")
                       : std::format("{} that no installed app needs, {}", m_unused.size(), formatSize(unusedBytes)),
      m_unused.empty() ? nullptr
                       : ui::button({
                             .text = "Remove",
                             .variant = ButtonVariant::Outline,
                             .onClick = [this] { m_callbacks.removeUnused(); },
                         })
  ));

  gap();
  m_body->addChild(heading("Store"));
  m_body->addChild(row(
      "Install new apps", "For you only needs no password. For all users of this computer asks for one.",
      ui::segmented({
          .options = std::vector<ui::SegmentedOption>{{.label = "For me", .glyph = "user", .tooltip = "The user installation"},
                                                      {.label = "For all users", .glyph = "users", .tooltip = "The system installation"}},
          .selectedIndex = m_settings.installation == InstallationKind::System ? std::size_t{1} : std::size_t{0},
          .onChange = [this, save](std::size_t index) {
            m_settings.installation = index == 1 ? InstallationKind::System : InstallationKind::User;
            save();
          },
      })
  ));

  gap();
  m_body->addChild(heading("Remotes"));
  m_body->addChild(plain("Where apps come from.", Style::fontSizeCaption, ColorRole::OnSurfaceVariant));
  for (const RemoteInfo& remote : m_remotes) {
    const std::string key = std::format("{}:{}", remote.installation == InstallationKind::User ? 'u' : 's', remote.name);
    const bool confirm = m_confirmRemote == key;
    m_body->addChild(row(
        remote.title.empty() ? remote.name : std::format("{} ({})", remote.title, remote.name),
        std::format(
            "{} · {}{}", remote.url, remote.installation == InstallationKind::User ? "user" : "system", remote.disabled ? " · disabled" : ""
        ),
        ui::button({
            .text = confirm ? "Remove for good" : "Remove",
            .variant = confirm ? ButtonVariant::Destructive : ButtonVariant::Outline,
            .tooltip = "Installed apps stay, but get no more updates from it",
            .onClick = [this, remote, key, confirm] {
              if (confirm) {
                m_callbacks.removeRemote(remote);
              } else {
                m_confirmRemote = key;
                m_stale = true;
                m_callbacks.changed();
              }
            },
        })
    ));
  }
  if (m_remotes.empty()) {
    m_body->addChild(plain("No remotes yet.", Style::fontSizeBody, ColorRole::OnSurfaceVariant));
  }
  const auto add = [this] {
    if (!m_addUrl.empty()) {
      m_callbacks.addRemote(m_addUrl);
    }
  };
  m_body->addChild(ui::row(
      {.align = FlexAlign::Center,
       .gap = Style::spaceSm,
       .paddingV = Style::spaceSm,
       .paddingH = Style::spaceMd,
       .fill = colorSpecFromRole(ColorRole::SurfaceContainer),
       .styleRadius = Style::radiusLg},
      ui::input({
          .value = m_addUrl,
          .placeholder = "Address of a .flatpakrepo file, https://…",
          .flexGrow = 1.0F,
          .onChange = [this](const std::string& text) { m_addUrl = text; },
          .onSubmit = [add](const std::string&) { add(); },
      }),
      ui::button({
          .text = "Add",
          .glyph = "plus",
          .variant = ButtonVariant::Primary,
          .onClick = add,
      }),
      ui::button({
          .text = "From File…",
          .variant = ButtonVariant::Outline,
          .tooltip = "A .flatpakrepo file on this computer",
          .onClick = [this] { m_callbacks.addRemoteFile(); },
      })
  ));
  if (!m_addStatus.empty()) {
    m_body->addChild(plain(m_addStatus, Style::fontSizeCaption, m_addFailed ? ColorRole::Error : ColorRole::OnSurfaceVariant));
  }

  gap();
  m_body->addChild(plain(std::format("Etal {}", m_version), Style::fontSizeCaption, ColorRole::OnSurfaceVariant));
}
