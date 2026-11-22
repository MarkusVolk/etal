#pragma once

#include <cstdint>
#include <functional>
#include <gio/gio.h>
#include <string>

namespace etal {
  // Desktop notifications over org.freedesktop.Notifications. A click on one reaches `activated`
  // only while this object lives: the notification server calls back, it does not start the app.
  class Notifier {
  public:
    Notifier();
    ~Notifier();
    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

    // Shows a notification, replacing the one shown before. `activated` runs when it is clicked,
    // `closed` when it goes away, clicked or not. False when there is no notification server.
    bool notify(const std::string& summary, const std::string& body, std::function<void()> activated, std::function<void()> closed = {});

  private:
    static void signal(
        GDBusConnection* connection, const char* sender, const char* path, const char* interface, const char* name,
        GVariant* parameters, gpointer data
    );

    GDBusConnection* m_bus = nullptr;
    guint m_subscription = 0;
    std::uint32_t m_id = 0;
    std::function<void()> m_activated;
    std::function<void()> m_closed;
  };
} // namespace etal
