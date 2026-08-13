/**
 * @file vport.cpp
 * @brief Implementation of VPort (Virtual Port)
 */

#include "project/vport.hpp"

#include <iostream>

namespace project
{
  const char* to_string(VPortError error) noexcept
  {
    switch (error)
    {
      case VPortError::TapDeviceCreationFailed: return "Failed to create TAP device";
      case VPortError::SocketCreationFailed: return "Failed to create UDP socket";
      case VPortError::InvalidVSwitchEndpoint: return "Invalid VSwitch endpoint";
      case VPortError::AlreadyRunning: return "VPort is already running";
      case VPortError::NotRunning: return "VPort is not running";
      default: return "Unknown VPort error";
    }
  }

  VPort::VPort(TapDevice tap_device, UdpSocket udp_socket, Endpoint vswitch_endpoint, std::string device_name)
      : tap_device_(std::move(tap_device)),
        udp_socket_(std::move(udp_socket)),
        vswitch_endpoint_(std::move(vswitch_endpoint)),
        device_name_(std::move(device_name)),
        running_(false)
  {
  }

  VPort::VPort(VPort&& other) noexcept
      : tap_device_(std::move(other.tap_device_)),
        udp_socket_(std::move(other.udp_socket_)),
        vswitch_endpoint_(std::move(other.vswitch_endpoint_)),
        device_name_(std::move(other.device_name_)),
        running_(other.running_.load()),
        tap_to_switch_thread_(std::move(other.tap_to_switch_thread_)),
        switch_to_tap_thread_(std::move(other.switch_to_tap_thread_))
  {
  }

  VPort& VPort::operator=(VPort&& other) noexcept
  {
    if (this != &other)
    {
      stop();  // Stop current threads if running
      tap_device_ = std::move(other.tap_device_);
      udp_socket_ = std::move(other.udp_socket_);
      vswitch_endpoint_ = std::move(other.vswitch_endpoint_);
      device_name_ = std::move(other.device_name_);
      running_.store(other.running_.load());
      tap_to_switch_thread_ = std::move(other.tap_to_switch_thread_);
      switch_to_tap_thread_ = std::move(other.switch_to_tap_thread_);
    }
    return *this;
  }

  expected<VPort, VPortError>
  VPort::create(std::string_view device_name, std::string_view vswitch_address, uint16_t vswitch_port)
  {
    // Validate VSwitch endpoint
    if (vswitch_address.empty() || vswitch_port == 0)
    {
      return unexpected(VPortError::InvalidVSwitchEndpoint);
    }

    // Create TAP device
    auto tap_result = TapDevice::create(device_name);
    if (!tap_result)
    {
      return unexpected(VPortError::TapDeviceCreationFailed);
    }

    // Create UDP socket
    auto socket_result = UdpSocket::create();
    if (!socket_result)
    {
      return unexpected(VPortError::SocketCreationFailed);
    }

    // Create VSwitch endpoint
    Endpoint vswitch_endpoint(std::string(vswitch_address), vswitch_port);

    std::string actual_device_name = tap_result->device_name();

    std::cout << "[VPort] Created TAP device: " << actual_device_name << ", VSwitch: " << vswitch_endpoint << "\n";

    // Construct VPort and wrap in expected
    VPort vport(
        std::move(*tap_result), std::move(*socket_result), std::move(vswitch_endpoint), std::move(actual_device_name));

    return expected<VPort, VPortError>(std::move(vport));
  }

  VPort::~VPort()
  {
    stop();
  }

  expected<void, VPortError> VPort::start()
  {
    if (running_.load())
    {
      return unexpected(VPortError::AlreadyRunning);
    }

    running_.store(true);

    // Start TAP → VSwitch forwarder thread
    tap_to_switch_thread_ = joining_thread([this]() { forward_tap_to_switch(); });

    // Start VSwitch → TAP forwarder thread
    switch_to_tap_thread_ = joining_thread([this]() { forward_switch_to_tap(); });

    std::cout << "[VPort] Started forwarder threads\n";

    return expected<void, VPortError>();
  }

  void VPort::stop() noexcept
  {
    if (!running_.load())
    {
      return;
    }

    std::cout << "[VPort] Stopping forwarder threads...\n";

    running_.store(false);

    // Threads will exit their loops and be joined automatically by joining_thread destructor
    // Note: In a production system, we might want to close the TAP device and socket
    // to interrupt blocking read/recv calls

    std::cout << "[VPort] Stopped\n";
  }

  void VPort::forward_tap_to_switch()
  {
    std::cout << "[VPort] TAP → VSwitch forwarder started\n";

    while (running_.load())
    {
      // Read Ethernet frame from TAP device
      auto frame_result = tap_device_.read_frame();

      if (!frame_result)
      {
        // Log error and continue (could be a temporary issue)
        std::cerr << "[VPort] TAP read error: " << to_string(frame_result.error()) << "\n";
        continue;
      }

      const auto& frame_data = *frame_result;

      // Parse the Ethernet frame for logging
      auto frame = EthernetFrame::parse(frame_data);

      // Send frame to VSwitch via UDP
      auto send_result = udp_socket_.send_to(frame_data, vswitch_endpoint_);

      if (!send_result)
      {
        std::cerr << "[VPort] UDP send error: " << to_string(send_result.error()) << "\n";
        continue;
      }

      // Log the frame
      log_frame("Sent to VSwitch", frame);
    }

    std::cout << "[VPort] TAP → VSwitch forwarder stopped\n";
  }

  void VPort::forward_switch_to_tap()
  {
    std::cout << "[VPort] VSwitch → TAP forwarder started\n";

    while (running_.load())
    {
      // Receive Ethernet frame from VSwitch
      auto recv_result = udp_socket_.receive_from(ETHER_MAX_LEN);

      if (!recv_result)
      {
        // Log error and continue
        std::cerr << "[VPort] UDP receive error: " << to_string(recv_result.error()) << "\n";
        continue;
      }

      auto& [frame_data, sender_endpoint] = *recv_result;

      // Parse the Ethernet frame for logging
      auto frame = EthernetFrame::parse(frame_data);

      // Write frame to TAP device
      auto write_result = tap_device_.write_frame(frame_data);

      if (!write_result)
      {
        std::cerr << "[VPort] TAP write error: " << to_string(write_result.error()) << "\n";
        continue;
      }

      // Log the frame
      log_frame("Forward to TAP device", frame);
    }

    std::cout << "[VPort] VSwitch → TAP forwarder stopped\n";
  }

  void VPort::log_frame(std::string_view direction, const EthernetFrame& frame) const
  {
    std::cout << "[VPort] " << direction << ": " << "dst=" << frame.dst_mac() << " " << "src=" << frame.src_mac() << " "
              << "type=" << std::hex << frame.ethertype() << std::dec << " " << "size=" << frame.size() << "\n";
  }

}  // namespace project
