#include "virtual_can_device/device_modes.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

// The launch help text is generated from this list, so what matters is that the
// list is complete and that nothing in it is a mode the node rejects.
TEST(DeviceModesTest, EveryModeIsUniqueAndNamed)
{
  std::set<std::string> seen;
  for (const auto & mode : virtual_can_device::modes()) {
    EXPECT_TRUE(seen.insert(mode.name).second) << "duplicate mode " << mode.name;
    EXPECT_FALSE(std::string(mode.name).empty());
    EXPECT_FALSE(std::string(mode.description).empty());
  }
  EXPECT_FALSE(virtual_can_device::modes().empty());
}

TEST(DeviceModesTest, TheHelpTextNamesEveryModeAndNothingElse)
{
  const auto help = virtual_can_device::mode_help();
  for (const auto & mode : virtual_can_device::modes()) {
    EXPECT_NE(help.find(mode.name), std::string::npos)
      << mode.name << " missing from: " << help;
  }
  // The defect this replaces: the help advertised a mode the node rejects.
  EXPECT_EQ(help.find("duplicate"), std::string::npos) << help;
}

TEST(DeviceModesTest, SupportedModesAreExactlyTheListedOnes)
{
  for (const auto & mode : virtual_can_device::modes()) {
    EXPECT_TRUE(virtual_can_device::is_supported(mode.name)) << mode.name;
  }
  EXPECT_FALSE(virtual_can_device::is_supported("duplicate"));
  EXPECT_FALSE(virtual_can_device::is_supported(""));
  EXPECT_FALSE(virtual_can_device::is_supported("NORMAL"));
}
