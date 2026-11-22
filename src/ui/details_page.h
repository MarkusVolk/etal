#pragma once

#include "catalog/app_entry.h"
#include "catalog/safety.h"
#include "flatpak/installations.h"
#include "ui/controls/flex.h"
#include "ui/controls/scroll_view.h"

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Button;
class Glyph;
class Image;
class Label;
class Renderer;

namespace etal {
  class ImageCache;

  // Everything about one app: header with icon and name, screenshots, description, context tiles
  // for safety, size, age rating and license, the permissions, version history and links.
  class DetailsPage final : public Flex {
  public:
    struct Callbacks {
      std::function<void()> back;
      // Something changed that needs a new frame (an image arrived).
      std::function<void()> changed;
      std::function<void(const std::string& url)> openUrl;
    };

    DetailsPage(ImageCache& images, Callbacks callbacks);
    ~DetailsPage() override;

    void show(const AppEntry& entry, bool installed);
    // What the remote says about the ref; arrives after show().
    void setRefInfo(const std::string& ref, const std::expected<RefInfo, std::string>& info);
    // Applies images that arrived since the last frame; call while the window's renderer is current.
    void sync(Renderer& renderer);
    const std::string& ref() const noexcept { return m_entry.ref; }
    void stepScreenshot(int delta);
    // Scrolls by a page (positive down) or, with 0, back to the top.
    void scrollPage(int pages);

  private:
    struct Tile {
      Flex* box = nullptr;
      Glyph* glyph = nullptr;
      Label* title = nullptr;
      Label* detail = nullptr;
    };

    void build();
    void rebuildBody();
    void showScreenshot();
    void setTile(Tile& tile, std::string glyph, std::string title, std::string detail, ColorRole tone);
    void requestImage(const std::string& url, bool screenshot);

    ImageCache& m_images;
    Callbacks m_callbacks;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    ScrollViewState m_scrollState;

    ScrollView* m_scroll = nullptr;
    Flex* m_body = nullptr;
    Image* m_icon = nullptr;
    Label* m_name = nullptr;
    Label* m_developer = nullptr;
    Glyph* m_verified = nullptr;
    Label* m_summary = nullptr;
    Flex* m_installedChip = nullptr;
    Flex* m_eolBanner = nullptr;
    Label* m_eolText = nullptr;
    Flex* m_shots = nullptr;
    Image* m_shot = nullptr;
    Label* m_shotCaption = nullptr;
    Button* m_shotPrevious = nullptr;
    Button* m_shotNext = nullptr;
    Tile m_safetyTile;
    Tile m_sizeTile;
    Tile m_ageTile;
    Tile m_licenseTile;
    Flex* m_permissions = nullptr;

    AppEntry m_entry;
    bool m_installed = false;
    std::size_t m_shotIndex = 0;
    // Files that arrived and wait for the next sync(), with the URL they belong to.
    std::optional<std::pair<std::string, std::filesystem::path>> m_pendingIcon;
    std::optional<std::pair<std::string, std::filesystem::path>> m_pendingShot;
    std::string m_wantedShotUrl;
    std::optional<Safety> m_safety;
    bool m_permissionsStale = false;
  };
} // namespace etal
