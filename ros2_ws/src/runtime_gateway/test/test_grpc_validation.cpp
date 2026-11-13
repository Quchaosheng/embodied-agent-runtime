#include "runtime_gateway/durable_idempotency.hpp"
#include "runtime_gateway/gateway_service.hpp"

#include "runtime_history/store.hpp"

#include <filesystem>
#include <fstream>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

namespace
{

class GrpcValidationTest : public testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    int argc = 1;
    char name[] = "test_grpc_validation";
    char * argv[] = {name, nullptr};
    rclcpp::init(argc, argv);
  }

  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    path_ = (std::filesystem::temp_directory_path() /
      ("runtime_gateway_" + std::to_string(counter_++) + ".sqlite3")).string();
    std::filesystem::remove(path_);
    store_ = std::make_unique<runtime_history::Store>(path_);
    node_ = std::make_shared<rclcpp::Node>("grpc_validation_test");
    service_ = std::make_unique<runtime_gateway::GatewayService>(*node_, *store_);

    grpc::ServerBuilder builder;
    builder.RegisterService(service_.get());
    int port{};
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    server_ = builder.BuildAndStart();
    ASSERT_NE(server_, nullptr);
    ASSERT_GT(port, 0);
    address_ = "127.0.0.1:" + std::to_string(port);
    stub_ = runtime_gateway::RobotRuntime::NewStub(
      grpc::CreateChannel(
        address_,
        grpc::InsecureChannelCredentials()));
  }

  void TearDown() override
  {
    service_->stop_accepting();
    service_->cancel_active();
    server_->Shutdown();
    server_->Wait();
    service_->clear();
    stub_.reset();
    service_.reset();
    node_.reset();
    store_.reset();
    std::filesystem::remove(path_);
    std::filesystem::remove(path_ + "-wal");
    std::filesystem::remove(path_ + "-shm");
  }

  runtime_gateway::SubmitWorkflowRequest valid_request() const
  {
    runtime_gateway::SubmitWorkflowRequest request;
    request.set_request_id("request-1");
    request.set_workflow_id("single_task");
    request.set_task_id("task-1");
    request.set_target_id("dock_a");
    request.set_timeout_ms(3000);
    return request;
  }

  static inline unsigned counter_{};
  std::string path_;
  std::string address_;
  std::shared_ptr<rclcpp::Node> node_;
  std::unique_ptr<runtime_history::Store> store_;
  std::unique_ptr<runtime_gateway::GatewayService> service_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<runtime_gateway::RobotRuntime::Stub> stub_;
};

TEST_F(GrpcValidationTest, RejectsMissingRequiredFieldAsInvalidArgument)
{
  auto request = valid_request();
  request.clear_target_id();
  runtime_gateway::SubmitWorkflowReply reply;
  grpc::ClientContext context;
  const auto status = stub_->SubmitWorkflow(&context, request, &reply);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(service_->active_request_count(), 0U);
}

// A request rejected at the gateway never reaches the executor, so it produced no
// TaskEvent and left no row: the success rate was computed over
// successful-to-start runs only. It now leaves a history row.
TEST_F(GrpcValidationTest, RejectedRequestIsCountedInHistoryAndStats)
{
  auto request = valid_request();
  request.set_workflow_id("uploaded_xml");
  runtime_gateway::SubmitWorkflowReply reply;
  grpc::ClientContext context;
  ASSERT_TRUE(stub_->SubmitWorkflow(&context, request, &reply).ok());
  EXPECT_EQ(reply.state(), "REJECTED");
  EXPECT_EQ(reply.error_layer(), 4U);  // gateway

  // The row exists, with the rejection's own error code.
  runtime_gateway::GetTaskRequest get_request;
  get_request.set_task_id("task-1");
  runtime_gateway::TaskRecord task;
  grpc::ClientContext get_context;
  ASSERT_TRUE(stub_->GetTask(&get_context, get_request, &task).ok());
  EXPECT_TRUE(task.found());
  EXPECT_EQ(task.error_code(), 400U);

  // And it is counted, which is what makes the success rate honest.
  runtime_gateway::GetStatsRequest stats_request;
  runtime_gateway::RuntimeStats stats;
  grpc::ClientContext stats_context;
  ASSERT_TRUE(stub_->GetStats(&stats_context, stats_request, &stats).ok());
  EXPECT_EQ(stats.sample_count(), 1U);
  // Counted as its own population: no action ever started, so it is not one of
  // the four action outcomes, and before this it was not counted at all.
  EXPECT_EQ(stats.rejected_count(), 1U);
}

