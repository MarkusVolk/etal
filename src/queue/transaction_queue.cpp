#include "queue/transaction_queue.h"

#include <algorithm>
#include <gio/gio.h>
#include <thread>

namespace {
  // Runs `call` on the main thread, through the default GLib context.
  void post(std::function<void()> call) {
    auto* owned = new std::function<void()>(std::move(call));
    g_main_context_invoke_full(
        g_main_context_default(), G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
          (*static_cast<std::function<void()>*>(data))();
          return G_SOURCE_REMOVE;
        },
        owned, [](gpointer data) { delete static_cast<std::function<void()>*>(data); }
    );
  }
} // namespace

// What the worker's posted calls reach: the queue while it exists, nothing after.
struct etal::TransactionQueue::Shared {
  TransactionQueue* queue = nullptr;
};

etal::TransactionQueue::TransactionQueue(Runner runner) : m_runner(std::move(runner)), m_shared(std::make_shared<Shared>()) {
  m_shared->queue = this;
}

etal::TransactionQueue::~TransactionQueue() {
  m_shared->queue = nullptr;
  if (m_cancellable) {
    g_cancellable_cancel(m_cancellable);
    g_object_unref(m_cancellable);
  }
}

etal::Job* etal::TransactionQueue::find(std::uint64_t id) {
  const auto it = std::ranges::find(m_jobs, id, &Job::id);
  return it == m_jobs.end() ? nullptr : &*it;
}

std::uint64_t etal::TransactionQueue::enqueue(JobRequest request) {
  Job& job = m_jobs.emplace_back();
  job.id = m_nextId++;
  job.request = std::move(request);
  job.status = "Waiting";
  const std::uint64_t id = job.id;
  if (m_onChanged) {
    m_onChanged(job, false);
  }
  startNext();
  return id;
}

void etal::TransactionQueue::cancel(std::uint64_t id) {
  Job* job = find(id);
  if (!job || job->finished()) {
    return;
  }
  if (job->state == JobState::Queued) {
    job->state = JobState::Cancelled;
    job->status = "Cancelled";
    if (m_onChanged) {
      m_onChanged(*job, true);
    }
    return;
  }
  if (m_cancellable && id == m_running) {
    g_cancellable_cancel(m_cancellable);
    job->status = "Cancelling…";
    if (m_onChanged) {
      m_onChanged(*job, false);
    }
  }
}

void etal::TransactionQueue::clearFinished() {
  std::erase_if(m_jobs, [](const Job& job) { return job.finished(); });
}

const etal::Job* etal::TransactionQueue::pendingFor(InstallationKind installation, const std::string& ref) const {
  for (const Job& job : m_jobs) {
    if (!job.finished() && job.request.installation == installation && std::ranges::find(job.request.refs, ref) != job.request.refs.end()) {
      return &job;
    }
  }
  return nullptr;
}

bool etal::TransactionQueue::busy() const { return m_running != 0; }

std::size_t etal::TransactionQueue::pendingCount() const {
  return static_cast<std::size_t>(std::ranges::count_if(m_jobs, [](const Job& job) { return !job.finished(); }));
}

void etal::TransactionQueue::startNext() {
  if (m_running != 0) {
    return;
  }
  const auto next = std::ranges::find(m_jobs, JobState::Queued, &Job::state);
  if (next == m_jobs.end()) {
    return;
  }
  next->state = JobState::Running;
  next->status = "Starting…";
  m_running = next->id;
  if (m_cancellable) {
    g_object_unref(m_cancellable);
  }
  m_cancellable = g_cancellable_new();
  if (m_onChanged) {
    m_onChanged(*next, false);
  }
  std::weak_ptr<Shared> shared = m_shared;
  g_object_ref(m_cancellable);
  std::thread([runner = m_runner, request = next->request, id = next->id, cancellable = m_cancellable, shared]() {
    const ProgressFn progress = [shared, id](const JobProgress& value) {
      post([shared, id, value] {
        if (auto alive = shared.lock(); alive && alive->queue) {
          alive->queue->progress(id, value);
        }
      });
    };
    auto result = runner(request, progress, cancellable);
    g_object_unref(cancellable);
    post([shared, id, result = std::move(result)]() mutable {
      if (auto alive = shared.lock(); alive && alive->queue) {
        alive->queue->finish(id, std::move(result));
      }
    });
  }).detach();
}

void etal::TransactionQueue::progress(std::uint64_t id, const JobProgress& value) {
  Job* job = find(id);
  if (!job || job->state != JobState::Running) {
    return;
  }
  job->fraction = value.fraction;
  if (!value.status.empty() && job->status != "Cancelling…") {
    job->status = value.status;
  }
  if (m_onChanged) {
    m_onChanged(*job, false);
  }
}

void etal::TransactionQueue::finish(std::uint64_t id, std::expected<void, std::string> result) {
  m_running = 0;
  if (Job* job = find(id)) {
    if (result) {
      job->state = JobState::Done;
      job->fraction = 1.0;
      job->status = "Done";
    } else {
      job->state = result.error() == "Cancelled" ? JobState::Cancelled : JobState::Failed;
      job->status = job->state == JobState::Cancelled ? "Cancelled" : "Failed";
      job->error = job->state == JobState::Failed ? result.error() : std::string();
    }
    if (m_onChanged) {
      m_onChanged(*job, true);
    }
  }
  startNext();
}
