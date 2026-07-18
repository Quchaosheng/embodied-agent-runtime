#include "task_executor/target_validator.hpp"

#include <gtest/gtest.h>

TEST(TargetValidatorTest, AcceptsConfiguredTargets)
{
  const task_executor::TargetValidator validator({"dock_a", "home"});

  EXPECT_TRUE(validator.is_known("dock_a"));
  EXPECT_TRUE(validator.is_known("home"));
}

TEST(TargetValidatorTest, RejectsUnknownAndEmptyTargets)
{
  const task_executor::TargetValidator validator({"dock_a", "home"});

  EXPECT_FALSE(validator.is_known("unknown"));
  EXPECT_FALSE(validator.is_known(""));
}

TEST(TargetValidatorTest, RemovesEmptyAndDuplicateConfigurationEntries)
{
  const task_executor::TargetValidator validator({"dock_a", "", "dock_a"});

  EXPECT_EQ(validator.size(), 1U);
}
