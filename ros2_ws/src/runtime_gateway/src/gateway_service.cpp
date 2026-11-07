#include "runtime_gateway/gateway_service.hpp"

#include "runtime_gateway/durable_idempotency.hpp"

#include "runtime_history/store.hpp"

#include "robot_task_interfaces/action/execute_workflow.hpp"

#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

namespace runtime_gateway
{
namespace
{

using namespace std::chrono_literals;
using ExecuteWorkflow = robot_task_interfaces::action::ExecuteWorkflow;
using GoalHandle = rclcpp_action::ClientGoalHandle<ExecuteWorkflow>;
constexpr auto kCancelActiveTimeout = 5s;

bool valid(const SubmitWorkflowRequest & request)
{
  constexpr std::uint64_t max_duration_ms =
    static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) * 1000 + 999;
  return !request.request_id().empty() && !request.workflow_id().empty() &&
         !request.task_id().empty() && !request.target_id().empty() &&
         request.timeout_ms() > 0 && request.timeout_ms() <= max_duration_ms;
}

bool allowed(const std::string & workflow_id)
{
  return workflow_id == "single_task" || workflow_id == "ready_then_task";
}

bool active(const RequestRecord & record)
{
  return record.state == "SUBMITTING" || record.state == "RUNNING" ||
         record.state == "CANCELING";
}

void fill_task(const RequestRecord & record, TaskRecord * reply)
{
  reply->set_found(true);
  reply->set_task_id(record.task_id);
  reply->set_target_id(record.target_id);
  reply->set_state(record.state);
  reply->set_outcome(record.outcome);
  reply->set_error_code(record.error_code);
  reply->set_message(record.message);
}

void fill_submit(const RequestRecord & record, SubmitWorkflowReply * reply)
{
  reply->set_accepted(
    record.state == "RUNNING" || record.state == "SUBMITTING" || record.state == "CANCELING");
  reply->set_request_id(record.request_id);
  reply->set_task_id(record.task_id);
  reply->set_state(record.state);
  reply->set_outcome(record.outcome);
  reply->set_error_code(record.error_code);
  reply->set_message(record.message);
}

std::int64_t now_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

// Fills a reply from the durable ledger alone, for retries the in-memory
// registry can no longer hold (restart or eviction at capacity).
void fill_durable(
  const IdempotencyOutcome & known, const SubmitWorkflowRequest & request,
  SubmitWorkflowReply * reply)
{
  reply->set_accepted(false);
  reply->set_request_id(request.request_id());
  reply->set_task_id(request.task_id());
  reply->set_state(known.state);
  reply->set_outcome(known.outcome);
  reply->set_error_code(known.error_code);
  reply->set_message(known.message);
}

std::string state_for_outcome(const std::uint8_t outcome)
{
  switch (outcome) {
    case ExecuteWorkflow::Result::COMPLETED: return "COMPLETED";
    case ExecuteWorkflow::Result::CANCELED: return "CANCELED";
    case ExecuteWorkflow::Result::SAFE_STOP: return "SAFE_STOP";
    case ExecuteWorkflow::Result::DEVICE_FAULT: return "DEVICE_FAULT";
    default: return "ABORTED";
  }
}

}  // namespace

struct GatewayService::State
{
  struct PendingGoal
  {
    GoalHandle::SharedPtr handle;
    // Retained so the terminal result can be attributed to the admitted payload.
    RequestRecord payload;
    bool pending{true};
    bool cancel_requested{false};
    bool cancel_dispatched{false};
    bool cancel_failed{false};
  };

  State(
    rclcpp::Node & node, runtime_history::Store & value,
    std::function<void()> before_send_value,
    std::function<void()> before_cancel_lookup_value,
    std::function<void()> before_cancel_dispatch_value)
  : store(value),
    durable(store),
    action_client(rclcpp_action::create_client<ExecuteWorkflow>(&node, "execute_workflow")),
    before_send(std::move(before_send_value)),
    before_cancel_lookup(std::move(before_cancel_lookup_value)),
    before_cancel_dispatch(std::move(before_cancel_dispatch_value)), logger(node.get_logger())
  {}

  struct ResolveResult
  {
    bool ok{false};
    bool fatal{false};
    std::string state;
    std::string message;
  };

