#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace runtime_history
{

struct TaskRecord
{
  std::string task_id;
  // Correlation identity carried by the terminal TaskEvent. These are
  // identifiers, not execution evidence, and stay empty when the caller
  // supplied none.
  std::string request_id;
  std::string workflow_id;
  std::string goal_id;
  std::string target_id;
  std::uint8_t action_status;
  std::uint8_t outcome;
  std::uint16_t error_code;
  std::uint64_t duration_ms;
  std::string message;
  std::int64_t completed_at_ns;
};

struct RuntimeStats
{
  bool has_data;
  std::uint64_t sample_count;
  std::array<std::uint64_t, 4> outcome_counts;
  // Requests the gateway never dispatched. They are outside the four action
  // outcomes because no action ever started, and they used to leave no row at
  // all, which biased the success rate upward.
  std::uint64_t rejected_count;
  // Latency for successful runs only. A SAFE_STOP that took 30 s to time out
  // moved p99 more than a fast completion did, so a mixed percentile described
  // neither population.
  std::uint64_t p50_ms;
  std::uint64_t p95_ms;
  std::uint64_t p99_ms;
  std::uint64_t max_ms;
  // The same percentiles over runs that did not succeed, reported separately
  // rather than folded in. has_failure_data is false when there were none.
  bool has_failure_data;
  std::uint64_t failure_count;
  std::uint64_t failure_p50_ms;
  std::uint64_t failure_p95_ms;
  std::uint64_t failure_p99_ms;
  std::uint64_t failure_max_ms;
};

// Durable idempotency record for one admitted gateway request. `fingerprint`
// is the semantic identity of the execution payload, so a retry with a changed
// payload conflicts even after a restart or eviction.
struct IdempotencyRecord
{
  std::string request_id;
  std::string fingerprint;
  std::string state;
  std::uint8_t outcome;
  std::uint16_t error_code;
  std::string message;
  std::int64_t updated_at_ns;
};

class Store
{
public:
  explicit Store(const std::string & path);
  ~Store();

  Store(const Store &) = delete;
  Store & operator=(const Store &) = delete;

  bool insert(const TaskRecord & record);
  std::optional<TaskRecord> get(const std::string & task_id) const;
  RuntimeStats stats() const;

  // Bounds task_runs the way request_idempotency is bounded: rows older than
  // older_than_ns go, but never below keep_at_least rows. The table had no
  // retention at all, so the sorted percentile scan grew without bound.
  std::size_t prune_task_runs(std::int64_t older_than_ns, std::size_t keep_at_least);

  // Idempotency rows survive restart. ADMITTED is written before dispatch so a
  // crash mid-dispatch is recoverable as uncertain instead of a silent re-run.
  bool put_idempotency(const IdempotencyRecord & record);
  std::optional<IdempotencyRecord> get_idempotency(const std::string & request_id) const;
  // Reclaims rows older than the horizon. Rows whose state is not listed in
  // reclaimable_states are always kept: an unresolved request must never age
  // into "never seen" just because later requests arrived.
  std::size_t prune_idempotency(
    std::int64_t older_than_ns, std::size_t keep_at_least,
    const std::vector<std::string> & reclaimable_states);
  // Number of unexpired ledger rows; the admission path uses it to enforce a
  // real bound instead of only limiting what retention keeps.
  std::size_t count_idempotency(std::int64_t not_older_than_ns) const;

private:
  std::optional<TaskRecord> get_unlocked(const std::string & task_id) const;

  sqlite3 * db_{};
  mutable std::mutex mutex_;
};

}  // namespace runtime_history
