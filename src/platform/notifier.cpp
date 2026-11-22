#include "platform/notifier.h"

#include "core/log.h"

#include <string_view>
#include <utility>

namespace {
  const Logger kLog("notify");
  constexpr const char* kName = "org.freedesktop.Notifications";
  constexpr const char* kPath = "/org/freedesktop/Notifications";
} // namespace

etal::Notifier::Notifier() {
  GError* error = nullptr;
  m_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!m_bus) {
    kLog.debug("no session bus: {}", error->message);
    g_error_free(error);
    return;
  }
  m_subscription = g_dbus_connection_signal_subscribe(
      m_bus, kName, kName, nullptr, kPath, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, signal, this, nullptr
  );
}

etal::Notifier::~Notifier() {
  if (m_bus) {
    g_dbus_connection_signal_unsubscribe(m_bus, m_subscription);
    g_object_unref(m_bus);
  }
}

bool etal::Notifier::notify(const std::string& summary, const std::string& body, std::function<void()> activated, std::function<void()> closed) {
  if (!m_bus) {
    return false;
  }
  GVariantBuilder actions;
  g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
  if (activated) {
    g_variant_builder_add(&actions, "s", "default");
    g_variant_builder_add(&actions, "s", "Open");
  }
  GVariantBuilder hints;
  g_variant_builder_init(&hints, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string("de.flk.Etal"));
  GError* error = nullptr;
  GVariant* reply = g_dbus_connection_call_sync(
      m_bus, kName, kPath, kName, "Notify",
      g_variant_new("(susssasa{sv}i)", "Etal", m_id, "de.flk.Etal", summary.c_str(), body.c_str(), &actions, &hints, -1),
      G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error
  );
  if (!reply) {
    kLog.warn("cannot notify: {}", error->message);
    g_error_free(error);
    return false;
  }
  g_variant_get(reply, "(u)", &m_id);
  g_variant_unref(reply);
  m_activated = std::move(activated);
  m_closed = std::move(closed);
  return true;
}

void etal::Notifier::signal(GDBusConnection*, const char*, const char*, const char*, const char* name, GVariant* parameters, gpointer data) {
  auto* self = static_cast<Notifier*>(data);
  const std::string_view signalName(name);
  std::uint32_t id = 0;
  if (signalName == "ActionInvoked" && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(us)"))) {
    const char* action = nullptr;
    g_variant_get(parameters, "(u&s)", &id, &action);
    if (id == self->m_id && self->m_id != 0 && self->m_activated) {
      // The handlers may notify again or destroy this object: run a copy, last.
      const auto activated = std::exchange(self->m_activated, {});
      activated();
    }
  } else if (signalName == "NotificationClosed" && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(uu)"))) {
    std::uint32_t reason = 0;
    g_variant_get(parameters, "(uu)", &id, &reason);
    if (id == self->m_id && self->m_id != 0) {
      self->m_id = 0;
      self->m_activated = {};
      if (const auto closed = std::exchange(self->m_closed, {})) {
        closed();
      }
    }
  }
}
