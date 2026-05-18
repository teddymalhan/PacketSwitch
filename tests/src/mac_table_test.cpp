/**
 * @file mac_table_test.cpp
 * @brief Unit tests for MAC address table
 */

#include "project/mac_table.hpp"

#include <gtest/gtest.h>

#include <array>
#include <thread>

using namespace project;

TEST(MacTableTest, DefaultConstruction)
{
  MacTable table;
  EXPECT_TRUE(table.empty());
  EXPECT_EQ(table.size(), 0);
}

TEST(MacTableTest, InsertAndLookup)
{
  MacTable table;

  MacAddress mac({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  Endpoint endpoint("192.168.1.100", 8080);

  // Insert
  bool is_new = table.insert(mac, endpoint);
  EXPECT_TRUE(is_new);
  EXPECT_EQ(table.size(), 1);
  EXPECT_FALSE(table.empty());

  // Lookup
  auto result = table.lookup(mac);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, endpoint);
}

TEST(MacTableTest, UpdateExistingMac)
{
  MacTable table;

  MacAddress mac({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  Endpoint endpoint1("192.168.1.100", 8080);
  Endpoint endpoint2("192.168.1.200", 9000);

  // Insert first endpoint
  bool is_new1 = table.insert(mac, endpoint1);
  EXPECT_TRUE(is_new1);

  // Update with second endpoint
  bool is_new2 = table.insert(mac, endpoint2);
  EXPECT_FALSE(is_new2);  // Not a new entry, it was updated

  // Lookup should return updated endpoint
  auto result = table.lookup(mac);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, endpoint2);
  EXPECT_NE(*result, endpoint1);
}

TEST(MacTableTest, LookupNotFound)
{
  MacTable table;

  MacAddress mac({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});

  auto result = table.lookup(mac);
  EXPECT_FALSE(result.has_value());
}

TEST(MacTableTest, Contains)
{
  MacTable table;

  MacAddress mac1({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  MacAddress mac2({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});

  table.insert(mac1, {"192.168.1.1", 8080});

  EXPECT_TRUE(table.contains(mac1));
  EXPECT_FALSE(table.contains(mac2));
}

TEST(MacTableTest, Remove)
{
  MacTable table;

  MacAddress mac({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  Endpoint endpoint("192.168.1.100", 8080);

  // Insert
  table.insert(mac, endpoint);
  EXPECT_EQ(table.size(), 1);

  // Remove existing
  bool removed = table.remove(mac);
  EXPECT_TRUE(removed);
  EXPECT_EQ(table.size(), 0);

  // Try to remove again
  bool removed_again = table.remove(mac);
  EXPECT_FALSE(removed_again);
}

TEST(MacTableTest, GetAllEndpoints)
{
  MacTable table;

  table.insert(MacAddress({0x00, 0x11, 0x22, 0x33, 0x44, 0x55}), {"192.168.1.1", 8080});
  table.insert(MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x11}), {"192.168.1.2", 8080});
  table.insert(MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x22}), {"192.168.1.3", 9000});

  auto endpoints = table.get_all_endpoints();

  EXPECT_EQ(endpoints.size(), 3);
  EXPECT_EQ(table.size(), 3);
}

TEST(MacTableTest, GetAllEndpointsExcept)
{
  MacTable table;

  MacAddress exclude_mac({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x11});

  table.insert(MacAddress({0x00, 0x11, 0x22, 0x33, 0x44, 0x55}), {"192.168.1.1", 8080});
  table.insert(exclude_mac, {"192.168.1.2", 8080});
  table.insert(MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x22}), {"192.168.1.3", 9000});

  auto endpoints = table.get_all_endpoints_except(exclude_mac);

  EXPECT_EQ(endpoints.size(), 2);
}

TEST(MacTableTest, Clear)
{
  MacTable table;

  table.insert(MacAddress({0x00, 0x11, 0x22, 0x33, 0x44, 0x55}), {"192.168.1.1", 8080});
  table.insert(MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}), {"192.168.1.2", 8080});

  EXPECT_EQ(table.size(), 2);

  table.clear();

  EXPECT_TRUE(table.empty());
  EXPECT_EQ(table.size(), 0);
}

TEST(MacTableTest, ThreadSafety)
{
  MacTable table;

  const int num_threads = 4;
  const int iterations_per_thread = 100;

  std::vector<std::thread> threads;

  // Writer threads
  for (int t = 0; t < num_threads; ++t)
  {
    threads.emplace_back([&table, t]() {
      for (int i = 0; i < 100; ++i)
      {
        std::array<uint8_t, MAC_ADDRESS_SIZE> bytes = {
            static_cast<uint8_t>(t), 0x11, 0x22, 0x33, 0x44, static_cast<uint8_t>(i)};
        MacAddress mac(bytes);
        Endpoint endpoint("192.168.1." + std::to_string(t + 100), 8080 + i);
        table.insert(mac, endpoint);
      }
    });
  }

  // Reader threads
  for (int t = 0; t < num_threads; ++t)
  {
    threads.emplace_back([&table, t]() {
      for (int i = 0; i < 100; ++i)
      {
        std::array<uint8_t, MAC_ADDRESS_SIZE> bytes = {
            static_cast<uint8_t>(t), 0x11, 0x22, 0x33, 0x44, static_cast<uint8_t>(i)};
        MacAddress mac(bytes);
        [[maybe_unused]] auto result = table.lookup(mac);

        // Try to get all endpoints
        [[maybe_unused]] auto endpoints = table.get_all_endpoints();
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads)
  {
    thread.join();
  }

  // All writer threads should have added entries
  EXPECT_EQ(table.size(), num_threads * iterations_per_thread);
}

TEST(MacTableTest, GetAllEntries)
{
  MacTable table;

  table.insert(MacAddress({0x00, 0x11, 0x22, 0x33, 0x44, 0x55}), {"192.168.1.1", 8080});
  table.insert(MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}), {"192.168.1.2", 9000});

  auto entries = table.get_all_entries();

  EXPECT_EQ(entries.size(), 2);
  EXPECT_TRUE(entries.find(MacAddress({0x00, 0x11, 0x22, 0x33, 0x44, 0x55})) != entries.end());
  EXPECT_TRUE(entries.find(MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff})) != entries.end());
}

TEST(MacTableTest, MoveSemantics)
{
  MacTable table1;

  table1.insert(MacAddress({0x00, 0x11, 0x22, 0x33, 0x44, 0x55}), {"192.168.1.1", 8080});
  table1.insert(MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}), {"192.168.1.2", 9000});

  EXPECT_EQ(table1.size(), 2);

  // Move construct
  MacTable table2(std::move(table1));

  EXPECT_EQ(table1.size(), 0);
  EXPECT_TRUE(table1.empty());
  EXPECT_EQ(table2.size(), 2);

  // Move assign
  MacTable table3;
  table3 = std::move(table2);

  EXPECT_EQ(table2.size(), 0);
  EXPECT_EQ(table3.size(), 2);
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

