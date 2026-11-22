#pragma once

#include "app/settings.h"

#include <cstdint>
#include <string>
#include <vector>

namespace etal {
  struct Screenshot {
    // The smallest image at least as wide as a thumbnail wants, and the largest one.
    std::string thumbnailUrl;
    std::string imageUrl;
    std::string caption;
  };

  struct Release {
    std::string version;
    std::int64_t timestamp = 0;
    // Pango markup, see appstreamToMarkup().
    std::string notes;
  };

  struct Links {
    std::string homepage;
    std::string bugtracker;
    std::string donation;
    std::string help;
    std::string translate;
    std::string vcs;
  };

  // One application of one remote, as the catalog of that remote describes it. The same app
  // offered by two remotes, or by the user and the system installation, is two entries.
  struct AppEntry {
    std::string id;
    // The flatpak ref, "app/<id>/<arch>/<branch>".
    std::string ref;
    std::string remote;
    InstallationKind installation = InstallationKind::User;

    std::string name;
    std::string summary;
    // Pango markup, see appstreamToMarkup().
    std::string description;
    std::string developer;
    std::string license;
    bool freeLicense = false;
    // Flathub has checked that the developer controls the app id's domain or account.
    bool verified = false;
    std::vector<std::string> categories;
    std::vector<std::string> keywords;
    // A file in the remote's appstream cache, the largest of 64 and 128 pixels; empty when the
    // remote ships none.
    std::string iconFile;
    std::string iconUrl;
    std::vector<Screenshot> screenshots;
    std::vector<Release> releases;
    Links links;
    // From the OARS content rating; 0 when it has none or nothing in it needs an age.
    unsigned minimumAge = 0;
    bool hasContentRating = false;
    // For an add-on: the ids of the apps it extends. Empty for an app.
    std::vector<std::string> extends;
  };
} // namespace etal
