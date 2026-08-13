/**
 * @file vswitch.hpp
 * @brief Virtual Switch for Layer 2 networking
 *
 * The VSwitch is a learning switch that:
 * - Learns MAC addresses from incoming frames
 * - Forwards frames based on MAC address table
 * - Handles unicast and broadcast frames
 */

#ifndef PROJECT_VSWITCH_HPP_
#define PROJECT_VSWITCH_HPP_

#include <atomic>
#include <memory>

#include "project/ethernet_frame.hpp"
#include "project/mac_table.hpp"
#include "project/udp_socket.hpp"

namespace project
{
  /**
   * @brief Error codes for VSwitch operations
   */
  enum class VSwitchError
  {
    SocketCreationFailed,
    BindFailed,
    AlreadyRunning,
    NotRunning
  };

  /**
   * @brief Convert VSwitchError to string representation
   * @param error The error code
   * @return String description of the error
   */
  [[nodiscard]] const char* to_string(VSwitchError error) noexcept;

  /**
   * @brief Virtual Switch - learning switch implementation
   *
   * VSwitch acts as a central switching fabric for VPN connections.
   * It receives Ethernet frames from VPorts, learns MAC addresses,
   * and forwards frames based on its MAC table.
   *
   * Behavior:
   * 1. When a frame arrives, learn source MAC → sender endpoint
   * 2. If destination MAC is known (in table):
   *    - Forward frame to that endpoint (unicast)
   * 3. If destination MAC is broadcast:
   *    - Forward frame to all known endpoints except source (broadcast)
   * 4. If destination MAC is unknown:
   *    - Discard frame (unknown unicast)
   *
   * Example:
   * @code
   * auto vswitch_result = VSwitch::create(8080);
   * if (!vswitch_result) {
   *   return;
   * }
   *
   * VSwitch& vswitch = *vswitch_result;
   * vswitch.start();
   *
   * // Switch is running...
   *
   * vswitch.stop();
   * @endcode
   */
  class VSwitch
  {
   private:
    UdpSocket socket_;
    MacTable mac_table_;
    uint16_t port_;

    std::atomic<bool> running_;

   public:
    /**
     * @brief Create a VSwitch instance
     *
     * @param port The UDP port to bind to (0 for ephemeral)
     * @return expected<VSwitch, VSwitchError> The created VSwitch or an error
     */
    [[nodiscard]] static expected<VSwitch, VSwitchError> create(uint16_t port);

    /**
     * @brief Default constructor
     */
    VSwitch() = default;

    /**
     * @brief Move constructor
     */
    VSwitch(VSwitch&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    VSwitch& operator=(VSwitch&& other) noexcept;

    /**
     * @brief Deleted copy constructor
     */
    VSwitch(const VSwitch&) = delete;

    /**
     * @brief Deleted copy assignment
     */
    VSwitch& operator=(const VSwitch&) = delete;

    /**
     * @brief Destructor - stops processing if running
     */
    ~VSwitch();

    /**
     * @brief Start the VSwitch main processing loop
     *
     * Begins listening for incoming frames and processing them.
     * This call blocks until stop() is called.
     *
     * @return expected<void, VSwitchError> Success or error
     */
    [[nodiscard]] expected<void, VSwitchError> start();

    /**
     * @brief Stop the VSwitch
     *
     * Signals the processing loop to stop and returns.
     */
    void stop() noexcept;

    /**
     * @brief Check if the VSwitch is running
     * @return true if processing is running
     */
    [[nodiscard]] bool is_running() const noexcept
    {
      return running_.load();
    }

    /**
     * @brief Get the port the VSwitch is listening on
     * @return The port number
     */
    [[nodiscard]] uint16_t port() const noexcept
    {
      return port_;
    }

    /**
     * @brief Get the number of learned MAC addresses
     * @return Number of entries in MAC table
     */
    [[nodiscard]] size_t learned_macs() const
    {
      return mac_table_.size();
    }

    /**
     * @brief Get a copy of the MAC table
     * @return Copy of the MAC table entries
     */
    [[nodiscard]] std::unordered_map<MacAddress, Endpoint> get_mac_table() const
    {
      return mac_table_.get_all_entries();
    }

   private:
    /**
     * @brief Private constructor for create()
     * @param socket UDP socket to bind to
     * @param port The port number
     */
    VSwitch(UdpSocket socket, uint16_t port) noexcept;

    /**
     * @brief Process a single Ethernet frame
     *
     * Implements the learning switch algorithm:
     * 1. Learn source MAC → sender endpoint
     * 2. Forward based on destination MAC
     *
     * @param frame_data The raw frame data
     * @param sender_endpoint The endpoint that sent the frame
     */
    void process_frame(const std::vector<uint8_t>& frame_data, const Endpoint& sender_endpoint);

    /**
     * @brief Log a frame processing event
     * @param frame The Ethernet frame
     * @param sender_endpoint The sender's endpoint
     * @param action Description of the action taken (e.g., "Forwarded", "Discarded")
     * @param details Additional details about the action
     */
    void log_frame(
        const EthernetFrame& frame,
        const Endpoint& sender_endpoint,
        std::string_view action,
        std::string_view details = "") const;
  };

}  // namespace project

#endif  // PROJECT_VSWITCH_HPP_
