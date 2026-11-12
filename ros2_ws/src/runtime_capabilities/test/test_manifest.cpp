#include "runtime_capabilities/manifest.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{

// The manifest a node reads. Pinned by the compile definition so a test cannot
// silently pass against a different file than the one that ships.
constexpr char kShipped[] = CAPABILITIES_CONFIG;

std::string write_temp(const std::string & name, const std::string & body)
{
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream(path) << body;
  return path.string();
}

TEST(CapabilityManifestTest, LoadsTheShippedManifest)
{
  const auto manifest = runtime_capabilities::Manifest::load(kShipped);

  EXPECT_TRUE(manifest.has_workflow("single_task"));
  EXPECT_TRUE(manifest.has_workflow("ready_then_task"));
  EXPECT_FALSE(manifest.has_workflow("drive_forever"));

  EXPECT_TRUE(manifest.has_target("dock_a"));
  EXPECT_TRUE(manifest.has_target("home"));
  EXPECT_FALSE(manifest.has_target("anywhere"));

  // The argument comes from the manifest, so an unknown target cannot be
  // smuggled in as a number and a known one cannot be redefined per node.
  ASSERT_TRUE(manifest.target_argument("dock_a").has_value());
  EXPECT_EQ(*manifest.target_argument("dock_a"), 42);
  EXPECT_EQ(*manifest.target_argument("home"), 0);
  EXPECT_FALSE(manifest.target_argument("anywhere").has_value());

  EXPECT_EQ(manifest.limits().max_duration_ms, 30000);
}

TEST(CapabilityManifestTest, EnforcesAClosedArgumentSetPerOpcode)
{
  const auto manifest = runtime_capabilities::Manifest::load(kShipped);

  EXPECT_TRUE(manifest.allows_command(1, 42, 1000));
  EXPECT_TRUE(manifest.allows_command(1, 0, 1000));
  // Declared opcode, undeclared argument.
  EXPECT_FALSE(manifest.allows_command(1, 7, 1000));
  // Undeclared opcode.
  EXPECT_FALSE(manifest.allows_command(9, 42, 1000));
  // Declared everything, over the bound.
  EXPECT_FALSE(manifest.allows_command(1, 42, 30001));
  // A non-positive timeout is not a command.
  EXPECT_FALSE(manifest.allows_command(1, 42, 0));
  EXPECT_FALSE(manifest.allows_command(1, 42, -1));
}

TEST(CapabilityManifestTest, RejectsAMissingFile)
{
  EXPECT_THROW(
    runtime_capabilities::Manifest::load("/nonexistent/capabilities.yaml"),
    std::runtime_error);
}

TEST(CapabilityManifestTest, RejectsAManifestWithNoWorkflows)
{
  const auto path = write_temp(
    "capabilities_no_workflows.yaml",
    "version: 1\nworkflows: {}\ntargets:\n  dock_a:\n    argument: 42\n"
    "device_commands:\n  - opcode: 1\n    arguments: [42]\n");
  // An empty capability set would reject everything and look like a fault, so
  // it is a load error instead.
  EXPECT_THROW(runtime_capabilities::Manifest::load(path), std::runtime_error);
  std::filesystem::remove(path);
}

TEST(CapabilityManifestTest, RejectsACommandLongerThanTheGlobalLimit)
{
  const auto path = write_temp(
    "capabilities_overlong.yaml",
    "version: 1\n"
    "workflows:\n  single_task:\n    steps: [execute_task]\n"
    "targets:\n  dock_a:\n    argument: 42\n"
    "device_commands:\n  - opcode: 1\n    arguments: [42]\n    max_duration_ms: 60000\n"
    "limits:\n  max_duration_ms: 30000\n");
  // Two ceilings that disagree is the bug the manifest exists to remove.
  EXPECT_THROW(runtime_capabilities::Manifest::load(path), std::runtime_error);
  std::filesystem::remove(path);
}

TEST(CapabilityManifestTest, RejectsAnUnknownVersion)
{
  const auto path = write_temp("capabilities_v2.yaml", "version: 2\n");
  EXPECT_THROW(runtime_capabilities::Manifest::load(path), std::runtime_error);
  std::filesystem::remove(path);
}

}
