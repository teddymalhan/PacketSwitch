/**
 * @file tap_device.hpp
 * @brief Modern C++ wrapper for TAP (network tunnel) devices
 *
 * Provides RAII management of TAP devices for Layer 2 networking.
 * TAP devices operate at the Ethernet frame level.
 */

#ifndef PROJECT_TAP_DEVICE_HPP_
#define PROJECT_TAP_DEVICE_HPP_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "project/expected.hpp"
#include "project/sys_utils.hpp"

namespace project
{
  /**
   * @brief Maximum size of an Ethernet frame (including header)
   */
  constexpr size_t ETHER_MAX_LEN = 1518;

  /**
   * @brief Error codes for TAP device operations
   */
  enum class TapError
  {
    DeviceOpenFailed,
    IoctlFailed,
    ReadFailed,
    WriteFailed,
    InvalidDevice,
    PartialWrite
  };

  /**
   * @brief Convert TapError to string representation
   * @param error The error code
   * @return String description of the error
   */
  [[nodiscard]] const char* to_string(TapError error) noexcept;

  /**
   * @brief RAII wrapper for TAP (network tunnel) devices
   *
   * A TAP device is a virtual network interface that operates at Layer 2
   * (Ethernet frame level). This class provides modern C++ RAII management
   * of TAP devices with safe read/write operations.
   *
   * The device is automatically closed when the object is destroyed.
   * This class is move-only to prevent resource duplication.
   *
   * Example:
   * @code
   * auto tap_result = TapDevice::create("tap0");
   * if (!tap_result) {
   *   // handle error
   *   return;
   * }
   * TapDevice tap = std::move(*tap_result);
   *
   * // Read an Ethernet frame
   * auto frame = tap.read_frame();
   * if (frame) {
   *   // process frame data
   * }
   * @endcode
   */
  class TapDevice
  {
   private:
    FileDescriptor fd_;
    std::string device_name_;

    /**
     * @brief Private constructor (use create() instead)
     */
    TapDevice(FileDescriptor fd, std::string device_name);

   public:
    /**
     * @brief Create a TAP device with the specified name
     *
     * Creates a TAP device. If the name is empty, the kernel assigns a name.
     * The device is configured with IFF_TAP | IFF_NO_PI flags.
     *
     * @param device_name Desired device name (e.g., "tap0"), or empty for auto-assignment
     * @return expected<TapDevice, TapError> The created device or an error
     */
    [[nodiscard]] static expected<TapDevice, TapError> create(std::string_view device_name = "");

    /**
     * @brief Default constructor - creates an invalid device
     */
    TapDevice() = default;

    /**
     * @brief Move constructor
     */
    TapDevice(TapDevice&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    TapDevice& operator=(TapDevice&& other) noexcept = default;

    /**
     * @brief Deleted copy constructor
     */
    TapDevice(const TapDevice&) = delete;

    /**
     * @brief Deleted copy assignment
     */
    TapDevice& operator=(const TapDevice&) = delete;

    /**
     * @brief Destructor - closes the TAP device
     */
    ~TapDevice() = default;

    /**
     * @brief Read an Ethernet frame from the TAP device
     *
     * Reads a complete Ethernet frame from the device. This is a blocking operation.
     *
     * @return expected<std::vector<uint8_t>, TapError> The frame data or an error
     */
    [[nodiscard]] expected<std::vector<uint8_t>, TapError> read_frame();

    /**
     * @brief Write an Ethernet frame to the TAP device
     *
     * Writes a complete Ethernet frame to the device.
     *
     * @param frame The frame data to write
     * @return expected<size_t, TapError> Number of bytes written or an error
     */
    [[nodiscard]] expected<size_t, TapError> write_frame(const std::vector<uint8_t>& frame);

    /**
     * @brief Write an Ethernet frame to the TAP device
     *
     * @param data Pointer to frame data
     * @param size Size of frame data
     * @return expected<size_t, TapError> Number of bytes written or an error
     */
    [[nodiscard]] expected<size_t, TapError> write_frame(const uint8_t* data, size_t size);

    /**
     * @brief Check if the device is valid
     * @return true if the device is valid and open
     */
    [[nodiscard]] bool is_valid() const noexcept
    {
      return fd_.is_valid();
    }

    /**
     * @brief Get the device name
     * @return The device name (e.g., "tap0")
     */
    [[nodiscard]] const std::string& device_name() const noexcept
    {
      return device_name_;
    }

    /**
     * @brief Get the file descriptor
     * @return The underlying file descriptor
     */
    [[nodiscard]] int get_fd() const noexcept
    {
      return fd_.get();
    }

    /**
     * @brief Close the device
     */
    void close() noexcept
    {
      fd_.close();
      device_name_.clear();
    }

    /**
     * @brief Explicit conversion to bool for validity checking
     */
    explicit operator bool() const noexcept
    {
      return is_valid();
    }
  };

}  // namespace project

#endif  // PROJECT_TAP_DEVICE_HPP_
