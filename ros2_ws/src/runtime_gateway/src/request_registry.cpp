#include "runtime_gateway/request_registry.hpp"

namespace runtime_gateway
{

InsertResult RequestRegistry::insert(const RequestRecord & record)
{
  if (record.request_id.empty() || record.task_id.empty()) {
    return InsertResult::INVALID;
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto existing = records_.find(record.request_id);
  if (existing != records_.end()) {
    return existing->second.task_id == record.task_id &&
           existing->second.workflow_id == record.workflow_id ?
           InsertResult::DUPLICATE : InsertResult::CONFLICT;
  }
  for (const auto & [request_id, value] : records_) {
    (void)request_id;
    if (value.task_id == record.task_id) {
      return InsertResult::CONFLICT;
    }
  }
  records_.emplace(record.request_id, record);
  return InsertResult::INSERTED;
}

std::optional<RequestRecord> RequestRegistry::get_by_request_id(
  const std::string & request_id) const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto value = records_.find(request_id);
  return value == records_.end() ? std::nullopt : std::optional<RequestRecord>{value->second};
}

std::optional<RequestRecord> RequestRegistry::get_by_task_id(const std::string & task_id) const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  for (const auto & [request_id, record] : records_) {
    (void)request_id;
    if (record.task_id == task_id) {
      return record;
    }
  }
  return std::nullopt;
}

bool RequestRegistry::update_terminal(
  const std::string & request_id, const std::string & state, const std::uint8_t outcome,
  const std::uint16_t error_code, const std::string & message)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto value = records_.find(request_id);
  if (value == records_.end()) {
    return false;
  }
  value->second.state = state;
  value->second.outcome = outcome;
  value->second.error_code = error_code;
  value->second.message = message;
  return true;
}

void RequestRegistry::clear()
{
  const std::lock_guard<std::mutex> lock(mutex_);
  records_.clear();
}

std::size_t RequestRegistry::size() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return records_.size();
}

}  // namespace runtime_gateway
