#pragma once

#include "catalog/catalog.h"
#include "flatpak/installations.h"
#include "flatpak/transactions.h"
#include "queue/transaction_queue.h"
#include "ui/controls/flex.h"
#include "ui/controls/scroll_view.h"

#include <deque>
#include <functional>
#include <string>
#include <vector>

class Renderer;

namespace etal {
  // What is on this computer: the jobs that run or ran, the updates that wait, and the installed apps.
  class LibraryPage final : public Flex {
  public:
    struct Callbacks {
      std::function<void(const InstalledApp& app)> open;
      std::function<void()> updateAll;
      std::function<void(const UpdateItem& item)> update;
      std::function<void()> checkUpdates;
      std::function<void(std::uint64_t job)> cancel;
      std::function<void()> clearFinished;
      std::function<void()> changed;
    };

    explicit LibraryPage(Callbacks callbacks);

    void setCatalog(const Catalog* catalog);
    void setInstalled(std::vector<InstalledApp> apps);
    // checking: the remotes are being asked right now.
    void setUpdates(std::vector<UpdateItem> updates, bool checking);
    void setJobs(const std::deque<Job>& jobs);
    // Rebuilds what changed; call while the window's renderer is current.
    void sync(Renderer& renderer);

  private:
    const AppEntry* entryOf(const std::string& id, InstallationKind installation) const;
    std::unique_ptr<Node> appRow(Renderer& renderer, const std::string& id, InstallationKind installation, std::string detail, std::unique_ptr<Node> trailing);
    void rebuildJobs();
    void rebuildUpdates(Renderer& renderer);
    void rebuildInstalled(Renderer& renderer);

    Callbacks m_callbacks;
    ScrollViewState m_scrollState;
    ScrollView* m_scroll = nullptr;
    Flex* m_jobsBox = nullptr;
    Flex* m_updatesBox = nullptr;
    Flex* m_installedBox = nullptr;

    const Catalog* m_catalog = nullptr;
    std::vector<InstalledApp> m_installed;
    std::vector<UpdateItem> m_updates;
    std::deque<Job> m_jobs;
    bool m_checking = false;
    bool m_sortBySize = false;
    bool m_jobsStale = true;
    bool m_updatesStale = true;
    bool m_installedStale = true;
  };
} // namespace etal
