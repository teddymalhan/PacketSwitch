/**
 * @file joining_thread_test.cpp
 * @brief Unit tests for joining_thread RAII wrapper
 */

#include "project/joining_thread.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace project;

TEST(JoiningThreadTest, DefaultConstruction)
{
  joining_thread t;
  EXPECT_FALSE(t.joinable());
}

TEST(JoiningThreadTest, ConstructWithFunction)
{
  std::atomic<bool> executed{false};

  {
    joining_thread t([&executed]() { executed = true; });
    EXPECT_TRUE(t.joinable());
  }  // Thread joins automatically here

  EXPECT_TRUE(executed);
}

TEST(JoiningThreadTest, ConstructWithFunctionAndArguments)
{
  std::atomic<int> result{0};

  {
    joining_thread t([](std::atomic<int>& res, int val) { res = val; }, std::ref(result), 42);
    EXPECT_TRUE(t.joinable());
  }  // Thread joins automatically here

  EXPECT_EQ(result, 42);
}

TEST(JoiningThreadTest, MoveConstruction)
{
  std::atomic<bool> executed{false};

  joining_thread t1([&executed]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    executed = true;
  });

  EXPECT_TRUE(t1.joinable());

  joining_thread t2(std::move(t1));
  EXPECT_FALSE(t1.joinable());
  EXPECT_TRUE(t2.joinable());

  // t2 will join when it goes out of scope
}

TEST(JoiningThreadTest, MoveAssignment)
{
  std::atomic<int> counter{0};

  joining_thread t1([&counter]() { counter++; });

  {
    joining_thread t2([&counter]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      counter++;
    });

    t2 = std::move(t1);  // t2's original thread joins here
  }                      // t1's thread (now in t2) joins here

  // Give threads time to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(counter, 2);
}

TEST(JoiningThreadTest, ExplicitJoin)
{
  std::atomic<bool> executed{false};

  joining_thread t([&executed]() { executed = true; });

  EXPECT_TRUE(t.joinable());
  t.join();
  EXPECT_FALSE(t.joinable());
  EXPECT_TRUE(executed);
}

TEST(JoiningThreadTest, GetId)
{
  std::atomic<bool> running{true};

  joining_thread t([&running]() {
    while (running)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  auto id = t.get_id();
  EXPECT_NE(id, std::thread::id{});

  running = false;
}

TEST(JoiningThreadTest, AutomaticJoinOnDestruction)
{
  std::atomic<int> value{0};

  {
    joining_thread t([&value]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      value = 42;
    });
    // Thread is running here
  }  // Destructor automatically joins the thread

  // If join didn't happen, value might still be 0
  EXPECT_EQ(value, 42);
}

TEST(JoiningThreadTest, Swap)
{
  std::atomic<int> val1{0};
  std::atomic<int> val2{0};

  joining_thread t1([&val1]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    val1 = 1;
  });

  joining_thread t2([&val2]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    val2 = 2;
  });

  auto id1 = t1.get_id();
  auto id2 = t2.get_id();

  t1.swap(t2);

  EXPECT_EQ(t1.get_id(), id2);
  EXPECT_EQ(t2.get_id(), id1);
}

TEST(JoiningThreadTest, LambdaCapture)
{
  int value = 100;
  std::atomic<int> result{0};

  {
    joining_thread t([value, &result]() { result = value * 2; });
  }

  EXPECT_EQ(result, 200);
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

