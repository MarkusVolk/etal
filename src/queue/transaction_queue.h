#pragma once

#include "flatpak/transactions.h"

#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace etal {
  enum class JobState : std::uint8_t { Queued, Running, Done, Failed, Cancelled };

  struct Job {
    std::uint64_t id = 0;
    JobRequest request;
    JobState state = JobState::Queued;
    double fraction = 0.0;
    std::string status;
    std::string error;
    bool finished() const noexcept { return state == JobState::Done || state == JobState::Failed || state == JobState::Cancelled; }
  };

  // Runs one transaction after the other on a worker thread; jobs can be queued while one runs, and
  // the queue lives as long as the application, not the window.
  class TransactionQueue {
  public:
    using Runner = std::function<std::expected<void, std::string>(const JobRequest&, const ProgressFn&, GCancellable*)>;

    explicit TransactionQueue(Runner runner = runJob);
    ~TransactionQueue();
    TransactionQueue(const TransactionQueue&) = delete;
    TransactionQueue& operator=(const TransactionQueue&) = delete;

    std::uint64_t enqueue(JobRequest request);
    // A queued job is dropped; a running one is asked to stop.
    void cancel(std::uint64_t id);
    // Forgets the finished jobs.
    void clearFinished();

    const std::deque<Job>& jobs() const noexcept { return m_jobs; }
    // The queued or running job that touches `ref` in that installation, if any.
    const Job* pendingFor(InstallationKind installation, const std::string& ref) const;
    bool busy() const;
    std::size_t pendingCount() const;
    // Called on the main thread whenever a job changes; finished is true once when it ends.
    void setOnChanged(std::function<void(const Job& job, bool finished)> callback) { m_onChanged = std::move(callback); }

  private:
    struct Shared;
    void startNext();
    Job* find(std::uint64_t id);
    void progress(std::uint64_t id, const JobProgress& progress);
    void finish(std::uint64_t id, std::expected<void, std::string> result);

    Runner m_runner;
    std::deque<Job> m_jobs;
    std::uint64_t m_nextId = 1;
    std::uint64_t m_running = 0;
    GCancellable* m_cancellable = nullptr;
    std::shared_ptr<Shared> m_shared;
    std::function<void(const Job&, bool)> m_onChanged;
  };
} // namespace etal
