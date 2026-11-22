#pragma once

#include "ui/controls/grid_tile.h"

#include <string>

class Glyph;
class Image;
class Label;
class Renderer;

namespace etal {
  struct AppEntry;

  // An app in the grid: icon, name with the verified mark, summary and developer.
  class AppTile final : public GridTile {
  public:
    AppTile();
    void bind(const AppEntry& entry, bool installed, bool selected, bool hovered, Renderer& renderer);

  private:
    Image* m_icon = nullptr;
    Glyph* m_placeholder = nullptr;
    Label* m_name = nullptr;
    Glyph* m_verified = nullptr;
    Label* m_summary = nullptr;
    Label* m_developer = nullptr;
    Glyph* m_installed = nullptr;
    std::string m_iconFile;
  };
} // namespace etal
