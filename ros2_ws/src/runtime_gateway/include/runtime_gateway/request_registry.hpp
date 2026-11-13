#pragma once

#include "runtime_gateway/request_state.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace runtime_gateway
{

struct RequestRecord
{
  std::string request_id;
  std::string task_id;
  std::string target_id;
  std::string workflow_id;
  std::uint64_t timeout_ms;
  RequestState state{RequestState::kSubmitting};
  std::uint8_t outcome;
  std::uint16_t error_code;
  std::string message;
};

// Semantic identity of an execution request: the fields that change what the
// device is asked to do. Transport-only fields (state, outcome, error_code,
// message) are excluded so a retry after a terminal result still matches.
std::string fingerprint(const RequestRecord & record);

enum class InsertResult {INSERTED, DUPLICATE, CONFLICT, INVALID, CAPACITY};

class RequestRegistry
{
public:
  explicit RequestRegistry(std::size_t capacity = 4096);
  InsertResult insert(const RequestRecord & record);
  std::optional<RequestRecord> get_by_request_id(const std::string & request_id) const;
  std::optional<RequestRecord> get_by_task_id(const std::string & task_id) const;
  // One guarded move. `transition` refuses a move the table does not declare,
  // including any second move out of a terminal state; `settle` is the terminal
  // form and is idempotent in the sense that a second call is refused rather than
  // overwriting the first result.
  Transition transition(
    const std::string & request_id, RequestState to, std::uint8_t outcome,
    std::uint16_t error_code, const std::string & message);
  // Convenience for the terminal states: the outcome comes from the table, so a
  // caller cannot settle COMPLETED with a CANCELED outcome.
  Transition settle(
    const std::string & request_id, RequestState to, std::uint16_t error_code,
    const std::string & message);
  // Undoes a reservation that never became an execution. Used when a later
  // admission step rejects the request: leaving it registered would make this
  // payload the identity that future retries of the same request_id compare
  // against, blocking the authoritative one.
  RequestState state_of(const std::string & request_id) const;
  bool erase(const std::string & request_id);
  void clear();
  std::size_t size() const;

private:
  mutable std::mutex mutex_;
  std::size_t capacity_;
  std::deque<std::string> insertion_order_;
  std::unordered_map<std::string, RequestRecord> records_;
};

}  // namespace runtime_gateway
