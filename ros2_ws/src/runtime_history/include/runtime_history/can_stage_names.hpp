#pragma once

// The stage-event vocabulary a CAN command attempt emits. Emitters and tests
// read these names instead of repeating string literals, so the recorded
// vocabulary cannot drift away from the one an offline diagnoser expects.

namespace runtime_history
{
namespace can_stage
{

inline constexpr char kEncodeStart[] = "can_encode_start";
inline constexpr char kEncodeEnd[] = "can_encode_end";
inline constexpr char kFrameSent[] = "can_frame_sent";
inline constexpr char kAckWaitStart[] = "can_ack_wait_start";
inline constexpr char kAckTimeout[] = "can_ack_timeout";
inline constexpr char kRetryScheduled[] = "can_retry_scheduled";
inline constexpr char kAckReceived[] = "can_ack_received";
inline constexpr char kRetryExhausted[] = "can_retry_exhausted";

}  // namespace can_stage
}  // namespace runtime_history
