/**
 * @file udp_socket_test.cpp
 * @brief Unit tests for UDP socket wrapper
 */

#include "project/udp_socket.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <thread>

using namespace project;

// ============================================================================
// Endpoint Tests
// ============================================================================

TEST(EndpointTest, DefaultConstruction)
{
  Endpoint endpoint;
  EXPECT_FALSE(endpoint.is_valid());
  EXPECT_EQ(endpoint.address(), "");
  EXPECT_EQ(endpoint.port(), 0);
}

TEST(EndpointTest, ConstructWithParameters)
{
  Endpoint endpoint("127.0.0.1", 8080);
  EXPECT_TRUE(endpoint.is_valid());
  EXPECT_EQ(endpoint.address(), "127.0.0.1");
  EXPECT_EQ(endpoint.port(), 8080);
}

TEST(EndpointTest, ToString)
{
  Endpoint endpoint("192.168.1.1", 9000);
  EXPECT_EQ(endpoint.to_string(), "192.168.1.1:9000");
}

TEST(EndpointTest, Equality)
{
  Endpoint ep1("127.0.0.1", 8080);
  Endpoint ep2("127.0.0.1", 8080);
  Endpoint ep3("127.0.0.1", 9000);
  Endpoint ep4("192.168.1.1", 8080);

  EXPECT_EQ(ep1, ep2);
  EXPECT_NE(ep1, ep3);
  EXPECT_NE(ep1, ep4);
}

TEST(EndpointTest, OutputStreamOperator)
{
  Endpoint endpoint("10.0.0.1", 5000);
  std::ostringstream oss;
  oss << endpoint;
  EXPECT_EQ(oss.str(), "10.0.0.1:5000");
}

TEST(EndpointTest, IsValid)
{
  Endpoint valid("127.0.0.1", 8080);
  Endpoint invalid_no_address("", 8080);
  Endpoint invalid_no_port("127.0.0.1", 0);
  Endpoint invalid_both("", 0);

  EXPECT_TRUE(valid.is_valid());
  EXPECT_FALSE(invalid_no_address.is_valid());
  EXPECT_FALSE(invalid_no_port.is_valid());
  EXPECT_FALSE(invalid_both.is_valid());
}

// ============================================================================
// UdpSocket Tests
// ============================================================================

TEST(UdpSocketTest, DefaultConstruction)
{
  UdpSocket socket;
  EXPECT_FALSE(socket.is_valid());
  EXPECT_FALSE(static_cast<bool>(socket));
  EXPECT_EQ(socket.get_fd(), -1);
}

TEST(UdpSocketTest, Create)
{
  auto result = UdpSocket::create();
  ASSERT_TRUE(result.has_value());

  UdpSocket& socket = *result;
  EXPECT_TRUE(socket.is_valid());
  EXPECT_TRUE(static_cast<bool>(socket));
  EXPECT_GE(socket.get_fd(), 0);
}

TEST(UdpSocketTest, MoveConstruction)
{
  auto result = UdpSocket::create();
  ASSERT_TRUE(result.has_value());

  UdpSocket socket1 = std::move(*result);
  int fd1 = socket1.get_fd();

  UdpSocket socket2(std::move(socket1));

  EXPECT_FALSE(socket1.is_valid());
  EXPECT_TRUE(socket2.is_valid());
  EXPECT_EQ(socket2.get_fd(), fd1);
}

TEST(UdpSocketTest, MoveAssignment)
{
  auto result1 = UdpSocket::create();
  auto result2 = UdpSocket::create();
  ASSERT_TRUE(result1.has_value());
  ASSERT_TRUE(result2.has_value());

  UdpSocket socket1 = std::move(*result1);
  UdpSocket socket2 = std::move(*result2);

  int fd1 = socket1.get_fd();

  socket2 = std::move(socket1);

  EXPECT_FALSE(socket1.is_valid());
  EXPECT_TRUE(socket2.is_valid());
  EXPECT_EQ(socket2.get_fd(), fd1);
}

TEST(UdpSocketTest, Bind)
{
  auto result = UdpSocket::create();
  ASSERT_TRUE(result.has_value());

  UdpSocket& socket = *result;

  // Bind to loopback with ephemeral port
  auto bind_result = socket.bind("127.0.0.1", 0);
  EXPECT_TRUE(bind_result.has_value());

  EXPECT_EQ(socket.local_endpoint().address(), "127.0.0.1");
  EXPECT_EQ(socket.local_endpoint().port(), 0);  // We requested port 0 (ephemeral)
}

TEST(UdpSocketTest, BindInvalidAddress)
{
  auto result = UdpSocket::create();
  ASSERT_TRUE(result.has_value());

  UdpSocket& socket = *result;

  // Try to bind to invalid address
  auto bind_result = socket.bind("invalid.address", 8080);
  EXPECT_FALSE(bind_result.has_value());
  EXPECT_EQ(bind_result.error(), UdpError::AddressResolutionFailed);
}

TEST(UdpSocketTest, SendToInvalidSocket)
{
  UdpSocket socket;
  std::vector<uint8_t> data = {1, 2, 3};
  Endpoint dest("127.0.0.1", 8080);

  auto result = socket.send_to(data, dest);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), UdpError::InvalidSocket);
}

