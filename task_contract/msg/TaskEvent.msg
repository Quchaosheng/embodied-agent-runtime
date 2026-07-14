uint8 STATE_IDLE=0
uint8 STATE_VALIDATING=1
uint8 STATE_DISPATCHING=2
uint8 STATE_RUNNING=3
uint8 STATE_RECOVERING=4
uint8 STATE_SUCCEEDED=5
uint8 STATE_CANCELLING=6
uint8 STATE_CANCELLED=7
uint8 STATE_SAFE_STOP=8
uint8 STATE_FAILED=9

builtin_interfaces/Time stamp
string task_id
uint8 state
uint8 error_code
uint32 attempt
string detail