  // Applies an explicit operator decision to an uncertain row. The payload is
  // rebuilt from the stored identity so the fingerprint matches by
  // construction; nothing is dispatched to the device.
  ResolveResult resolve(
    const std::string & request_id, const std::string & state,
    const std::string & message)
  {
    ResolveResult result{false, false, state, message};
    std::optional<runtime_history::IdempotencyRecord> stored;
    try {
      stored = store.get_idempotency(request_id);
    } catch (const std::exception & error) {
      result.fatal = true;
      result.message = error.what();
      return result;
    }
    if (!stored) {
      result.state = "NOT_FOUND";
      result.message = "no durable record for this request_id";
      return result;
    }
    if (is_terminal_state(stored->state)) {
      result.state = stored->state;
      result.message = "request already has a terminal outcome";
      return result;
    }
    try {
      result.ok = durable.resolve_uncertain(
        request_id, stored->fingerprint, state, 0, message, now_ns());
    } catch (const std::exception & error) {
      result.fatal = true;
      result.message = error.what();
      return result;
    }
    if (!result.ok) {
      result.message = "the stored row could not be updated";
      return result;
    }
    {
      const std::lock_guard<std::mutex> lock(mutex);
      registry.update_terminal(request_id, state, 0, 0, message);
    }
    result.state = state;
    return result;
  }

  bool dispatch_cancel(const std::string & request_id, const GoalHandle::SharedPtr & handle)
  {
    {
      const std::lock_guard<std::mutex> lock(mutex);
      const auto goal = goals.find(request_id);
      if (goal == goals.end() || goal->second.cancel_dispatched) {
        return true;
      }
      goal->second.cancel_dispatched = true;
      goal->second.cancel_failed = false;
      ++cancel_dispatches;
    }
    try {
      action_client->async_cancel_goal(handle);
    } catch (const std::exception & error) {
      const std::lock_guard<std::mutex> lock(mutex);
      const auto goal = goals.find(request_id);
      if (goal != goals.end()) {
        goal->second.cancel_dispatched = false;
        goal->second.cancel_failed = true;
        registry.update_terminal(request_id, "CANCEL_FAILED", 0, 500, error.what());
      }
      changed.notify_all();
      return false;
    }
    changed.notify_all();
    return true;
  }

  // Records a terminal rejection in both the cache and the durable ledger, so
  // a rejected request is not admitted again after a restart.
  void reject(
    const RequestRecord & payload, const std::string & state, const std::uint16_t error_code,
    const std::string & message)
  {
    registry.update_terminal(
      payload.request_id, state, 0, error_code, message);
    if (!durable.complete(payload, state, 0, error_code, message, now_ns())) {
      RCLCPP_ERROR(
        logger, "failed to persist durable %s request_id=%s", state.c_str(),
        payload.request_id.c_str());
    }
  }

  runtime_history::Store & store;
  DurableIdempotency durable;
  RequestRegistry registry;
  rclcpp_action::Client<ExecuteWorkflow>::SharedPtr action_client;
  std::function<void()> before_send;
  std::function<void()> before_cancel_lookup;
  std::function<void()> before_cancel_dispatch;
  rclcpp::Logger logger;
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::unordered_map<std::string, PendingGoal> goals;
  bool stopping{false};
  std::size_t cancel_dispatches{0};
};

GatewayService::GatewayService(
  rclcpp::Node & node, runtime_history::Store & store,
  std::function<void()> before_send,
  std::function<void()> before_cancel_lookup,
  std::function<void()> before_cancel_dispatch)
: state_(std::make_shared<State>(
      node, store, std::move(before_send), std::move(before_cancel_lookup),
      std::move(before_cancel_dispatch)))
{}

GatewayService::~GatewayService() = default;