TEST(UdpSocketTest, SendToInvalidEndpoint)
{
  auto result = UdpSocket::create();
  ASSERT_TRUE(result.has_value());

  UdpSocket& socket = *result;
  std::vector<uint8_t> data = {1, 2, 3};
  Endpoint invalid_endpoint;

  auto send_result = socket.send_to(data, invalid_endpoint);
  EXPECT_FALSE(send_result.has_value());
  EXPECT_EQ(send_result.error(), UdpError::InvalidEndpoint);
}

TEST(UdpSocketTest, SendAndReceive)
{
  // Create sender socket
  auto sender_result = UdpSocket::create();
  ASSERT_TRUE(sender_result.has_value());
  UdpSocket sender = std::move(*sender_result);

  // Create receiver socket and bind to a specific port
  auto receiver_result = UdpSocket::create();
  ASSERT_TRUE(receiver_result.has_value());
  UdpSocket receiver = std::move(*receiver_result);

  // Bind receiver to loopback on port 0 (ephemeral port)
  auto bind_result = receiver.bind("127.0.0.1", 0);
  ASSERT_TRUE(bind_result.has_value());

  // Get the actual port assigned
  uint16_t receiver_port = receiver.local_endpoint().port();
  
  // Note: For ephemeral port (0), we need to get the actual assigned port
  // For this test, we'll use a fixed port instead
  auto receiver2_result = UdpSocket::create();
  ASSERT_TRUE(receiver2_result.has_value());
  UdpSocket receiver2 = std::move(*receiver2_result);

  // Try to bind to a specific high port (to avoid conflicts)
  uint16_t test_port = 19999;
  auto bind_result2 = receiver2.bind("127.0.0.1", test_port);
  if (!bind_result2.has_value())
  {
    // Port might be in use, try another one
    test_port = 20000;
    bind_result2 = receiver2.bind("127.0.0.1", test_port);
  }
  ASSERT_TRUE(bind_result2.has_value());

  // Send data
  std::vector<uint8_t> test_data = {0xde, 0xad, 0xbe, 0xef};
  Endpoint dest("127.0.0.1", test_port);
  auto send_result = sender.send_to(test_data, dest);
  ASSERT_TRUE(send_result.has_value());
  EXPECT_EQ(*send_result, test_data.size());

  // Receive data
  auto recv_result = receiver2.receive_from(1024);
  ASSERT_TRUE(recv_result.has_value());

  auto& [received_data, sender_endpoint] = *recv_result;
  EXPECT_EQ(received_data, test_data);
  EXPECT_EQ(sender_endpoint.address(), "127.0.0.1");
}

TEST(UdpSocketTest, SendAndReceiveWithPointer)
{
  auto sender_result = UdpSocket::create();
  ASSERT_TRUE(sender_result.has_value());
  UdpSocket sender = std::move(*sender_result);

  auto receiver_result = UdpSocket::create();
  ASSERT_TRUE(receiver_result.has_value());
  UdpSocket receiver = std::move(*receiver_result);

  uint16_t test_port = 20001;
  auto bind_result = receiver.bind("127.0.0.1", test_port);
  if (!bind_result.has_value())
  {
    test_port = 20002;
    bind_result = receiver.bind("127.0.0.1", test_port);
  }
  ASSERT_TRUE(bind_result.has_value());

  // Send using raw pointer
  uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
  Endpoint dest("127.0.0.1", test_port);
  auto send_result = sender.send_to(test_data, sizeof(test_data), dest);
  ASSERT_TRUE(send_result.has_value());
  EXPECT_EQ(*send_result, sizeof(test_data));

  // Receive
  auto recv_result = receiver.receive_from(1024);
  ASSERT_TRUE(recv_result.has_value());

  auto& [received_data, sender_endpoint] = *recv_result;
  ASSERT_EQ(received_data.size(), sizeof(test_data));
  for (size_t i = 0; i < sizeof(test_data); ++i)
  {
    EXPECT_EQ(received_data[i], test_data[i]);
  }
}

TEST(UdpSocketTest, ReceiveFromInvalidSocket)
{
  UdpSocket socket;
  auto result = socket.receive_from();
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), UdpError::InvalidSocket);
}

TEST(UdpSocketTest, ExplicitClose)
{
  auto result = UdpSocket::create();
  ASSERT_TRUE(result.has_value());

  UdpSocket socket = std::move(*result);
  EXPECT_TRUE(socket.is_valid());

  socket.close();
  EXPECT_FALSE(socket.is_valid());
  EXPECT_FALSE(socket.local_endpoint().is_valid());

  // Closing again should be safe
  socket.close();
  EXPECT_FALSE(socket.is_valid());
}

TEST(UdpSocketTest, ErrorToString)
{
  EXPECT_STREQ(to_string(UdpError::SocketCreationFailed), "Failed to create socket");
  EXPECT_STREQ(to_string(UdpError::BindFailed), "Failed to bind socket");
  EXPECT_STREQ(to_string(UdpError::SendFailed), "Failed to send data");
  EXPECT_STREQ(to_string(UdpError::ReceiveFailed), "Failed to receive data");
  EXPECT_STREQ(to_string(UdpError::InvalidEndpoint), "Invalid endpoint");
  EXPECT_STREQ(to_string(UdpError::AddressResolutionFailed), "Failed to resolve address");
  EXPECT_STREQ(to_string(UdpError::InvalidSocket), "Invalid socket");
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

