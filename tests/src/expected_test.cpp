/**
 * @file expected_test.cpp
 * @brief Unit tests for expected<T, E> implementation
 */

#include "project/expected.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace project;

TEST(ExpectedTest, DefaultConstruction)
{
  expected<int, std::string> ex;
  EXPECT_TRUE(ex.has_value());
  EXPECT_EQ(*ex, 0);
}

TEST(ExpectedTest, ValueConstruction)
{
  expected<int, std::string> ex(42);
  EXPECT_TRUE(ex.has_value());
  EXPECT_EQ(*ex, 42);
  EXPECT_EQ(ex.value(), 42);
}

TEST(ExpectedTest, ErrorConstruction)
{
  expected<int, std::string> ex(unexpected<std::string>("error"));
  EXPECT_FALSE(ex.has_value());
  EXPECT_EQ(ex.error(), "error");
}

TEST(ExpectedTest, UnexpectConstruction)
{
  expected<int, std::string> ex(unexpect, "error message");
  EXPECT_FALSE(ex.has_value());
  EXPECT_EQ(ex.error(), "error message");
}

TEST(ExpectedTest, BoolConversion)
{
  expected<int, std::string> ex_value(42);
  expected<int, std::string> ex_error(unexpected<std::string>("error"));

  EXPECT_TRUE(static_cast<bool>(ex_value));
  EXPECT_FALSE(static_cast<bool>(ex_error));
}

TEST(ExpectedTest, ValueAccess)
{
  expected<int, std::string> ex(42);
  EXPECT_EQ(ex.value(), 42);
  EXPECT_EQ(*ex, 42);
}

TEST(ExpectedTest, ValueAccessThrows)
{
  expected<int, std::string> ex(unexpected<std::string>("error"));
  EXPECT_THROW(
      {
        [[maybe_unused]] int val = ex.value();
      },
      bad_expected_access<std::string>);
}

TEST(ExpectedTest, ValueOr)
{
  expected<int, std::string> ex_value(42);
  expected<int, std::string> ex_error(unexpected<std::string>("error"));

  EXPECT_EQ(ex_value.value_or(100), 42);
  EXPECT_EQ(ex_error.value_or(100), 100);
}

TEST(ExpectedTest, Assignment)
{
  expected<int, std::string> ex(42);
  EXPECT_EQ(*ex, 42);

  ex = 100;
  EXPECT_EQ(*ex, 100);

  ex = unexpected<std::string>("error");
  EXPECT_FALSE(ex.has_value());
  EXPECT_EQ(ex.error(), "error");
}

TEST(ExpectedTest, MoveSemantics)
{
  expected<std::string, int> ex1("hello");
  expected<std::string, int> ex2(std::move(ex1));

  EXPECT_TRUE(ex2.has_value());
  EXPECT_EQ(*ex2, "hello");
}

TEST(ExpectedTest, VoidSpecialization)
{
  expected<void, std::string> ex;
  EXPECT_TRUE(ex.has_value());

  expected<void, std::string> ex_error(unexpected<std::string>("error"));
  EXPECT_FALSE(ex_error.has_value());
  EXPECT_EQ(ex_error.error(), "error");
}

TEST(ExpectedTest, VoidValue)
{
  expected<void, std::string> ex;
  EXPECT_NO_THROW(ex.value());

  expected<void, std::string> ex_error(unexpected<std::string>("error"));
  EXPECT_THROW(ex_error.value(), bad_expected_access<std::string>);
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

