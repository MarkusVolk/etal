#pragma once

#include "app/settings.h"
#include "flatpak/installations.h"
#include "ui/controls/flex.h"
#include "ui/controls/scroll_view.h"

#include <functional>
#include <string>
#include <vector>

namespace etal {
  // Updates in the background, where new apps go, the remotes, and runtimes nothing needs any more.
  class SettingsPage final : public Flex {
  public:
    struct Callbacks {
      std::function<void(const Settings& settings)> save;
      std::function<void()> removeUnused;
      std::function<void(const RemoteInfo& remote)> removeRemote;
      std::function<void()> changed;
    };

    SettingsPage(Settings settings, std::string version, Callbacks callbacks);

    void setRemotes(std::vector<RemoteInfo> remotes);
    void setUnused(std::vector<UnusedRef> unused);
    // Rebuilds what changed; call before the frame is laid out.
    void sync();

  private:
    void rebuild();

    Settings m_settings;
    std::string m_version;
    Callbacks m_callbacks;
    ScrollViewState m_scrollState;
    Flex* m_body = nullptr;
    std::vector<RemoteInfo> m_remotes;
    std::vector<UnusedRef> m_unused;
    // The remote whose Remove was clicked once; the second click removes it.
    std::string m_confirmRemote;
    bool m_stale = true;
  };
} // namespace etal
