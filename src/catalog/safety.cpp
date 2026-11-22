#include "catalog/safety.h"

#include <algorithm>
#include <glib.h>
#include <set>

namespace {
  std::vector<std::string> list(GKeyFile* file, const char* group, const char* key) {
    std::vector<std::string> out;
    gchar** values = g_key_file_get_string_list(file, group, key, nullptr, nullptr);
    for (gchar** value = values; value && *value; ++value) {
      if (**value != '\0' && **value != '!') {
        out.emplace_back(*value);
      }
    }
    g_strfreev(values);
    return out;
  }

  bool has(const std::vector<std::string>& values, std::string_view wanted) {
    return std::ranges::find(values, wanted) != values.end();
  }

  class Collector {
  public:
    void add(std::string glyph, std::string title, etal::SafetyLevel level) {
      if (m_titles.insert(title).second) {
        m_safety.permissions.push_back({std::move(glyph), std::move(title), level});
        m_safety.level = std::max(m_safety.level, level);
      }
    }
    etal::Safety finish() {
      if (m_safety.permissions.empty()) {
        add("shield-check", "No permissions", etal::SafetyLevel::Safe);
      }
      std::ranges::stable_sort(m_safety.permissions, std::greater{}, &etal::Permission::level);
      return std::move(m_safety);
    }

  private:
    etal::Safety m_safety;
    std::set<std::string> m_titles;
  };

  void filesystems(const std::vector<std::string>& entries, Collector& out) {
    using etal::SafetyLevel;
    for (std::string entry : entries) {
      bool readOnly = false;
      if (const auto colon = entry.find(':'); colon != std::string::npos) {
        readOnly = entry.substr(colon + 1) == "ro";
        entry.resize(colon);
      }
      const std::string access = readOnly ? " (read-only)" : "";
      if (entry == "host" || entry == "host-os" || entry == "host-etc" || entry == "host-root" || entry == "/") {
        out.add("folders", "Full file system access" + access, SafetyLevel::Privileged);
      } else if (entry == "home" || entry == "~" || entry == "~/") {
        out.add("home", "Home folder" + access, readOnly ? SafetyLevel::Unsafe : SafetyLevel::Privileged);
      } else if (entry.starts_with("xdg-config") || entry.starts_with("~/.config") || entry.starts_with("xdg-data")
                 || entry.starts_with("~/.local")) {
        out.add("settings", "Settings and data of other apps" + access, SafetyLevel::Unsafe);
      } else if (entry.starts_with("xdg-run") || entry.starts_with("/run")) {
        out.add("plug-connected", "Sockets of other programs", SafetyLevel::Unsafe);
      } else if (entry == "xdg-download" || entry.starts_with("xdg-download/")) {
        out.add("download", "Download folder" + access, SafetyLevel::Safe);
      } else if (entry.starts_with("xdg-")) {
        out.add("folder", "User folders such as Documents, Music or Pictures" + access, SafetyLevel::Safe);
      } else {
        out.add("folder", "Other folders" + access, SafetyLevel::Unsafe);
      }
    }
  }

  void sessionBus(GKeyFile* file, Collector& out) {
    gchar** keys = g_key_file_get_keys(file, "Session Bus Policy", nullptr, nullptr);
    for (gchar** key = keys; key && *key; ++key) {
      gchar* value = g_key_file_get_string(file, "Session Bus Policy", *key, nullptr);
      const std::string_view name(*key);
      const std::string_view policy(value ? value : "");
      if (policy == "talk" || policy == "own") {
        if (name == "org.freedesktop.Flatpak" || name.starts_with("org.freedesktop.Flatpak.")) {
          out.add("terminal", "Can run programs outside the sandbox", etal::SafetyLevel::Privileged);
        } else if (name == "ca.desrt.dconf" || name.starts_with("org.gnome.SettingsDaemon")) {
          out.add("settings", "Desktop settings", etal::SafetyLevel::Unsafe);
        } else if (name == "org.freedesktop.secrets") {
          out.add("key", "Passwords in the keyring", etal::SafetyLevel::Unsafe);
        } else if (name == "*" || name == "org.freedesktop.*") {
          out.add("network", "Any program on the session bus", etal::SafetyLevel::Privileged);
        }
      }
      g_free(value);
    }
    g_strfreev(keys);
  }
} // namespace

etal::Safety etal::assessPermissions(std::string_view metadata) {
  Collector out;
  GKeyFile* file = g_key_file_new();
  if (!g_key_file_load_from_data(file, metadata.data(), metadata.size(), G_KEY_FILE_NONE, nullptr)) {
    g_key_file_free(file);
    return out.finish();
  }
  const auto sockets = list(file, "Context", "sockets");
  const auto shared = list(file, "Context", "shared");
  const auto devices = list(file, "Context", "devices");

  if (has(sockets, "session-bus")) {
    out.add("network", "Full access to the session bus", SafetyLevel::Privileged);
  }
  if (has(sockets, "system-bus")) {
    out.add("server", "Full access to the system bus", SafetyLevel::Privileged);
  }
  filesystems(list(file, "Context", "filesystems"), out);
  sessionBus(file, out);
  if (g_key_file_has_group(file, "System Bus Policy")) {
    out.add("server", "System services", SafetyLevel::Unsafe);
  }
  if (has(sockets, "x11") && !has(sockets, "wayland")) {
    out.add("app-window", "Legacy windowing system (X11)", SafetyLevel::Unsafe);
  }
  if (has(devices, "all")) {
    out.add("device-usb", "All devices, such as webcams and USB", SafetyLevel::Unsafe);
  } else {
    if (has(devices, "input")) {
      out.add("keyboard", "Input devices", SafetyLevel::Unsafe);
    }
    if (has(devices, "usb")) {
      out.add("device-usb", "USB devices", SafetyLevel::Unsafe);
    }
    if (has(devices, "kvm")) {
      out.add("cpu", "Virtual machines (KVM)", SafetyLevel::Unsafe);
    }
  }
  if (has(sockets, "ssh-auth") || has(sockets, "gpg-agent")) {
    out.add("key", "SSH or GPG keys", SafetyLevel::Unsafe);
  }
  if (has(sockets, "pcsc")) {
    out.add("id", "Smart cards", SafetyLevel::Unsafe);
  }
  if (has(shared, "network")) {
    out.add("world", "Network access", SafetyLevel::Safe);
  }
  if (has(sockets, "pulseaudio")) {
    out.add("volume", "Sound and microphone", SafetyLevel::Safe);
  }
  if (has(sockets, "cups")) {
    out.add("printer", "Printing", SafetyLevel::Safe);
  }
  g_key_file_free(file);
  return out.finish();
}

std::string_view etal::safetyTitle(SafetyLevel level) {
  switch (level) {
  case SafetyLevel::Safe:
    return "Probably safe";
  case SafetyLevel::Unsafe:
    return "Potentially unsafe";
  case SafetyLevel::Privileged:
    return "Privileged";
  }
  return {};
}