TEST_F(GrpcValidationTest, RejectsUnknownWorkflowWithoutActionSubmission)
{
  auto request = valid_request();
  request.set_workflow_id("uploaded_xml");
  runtime_gateway::SubmitWorkflowReply reply;
  grpc::ClientContext context;
  const auto status = stub_->SubmitWorkflow(&context, request, &reply);
  EXPECT_TRUE(status.ok());
  EXPECT_FALSE(reply.accepted());
  EXPECT_EQ(reply.state(), "REJECTED");

  runtime_gateway::GetTaskRequest get_request;
  get_request.set_task_id("task-1");
  runtime_gateway::TaskRecord task;
  grpc::ClientContext get_context;
  ASSERT_TRUE(stub_->GetTask(&get_context, get_request, &task).ok());
  EXPECT_TRUE(task.found());
  EXPECT_EQ(task.state(), "REJECTED");
}

// The in-memory task_id check forgets on restart; the history primary key does
// not, so reuse must be refused before dispatch instead of silently dropping
// the new terminal record.
TEST_F(GrpcValidationTest, RefusesATaskIdThatAlreadyHasARecordedRun)
{
  const auto request = valid_request();
  ASSERT_TRUE(
    store_->insert(
      {"task-1", "request-old", "single_task", "goal-old", "dock_a", 1, 0, 0, 5, "old", 1}));

  runtime_gateway::SubmitWorkflowReply reply;
  grpc::ClientContext context;
  const auto status = stub_->SubmitWorkflow(&context, request, &reply);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(service_->active_request_count(), 0U);

  // The earlier run is still the one GetTask answers with.
  runtime_gateway::GetTaskRequest get_request;
  get_request.set_task_id("task-1");
  runtime_gateway::TaskRecord task;
  grpc::ClientContext get_context;
  ASSERT_TRUE(stub_->GetTask(&get_context, get_request, &task).ok());
  EXPECT_EQ(task.message(), "old");
}

// A brand-new task_id is admitted, so the check above is not a blanket refusal.
TEST_F(GrpcValidationTest, AcceptsATaskIdWithNoRecordedRun)
{
  const auto request = valid_request();
  runtime_gateway::SubmitWorkflowReply reply;
  grpc::ClientContext context;
  const auto status = stub_->SubmitWorkflow(&context, request, &reply);
  // No orchestrator here, so admission fails later, but not on the task_id.
  EXPECT_NE(status.error_message(), "task_id already belongs to a recorded task");
}

// A rejected conflict must not stay registered: the reservation carries the
// conflicting payload, and a later authoritative retry would be compared
// against it and refused before the durable replay is ever consulted.
TEST_F(GrpcValidationTest, ConflictingReservationDoesNotBlockTheAuthoritativeRetry)
{
  const auto request = valid_request();
  {
    runtime_gateway::DurableIdempotency ledger(*store_);
    runtime_gateway::RequestRecord admitted{
      request.request_id(), request.task_id(), request.target_id(), request.workflow_id(),
      request.timeout_ms(), runtime_gateway::RequestState::kSubmitting, 0, 0, ""};
    // Seed the authoritative identity, then a different payload under the same
    // request_id after the "restart" represented by a fresh registry.
    ASSERT_EQ(
      ledger.admit(admitted, 1).decision,
      runtime_gateway::IdempotencyDecision::PROCEED);
    ASSERT_TRUE(ledger.complete(admitted, "COMPLETED", 0, 0, "done", 2));
  }

  auto conflicting = request;
  conflicting.set_target_id("other_target");
  runtime_gateway::SubmitWorkflowReply conflict_reply;
  grpc::ClientContext conflict_context;
  stub_->SubmitWorkflow(&conflict_context, conflicting, &conflict_reply);
  EXPECT_FALSE(conflict_reply.accepted());

  // The original request must still replay its stored result.
  runtime_gateway::SubmitWorkflowReply replay;
  grpc::ClientContext context;
  const auto status = stub_->SubmitWorkflow(&context, request, &replay);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(replay.state(), "COMPLETED");
}

// The resolve RPC is the only supported way to clear an unknown outcome after
// a restart; it must never dispatch anything.
TEST_F(GrpcValidationTest, ResolvesAnUncertainRequestWithoutDispatching)
{
  {
    runtime_gateway::DurableIdempotency ledger(*store_);
    runtime_gateway::RequestRecord admitted{
      "request-1", "task-1", "dock_a", "single_task", 3000,
      runtime_gateway::RequestState::kSubmitting, 0, 0, ""};
    ASSERT_EQ(
      ledger.admit(admitted, 1).decision,
      runtime_gateway::IdempotencyDecision::PROCEED);
  }

  runtime_gateway::ResolveUncertainRequest request;
  request.set_request_id("request-1");
  request.set_state("ABORTED");
  request.set_message("operator confirmed the robot did not move");
  runtime_gateway::ResolveUncertainReply reply;
  grpc::ClientContext context;
  ASSERT_TRUE(stub_->ResolveUncertain(&context, request, &reply).ok());
  EXPECT_TRUE(reply.resolved());
  EXPECT_EQ(reply.state(), "ABORTED");

  // The row is terminal now, and a retry replays instead of re-dispatching.
  const auto stored = store_->get_idempotency("request-1");
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->state, "ABORTED");
  EXPECT_EQ(service_->active_request_count(), 0U);
}