grpc::Status GatewayService::SubmitWorkflow(
  grpc::ServerContext *, const SubmitWorkflowRequest * request, SubmitWorkflowReply * reply)
{
  {
    const std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopping) {
      return {grpc::StatusCode::UNAVAILABLE, "gateway is shutting down"};
    }
  }
  if (!valid(*request)) {
    return {grpc::StatusCode::INVALID_ARGUMENT, "required fields and timeout_ms must be valid"};
  }
  const RequestRecord initial{
    request->request_id(), request->task_id(), request->target_id(), request->workflow_id(),
    request->timeout_ms(), "SUBMITTING", 0, 0, ""};

  // Live duplicates are handled by the in-memory registry so an in-flight
  // retry still reports its real state instead of an unknown one.
  const auto inserted = state_->registry.insert(initial);
  if (inserted == InsertResult::DUPLICATE) {
    fill_submit(*state_->registry.get_by_request_id(request->request_id()), reply);
    return grpc::Status::OK;
  }
  if (inserted == InsertResult::CONFLICT) {
    return {grpc::StatusCode::INVALID_ARGUMENT, "request_id conflicts with an existing request"};
  }
  if (inserted == InsertResult::CAPACITY) {
    // The in-memory cache is full, but the durable ledger can still replay a
    // known request; an unknown one is refused rather than risked.
    const auto known = state_->durable.lookup(request->request_id());
    if (known) {
      fill_durable(*known, *request, reply);
      return grpc::Status::OK;
    }
    return {grpc::StatusCode::RESOURCE_EXHAUSTED, "request registry capacity reached"};
  }

  // A live duplicate already returned above, so reaching this point means a
  // new dispatch. `task_id` is the history primary key: a reused one would
  // silently drop the new terminal record and make GetTask answer with the
  // older run. The in-memory check is bounded and forgets on restart; the
  // recorded row is what actually decides.
  try {
    if (state_->store.get(request->task_id()).has_value()) {
      state_->registry.erase(request->request_id());
      return {grpc::StatusCode::INVALID_ARGUMENT,
        "task_id already belongs to a recorded task"};
    }
  } catch (const std::exception & error) {
    state_->registry.erase(request->request_id());
    return {grpc::StatusCode::UNAVAILABLE, error.what()};
  }

  // The registry is bounded and forgets on restart, so only the durable ledger
  // can stop a replay from reaching the device.
  const auto admission = state_->durable.admit(initial, now_ns());
  if (admission.decision == IdempotencyDecision::UNAVAILABLE) {
    // The reservation must not outlive a failed admission: it carries this
    // payload's fingerprint, and a later retry of the authoritative request
    // would then be compared against it and rejected as a conflict.
    state_->registry.erase(request->request_id());
    return {grpc::StatusCode::UNAVAILABLE, admission.message};
  }
  if (admission.decision == IdempotencyDecision::CONFLICT) {
    state_->registry.erase(request->request_id());
    return {grpc::StatusCode::INVALID_ARGUMENT, "request_id conflicts with a durable request"};
  }
  if (admission.decision == IdempotencyDecision::UNCERTAIN) {
    // A crash between admission and dispatch leaves the outcome unknown: never
    // guess that a redispatch is safe.
    state_->registry.update_terminal(
      request->request_id(), "UNCERTAIN", 0,
      admission.error_code == 0 ? 409 : admission.error_code,
      admission.message.empty() ? "prior dispatch outcome unknown" : admission.message);
    fill_submit(*state_->registry.get_by_request_id(request->request_id()), reply);
    return grpc::Status::OK;
  }
  if (admission.decision == IdempotencyDecision::REPLAY) {
    state_->registry.update_terminal(
      request->request_id(), admission.state, admission.outcome, admission.error_code,
      admission.message);
    fill_submit(*state_->registry.get_by_request_id(request->request_id()), reply);
    return grpc::Status::OK;
  }

  if (!allowed(request->workflow_id())) {
    state_->reject(initial, "REJECTED", 400, "workflow is not allowlisted");
    fill_submit(*state_->registry.get_by_request_id(request->request_id()), reply);
    return grpc::Status::OK;
  }
  if (!state_->action_client->wait_for_action_server(0s)) {
    state_->reject(initial, "REJECTED", 503, "orchestrator unavailable");
    return {grpc::StatusCode::UNAVAILABLE, "orchestrator unavailable"};
  }

  {
    const std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopping) {
      state_->reject(initial, "REJECTED", 503, "gateway is shutting down");
      return {grpc::StatusCode::UNAVAILABLE, "gateway is shutting down"};
    }
    state_->goals.emplace(request->request_id(), State::PendingGoal{nullptr, initial});
  }

  ExecuteWorkflow::Goal goal;
  goal.request_id = request->request_id();
  goal.workflow_id = request->workflow_id();
  goal.task_id = request->task_id();
  goal.target_id = request->target_id();
  goal.allowed_duration.sec = static_cast<std::int32_t>(request->timeout_ms() / 1000);
  goal.allowed_duration.nanosec = static_cast<std::uint32_t>((request->timeout_ms() % 1000) *
    1000000);

  const std::weak_ptr<State> weak_state = state_;
  rclcpp_action::Client<ExecuteWorkflow>::SendGoalOptions options;
  options.goal_response_callback =
    [weak_state, request_id = request->request_id()](const GoalHandle::SharedPtr handle) {
      if (const auto state = weak_state.lock()) {
        bool cancel = false;
        if (handle) {
          {
            const std::lock_guard<std::mutex> lock(state->mutex);
            const auto goal = state->goals.find(request_id);
            if (goal == state->goals.end()) {
              return;
            }
            goal->second.pending = false;
            goal->second.handle = handle;
            cancel = state->stopping || goal->second.cancel_requested;
            state->registry.update_terminal(
              request_id, cancel ? "CANCELING" : "RUNNING", 0, 0,
              cancel ? "cancel requested" : "accepted");
          }
          state->changed.notify_all();
          if (cancel) {
            state->dispatch_cancel(request_id, handle);
          }
        } else {
          RequestRecord payload;
          bool rejected = false;
          {
            const std::lock_guard<std::mutex> lock(state->mutex);
            const auto goal = state->goals.find(request_id);
            if (goal != state->goals.end()) {
              goal->second.pending = false;
              payload = goal->second.payload;
              state->goals.erase(goal);
              rejected = true;
            }
          }
          if (rejected && !payload.request_id.empty()) {
            // A rejection is terminal. Persisting only the in-memory copy made a
            // restarted gateway see the request as UNCERTAIN instead of refused.
            if (!state->durable.complete(
                payload, "REJECTED", 0, 400, "goal rejected", now_ns()))
            {
              RCLCPP_ERROR(
                state->logger, "failed to persist durable rejection request_id=%s",
                request_id.c_str());
            }
            const std::lock_guard<std::mutex> lock(state->mutex);
            state->registry.update_terminal(request_id, "REJECTED", 0, 400, "goal rejected");
          }
          state->changed.notify_all();
        }
      }
    };
  options.result_callback =
    [weak_state, request_id = request->request_id()](const GoalHandle::WrappedResult & result) {
      if (const auto state = weak_state.lock()) {
        std::uint8_t outcome = ExecuteWorkflow::Result::SAFE_STOP;
        std::uint16_t error_code = 500;
        std::string message = "workflow result unavailable";
        if (result.result) {
          outcome = result.result->outcome;
          error_code = result.result->error_code;
          message = result.result->message;
        }
        RequestRecord payload;
        {
          const std::lock_guard<std::mutex> lock(state->mutex);
          state->registry.update_terminal(
            request_id, state_for_outcome(outcome), outcome, error_code, message);
          // Capture the admitted payload before the goal is erased.
          const auto goal = state->goals.find(request_id);
          if (goal != state->goals.end()) {
            payload = goal->second.payload;
          }
          state->goals.erase(request_id);
        }
        // Durable terminal result: the next retry replays it without dispatch.
        const auto terminal = payload.request_id.empty() ? false : state->durable.complete(
          payload, state_for_outcome(outcome), outcome, error_code, message, now_ns());
        if (!terminal) {
          RCLCPP_ERROR(
            state->logger, "failed to persist durable terminal result request_id=%s",
            request_id.c_str());
        }
        state->changed.notify_all();
      }
    };

  std::shared_future<GoalHandle::SharedPtr> future;
  try {
    if (state_->before_send) {
      state_->before_send();
    }
    RCLCPP_INFO(
      state_->logger, "Dispatching ExecuteWorkflow Goal request_id=%s task_id=%s",
      request->request_id().c_str(), request->task_id().c_str());
    future = state_->action_client->async_send_goal(goal, options);
  } catch (const std::exception & error) {
    {
      const std::lock_guard<std::mutex> lock(state_->mutex);
      state_->goals.erase(request->request_id());
      state_->registry.update_terminal(request->request_id(), "REJECTED", 0, 503, error.what());
    }
    // The goal never reached the orchestrator, so this rejection is final.
    state_->durable.complete(initial, "REJECTED", 0, 503, error.what(), now_ns());
    state_->changed.notify_all();
    return {grpc::StatusCode::UNAVAILABLE, error.what()};
  }
  if (future.wait_for(500ms) != std::future_status::ready) {
    GoalHandle::SharedPtr late_handle;
    {
      const std::lock_guard<std::mutex> lock(state_->mutex);
      const auto pending = state_->goals.find(request->request_id());
      if (pending != state_->goals.end()) {
        const bool already_canceling = pending->second.cancel_requested;
        pending->second.cancel_requested = true;
        late_handle = pending->second.handle;
        if (!already_canceling) {
          state_->registry.update_terminal(
            request->request_id(), "TIMED_OUT", 0, 504, "goal response exceeded 500ms");
        }
      }
    }
    if (late_handle) {
      state_->dispatch_cancel(request->request_id(), late_handle);
    }
    return {grpc::StatusCode::DEADLINE_EXCEEDED, "goal response exceeded 500ms"};
  }
  const auto handle = future.get();
  {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->changed.wait_for(
      lock, 50ms, [&]() {
        const auto pending = state_->goals.find(request->request_id());
        return pending == state_->goals.end() || !pending->second.pending;
      });
  }
  const auto current = state_->registry.get_by_request_id(request->request_id()).value();
  fill_submit(current, reply);
  reply->set_accepted(handle != nullptr);
  if (!handle) {
    reply->set_state("REJECTED");
    reply->set_error_code(400);
    reply->set_message("goal rejected");
  }
  return grpc::Status::OK;
}

