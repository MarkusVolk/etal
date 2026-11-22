#pragma once

#include "flatpak/update_plan.h"

namespace etal {
  // The run of `etal --check-updates`, without a window: refreshes the catalogs, looks for
  // updates, installs those that may be installed when the settings say so, removes runtimes
  // nothing needs any more, and notifies. A running Etal is asked to do it instead.
  int runBackgroundCheck();
} // namespace etal
