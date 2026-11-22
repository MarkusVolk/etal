#include "ui/app_tile.h"

#include "catalog/app_entry.h"
#include "ui/builders.h"
#include "ui/controls/glyph.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"

namespace {
  ImageSourceOptions sourceOptions(int targetSize) {
    ImageSourceOptions options;
    options.targetSize = targetSize;
    options.mipmap = true;
    return options;
  }

  constexpr float kIconSize = 64.0F;
}

etal::AppTile::AppTile() {
  setDirection(FlexDirection::Horizontal);
  setAlign(FlexAlign::Center);
  setGap(Style::spaceMd);
  setPadding(Style::spaceMd);
  setStyleRadius(Style::radiusLg);

  addChild(
      ui::row(
          {.align = FlexAlign::Center, .justify = FlexJustify::Center, .width = kIconSize, .height = kIconSize},
          ui::image({.out = &m_icon, .fit = ImageFit::Contain, .width = kIconSize, .height = kIconSize}),
          ui::glyph({
              .out = &m_placeholder,
              .glyph = "package",
              .glyphSize = kIconSize * 0.6F,
              .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          })
      )
  );

  auto text = ui::column({.align = FlexAlign::Stretch, .gap = 2.0F, .flexGrow = 1.0F});
  text->addChild(
      ui::row(
          {.align = FlexAlign::Center, .gap = Style::spaceXs},
          ui::label({
              .out = &m_name,
              .fontSize = Style::fontSizeTitle,
              .fontWeight = FontWeight::Medium,
              .color = colorSpecFromRole(ColorRole::OnSurface),
              .maxLines = 1,
              .ellipsize = TextEllipsize::End,
              .flexGrow = 1.0F,
              .configure = [](Label& label) { label.setUseMarkup(false); },
          }),
          ui::glyph({
              .out = &m_verified,
              .glyph = "rosette-discount-check",
              .glyphSize = Style::fontSizeBody,
              .color = colorSpecFromRole(ColorRole::Primary),
          }),
          ui::glyph({
              .out = &m_installed,
              .glyph = "circle-check",
              .glyphSize = Style::fontSizeBody,
              .color = colorSpecFromRole(ColorRole::Tertiary),
          })
      )
  );
  text->addChild(ui::label({
      .out = &m_summary,
      .fontSize = Style::fontSizeCaption,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = 2,
      .ellipsize = TextEllipsize::End,
      .configure = [](Label& label) { label.setUseMarkup(false); },
  }));
  text->addChild(ui::label({
      .out = &m_developer,
      .fontSize = Style::fontSizeMini,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = 1,
      .ellipsize = TextEllipsize::End,
      .configure = [](Label& label) { label.setUseMarkup(false); },
  }));
  addChild(std::move(text));
}

void etal::AppTile::bind(const AppEntry& entry, bool installed, bool selected, bool hovered, Renderer& renderer) {
  setFill(colorSpecFromRole(
      selected  ? ColorRole::SecondaryContainer
      : hovered ? ColorRole::SurfaceContainerHigh
                : ColorRole::SurfaceContainer
  ));
  if (entry.iconFile != m_iconFile) {
    m_iconFile = entry.iconFile;
    if (m_iconFile.empty()) {
      m_icon->clear(renderer);
    } else {
      m_icon->setSourceFile(renderer, m_iconFile, sourceOptions(static_cast<int>(kIconSize)));
    }
  }
  m_icon->setVisible(!m_iconFile.empty());
  m_icon->setParticipatesInLayout(!m_iconFile.empty());
  m_placeholder->setVisible(m_iconFile.empty());
  m_placeholder->setParticipatesInLayout(m_iconFile.empty());
  m_name->setText(entry.name);
  m_verified->setVisible(entry.verified);
  m_installed->setVisible(installed);
  m_summary->setText(entry.summary);
  m_developer->setText(entry.developer);
  m_developer->setVisible(!entry.developer.empty());
}
