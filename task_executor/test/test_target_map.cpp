#include <gtest/gtest.h>

#include <stdexcept>

#include "task_executor/target_map.hpp"

TEST(TargetMapTest, LoadsAllContractTargets) {
  const auto targets = task_executor::load_targets_from_yaml(TARGET_MAP_TEST_VALID_PATH);

  ASSERT_EQ(targets.size(), 3U);
  EXPECT_DOUBLE_EQ(targets.at("dock").x, 0.0);
  EXPECT_DOUBLE_EQ(targets.at("workbench").x, 1.0);
  EXPECT_DOUBLE_EQ(targets.at("home").y, 1.0);
  EXPECT_DOUBLE_EQ(targets.at("home").yaw, 1.57);
  EXPECT_EQ(targets.at("dock").frame_id, "map");
}

TEST(TargetMapTest, RejectsMissingContractTarget) {
  EXPECT_THROW(task_executor::load_targets_from_yaml(TARGET_MAP_TEST_MISSING_PATH),
               std::runtime_error);
}

TEST(TargetMapTest, RejectsTargetOutsideContract) {
  EXPECT_THROW(task_executor::load_targets_from_yaml(TARGET_MAP_TEST_UNKNOWN_PATH),
               std::runtime_error);
}

TEST(TargetMapTest, RejectsNonFiniteCoordinate) {
  EXPECT_THROW(task_executor::load_targets_from_yaml(TARGET_MAP_TEST_NONFINITE_PATH),
               std::runtime_error);
}

TEST(TargetMapTest, RejectsExtraPoseField) {
  EXPECT_THROW(task_executor::load_targets_from_yaml(TARGET_MAP_TEST_EXTRA_FIELD_PATH),
               std::runtime_error);
}

TEST(TargetMapTest, RejectsFrameOutsideMap) {
  EXPECT_THROW(task_executor::load_targets_from_yaml(TARGET_MAP_TEST_FRAME_PATH),
               std::runtime_error);
}

TEST(TargetMapTest, RejectsYawOutsideNormalizedRange) {
  EXPECT_THROW(task_executor::load_targets_from_yaml(TARGET_MAP_TEST_YAW_PATH), std::runtime_error);
}
