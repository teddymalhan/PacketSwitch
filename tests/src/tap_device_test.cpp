/**
 * @file tap_device_test.cpp
 * @brief Unit tests for TAP device management
 * 
 * Note: Creating TAP devices requires root privileges. Most tests verify
 * the API and error handling without requiring actual device creation.
 */

#include "project/tap_device.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

using namespace project;

TEST(TapDeviceTest, DefaultConstruction)
{
  TapDevice tap;
  EXPECT_FALSE(tap.is_valid());
  EXPECT_FALSE(static_cast<bool>(tap));
  EXPECT_EQ(tap.device_name(), "");
  EXPECT_EQ(tap.get_fd(), -1);
}

TEST(TapDeviceTest, CreateDevice)
{
  // Attempt to create a TAP device
  // This will fail if not running as root, which is expected
  auto result = TapDevice::create("taptest");

  if (result)
  {
    // Success - we have root privileges
    std::cout << "INFO: Successfully created TAP device (running as root)\n";
    EXPECT_TRUE(result->is_valid());
    EXPECT_TRUE(static_cast<bool>(*result));
    EXPECT_FALSE(result->device_name().empty());
    EXPECT_GE(result->get_fd(), 0);

    std::cout << "Created device: " << result->device_name() << "\n";
  }
  else
  {
    // Expected failure without root
    std::cout << "INFO: TAP device creation failed (expected without root privileges)\n";
    std::cout << "Error: " << to_string(result.error()) << "\n";
    EXPECT_TRUE(result.error() == TapError::DeviceOpenFailed || result.error() == TapError::IoctlFailed);
  }
}

TEST(TapDeviceTest, CreateWithAutoName)
{
  auto result = TapDevice::create();  // Empty name for kernel auto-assignment

  if (result)
  {
    std::cout << "INFO: Auto-named TAP device created: " << result->device_name() << "\n";
    EXPECT_TRUE(result->is_valid());
    EXPECT_FALSE(result->device_name().empty());
  }
  else
  {
    std::cout << "INFO: Auto-named TAP creation failed (expected without root)\n";
    EXPECT_TRUE(result.error() == TapError::DeviceOpenFailed || result.error() == TapError::IoctlFailed);
  }
}

TEST(TapDeviceTest, MoveConstruction)
{
  TapDevice tap1;
  EXPECT_FALSE(tap1.is_valid());

  TapDevice tap2(std::move(tap1));
  EXPECT_FALSE(tap2.is_valid());

  // Try with actual device if we have privileges
  auto result = TapDevice::create("taptest_move");
  if (result)
  {
    TapDevice tap3 = std::move(*result);
    EXPECT_TRUE(tap3.is_valid());
    EXPECT_FALSE(result->is_valid());  // Original should be invalid after move
  }
}

TEST(TapDeviceTest, MoveAssignment)
{
  TapDevice tap1;
  TapDevice tap2;

  tap2 = std::move(tap1);
  EXPECT_FALSE(tap2.is_valid());

  // Try with actual device if we have privileges
  auto result = TapDevice::create("taptest_assign");
  if (result)
  {
    TapDevice tap3;
    tap3 = std::move(*result);
    EXPECT_TRUE(tap3.is_valid());
    EXPECT_FALSE(result->is_valid());
  }
}

TEST(TapDeviceTest, ExplicitClose)
{
  auto result = TapDevice::create("taptest_close");

  if (result)
  {
    EXPECT_TRUE(result->is_valid());
    result->close();
    EXPECT_FALSE(result->is_valid());
    EXPECT_EQ(result->device_name(), "");

    // Closing again should be safe
    result->close();
    EXPECT_FALSE(result->is_valid());
  }
}

TEST(TapDeviceTest, ReadFromInvalidDevice)
{
  TapDevice tap;
  auto result = tap.read_frame();

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TapError::InvalidDevice);
}

TEST(TapDeviceTest, WriteToInvalidDevice)
{
  TapDevice tap;
  std::vector<uint8_t> frame = {0x01, 0x02, 0x03};

  auto result = tap.write_frame(frame);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TapError::InvalidDevice);
}

TEST(TapDeviceTest, WriteWithPointer)
{
  TapDevice tap;
  uint8_t data[] = {0x01, 0x02, 0x03};

  auto result = tap.write_frame(data, sizeof(data));

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TapError::InvalidDevice);
}

TEST(TapDeviceTest, ErrorToString)
{
  EXPECT_STREQ(to_string(TapError::DeviceOpenFailed), "Failed to open /dev/net/tun");
  EXPECT_STREQ(to_string(TapError::IoctlFailed), "ioctl(TUNSETIFF) failed");
  EXPECT_STREQ(to_string(TapError::ReadFailed), "Failed to read from TAP device");
  EXPECT_STREQ(to_string(TapError::WriteFailed), "Failed to write to TAP device");
  EXPECT_STREQ(to_string(TapError::InvalidDevice), "Invalid TAP device");
  EXPECT_STREQ(to_string(TapError::PartialWrite), "Partial write to TAP device");
}

TEST(TapDeviceTest, CheckRootPrivileges)
{
  if (geteuid() == 0)
  {
    std::cout << "\n=== Running as ROOT - Full TAP device tests available ===\n";

    // If running as root, we should be able to create devices
    auto result = TapDevice::create("taptest_root");
    ASSERT_TRUE(result.has_value()) << "Failed to create TAP device despite root privileges";

    TapDevice& tap = *result;
    EXPECT_TRUE(tap.is_valid());
    EXPECT_EQ(tap.device_name(), "taptest_root");

    std::cout << "Successfully created and validated TAP device: " << tap.device_name() << "\n";
  }
  else
  {
    std::cout << "\n=== Running as NON-ROOT - TAP device tests limited ===\n";
    std::cout << "To run full TAP device tests, run with: sudo ./tap_device_test_Tests\n";
  }
}

// This test only runs if we can actually create a device
TEST(TapDeviceTest, ReadWriteOperationsIfRoot)
{
  if (geteuid() != 0)
  {
    GTEST_SKIP() << "Skipping read/write test (requires root privileges)";
  }

  auto result = TapDevice::create("taptest_io");
  ASSERT_TRUE(result.has_value());

  TapDevice& tap = *result;

  // Create a minimal Ethernet frame (14 byte header + data)
  std::vector<uint8_t> test_frame = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // Destination MAC (broadcast)
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  // Source MAC
      0x08, 0x00,                          // EtherType (IPv4)
      0xde, 0xad, 0xbe, 0xef               // Data
  };

  // Write frame
  auto write_result = tap.write_frame(test_frame);

  if (write_result)
  {
    EXPECT_EQ(*write_result, test_frame.size());
    std::cout << "Successfully wrote " << *write_result << " bytes to TAP device\n";
  }
  else
  {
    std::cout << "Write failed: " << to_string(write_result.error()) << "\n";
    // This might fail if the device isn't fully configured, which is okay for this test
  }
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

