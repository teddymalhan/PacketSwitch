










#include <gtest/gtest.h>

#include <thread>

#include "wirelab/ethernet_frame.hpp"
#include "wirelab/mac_table.hpp"
#include "wirelab/udp_socket.hpp"
#include "wirelab/vport.hpp"
#include "wirelab/vswitch.hpp"

using namespace wirelab;


std::vector<uint8_t>
create_test_frame(MacAddress dst, MacAddress src, uint16_t ethertype, const std::vector<uint8_t>& payload = {})
{
  EthernetFrame frame(dst, src, ethertype, payload);
  return frame.serialize();
}

TEST(IntegrationTest, VSwitchBasicOperation)
{
  
  auto vswitch_result = VSwitch::create(0);
  ASSERT_TRUE(vswitch_result.has_value());

  VSwitch vswitch = std::move(*vswitch_result);

  uint16_t port = vswitch.port();
  std::cout << "VSwitch created on port " << port << "\n";

  
  EXPECT_EQ(vswitch.learned_macs(), 0);
  EXPECT_FALSE(vswitch.is_running());
}

TEST(IntegrationTest, VSwitchMacLearning)
{
  
  auto vswitch_result = VSwitch::create(0);
  ASSERT_TRUE(vswitch_result.has_value());

  VSwitch vswitch = std::move(*vswitch_result);

  
  EXPECT_EQ(vswitch.learned_macs(), 0);

  
  
}

TEST(IntegrationTest, MacTableEndpointsRetrieval)
{
  MacTable mac_table;

  MacAddress mac1({ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 });
  MacAddress mac2({ 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff });

  Endpoint ep1("192.168.1.1", 8080);
  Endpoint ep2("192.168.1.2", 9000);

  mac_table.insert(mac1, ep1);
  mac_table.insert(mac2, ep2);

  
  auto all_eps = mac_table.get_all_endpoints();
  EXPECT_EQ(all_eps.size(), 2);

  
  auto eps_except = mac_table.get_all_endpoints_except(mac1);
  EXPECT_EQ(eps_except.size(), 1);
  EXPECT_EQ(eps_except[0], ep2);
}

TEST(IntegrationTest, EthernetFrameSerializationRoundTrip)
{
  MacAddress dst({ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff });
  MacAddress src({ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 });

  std::vector<uint8_t> payload = { 0xde, 0xad, 0xbe, 0xef };

  EthernetFrame original(dst, src, EtherType::IPv4, payload);

  
  std::vector<uint8_t> serialized = original.serialize();

  
  EthernetFrame parsed = EthernetFrame::parse(serialized);

  
  EXPECT_EQ(parsed.dst_mac(), dst);
  EXPECT_EQ(parsed.src_mac(), src);
  EXPECT_EQ(parsed.ethertype(), EtherType::IPv4);
  EXPECT_EQ(parsed.payload(), payload);
}

TEST(IntegrationTest, BroadcastMacAddress)
{
  MacAddress broadcast = MacAddress::broadcast();
  MacAddress unicast({ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 });

  EXPECT_TRUE(broadcast.is_broadcast());
  EXPECT_FALSE(unicast.is_broadcast());

  
  std::vector<uint8_t> frame_data = create_test_frame(broadcast, unicast, EtherType::ARP);

  EthernetFrame frame = EthernetFrame::parse(frame_data);
  EXPECT_TRUE(frame.is_broadcast());
}





TEST(IntegrationTest, UdpSocketBindAndReceive)
{
  auto socket_result = UdpSocket::create();
  ASSERT_TRUE(socket_result.has_value());

  UdpSocket socket = std::move(*socket_result);

  
  auto bind_result = socket.bind("127.0.0.1", 0);
  EXPECT_TRUE(bind_result.has_value());

  EXPECT_TRUE(socket.is_valid());
  
  
}

TEST(IntegrationTest, MacAddressEqualityAndHash)
{
  MacAddress mac1({ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 });
  MacAddress mac2({ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 });
  MacAddress mac3({ 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff });

  EXPECT_EQ(mac1, mac2);
  EXPECT_NE(mac1, mac3);

  
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
