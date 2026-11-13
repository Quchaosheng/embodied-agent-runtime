// Prints a named error code so shell scripts can assert on the table instead of
// restating its numbers. Restating them is how the E2E drifted from the table in
// the first place.
#include "robot_task_interfaces/error_code.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char ** argv)
{
  using namespace robot_task_interfaces::error_code;
  if (argc != 2) {
    std::cerr << "usage: print_error_code <name|device_rejected:N>\n";
    return 2;
  }
  const std::string_view name{argv[1]};
  constexpr std::string_view kPrefix{"device_rejected:"};
  if (name.substr(0, kPrefix.size()) == kPrefix) {
    const auto result_code = std::stoi(std::string(name.substr(kPrefix.size())));
    std::cout << device_rejected(static_cast<std::uint8_t>(result_code)) << "\n";
    return 0;
  }
  std::size_t count = 0;
  const auto * entries = table(count);
  for (std::size_t index = 0; index < count; ++index) {
    if (entries[index].name == name) {
      std::cout << entries[index].code << "\n";
      return 0;
    }
  }
  std::cerr << "unknown error code name: " << name << "\n";
  return 1;
}
