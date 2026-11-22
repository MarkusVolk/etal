#include "check.h"
#include "queue/transaction_queue.h"

#include <atomic>
#include <chrono>
#include <gio/gio.h>
#include <thread>
#include <vector>

namespace {
  std::atomic<int> g_concurrent = 0;
  std::atomic<int> g_maxConcurrent = 0;

  // Pretends to work: reports progress, fails for refs named "bad", waits to be cancelled for "slow".
  std::expected<void, std::string> fakeRun(const etal::JobRequest& request, const etal::ProgressFn& progress, GCancellable* cancellable) {
    const int now = ++g_concurrent;
    g_maxConcurrent = std::max(g_maxConcurrent.load(), now);
    progress({0.5, "Half way"});
    std::expected<void, std::string> result;
    if (request.refs.front() == "bad") {
      result = std::unexpected(std::string("broken"));
    } else if (request.refs.front() == "slow") {
      while (!g_cancellable_is_cancelled(cancellable)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      result = std::unexpected(std::string("Cancelled"));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    --g_concurrent;
    return result;
  }

  void spinUntil(const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done()) {
      CHECK(std::chrono::steady_clock::now() < deadline);
      g_main_context_iteration(nullptr, FALSE);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  etal::JobRequest request(std::string ref) {
    etal::JobRequest out;
    out.refs = {std::move(ref)};
    return out;
  }
} // namespace

int main() {
  etal::TransactionQueue queue(fakeRun);
  std::vector<std::uint64_t> finished;
  bool sawProgress = false;
  queue.setOnChanged([&](const etal::Job& job, bool done) {
    if (done) {
      finished.push_back(job.id);
    }
    if (job.status == "Half way") {
      sawProgress = true;
    }
  });

  const auto a = queue.enqueue(request("good"));
  const auto b = queue.enqueue(request("bad"));
  const auto c = queue.enqueue(request("queued-then-cancelled"));
  CHECK(queue.pendingFor(etal::InstallationKind::User, "bad") != nullptr);
  CHECK(queue.pendingFor(etal::InstallationKind::System, "bad") == nullptr);
  queue.cancel(c);
  spinUntil([&] { return finished.size() == 3; });

  // One at a time, in order, with progress on the main thread.
  CHECK(g_maxConcurrent == 1);
  CHECK(sawProgress);
  CHECK(finished[0] == c);
  CHECK(finished[1] == a);
  CHECK(finished[2] == b);
  CHECK(queue.jobs()[0].state == etal::JobState::Done);
  CHECK(queue.jobs()[1].state == etal::JobState::Failed);
  CHECK(queue.jobs()[1].error == "broken");
  CHECK(queue.jobs()[2].state == etal::JobState::Cancelled);
  CHECK(!queue.busy());

  // Cancelling the running job.
  const auto slow = queue.enqueue(request("slow"));
  spinUntil([&] { return queue.busy(); });
  queue.cancel(slow);
  spinUntil([&] { return finished.size() == 4; });
  CHECK(queue.jobs().back().state == etal::JobState::Cancelled);
  CHECK(queue.jobs().back().error.empty());

  queue.clearFinished();
  CHECK(queue.jobs().empty());
  CHECK(queue.pendingCount() == 0);
  return 0;
}
