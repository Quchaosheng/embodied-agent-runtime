#ifndef RUNTIME_GATEWAY__REQUEST_REGISTRY_HPP_
#define RUNTIME_GATEWAY__REQUEST_REGISTRY_HPP_

#include <cstdint>
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
  std::string workflow_id;
  std::string state;
  std::uint8_t outcome;
  std::uint16_t error_code;
  std::string message;
};

enum class InsertResult {INSERTED, DUPLICATE, CONFLICT, INVALID};

class RequestRegistry
{
public:
  InsertResult insert(const RequestRecord & record);
  std::optional<RequestRecord> get_by_request_id(const std::string & request_id) const;
  std::optional<RequestRecord> get_by_task_id(const std::string & task_id) const;
  bool update_terminal(
    const std::string & request_id, const std::string & state, std::uint8_t outcome,
    std::uint16_t error_code, const std::string & message);
  void clear();
  std::size_t size() const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, RequestRecord> records_;
};

}  // namespace runtime_gateway

#endif  // RUNTIME_GATEWAY__REQUEST_REGISTRY_HPP_
