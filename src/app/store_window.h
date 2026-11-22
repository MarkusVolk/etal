#pragma once

#include "app/settings.h"
#include "catalog/catalog.h"
#include "catalog/search.h"
#include "flatpak/installations.h"
#include "platform/desktop_integration.h"
#include "render/scene/input_dispatcher.h"
#include "ui/controls/scroll_view.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

class AnimationManager;
class Button;
class Flex;
class Input;
class Label;
class RenderContext;
class Spinner;
class TextInputService;
class VirtualGridView;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;

namespace etal {
  class AppGridAdapter;
  class DetailsPage;
  class ImageCache;
  class TransactionQueue;
  struct Job;

  struct Services {
    WaylandConnection& connection;
    RenderContext& renderer;
    TextInputService& textInput;
    AnimationManager& animations;
    SettingsStore& settings;
    ImageCache& images;
    TransactionQueue& queue;
  };

  // The store window: categories and the search field on the left, the apps that match on the right.
  class StoreWindow {
  public:
    explicit StoreWindow(Services services);
    ~StoreWindow();
    StoreWindow(const StoreWindow&) = delete;
    StoreWindow& operator=(const StoreWindow&) = delete;

    // Runs a named action ("search", "category", "reload", ...); false when there is none of that name.
    bool action(const std::string& name, const std::string& parameter);
    bool running() const { return m_surface.isRunning(); }
    void close() { m_surface.close(); }
    wl_surface* surface() const { return m_surface.wlSurface(); }

    void keyboard(const KeyboardEvent& event);
    void pointer(const PointerEvent& event);
    void focus(wl_surface* surface, bool entered);

  private:
    struct Loaded;

    void load();
    void loaded(Loaded result);
    void prepare();
    void build();
    void rebuildResults();
    void selectCategory(std::string category);
    void setQuery(std::string query);
    void toggleFilter(bool verified);
    void syncSidebar();
    void requestUpdate();
    void showDetails(const AppEntry& entry);
    void showBrowse();
    bool isInstalled(const AppEntry& entry) const;
    void jobChanged(const Job& job, bool finished);
    void syncDetailsState();
    void refreshInstalled();
    void install(const AppEntry& entry);
    void remove(const AppEntry& entry, bool deleteData);

    Services m_services;
    WindowSurface m_surface;
    InputDispatcher m_dispatcher;
    std::unique_ptr<Node> m_root;
    std::unique_ptr<AppGridAdapter> m_adapter;
    ScrollViewState m_scrollState;

    Input* m_search = nullptr;
    std::vector<std::pair<std::string, Button*>> m_categoryButtons;
    Label* m_title = nullptr;
    Label* m_count = nullptr;
    Button* m_verifiedButton = nullptr;
    Button* m_freeButton = nullptr;
    Flex* m_browse = nullptr;
    DetailsPage* m_details = nullptr;
    Flex* m_header = nullptr;
    VirtualGridView* m_grid = nullptr;
    Flex* m_status = nullptr;
    Spinner* m_spinner = nullptr;
    Label* m_statusText = nullptr;

    Settings m_settings;
    Catalog m_catalog;
    std::unordered_set<std::string> m_installed;
    std::vector<std::size_t> m_results;
    std::string m_query;
    std::string m_category;
    bool m_loading = true;
    std::string m_problem;
    bool m_resultsStale = true;
    bool m_sidebarStale = true;
    bool m_detailsShown = false;
    std::string m_detailsName;
    std::optional<AppEntry> m_detailsEntry;
    // The last job that touched the app on the details page, so a failure stays readable.
    std::optional<std::uint64_t> m_detailsJob;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
  };
} // namespace etal
