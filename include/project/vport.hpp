/**
 * @file vport.hpp
 * @brief Virtual Port for connecting TAP devices to VSwitch
 *
 * VPort creates a virtual port that bridges a TAP device (connected to the
 * kernel's network stack) with a VSwitch via UDP. It runs two forwarder threads:
 * - TAP → VSwitch: Reads Ethernet frames from TAP, sends via UDP
 * - VSwitch → TAP: Receives frames via UDP, writes to TAP
 */

#ifndef PROJECT_VPORT_HPP_
#define PROJECT_VPORT_HPP_

#include <atomic>
#include <string>
#include <string_view>

#include "project/ethernet_frame.hpp"
#include "project/expected.hpp"
#include "project/joining_thread.hpp"
#include "project/tap_device.hpp"
#include "project/udp_socket.hpp"

namespace project
{
  /**
   * @brief Error codes for VPort operations
   */
  enum class VPortError
  {
    TapDeviceCreationFailed,
    SocketCreationFailed,
    InvalidVSwitchEndpoint,
    AlreadyRunning,
    NotRunning
  };

  /**
   * @brief Convert VPortError to string representation
   * @param error The error code
   * @return String description of the error
   */
  [[nodiscard]] const char* to_string(VPortError error) noexcept;

  /**
   * @brief Virtual Port that connects a TAP device to a VSwitch
   *
   * VPort encapsulates a TAP device and UDP socket, running two forwarder
   * threads to relay Ethernet frames bidirectionally between the TAP device
   * and a remote VSwitch.
   *
   * Example usage:
   * @code
   * auto vport_result = VPort::create("tap0", "127.0.0.1", 8080);
   * if (!vport_result) {
   *   std::cerr << "Failed to create VPort\n";
   *   return;
   * }
   *
   * VPort vport = std::move(*vport_result);
   * vport.start();
   *
   * // ... VPort runs in background ...
   *
   * vport.stop();  // Graceful shutdown
   * @endcode
   */
  class VPort
  {
   private:
    TapDevice tap_device_;
    UdpSocket udp_socket_;
    Endpoint vswitch_endpoint_;
    std::string device_name_;

    std::atomic<bool> running_;
    joining_thread tap_to_switch_thread_;
    joining_thread switch_to_tap_thread_;

    /**
     * @brief Private constructor (use create() instead)
     */
    VPort(TapDevice tap_device, UdpSocket udp_socket, Endpoint vswitch_endpoint, std::string device_name);

   public:
    /**
     * @brief Create a VPort instance
     *
     * Creates a TAP device and UDP socket, ready to start forwarding.
     *
     * @param device_name TAP device name (e.g., "tap0")
     * @param vswitch_address VSwitch IP address
     * @param vswitch_port VSwitch port number
     * @return expected<VPort, VPortError> The created VPort or an error
     */
    [[nodiscard]] static expected<VPort, VPortError>
    create(std::string_view device_name, std::string_view vswitch_address, uint16_t vswitch_port);

    /**
     * @brief Move constructor
     */
    VPort(VPort&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    VPort& operator=(VPort&& other) noexcept;

    /**
     * @brief Deleted copy constructor
     */
    VPort(const VPort&) = delete;

    /**
     * @brief Deleted copy assignment
     */
    VPort& operator=(const VPort&) = delete;

    /**
     * @brief Destructor - stops forwarding if running
     */
    ~VPort();

    /**
     * @brief Start the forwarder threads
     *
     * Spawns two threads:
     * - TAP → VSwitch forwarder
     * - VSwitch → TAP forwarder
     *
     * @return expected<void, VPortError> Success or error
     */
    [[nodiscard]] expected<void, VPortError> start();

    /**
     * @brief Stop the forwarder threads
     *
     * Signals threads to stop and waits for them to complete.
     */
    void stop() noexcept;

    /**
     * @brief Check if the VPort is running
     * @return true if forwarder threads are running
     */
    [[nodiscard]] bool is_running() const noexcept
    {
      return running_.load();
    }

    /**
     * @brief Get the TAP device name
     * @return The device name
     */
    [[nodiscard]] const std::string& device_name() const noexcept
    {
      return device_name_;
    }

    /**
     * @brief Get the VSwitch endpoint
     * @return The VSwitch endpoint
     */
    [[nodiscard]] const Endpoint& vswitch_endpoint() const noexcept
    {
      return vswitch_endpoint_;
    }

   private:
    /**
     * @brief Forward frames from TAP device to VSwitch
     *
     * Runs in a loop: reads Ethernet frames from TAP device and sends them
     * to VSwitch via UDP.
     */
    void forward_tap_to_switch();

    /**
     * @brief Forward frames from VSwitch to TAP device
     *
     * Runs in a loop: receives Ethernet frames from VSwitch via UDP and
     * writes them to TAP device.
     */
    void forward_switch_to_tap();

    /**
     * @brief Log an Ethernet frame
     * @param direction Description of the direction (e.g., "Sent to VSwitch")
     * @param frame The Ethernet frame to log
     */
    void log_frame(std::string_view direction, const EthernetFrame& frame) const;
  };

}  // namespace project

#endif  // PROJECT_VPORT_HPP_