grpc::Status GatewayService::CancelWorkflow(
  grpc::ServerContext *, const CancelWorkflowRequest * request, CancelWorkflowReply * reply)
{
  if (request->request_id().empty()) {
    return {grpc::StatusCode::INVALID_ARGUMENT, "request_id is required"};
  }
  const auto record = state_->registry.get_by_request_id(request->request_id());
  if (!record) {
    reply->set_state("NOT_FOUND");
    reply->set_message("request not found");
    return grpc::Status::OK;
  }
  if (state_->before_cancel_lookup) {
    state_->before_cancel_lookup();
  }
  GoalHandle::SharedPtr handle;
  {
    const std::lock_guard<std::mutex> lock(state_->mutex);
    const auto value = state_->goals.find(request->request_id());
    if (value != state_->goals.end()) {
      value->second.cancel_requested = true;
      handle = value->second.handle;
      state_->registry.update_terminal(
        request->request_id(), "CANCELING", record->outcome, record->error_code,
        "cancel requested");
    }
  }
  if (!handle) {
    const auto latest = state_->registry.get_by_request_id(request->request_id());
    if (!latest) {
      reply->set_state("NOT_FOUND");
      reply->set_message("request not found");
      return grpc::Status::OK;
    }
    if (latest->state != "CANCELING") {
      reply->set_state(latest->state);
      reply->set_outcome(latest->outcome);
      reply->set_error_code(latest->error_code);
      reply->set_message(latest->message);
      return grpc::Status::OK;
    }
  }
  if (handle) {
    if (state_->before_cancel_dispatch) {
      state_->before_cancel_dispatch();
    }
    if (!state_->dispatch_cancel(request->request_id(), handle)) {
      return {grpc::StatusCode::INTERNAL, "failed to dispatch workflow cancel"};
    }
    const auto latest = state_->registry.get_by_request_id(request->request_id());
    if (latest && !active(*latest)) {
      reply->set_state(latest->state);
      reply->set_outcome(latest->outcome);
      reply->set_error_code(latest->error_code);
      reply->set_message(latest->message);
      return grpc::Status::OK;
    }
  }
  reply->set_accepted(true);
  reply->set_state("CANCELING");
  reply->set_message("cancel requested");
  return grpc::Status::OK;
}

