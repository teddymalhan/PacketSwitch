#include "project/vswitch.hpp"

#include <iostream>

namespace project
{
  const char* to_string(VSwitchError error) noexcept
  {
    switch (error)
    {
      case VSwitchError::SocketCreationFailed: return "Failed to create socket";
      case VSwitchError::BindFailed: return "Failed to bind socket";
      case VSwitchError::AlreadyRunning: return "VSwitch is already running";
      case VSwitchError::NotRunning: return "VSwitch is not running";
      default: return "Unknown VSwitch error";
    }
  }

  expected<VSwitch, VSwitchError> VSwitch::create(uint16_t port)
  {
    auto socket_result = UdpSocket::create();
    if (!socket_result)
    {
      return unexpected(VSwitchError::SocketCreationFailed);
    }

    UdpSocket socket = std::move(*socket_result);

    auto bind_result = socket.bind("0.0.0.0", port);
    if (!bind_result)
    {
      return unexpected(VSwitchError::BindFailed);
    }

    const uint16_t actual_port = socket.local_endpoint().port();
    return VSwitch(std::move(socket), actual_port);
  }

  VSwitch::VSwitch(UdpSocket socket, uint16_t port) noexcept : socket_(std::move(socket)), port_(port), running_(false)
  {
  }

  VSwitch::VSwitch(VSwitch&& other) noexcept
      : socket_(std::move(other.socket_)),
        mac_table_(std::move(other.mac_table_)),
        port_(other.port_),
        running_(other.running_.load())
  {
  }

  VSwitch& VSwitch::operator=(VSwitch&& other) noexcept
  {
    if (this != &other)
    {
      stop();
      socket_ = std::move(other.socket_);
      mac_table_ = std::move(other.mac_table_);
      port_ = other.port_;
      running_.store(other.running_.load());
    }
    return *this;
  }

  VSwitch::~VSwitch()
  {
    stop();
  }

  expected<void, VSwitchError> VSwitch::start()
  {
    if (running_.load())
    {
      return unexpected(VSwitchError::AlreadyRunning);
    }

    std::cout << "[VSwitch] Started at 0.0.0.0:" << port_ << "\n";
    std::cout << "[VSwitch] Ready to receive frames from VPorts\n";

    running_.store(true);

    while (running_.load())
    {
      auto recv_result = socket_.receive_from();

      if (!recv_result)
      {
        continue;
      }

      auto& [frame_data, sender_endpoint] = *recv_result;

      process_frame(frame_data, sender_endpoint);
    }

    return expected<void, VSwitchError>();
  }

  void VSwitch::stop() noexcept
  {
    if (!running_.exchange(false)) return;
    std::cout << "[VSwitch] Stopping...\n";
    socket_.close();
    std::cout << "[VSwitch] Stopped. Learned " << mac_table_.size() << " MAC addresses.\n";
  }

  void VSwitch::process_frame(const std::vector<uint8_t>& frame_data, const Endpoint& sender_endpoint)
  {
    auto frame = EthernetFrame::parse(frame_data);

    std::cout << "[VSwitch] Received frame from " << sender_endpoint << ": dst=" << frame.dst_mac()
              << " src=" << frame.src_mac() << " size=" << frame_data.size() << "\n";

    bool is_new = mac_table_.insert(frame.src_mac(), sender_endpoint);
    if (is_new)
    {
      std::cout << "  [Learn] " << frame.src_mac() << " → " << sender_endpoint << "\n";
    }

    const auto& dst_mac = frame.dst_mac();

    auto dst_endpoint = mac_table_.lookup(dst_mac);

    if (dst_endpoint.has_value())
    {
      auto send_result = socket_.send_to(frame_data, *dst_endpoint);
      if (send_result)
      {
        log_frame(frame, sender_endpoint, "Forwarded to", dst_mac.to_string());
      }
    }
    else if (dst_mac.is_broadcast())
    {
      auto all_endpoints = mac_table_.get_all_endpoints_except(frame.src_mac());

      int sent_count = 0;
      for (const auto& endpoint : all_endpoints)
      {
        auto send_result = socket_.send_to(frame_data, endpoint);
        if (send_result)
        {
          sent_count++;
        }
      }

      if (sent_count > 0)
      {
        log_frame(frame, sender_endpoint, "Broadcasted to", std::to_string(sent_count) + " endpoints");
      }
    }
    else
    {
      log_frame(frame, sender_endpoint, "Discarded", "unknown MAC address");
    }
  }

  void VSwitch::log_frame(const EthernetFrame&, const Endpoint&, std::string_view action, std::string_view details) const
  {
    std::cout << "  [" << action << "]";
    if (!details.empty())
    {
      std::cout << " " << details;
    }
    std::cout << "\n";
  }

}  
