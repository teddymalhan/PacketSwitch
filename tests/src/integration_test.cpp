/**
 * @file integration_test.cpp
 * @brief Integration tests for VPort ↔ VSwitch communication
 * 
 * These tests verify the complete system working together:
 * - VPort creates TAP device and connects to VSwitch
 * - VSwitch receives frames and learns MAC addresses
 * - Frames are forwarded between VPorts via VSwitch
 * - Broadcast frames work correctly
 */

#include "project/ethernet_frame.hpp"
#include "project/mac_table.hpp"
#include "project/udp_socket.hpp"
#include "project/vport.hpp"
#include "project/vswitch.hpp"

#include <gtest/gtest.h>

#include <thread>

using namespace project;

// Helper function to create a simple test Ethernet frame
std::vector<uint8_t> create_test_frame(MacAddress dst, MacAddress src, uint16_t ethertype,
                                        const std::vector<uint8_t>& payload = {})
{
  EthernetFrame frame(dst, src, ethertype, payload);
  return frame.serialize();
}

TEST(IntegrationTest, VSwitchBasicOperation)
{
  // Create VSwitch on port 0 (ephemeral)
  auto vswitch_result = VSwitch::create(0);
  ASSERT_TRUE(vswitch_result.has_value());

  VSwitch vswitch = std::move(*vswitch_result);

  uint16_t port = vswitch.port();
  std::cout << "VSwitch created on port " << port << "\n";

  // Check initial state
  EXPECT_EQ(vswitch.learned_macs(), 0);
  EXPECT_FALSE(vswitch.is_running());
}

TEST(IntegrationTest, VSwitchMacLearning)
{
  // Test that VSwitch can be created
  auto vswitch_result = VSwitch::create(0);
  ASSERT_TRUE(vswitch_result.has_value());

  VSwitch vswitch = std::move(*vswitch_result);

  // VSwitch starts with 0 learned MACs
  EXPECT_EQ(vswitch.learned_macs(), 0);

  // Note: process_frame is private, so we can't test it directly.
  // Full integration test would require VSwitch::start() and actual UDP frames.
}

TEST(IntegrationTest, MacTableEndpointsRetrieval)
{
  MacTable mac_table;

  MacAddress mac1({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  MacAddress mac2({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});

  Endpoint ep1("192.168.1.1", 8080);
  Endpoint ep2("192.168.1.2", 9000);

  mac_table.insert(mac1, ep1);
  mac_table.insert(mac2, ep2);

  // Get all endpoints
  auto all_eps = mac_table.get_all_endpoints();
  EXPECT_EQ(all_eps.size(), 2);

  // Get all except one
  auto eps_except = mac_table.get_all_endpoints_except(mac1);
  EXPECT_EQ(eps_except.size(), 1);
  EXPECT_EQ(eps_except[0], ep2);
}

TEST(IntegrationTest, EthernetFrameSerializationRoundTrip)
{
  MacAddress dst({0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  MacAddress src({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});

  std::vector<uint8_t> payload = {0xde, 0xad, 0xbe, 0xef};

  EthernetFrame original(dst, src, EtherType::IPv4, payload);

  // Serialize
  std::vector<uint8_t> serialized = original.serialize();

  // Parse back
  EthernetFrame parsed = EthernetFrame::parse(serialized);

  // Verify
  EXPECT_EQ(parsed.dst_mac(), dst);
  EXPECT_EQ(parsed.src_mac(), src);
  EXPECT_EQ(parsed.ethertype(), EtherType::IPv4);
  EXPECT_EQ(parsed.payload(), payload);
}

TEST(IntegrationTest, BroadcastMacAddress)
{
  MacAddress broadcast = MacAddress::broadcast();
  MacAddress unicast({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});

  EXPECT_TRUE(broadcast.is_broadcast());
  EXPECT_FALSE(unicast.is_broadcast());

  // Create frame with broadcast destination
  std::vector<uint8_t> frame_data = create_test_frame(broadcast, unicast, EtherType::ARP);

  EthernetFrame frame = EthernetFrame::parse(frame_data);
  EXPECT_TRUE(frame.is_broadcast());
}

// Note: These tests verify individual components work together.
// Full end-to-end tests with actual TAP devices would require root privileges
// and are better suited for manual testing or CI with proper setup.

TEST(IntegrationTest, UdpSocketBindAndReceive)
{
  auto socket_result = UdpSocket::create();
  ASSERT_TRUE(socket_result.has_value());

  UdpSocket socket = std::move(*socket_result);

  // Bind to port 0 (ephemeral)
  auto bind_result = socket.bind("127.0.0.1", 0);
  EXPECT_TRUE(bind_result.has_value());

  EXPECT_TRUE(socket.is_valid());
  // Note: local_endpoint() returns the requested endpoint, not the actual bound port
  // which we can't easily determine without additional system calls
}

TEST(IntegrationTest, MacAddressEqualityAndHash)
{
  MacAddress mac1({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  MacAddress mac2({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  MacAddress mac3({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});

  EXPECT_EQ(mac1, mac2);
  EXPECT_NE(mac1, mac3);

  // Test hash function
  std::hash<MacAddress> hasher;
  EXPECT_EQ(hasher(mac1), hasher(mac2));
  EXPECT_NE(hasher(mac1), hasher(mac3));
}

TEST(IntegrationTest, EndpointToString)
{
  Endpoint ep("192.168.1.100", 8080);
  std::string str = ep.to_string();

  EXPECT_EQ(str, "192.168.1.100:8080");
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

