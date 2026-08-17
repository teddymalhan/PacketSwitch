#include "wirelab/vswitch.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace wirelab
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

  expected<VSwitch, VSwitchError> VSwitch::create(uint16_t port, VSwitchLogLevel log_level)
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
    return VSwitch(std::move(socket), actual_port, log_level);
  }

  VSwitch::VSwitch(UdpSocket socket, uint16_t port, VSwitchLogLevel log_level) noexcept
      : socket_(std::move(socket)),
        port_(port),
        running_(false),
        log_level_(log_level)
  {
  }

  VSwitch::VSwitch(VSwitch&& other) noexcept
      : socket_(std::move(other.socket_)),
        mac_table_(std::move(other.mac_table_)),
        port_(other.port_),
        metrics_(std::move(other.metrics_)),
        running_(other.running_.load()),
        log_level_(other.log_level_.load()),
        gate_(other.gate_),
        pending_(std::move(other.pending_))
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
      metrics_ = std::move(other.metrics_);
      running_.store(other.running_.load());
      log_level_.store(other.log_level_.load());
      gate_ = other.gate_;
      pending_ = std::move(other.pending_);
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

    if (log_level() == VSwitchLogLevel::Lifecycle)
    {
      std::cout << "[VSwitch] Started at 0.0.0.0:" << port_ << "\n";
      std::cout << "[VSwitch] Ready to receive frames from VPorts\n";
    }

    running_.store(true);

    while (running_.load())
    {
      const auto now = std::chrono::steady_clock::now();
      drain_due(now);

      auto readable = socket_.wait_readable(wait_budget(now));
      if (!readable)
      {
        // Either stop() closed the socket or the wait itself failed; neither is
        // recoverable inside the loop, and spinning on it would burn a core.
        stop();
        break;
      }

      if (*readable)
      {
        auto recv_result = socket_.receive_from();
        if (recv_result)
        {
          auto& [frame_data, sender_endpoint] = *recv_result;
          process_frame(frame_data, sender_endpoint);
        }
      }

      if (gate_ != nullptr)
      {
        gate_->tick(std::chrono::steady_clock::now());
      }
    }

    pending_.clear();

    return expected<void, VSwitchError>();
  }

  void VSwitch::stop() noexcept
  {
    if (!running_.exchange(false))
      return;
    if (log_level() == VSwitchLogLevel::Lifecycle)
    {
      std::cout << "[VSwitch] Stopping...\n";
    }
    socket_.close();
    if (log_level() == VSwitchLogLevel::Lifecycle)
    {
      std::cout << "[VSwitch] Stopped. Learned " << mac_table_.size() << " MAC addresses.\n";
    }
  }

  void VSwitch::process_frame(const std::vector<uint8_t>& frame_data, const Endpoint& sender_endpoint)
  {
    metrics_.record_received(frame_data.size());

    auto parsed = EthernetFrame::try_parse(frame_data);
    if (!parsed)
    {
      metrics_.record_malformed();
      metrics_.record_drop();
      if (log_level() == VSwitchLogLevel::Frame)
      {
        std::cout << "  [Drop] malformed frame from " << sender_endpoint << " size=" << frame_data.size() << "\n";
      }
      return;
    }
    const EthernetFrame frame = std::move(parsed).value();

    if (log_level() == VSwitchLogLevel::Frame)
    {
      std::cout << "[VSwitch] Received frame from " << sender_endpoint << ": dst=" << frame.dst_mac()
                << " src=" << frame.src_mac() << " size=" << frame_data.size() << "\n";
    }

    const auto now = std::chrono::steady_clock::now();
    FaultDecision decision;
    decision.delivery_count = 1;
    decision.delivery_times[0] = now;
    if (gate_ != nullptr)
    {
      decision = gate_->inspect(frame_data, sender_endpoint, now);
    }

    // Gated before learning: a quarantined port must not teach the MAC table an
    // entry that would then attract traffic it cannot forward.
    if (decision.dropped || decision.delivery_count == 0)
    {
      metrics_.record_drop();
      if (log_level() == VSwitchLogLevel::Frame)
      {
        log_frame(frame, sender_endpoint, "Blocked", "ingress fault");
      }
      return;
    }

    const bool is_new = mac_table_.insert(frame.src_mac(), sender_endpoint);
    if (is_new)
    {
      metrics_.record_mac_learned();
      if (log_level() == VSwitchLogLevel::Frame)
      {
        std::cout << "  [Learn] " << frame.src_mac() << " → " << sender_endpoint << "\n";
      }
    }

    const auto& dst_mac = frame.dst_mac();
    const auto dst_endpoint = mac_table_.lookup(dst_mac);

    if (dst_endpoint.has_value())
    {
      schedule(frame_data, *dst_endpoint, decision, now);
      if (log_level() == VSwitchLogLevel::Frame)
      {
        log_frame(frame, sender_endpoint, "Forwarded to", dst_mac.to_string());
      }
      return;
    }

    if (dst_mac.is_broadcast())
    {
      metrics_.record_broadcast();
      const auto all_endpoints = mac_table_.get_all_endpoints_except(frame.src_mac());
      for (const auto& endpoint : all_endpoints)
      {
        schedule(frame_data, endpoint, decision, now);
      }

      if (!all_endpoints.empty() && log_level() == VSwitchLogLevel::Frame)
      {
        log_frame(frame, sender_endpoint, "Broadcasted to", std::to_string(all_endpoints.size()) + " endpoints");
      }
      return;
    }

    metrics_.record_unknown_unicast();
    metrics_.record_drop();
    if (log_level() == VSwitchLogLevel::Frame)
    {
      log_frame(frame, sender_endpoint, "Discarded", "unknown MAC address");
    }
  }

  void VSwitch::deliver(std::vector<uint8_t> frame_data, const Endpoint& destination)
  {
    if (socket_.send_to(frame_data, destination))
    {
      metrics_.record_forwarded(frame_data.size());
    }
    else
    {
      metrics_.record_drop();
    }
  }

  void VSwitch::schedule(
      const std::vector<uint8_t>& frame_data,
      const Endpoint& destination,
      const FaultDecision& decision,
      std::chrono::steady_clock::time_point now)
  {
    // delivery_count is 2 when a fault duplicates the frame; each copy carries
    // its own due time, so latency and jitter apply per copy.
    const size_t copies = std::min<size_t>(decision.delivery_count, decision.delivery_times.size());
    for (size_t copy = 0; copy < copies; ++copy)
    {
      const auto due = decision.delivery_times[copy];
      if (due <= now)
      {
        deliver(frame_data, destination);
        continue;
      }
      pending_.push_back({ due, frame_data, destination });
    }
  }

  void VSwitch::drain_due(std::chrono::steady_clock::time_point now)
  {
    if (pending_.empty())
    {
      return;
    }
    const auto first_late = std::stable_partition(
        pending_.begin(), pending_.end(), [now](const ScheduledDelivery& delivery) { return delivery.due <= now; });
    for (auto delivery = pending_.begin(); delivery != first_late; ++delivery)
    {
      deliver(std::move(delivery->frame_data), delivery->destination);
    }
    pending_.erase(pending_.begin(), first_late);
  }

  std::chrono::milliseconds VSwitch::wait_budget(std::chrono::steady_clock::time_point now) const noexcept
  {
    // With no gate and nothing deferred the wait is still bounded, so stop() is
    // observed promptly instead of only when the next frame arrives.
    auto budget = gate_ != nullptr ? gate_->tick_interval() : std::chrono::milliseconds{ 250 };
    for (const auto& delivery : pending_)
    {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(delivery.due - now);
      budget = std::min(budget, remaining);
    }
    return budget < std::chrono::milliseconds::zero() ? std::chrono::milliseconds::zero() : budget;
  }

  void VSwitch::log_frame(const EthernetFrame&, const Endpoint&, std::string_view action, std::string_view details) const
  {
    if (log_level() != VSwitchLogLevel::Frame)
      return;

    std::cout << "  [" << action << "]";
    if (!details.empty())
    {
      std::cout << " " << details;
    }
    std::cout << "\n";
  }

}  // namespace wirelab