grpc::Status GatewayService::GetTask(
  grpc::ServerContext *, const GetTaskRequest * request, TaskRecord * reply)
{
  if (request->task_id().empty()) {
    return {grpc::StatusCode::INVALID_ARGUMENT, "task_id is required"};
  }
  const auto memory = state_->registry.get_by_task_id(request->task_id());
  if (memory && active(*memory)) {
    fill_task(*memory, reply);
    return grpc::Status::OK;
  }
  try {
    const auto stored = state_->store.get(request->task_id());
    if (!stored) {
      if (memory) {
        fill_task(*memory, reply);
        return grpc::Status::OK;
      }
      reply->set_task_id(request->task_id());
      reply->set_state("NOT_FOUND");
      return grpc::Status::OK;
    }
    reply->set_found(true);
    reply->set_task_id(stored->task_id);
    reply->set_request_id(stored->request_id);
    reply->set_workflow_id(stored->workflow_id);
    reply->set_goal_id(stored->goal_id);
    reply->set_target_id(stored->target_id);
    reply->set_state(state_for_outcome(stored->outcome));
    reply->set_action_status(stored->action_status);
    reply->set_outcome(stored->outcome);
    reply->set_error_code(stored->error_code);
    reply->set_duration_ms(stored->duration_ms);
    reply->set_message(stored->message);
    reply->set_completed_at_ns(stored->completed_at_ns);
    return grpc::Status::OK;
  } catch (const std::exception & error) {
    return {grpc::StatusCode::INTERNAL, error.what()};
  }
}

