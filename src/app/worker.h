#pragma once

#include <functional>
#include <glib.h>
#include <memory>
#include <thread>
#include <utility>

namespace etal {
  // Runs `work` on a thread of its own and hands its result to `done` on the main thread, through
  // the default GLib context that GlibPollSource dispatches. `done` must check itself whether what
  // it refers to is still there.
  template <typename Result>
  void runInThread(std::function<Result()> work, std::function<void(Result)> done) {
    struct Delivery {
      std::function<void(Result)> done;
      Result result;
    };
    std::thread([work = std::move(work), done = std::move(done)]() mutable {
      auto* delivery = new Delivery{std::move(done), work()};
      g_main_context_invoke_full(
          g_main_context_default(), G_PRIORITY_DEFAULT,
          [](gpointer data) -> gboolean {
            auto* delivery = static_cast<Delivery*>(data);
            delivery->done(std::move(delivery->result));
            return G_SOURCE_REMOVE;
          },
          delivery, [](gpointer data) { delete static_cast<Delivery*>(data); }
      );
    }).detach();
  }
} // namespace etal
