/**
 * @file ethernet_frame_test.cpp
 * @brief Unit tests for Ethernet frame parsing and MAC address handling
 */

#include "project/ethernet_frame.hpp"

#include <gtest/gtest.h>

#include <sstream>

using namespace project;

// ============================================================================
// MacAddress Tests
// ============================================================================

TEST(MacAddressTest, DefaultConstruction)
{
  MacAddress mac;
  EXPECT_TRUE(mac.is_zero());
  EXPECT_FALSE(mac.is_broadcast());
  EXPECT_EQ(mac.to_string(), "00:00:00:00:00:00");
}

TEST(MacAddressTest, ConstructFromArray)
{
  std::array<uint8_t, MAC_ADDRESS_SIZE> bytes = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  MacAddress mac(bytes);

  EXPECT_EQ(mac.bytes(), bytes);
  EXPECT_EQ(mac.to_string(), "00:11:22:33:44:55");
  EXPECT_FALSE(mac.is_zero());
  EXPECT_FALSE(mac.is_broadcast());
}

TEST(MacAddressTest, ConstructFromPointer)
{
  uint8_t bytes[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  MacAddress mac(bytes);

  EXPECT_EQ(mac.to_string(), "aa:bb:cc:dd:ee:ff");
}

TEST(MacAddressTest, BroadcastAddress)
{
  MacAddress broadcast = MacAddress::broadcast();

  EXPECT_TRUE(broadcast.is_broadcast());
  EXPECT_FALSE(broadcast.is_zero());
  EXPECT_EQ(broadcast.to_string(), "ff:ff:ff:ff:ff:ff");
}

TEST(MacAddressTest, FromStringWithColon)
{
  MacAddress mac = MacAddress::from_string("00:11:22:33:44:55");

  EXPECT_EQ(mac.to_string(), "00:11:22:33:44:55");
  EXPECT_FALSE(mac.is_zero());
}

TEST(MacAddressTest, FromStringWithDash)
{
  MacAddress mac = MacAddress::from_string("aa-bb-cc-dd-ee-ff");

  EXPECT_EQ(mac.to_string(), "aa:bb:cc:dd:ee:ff");
}

TEST(MacAddressTest, FromStringInvalid)
{
  MacAddress mac1 = MacAddress::from_string("invalid");
  MacAddress mac2 = MacAddress::from_string("00:11:22:33:44");     // Too short
  MacAddress mac3 = MacAddress::from_string("00:11:22:33:44:55:66");  // Too long

  EXPECT_TRUE(mac1.is_zero());
  EXPECT_TRUE(mac2.is_zero());
  EXPECT_TRUE(mac3.is_zero());
}

TEST(MacAddressTest, Equality)
{
  MacAddress mac1({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  MacAddress mac2({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  MacAddress mac3({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});

  EXPECT_EQ(mac1, mac2);
  EXPECT_NE(mac1, mac3);
  EXPECT_NE(mac2, mac3);
}

TEST(MacAddressTest, Comparison)
{
  MacAddress mac1({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  MacAddress mac2({0x00, 0x11, 0x22, 0x33, 0x44, 0x56});

  EXPECT_TRUE(mac1 < mac2);
  EXPECT_FALSE(mac2 < mac1);
}

TEST(MacAddressTest, OutputStreamOperator)
{
  MacAddress mac({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  std::ostringstream oss;
  oss << mac;

  EXPECT_EQ(oss.str(), "00:11:22:33:44:55");
}

TEST(MacAddressTest, IsZero)
{
  MacAddress zero;
  MacAddress non_zero({0x00, 0x00, 0x00, 0x00, 0x00, 0x01});

  EXPECT_TRUE(zero.is_zero());
  EXPECT_FALSE(non_zero.is_zero());
}

TEST(MacAddressTest, IsBroadcast)
{
  MacAddress broadcast = MacAddress::broadcast();
  MacAddress almost_broadcast({0xff, 0xff, 0xff, 0xff, 0xff, 0xfe});

  EXPECT_TRUE(broadcast.is_broadcast());
  EXPECT_FALSE(almost_broadcast.is_broadcast());
}

TEST(MacAddressTest, DataPointer)
{
  MacAddress mac({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  const uint8_t* data = mac.data();

  EXPECT_EQ(data[0], 0x00);
  EXPECT_EQ(data[1], 0x11);
  EXPECT_EQ(data[2], 0x22);
  EXPECT_EQ(data[3], 0x33);
  EXPECT_EQ(data[4], 0x44);
  EXPECT_EQ(data[5], 0x55);
}

// ============================================================================
// EthernetFrame Tests
// ============================================================================

TEST(EthernetFrameTest, DefaultConstruction)
{
  EthernetFrame frame;

  EXPECT_TRUE(frame.dst_mac().is_zero());
  EXPECT_TRUE(frame.src_mac().is_zero());
  EXPECT_EQ(frame.ethertype(), 0);
  EXPECT_TRUE(frame.payload().empty());
  EXPECT_EQ(frame.size(), ETHERNET_HEADER_SIZE);
}

TEST(EthernetFrameTest, ConstructWithParameters)
{
  MacAddress dst({0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  MacAddress src({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  uint16_t ethertype = EtherType::IPv4;
  std::vector<uint8_t> payload = {0xde, 0xad, 0xbe, 0xef};

  EthernetFrame frame(dst, src, ethertype, payload);

  EXPECT_EQ(frame.dst_mac(), dst);
  EXPECT_EQ(frame.src_mac(), src);
  EXPECT_EQ(frame.ethertype(), ethertype);
  EXPECT_EQ(frame.payload(), payload);
  EXPECT_EQ(frame.size(), ETHERNET_HEADER_SIZE + 4);
}

TEST(EthernetFrameTest, ParseValidFrame)
{
  std::vector<uint8_t> raw_frame = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // Destination MAC (broadcast)
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  // Source MAC
      0x08, 0x00,                          // EtherType (IPv4)
      0xde, 0xad, 0xbe, 0xef               // Payload
  };

  EthernetFrame frame = EthernetFrame::parse(raw_frame);

  EXPECT_TRUE(frame.dst_mac().is_broadcast());
  EXPECT_EQ(frame.src_mac().to_string(), "00:11:22:33:44:55");
  EXPECT_EQ(frame.ethertype(), EtherType::IPv4);
  EXPECT_EQ(frame.payload().size(), 4);
  EXPECT_EQ(frame.payload()[0], 0xde);
  EXPECT_EQ(frame.payload()[1], 0xad);
  EXPECT_EQ(frame.payload()[2], 0xbe);
  EXPECT_EQ(frame.payload()[3], 0xef);
}

TEST(EthernetFrameTest, ParseHeaderOnly)
{
  std::vector<uint8_t> raw_frame = {
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  // Destination MAC
      0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,  // Source MAC
      0x08, 0x06                           // EtherType (ARP)
  };

  EthernetFrame frame = EthernetFrame::parse(raw_frame);

  EXPECT_EQ(frame.dst_mac().to_string(), "00:11:22:33:44:55");
  EXPECT_EQ(frame.src_mac().to_string(), "aa:bb:cc:dd:ee:ff");
  EXPECT_EQ(frame.ethertype(), EtherType::ARP);
  EXPECT_TRUE(frame.payload().empty());
}

TEST(EthernetFrameTest, ParseInvalidFrame)
{
  std::vector<uint8_t> short_frame = {0x00, 0x11, 0x22};  // Too short

  EthernetFrame frame = EthernetFrame::parse(short_frame);

  EXPECT_TRUE(frame.dst_mac().is_zero());
  EXPECT_TRUE(frame.src_mac().is_zero());
  EXPECT_EQ(frame.ethertype(), 0);
}

TEST(EthernetFrameTest, Serialize)
{
  MacAddress dst({0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  MacAddress src({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  uint16_t ethertype = EtherType::IPv4;
  std::vector<uint8_t> payload = {0xde, 0xad, 0xbe, 0xef};

  EthernetFrame frame(dst, src, ethertype, payload);
  std::vector<uint8_t> serialized = frame.serialize();

  EXPECT_EQ(serialized.size(), ETHERNET_HEADER_SIZE + 4);

  // Verify destination MAC
  EXPECT_EQ(serialized[0], 0xff);
  EXPECT_EQ(serialized[5], 0xff);

  // Verify source MAC
  EXPECT_EQ(serialized[6], 0x00);
  EXPECT_EQ(serialized[11], 0x55);

  // Verify EtherType (big endian)
  EXPECT_EQ(serialized[12], 0x08);
  EXPECT_EQ(serialized[13], 0x00);

  // Verify payload
  EXPECT_EQ(serialized[14], 0xde);
  EXPECT_EQ(serialized[17], 0xef);
}

TEST(EthernetFrameTest, ParseAndSerializeRoundTrip)
{
  std::vector<uint8_t> original = {
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  // Destination MAC
      0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,  // Source MAC
      0x86, 0xdd,                          // EtherType (IPv6)
      0x12, 0x34, 0x56, 0x78               // Payload
  };

  EthernetFrame frame = EthernetFrame::parse(original);
  std::vector<uint8_t> serialized = frame.serialize();

  EXPECT_EQ(original, serialized);
}

TEST(EthernetFrameTest, Setters)
{
  EthernetFrame frame;

  MacAddress dst({0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  MacAddress src({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  std::vector<uint8_t> payload = {0xaa, 0xbb};

  frame.set_dst_mac(dst);
  frame.set_src_mac(src);
  frame.set_ethertype(EtherType::ARP);
  frame.set_payload(payload);

  EXPECT_EQ(frame.dst_mac(), dst);
  EXPECT_EQ(frame.src_mac(), src);
  EXPECT_EQ(frame.ethertype(), EtherType::ARP);
  EXPECT_EQ(frame.payload(), payload);
}

TEST(EthernetFrameTest, IsBroadcast)
{
  MacAddress broadcast = MacAddress::broadcast();
  MacAddress unicast({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});

  EthernetFrame broadcast_frame(broadcast, unicast, EtherType::IPv4);
  EthernetFrame unicast_frame(unicast, unicast, EtherType::IPv4);

  EXPECT_TRUE(broadcast_frame.is_broadcast());
  EXPECT_FALSE(unicast_frame.is_broadcast());
}

TEST(EthernetFrameTest, EtherTypeConstants)
{
  EXPECT_EQ(EtherType::IPv4, 0x0800);
  EXPECT_EQ(EtherType::ARP, 0x0806);
  EXPECT_EQ(EtherType::IPv6, 0x86DD);
}

TEST(EthernetFrameTest, EmptyPayload)
{
  MacAddress dst({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  MacAddress src({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});

  EthernetFrame frame(dst, src, EtherType::ARP);

  EXPECT_TRUE(frame.payload().empty());
  EXPECT_EQ(frame.size(), ETHERNET_HEADER_SIZE);

  std::vector<uint8_t> serialized = frame.serialize();
  EXPECT_EQ(serialized.size(), ETHERNET_HEADER_SIZE);
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

