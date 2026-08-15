#ifndef PROJECT_VSWITCH_HPP_
#define PROJECT_VSWITCH_HPP_

#include <atomic>
#include <memory>

#include "wirelab/ethernet_frame.hpp"
#include "wirelab/mac_table.hpp"
#include "wirelab/udp_socket.hpp"
#include "wirelab/switch_metrics.hpp"


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

  class VSwitch
  {
   private:
    UdpSocket socket_;
    MacTable mac_table_;
    uint16_t port_;
    SwitchMetrics metrics_;
    std::atomic<bool> running_;
    std::atomic<VSwitchLogLevel> log_level_;


   public:
    [[nodiscard]] static expected<VSwitch, VSwitchError> create(
        uint16_t port, VSwitchLogLevel log_level = VSwitchLogLevel::Lifecycle);
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


   private:
      VSwitch(UdpSocket socket, uint16_t port, VSwitchLogLevel log_level) noexcept;
       void process_frame(const std::vector<uint8_t>& frame_data, const Endpoint& sender_endpoint);
    void log_frame(
        const EthernetFrame& frame,
        const Endpoint& sender_endpoint,
        std::string_view action,
        std::string_view details = "") const;
  };

}  

#endif  