TEST_F(GrpcValidationTest, ResolveRejectsANonTerminalStateAndUnknownId)
{
  {
    runtime_gateway::DurableIdempotency ledger(*store_);
    runtime_gateway::RequestRecord admitted{
      "request-1", "task-1", "dock_a", "single_task", 3000,
      runtime_gateway::RequestState::kSubmitting, 0, 0, ""};
    ASSERT_EQ(
      ledger.admit(admitted, 1).decision,
      runtime_gateway::IdempotencyDecision::PROCEED);
  }

  runtime_gateway::ResolveUncertainRequest non_terminal;
  non_terminal.set_request_id("request-1");
  non_terminal.set_state("RUNNING");
  runtime_gateway::ResolveUncertainReply non_terminal_reply;
  grpc::ClientContext non_terminal_context;
  EXPECT_EQ(
    stub_->ResolveUncertain(&non_terminal_context, non_terminal, &non_terminal_reply)
    .error_code(),
    grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(store_->get_idempotency("request-1")->state, "ADMITTED");

  runtime_gateway::ResolveUncertainRequest unknown;
  unknown.set_request_id("never-seen");
  unknown.set_state("ABORTED");
  runtime_gateway::ResolveUncertainReply unknown_reply;
  grpc::ClientContext unknown_context;
  ASSERT_TRUE(stub_->ResolveUncertain(&unknown_context, unknown, &unknown_reply).ok());
  EXPECT_FALSE(unknown_reply.resolved());
  EXPECT_EQ(unknown_reply.state(), "NOT_FOUND");
}

TEST_F(GrpcValidationTest, ReportsUnavailableWhenOrchestratorIsMissing)
{
  const auto request = valid_request();
  runtime_gateway::SubmitWorkflowReply reply;
  grpc::ClientContext context;
  const auto status = stub_->SubmitWorkflow(&context, request, &reply);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
}

TEST_F(GrpcValidationTest, ReportsNoDataForEmptySqliteStats)
{
  runtime_gateway::GetStatsRequest request;
  runtime_gateway::RuntimeStats reply;
  grpc::ClientContext context;
  const auto status = stub_->GetStats(&context, request, &reply);
  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_FALSE(reply.has_data());
  EXPECT_EQ(reply.sample_count(), 0U);
  ASSERT_EQ(reply.outcome_counts_size(), 4);
}

TEST_F(GrpcValidationTest, MapsNonemptySqliteStatistics)
{
  const std::array<std::uint64_t, 5> durations{10, 20, 30, 40, 100};
  for (std::size_t index = 0; index < durations.size(); ++index) {
    ASSERT_TRUE(
      store_->insert(
      {
        "stats-" + std::to_string(index), "", "", "", "dock_a", 1,
        static_cast<std::uint8_t>(index % 4), 0, durations[index], "done",
        static_cast<std::int64_t>(index)}));
  }
  runtime_gateway::GetStatsRequest request;
  runtime_gateway::RuntimeStats reply;
  grpc::ClientContext context;
  ASSERT_TRUE(stub_->GetStats(&context, request, &reply).ok());
  EXPECT_TRUE(reply.has_data());
  EXPECT_EQ(reply.sample_count(), 5U);
  ASSERT_EQ(reply.outcome_counts_size(), 4);
  EXPECT_EQ(reply.outcome_counts(0), 2U);
  EXPECT_EQ(reply.outcome_counts(1), 1U);
  EXPECT_EQ(reply.outcome_counts(2), 1U);
  EXPECT_EQ(reply.outcome_counts(3), 1U);
  // Durations 10 and 100 are the two successes; the percentiles are per
  // population now, so mixing them is no longer what these fields report.
  EXPECT_EQ(reply.p50_ms(), 10U);
  EXPECT_EQ(reply.p95_ms(), 100U);
  EXPECT_EQ(reply.p99_ms(), 100U);
  EXPECT_EQ(reply.max_ms(), 100U);
  EXPECT_TRUE(reply.has_failure_data());
  EXPECT_EQ(reply.failure_count(), 3U);
  EXPECT_EQ(reply.failure_p50_ms(), 30U);
  EXPECT_EQ(reply.failure_max_ms(), 40U);
}

TEST_F(GrpcValidationTest, InstalledStyleClientSuccessIsOneJsonLine)
{
  const auto output = path_ + ".json";
  const std::string command = "RUNTIME_GATEWAY_ADDRESS=" + address_ + " \"" +
    RUNTIME_GATEWAY_CLIENT_PATH + "\" get-stats > \"" + output + "\"";
  ASSERT_EQ(std::system(command.c_str()), 0);
  std::ifstream stream(output);
  std::string first;
  std::string second;
  ASSERT_TRUE(std::getline(stream, first));
  EXPECT_FALSE(first.empty());
  EXPECT_EQ(first.front(), '{');
  EXPECT_EQ(first.back(), '}');
  EXPECT_FALSE(std::getline(stream, second));
  std::filesystem::remove(output);
}

}  // namespace