grpc::Status GatewayService::ResolveUncertain(
  grpc::ServerContext *, const ResolveUncertainRequest * request,
  ResolveUncertainReply * reply)
{
  if (request->request_id().empty()) {
    return {grpc::StatusCode::INVALID_ARGUMENT, "request_id is required"};
  }
  if (!is_terminal_state(request->state())) {
    return {grpc::StatusCode::INVALID_ARGUMENT,
      "state must be a terminal outcome: COMPLETED, CANCELED, SAFE_STOP, "
      "DEVICE_FAULT, ABORTED or REJECTED"};
  }

  // The ledger is the authority. A missing row, an already-terminal row and a
  // write that did not apply are reported distinctly instead of collapsing
  // into one failure, and nothing is redispatched to the device.
  const auto resolved = state_->resolve(
    request->request_id(), request->state(), request->message());
  if (resolved.fatal) {
    return {grpc::StatusCode::UNAVAILABLE, resolved.message};
  }
  reply->set_resolved(resolved.ok);
  reply->set_state(resolved.state);
  reply->set_message(resolved.message);
  return grpc::Status::OK;
}

grpc::Status GatewayService::GetStats(
  grpc::ServerContext *, const GetStatsRequest *, RuntimeStats * reply)
{
  try {
    const auto stats = state_->store.stats();
    reply->set_has_data(stats.has_data);
    reply->set_sample_count(stats.sample_count);
    for (const auto count : stats.outcome_counts) {
      reply->add_outcome_counts(count);
    }
    reply->set_p50_ms(stats.p50_ms);
    reply->set_p95_ms(stats.p95_ms);
    reply->set_p99_ms(stats.p99_ms);
    reply->set_max_ms(stats.max_ms);
    reply->set_state(stats.has_data ? "AVAILABLE" : "EMPTY");
    return grpc::Status::OK;
  } catch (const std::exception & error) {
    return {grpc::StatusCode::INTERNAL, error.what()};
  }
}

void GatewayService::stop_accepting()
{
  const std::lock_guard<std::mutex> lock(state_->mutex);
  state_->stopping = true;
}

void GatewayService::cancel_active()
{
  std::vector<std::pair<std::string, GoalHandle::SharedPtr>> handles;
  {
    const std::lock_guard<std::mutex> lock(state_->mutex);
    state_->stopping = true;
    for (auto & [request_id, goal] : state_->goals) {
      goal.cancel_requested = true;
      state_->registry.update_terminal(request_id, "CANCELING", 0, 0, "shutdown cancel requested");
      if (goal.handle && !goal.cancel_dispatched) {
        handles.emplace_back(request_id, goal.handle);
      }
    }
  }
  for (const auto & [request_id, handle] : handles) {
    (void)state_->dispatch_cancel(request_id, handle);
  }
  std::unique_lock<std::mutex> lock(state_->mutex);
  const bool drained = state_->changed.wait_for(
    lock, kCancelActiveTimeout, [this]() {
      for (const auto & [request_id, goal] : state_->goals) {
        (void)request_id;
        if (!goal.cancel_failed) {
          return false;
        }
      }
      return true;
    });
  if (!drained) {
    std::string request_ids;
    for (const auto & [request_id, goal] : state_->goals) {
      (void)goal;
      if (!request_ids.empty()) {
        request_ids += ",";
      }
      request_ids += request_id;
    }
    lock.unlock();
    throw std::runtime_error("timed out waiting for workflow cancellation: " + request_ids);
  }
  for (const auto & [request_id, goal] : state_->goals) {
    if (goal.cancel_failed) {
      throw std::runtime_error("failed to dispatch cancel for request " + request_id);
    }
  }
}

void GatewayService::clear()
{
  state_->registry.clear();
}

std::size_t GatewayService::active_request_count() const {return state_->registry.size();}

std::size_t GatewayService::cancel_dispatch_count() const
{
  const std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->cancel_dispatches;
}

}  // namespace runtime_gateway
