#include "runtime_history/store.hpp"

#include <sqlite3.h>

#include <limits>
#include <stdexcept>
#include <vector>

namespace runtime_history
{
namespace
{

constexpr char kSchema[] =
  "CREATE TABLE IF NOT EXISTS task_runs ("
  "task_id TEXT PRIMARY KEY,"
  "target_id TEXT NOT NULL,"
  "action_status INTEGER NOT NULL,"
  "outcome INTEGER NOT NULL,"
  "error_code INTEGER NOT NULL,"
  "duration_ms INTEGER NOT NULL,"
  "message TEXT NOT NULL,"
  "completed_at_ns INTEGER NOT NULL)";

class Statement
{
public:
  Statement(sqlite3 * db, const char * sql)
  {
    if (sqlite3_prepare_v2(db, sql, -1, &value_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(sqlite3_errmsg(db));
    }
  }

  ~Statement() {sqlite3_finalize(value_);}
  sqlite3_stmt * get() const {return value_;}

private:
  sqlite3_stmt * value_{};
};

void check(sqlite3 * db, int result)
{
  if (result != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }
}

void execute(sqlite3 * db, const char * sql)
{
  char * error{};
  if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error == nullptr ? sqlite3_errmsg(db) : error;
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

void bind_text(sqlite3 * db, sqlite3_stmt * statement, int index, const std::string & value)
{
  check(
    db, sqlite3_bind_text(
      statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

std::string enable_wal(sqlite3 * db)
{
  Statement statement(db, "PRAGMA journal_mode=WAL");
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    throw std::runtime_error(sqlite3_errmsg(db));
  }
  const auto * value = sqlite3_column_text(statement.get(), 0);
  return value == nullptr ? std::string{} :
         std::string(reinterpret_cast<const char *>(value));
}

bool same(const TaskRecord & left, const TaskRecord & right)
{
  return left.task_id == right.task_id && left.target_id == right.target_id &&
         left.action_status == right.action_status && left.outcome == right.outcome &&
         left.error_code == right.error_code && left.duration_ms == right.duration_ms &&
         left.message == right.message && left.completed_at_ns == right.completed_at_ns;
}

std::uint64_t nearest_rank(
  const std::vector<std::uint64_t> & sorted, std::uint64_t percent)
{
  const auto index = (percent * sorted.size() + 99) / 100 - 1;
  return sorted.at(index);
}

}  // namespace

Store::Store(const std::string & path)
{
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
    const std::string message = db_ ==
      nullptr ? "failed to open SQLite database" : sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(message);
  }

  try {
    execute(db_, "PRAGMA busy_timeout=1000");
    if (enable_wal(db_) != "wal") {
      throw std::runtime_error("SQLite database does not support WAL mode");
    }
    execute(db_, kSchema);
  } catch (...) {
    sqlite3_close(db_);
    db_ = nullptr;
    throw;
  }
}

Store::~Store()
{
  sqlite3_close(db_);
}

bool Store::insert(const TaskRecord & record)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  if (record.duration_ms > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())) {
    return false;
  }

  try {
    execute(db_, "BEGIN IMMEDIATE");
    Statement statement(
      db_,
      "INSERT OR IGNORE INTO task_runs ("
      "task_id,target_id,action_status,outcome,error_code,duration_ms,message,completed_at_ns) "
      "VALUES (?,?,?,?,?,?,?,?)");
    auto * value = statement.get();
    bind_text(db_, value, 1, record.task_id);
    bind_text(db_, value, 2, record.target_id);
    check(db_, sqlite3_bind_int(value, 3, record.action_status));
    check(db_, sqlite3_bind_int(value, 4, record.outcome));
    check(db_, sqlite3_bind_int(value, 5, record.error_code));
    check(db_, sqlite3_bind_int64(value, 6, static_cast<sqlite3_int64>(record.duration_ms)));
    bind_text(db_, value, 7, record.message);
    check(db_, sqlite3_bind_int64(value, 8, record.completed_at_ns));
    if (sqlite3_step(value) != SQLITE_DONE) {
      execute(db_, "ROLLBACK");
      return false;
    }

    const bool inserted = sqlite3_changes(db_) == 1;
    const auto existing = inserted ? std::optional<TaskRecord>{} : get_unlocked(record.task_id);
    const bool accepted = inserted || (existing.has_value() && same(*existing, record));
    execute(db_, "COMMIT");
    return accepted;
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
  }
}

std::optional<TaskRecord> Store::get(const std::string & task_id) const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return get_unlocked(task_id);
}

std::optional<TaskRecord> Store::get_unlocked(const std::string & task_id) const
{
  Statement statement(
    db_,
    "SELECT task_id,target_id,action_status,outcome,error_code,duration_ms,message,completed_at_ns "
    "FROM task_runs WHERE task_id=?");
  auto * value = statement.get();
  bind_text(db_, value, 1, task_id);
  const int result = sqlite3_step(value);
  if (result == SQLITE_DONE) {
    return std::nullopt;
  }
  if (result != SQLITE_ROW) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }

  const auto text = [value](int column) {
      const auto * result = sqlite3_column_text(value, column);
      return result == nullptr ? std::string{} :
             std::string(reinterpret_cast<const char *>(result));
    };
  return TaskRecord{
    text(0), text(1), static_cast<std::uint8_t>(sqlite3_column_int(value, 2)),
    static_cast<std::uint8_t>(sqlite3_column_int(value, 3)),
    static_cast<std::uint16_t>(sqlite3_column_int(value, 4)),
    static_cast<std::uint64_t>(sqlite3_column_int64(value, 5)), text(6),
    sqlite3_column_int64(value, 7)};
}

RuntimeStats Store::stats() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  RuntimeStats result{};
  std::vector<std::uint64_t> durations;
  Statement statement(db_, "SELECT duration_ms,outcome FROM task_runs ORDER BY duration_ms");
  while (true) {
    const int step_result = sqlite3_step(statement.get());
    if (step_result == SQLITE_DONE) {
      break;
    }
    if (step_result != SQLITE_ROW) {
      throw std::runtime_error(sqlite3_errmsg(db_));
    }
    durations.push_back(static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 0)));
    const auto outcome = sqlite3_column_int(statement.get(), 1);
    if (outcome >= 0 && outcome < static_cast<int>(result.outcome_counts.size())) {
      ++result.outcome_counts[static_cast<std::size_t>(outcome)];
    }
  }
  if (durations.empty()) {
    return result;
  }

  result.has_data = true;
  result.sample_count = durations.size();
  result.p50_ms = nearest_rank(durations, 50);
  result.p95_ms = nearest_rank(durations, 95);
  result.p99_ms = nearest_rank(durations, 99);
  result.max_ms = durations.back();
  return result;
}

}  // namespace runtime_history
