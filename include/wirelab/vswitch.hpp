#ifndef PROJECT_VSWITCH_HPP_
#define PROJECT_VSWITCH_HPP_

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include "wirelab/ethernet_frame.hpp"
#include "wirelab/fault_engine.hpp"
#include "wirelab/mac_table.hpp"
#include "wirelab/switch_metrics.hpp"
#include "wirelab/udp_socket.hpp"

namespace wirelab
{
  enum class VSwitchError
  {
    SocketCreationFailed,
    BindFailed,
    AlreadyRunning,
    NotRunning
  };

  [[nodiscard]] const char* to_string(VSwitchError error) noexcept;

  enum class VSwitchLogLevel
  {
    Lifecycle,
    Frame
  };

  // The switch's one hook into the analysis and fault planes.
  //
  // inspect() runs before a frame is learned or forwarded, so an isolated port
  // neither teaches the MAC table nor reaches anyone, while the frame is still
  // recorded for detection: a storm stays visible while it is being contained.
  class FrameGate
  {
   public:
    virtual ~FrameGate() = default;
    [[nodiscard]] virtual FaultDecision inspect(
        const std::vector<uint8_t>& frame_data,
        const Endpoint& sender,
        std::chrono::steady_clock::time_point arrival) = 0;
    // Called whenever the receive loop is idle or a frame has just been handled,
    // so batched analysis and lease expiry advance without traffic to carry them.
    virtual void tick(std::chrono::steady_clock::time_point now) = 0;
    // How long the loop may sleep before tick() must run again.
    [[nodiscard]] virtual std::chrono::milliseconds tick_interval() const noexcept = 0;
  };

  class VSwitch
  {
   private:
    UdpSocket socket_;
    MacTable mac_table_;
    uint16_t port_;
    SwitchMetrics metrics_;
    std::atomic<bool> running_;
    std::atomic<VSwitchLogLevel> log_level_;
    FrameGate* gate_ = nullptr;
    // Deliveries a fault deferred. The loop drains them as they come due, which
    // is why the receive wait has a deadline rather than blocking forever.
    struct ScheduledDelivery
    {
      std::chrono::steady_clock::time_point due{};
      std::vector<uint8_t> frame_data;
      Endpoint destination;
    };
    std::vector<ScheduledDelivery> pending_;

   public:
    [[nodiscard]] static expected<VSwitch, VSwitchError> create(
        uint16_t port,
        VSwitchLogLevel log_level = VSwitchLogLevel::Lifecycle);
    VSwitch() : port_(0), running_(false), log_level_(VSwitchLogLevel::Lifecycle)
    {
    }
    VSwitch(VSwitch&& other) noexcept;
    VSwitch& operator=(VSwitch&& other) noexcept;
    VSwitch(const VSwitch&) = delete;
    VSwitch& operator=(const VSwitch&) = delete;
    ~VSwitch();
    [[nodiscard]] expected<void, VSwitchError> start();
    void stop() noexcept;
    [[nodiscard]] bool is_running() const noexcept
    {
      return running_.load();
    }
    [[nodiscard]] uint16_t port() const noexcept
    {
      return port_;
    }
    [[nodiscard]] size_t learned_macs() const
    {
      return mac_table_.size();
    }
    [[nodiscard]] std::unordered_map<MacAddress, Endpoint> get_mac_table() const
    {
      return mac_table_.get_all_entries();
    }
    [[nodiscard]] SwitchMetricsSnapshot metrics() const noexcept
    {
      return metrics_.snapshot();
    }
    void set_log_level(VSwitchLogLevel log_level) noexcept
    {
      log_level_.store(log_level, std::memory_order_relaxed);
    }
    [[nodiscard]] VSwitchLogLevel log_level() const noexcept
    {
      return log_level_.load(std::memory_order_relaxed);
    }
    // Non-owning; the gate must outlive the switch. Null means every frame is
    // forwarded unconditionally, which is the standalone switch's behaviour.
    void set_frame_gate(FrameGate* gate) noexcept
    {
      gate_ = gate;
    }

   private:
    VSwitch(UdpSocket socket, uint16_t port, VSwitchLogLevel log_level) noexcept;
    void process_frame(const std::vector<uint8_t>& frame_data, const Endpoint& sender_endpoint);
    void deliver(std::vector<uint8_t> frame_data, const Endpoint& destination);
    void schedule(
        const std::vector<uint8_t>& frame_data,
        const Endpoint& destination,
        const FaultDecision& decision,
        std::chrono::steady_clock::time_point now);
    void drain_due(std::chrono::steady_clock::time_point now);
    [[nodiscard]] std::chrono::milliseconds wait_budget(std::chrono::steady_clock::time_point now) const noexcept;
    void log_frame(
        const EthernetFrame& frame,
        const Endpoint& sender_endpoint,
        std::string_view action,
        std::string_view details = "") const;
  };

}  // namespace wirelab

#endif
