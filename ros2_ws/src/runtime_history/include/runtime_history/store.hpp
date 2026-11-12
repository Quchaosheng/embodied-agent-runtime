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
  std::uint64_t p50_ms;
  std::uint64_t p95_ms;
  std::uint64_t p99_ms;
  std::uint64_t max_ms;
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

  // The file this store was opened on, so a read-only Reader can be pointed at
  // the same one instead of trusting a second copy of the path.
  const std::string & path() const {return path_;}

  bool insert(const TaskRecord & record);
  std::optional<TaskRecord> get(const std::string & task_id) const;
  RuntimeStats stats() const;

  // Opens a second read-only connection to the same file, for queries whose
  // cost grows with the table. WAL lets a reader run while the writer commits,
  // so a large statistics query stops holding the lock admission needs.
  // Throws when the path cannot be opened: stats over a silent empty database
  // would be worse than a failed call.
  class Reader
  {
public:
    explicit Reader(const std::string & path);
    ~Reader();

    Reader(const Reader &) = delete;
    Reader & operator=(const Reader &) = delete;

    RuntimeStats stats() const;

private:
    sqlite3 * db_{};
    mutable std::mutex mutex_;
  };

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

  std::string path_;
  sqlite3 * db_{};
  mutable std::mutex mutex_;
};

}  // namespace runtime_history
